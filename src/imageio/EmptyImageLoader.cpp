// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#include <tev/imageio/EmptyImageLoader.h>

#include <istream>

using namespace filesystem;
using namespace nanogui;
using namespace std;

TEV_NAMESPACE_BEGIN

bool EmptyImageLoader::canLoadFile(istream& iStream) const {
    char b[5];
    iStream.read(b, sizeof(b));

    bool result = !!iStream && iStream.gcount() == sizeof(b) && string(b, sizeof(b)) == "empty";

    iStream.clear();
    iStream.seekg(0);
    return result;
}

Task<std::tuple<ImageData, bool>> EmptyImageLoader::load(istream& iStream, const path&, const string&, int priority) const {
    ImageData result;

    string magic;
    Vector2i size;
    int nChannels;
    iStream >> magic >> size.x() >> size.y() >> nChannels;

    if (magic != "empty") {
        throw invalid_argument{tfm::format("Invalid magic empty string %s", magic)};
    }

    auto numPixels = (size_t)size.x() * size.y();
    if (numPixels == 0) {
        throw invalid_argument{"Image has zero pixels."};
    }

    set<string> layerNames;
    for (int i = 0; i < nChannels; ++i) {
        // The following lines decode strings by prefix length.
        // The reason for using sthis encoding is to allow arbitrary characters,
        // including whitespaces, in the channel names.
        std::vector<char> channelNameData;
        int length;
        iStream >> length;
        channelNameData.resize(length+1, 0);
        iStream.read(channelNameData.data(), length);

        string channelName = channelNameData.data();

        result.channels.emplace_back(Channel{channelName, size});
        result.channels.back().setZero();
        layerNames.insert(Channel::head(channelName));
    }

    for (const string& layer : layerNames) {
        result.layers.emplace_back(layer);
    }

    // Empty images currently do not support custom data and display windows
    result.dataWindow = result.displayWindow = result.channels.front().size();

    co_return {result, true};
}

TEV_NAMESPACE_END
