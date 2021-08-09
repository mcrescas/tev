// This file was developed by Thomas Müller <thomas94@gmx.net>.
// It is published under the BSD 3-Clause License within the LICENSE file.

#pragma once

#include <tev/Common.h>
#include <tev/ThreadPool.h>

#include <nanogui/vector.h>

#include <Eigen/Dense>

#include <future>
#include <string>
#include <vector>

TEV_NAMESPACE_BEGIN

class Channel {
public:
    using RowMatrixXf = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

    Channel(const std::string& name, Eigen::Vector2i size);

    const std::string& name() const {
        return mName;
    }

    const RowMatrixXf& data() const {
        return mData;
    }

    float eval(Eigen::DenseIndex index) const {
        if (index >= mData.size()) {
            return 0;
        }
        return mData(index);
    }

    float eval(Eigen::Vector2i index) const {
        if (index.x() < 0 || index.x() >= mData.cols() ||
            index.y() < 0 || index.y() >= mData.rows()) {
            return 0;
        }

        return mData(index.x() + index.y() * mData.cols());
    }

    float& at(Eigen::DenseIndex index) {
        return mData(index);
    }

    float at(Eigen::DenseIndex index) const {
        return mData(index);
    }

    float& at(Eigen::Vector2i index) {
        return at(index.x() + index.y() * mData.cols());
    }

    float at(Eigen::Vector2i index) const {
        return at(index.x() + index.y() * mData.cols());
    }

    Eigen::DenseIndex count() const {
        return mData.size();
    }

    Eigen::Vector2i size() const {
        return {mData.cols(), mData.rows()};
    }

    auto divideByAsync(const Channel& other, int priority) {
        return gThreadPool->parallelForAsync<Eigen::DenseIndex>(0, other.count(), [&](Eigen::DenseIndex i) {
            if (other.at(i) != 0) {
                at(i) /= other.at(i);
            } else {
                at(i) = 0;
            }
        }, priority);
    }

    auto multiplyWithAsync(const Channel& other, int priority) {
        return gThreadPool->parallelForAsync<Eigen::DenseIndex>(0, other.count(), [&](Eigen::DenseIndex i) {
            at(i) *= other.at(i);
        }, priority);
    }

    void setZero() { mData.setZero(); }

    void updateTile(int x, int y, int width, int height, const std::vector<float>& newData);

    static std::pair<std::string, std::string> split(const std::string& fullChannel);

    static std::string tail(const std::string& fullChannel);
    static std::string head(const std::string& fullChannel);

    static bool isTopmost(const std::string& fullChannel);

    static nanogui::Color color(std::string fullChannel);

private:
    std::string mName;
    RowMatrixXf mData;
};

TEV_NAMESPACE_END
