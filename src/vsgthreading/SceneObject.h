#pragma once

#include "vsgthreading/FrameData.h"

#include <vsg/all.h>

namespace vsgthreading {

class SceneObject : public vsg::Inherit<vsg::Object, SceneObject>
{
public:
    SceneObject(uint64_t id, ObjectType type, vsg::ref_ptr<vsg::Node> prototype);

    uint64_t id() const { return id_; }
    ObjectType type() const { return type_; }
    vsg::ref_ptr<vsg::MatrixTransform> node() const { return transform_; }

    void init(vsg::ref_ptr<vsg::Group> parent);
    void update(const ObjectState& state);

private:
    uint64_t id_ = 0;
    ObjectType type_ = ObjectType::Cube;
    vsg::ref_ptr<vsg::MatrixTransform> transform_;
    vsg::ref_ptr<vsg::Node> prototype_;
};

} // namespace vsgthreading
