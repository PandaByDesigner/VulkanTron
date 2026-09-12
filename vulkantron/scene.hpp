#pragma once
#include "renderer.hpp"
#include "classic_bridge.h"

namespace vt {
class Scene {
public:
    explicit Scene(const std::filesystem::path& asset_root);
    Frame frame(const VTSnapshot& state, float aspect, bool overview) const;
    std::size_t cycle_vertices() const { return cycle_.size(); }
private:
    std::vector<Vertex> cycle_;
};
}
