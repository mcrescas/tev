// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include <tev/Common.h>

#include <Eigen/Dense>

#include <ostream>
#include <string>
#include <vector>

TEV_NAMESPACE_BEGIN

template <typename T>
class TypedImageSaver;

class ImageSaver {
public:
    virtual ~ImageSaver() {}

    virtual bool hasPremultipliedAlpha() const = 0;

    virtual bool canSaveFile(const std::string& extension) const = 0;
    bool canSaveFile(const filesystem::path& path) const {
        return canSaveFile(toLower(path.extension()));
    }

    static const std::vector<std::unique_ptr<ImageSaver>>& getSavers();
};

template <typename T>
class TypedImageSaver : public ImageSaver {
public:
    virtual void save(std::ostream& oStream, const ::filesystem::path& path, const std::vector<T>& data, const Eigen::Vector2i& imageSize, int nChannels) const = 0;
};

TEV_NAMESPACE_END
