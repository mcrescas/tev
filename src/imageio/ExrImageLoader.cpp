// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include <tev/imageio/ExrImageLoader.h>
#include <tev/ThreadPool.h>

#include <ImfChannelList.h>
#include <ImfInputFile.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <Iex.h>

#include <istream>

#include <errno.h>

using namespace filesystem;
using namespace nanogui;
using namespace std;

TEV_NAMESPACE_BEGIN

class StdIStream: public Imf::IStream
{
public:
    StdIStream(istream& stream, const char fileName[])
    : Imf::IStream{fileName}, mStream{stream} { }

    bool read(char c[/*n*/], int n) override {
        if (!mStream)
            throw IEX_NAMESPACE::InputExc("Unexpected end of file.");

        clearError();
        mStream.read(c, n);
        return checkError(mStream, n);
    }

    Imf::Int64 tellg() override {
        return streamoff(mStream.tellg());
    }

    void seekg(Imf::Int64 pos) override {
        mStream.seekg(pos);
        checkError(mStream);
    }

    void clear() override {
        mStream.clear();
    }

private:
    // The following error-checking functions were copy&pasted from the OpenEXR source code
    static void clearError() {
        errno = 0;
    }

    static bool checkError(istream& is, streamsize expected = 0) {
        if (!is) {
            if (errno) {
                IEX_NAMESPACE::throwErrnoExc();
            }

            if (is.gcount() < expected) {
                THROW (IEX_NAMESPACE::InputExc, "Early end of file: read " << is.gcount()
                    << " out of " << expected << " requested bytes.");
            }

            return false;
        }

        return true;
    }

    istream& mStream;
};

bool ExrImageLoader::canLoadFile(istream& iStream) const {
    // Taken from http://www.openexr.com/ReadingAndWritingImageFiles.pdf
    char b[4];
    iStream.read(b, sizeof(b));

    bool result = !!iStream && iStream.gcount() == sizeof(b) && b[0] == 0x76 && b[1] == 0x2f && b[2] == 0x31 && b[3] == 0x01;

    iStream.clear();
    iStream.seekg(0);
    return result;
}

// Helper class for dealing with the raw channels loaded from an exr file.
class RawChannel {
public:
    RawChannel(string name, Imf::Channel imfChannel)
    : mName(name), mImfChannel(imfChannel) {
    }

    void resize(size_t size) {
        mData.resize(size * bytesPerPixel());
    }

    void registerWith(Imf::FrameBuffer& frameBuffer, const Imath::Box2i& dw) {
        int width = dw.max.x - dw.min.x + 1;
        frameBuffer.insert(mName.c_str(), Imf::Slice(
            mImfChannel.type,
            mData.data() - (dw.min.x + dw.min.y * width) * bytesPerPixel(),
            bytesPerPixel(), bytesPerPixel() * (width/mImfChannel.xSampling),
            mImfChannel.xSampling, mImfChannel.ySampling, 0
        ));
    }

    template <typename T>
    Task<void> copyToTyped(Channel& channel, int priority) const {
        int width = channel.size().x();
        int widthSubsampled = width/mImfChannel.ySampling;

        auto data = reinterpret_cast<const T*>(mData.data());
        co_await gThreadPool->parallelForAsync<int>(0, channel.size().y(), [&, data](int y) {
            for (int x = 0; x < width; ++x) {
                channel.at({x, y}) = data[x/mImfChannel.xSampling + (y/mImfChannel.ySampling) * widthSubsampled];
            }
        }, priority);
    }

    Task<void> copyTo(Channel& channel, int priority) const {
        switch (mImfChannel.type) {
            case Imf::HALF:
                co_await copyToTyped<::half>(channel, priority); break;
            case Imf::FLOAT:
                co_await copyToTyped<float>(channel, priority); break;
            case Imf::UINT:
                co_await copyToTyped<uint32_t>(channel, priority); break;
            default:
                throw runtime_error("Invalid pixel type encountered.");
        }
    }

    const string& name() const {
        return mName;
    }

private:
    int bytesPerPixel() const {
        switch (mImfChannel.type) {
            case Imf::HALF:  return sizeof(::half);
            case Imf::FLOAT: return sizeof(float);
            case Imf::UINT:  return sizeof(uint32_t);
            default:
                throw runtime_error("Invalid pixel type encountered.");
        }
    }

    string mName;
    Imf::Channel mImfChannel;
    vector<char> mData;
};

Task<vector<ImageData>> ExrImageLoader::load(istream& iStream, const path& path, const string& channelSelector, int priority) const {
    vector<ImageData> result(1);
    ImageData& data = result.front();

    StdIStream stdIStream{iStream, path.str().c_str()};
    Imf::MultiPartInputFile multiPartFile{stdIStream};
    int numParts = multiPartFile.parts();

    if (numParts <= 0) {
        throw invalid_argument{"EXR image does not contain any parts."};
    }

    // Find the first part containing a channel that matches the given channelSubstr.
    int partIdx = 0;
    for (int i = 0; i < numParts; ++i) {
        Imf::InputPart part{multiPartFile, i};

        const Imf::ChannelList& imfChannels = part.header().channels();

        for (Imf::ChannelList::ConstIterator c = imfChannels.begin(); c != imfChannels.end(); ++c) {
            if (matchesFuzzy(c.name(), channelSelector)) {
                partIdx = i;
                goto l_foundPart;
            }
        }
    }
l_foundPart:

    Imf::InputPart file{multiPartFile, partIdx};
    Imath::Box2i dataWindow = file.header().dataWindow();
    Imath::Box2i displayWindow = file.header().displayWindow();
    Vector2i size = {dataWindow.max.x - dataWindow.min.x + 1 , dataWindow.max.y - dataWindow.min.y + 1};

    if (size.x() == 0 || size.y() == 0) {
        throw invalid_argument{"EXR image has zero pixels."};
    }

    // EXR's display- and data windows have inclusive upper ends while tev's upper ends are exclusive.
    // This allows easy conversion from window to size. Hence the +1.
    data.dataWindow =    {{dataWindow.min.x,    dataWindow.min.y   }, {dataWindow.max.x+1,    dataWindow.max.y+1   }};
    data.displayWindow = {{displayWindow.min.x, displayWindow.min.y}, {displayWindow.max.x+1, displayWindow.max.y+1}};

    if (!data.dataWindow.isValid()) {
        throw invalid_argument{tfm::format(
            "EXR image has invalid data window: [%d,%d] - [%d,%d]",
            data.dataWindow.min.x(), data.dataWindow.min.y(), data.dataWindow.max.x(), data.dataWindow.max.y()
        )};
    }

    if (!data.displayWindow.isValid()) {
        throw invalid_argument{tfm::format(
            "EXR image has invalid display window: [%d,%d] - [%d,%d]",
            data.displayWindow.min.x(), data.displayWindow.min.y(), data.displayWindow.max.x(), data.displayWindow.max.y()
        )};
    }

    // Allocate raw channels on the heap, because it'll be references
    // by nested parallel for coroutine.
    auto rawChannels = std::make_unique<vector<RawChannel>>();
    Imf::FrameBuffer frameBuffer;

    const Imf::ChannelList& imfChannels = file.header().channels();

    using match_t = pair<size_t, Imf::ChannelList::ConstIterator>;
    vector<match_t> matches;
    for (Imf::ChannelList::ConstIterator c = imfChannels.begin(); c != imfChannels.end(); ++c) {
        size_t matchId;
        if (matchesFuzzy(c.name(), channelSelector, &matchId)) {
            matches.emplace_back(matchId, c);
        }
    }

    // Sort matched channels by matched component of the selector, if one exists.
    if (!channelSelector.empty()) {
        sort(begin(matches), end(matches), [](const match_t& m1, const match_t& m2) { return m1.first < m2.first; });
    }

    for (const auto& match : matches) {
        const auto& c = match.second;
        rawChannels->emplace_back(c.name(), c.channel());
    }

    if (rawChannels->empty()) {
        throw invalid_argument{tfm::format("No channels match '%s'.", channelSelector)};
    }

    co_await gThreadPool->parallelForAsync(0, (int)rawChannels->size(), [c = rawChannels.get(), size](int i) {
        c->at(i).resize((size_t)size.x() * size.y());
    }, priority);

    for (size_t i = 0; i < rawChannels->size(); ++i) {
        rawChannels->at(i).registerWith(frameBuffer, dataWindow);
    }

    file.setFrameBuffer(frameBuffer);
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    for (const auto& rawChannel : *rawChannels) {
        data.channels.emplace_back(Channel{rawChannel.name(), size});
    }

    vector<Task<void>> tasks;
    for (size_t i = 0; i < rawChannels->size(); ++i) {
        tasks.emplace_back(rawChannels->at(i).copyTo(data.channels[i], priority));
    }

    for (auto& task : tasks) {
        co_await task;
    }

    data.hasPremultipliedAlpha = true;

    co_return result;
}

TEV_NAMESPACE_END
