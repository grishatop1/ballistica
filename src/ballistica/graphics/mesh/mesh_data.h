// Released under the MIT License. See LICENSE for details.

#ifndef BALLISTICA_GRAPHICS_MESH_MESH_DATA_H_
#define BALLISTICA_GRAPHICS_MESH_MESH_DATA_H_

#include <list>

#include "ballistica/ballistica.h"

namespace ballistica {

// The portion of a mesh that is owned by the graphics thread.
// This contains the renderer-specific data (GL buffers, etc)
class MeshData {
 public:
  MeshData(MeshDataType type, MeshDrawType draw_type)
      : type_(type), draw_type_(draw_type) {}
  virtual ~MeshData() {
    if (renderer_data_) {
      Log(LogLevel::kError, "MeshData going down with rendererData intact!");
    }
  }
  std::list<MeshData*>::iterator iterator_;
  auto type() -> MeshDataType { return type_; }
  auto draw_type() -> MeshDrawType { return draw_type_; }
  void Load(Renderer* renderer);
  void Unload(Renderer* renderer);
  auto renderer_data() const -> MeshRendererData* {
    assert(renderer_data_);
    return renderer_data_;
  }

 private:
  MeshRendererData* renderer_data_{};
  MeshDataType type_{};
  MeshDrawType draw_type_{};
  BA_DISALLOW_CLASS_COPIES(MeshData);
};

}  // namespace ballistica

#endif  // BALLISTICA_GRAPHICS_MESH_MESH_DATA_H_
