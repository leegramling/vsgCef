#include "vsgthreading/SceneObject.h"

namespace vsgthreading {

SceneObject::SceneObject(uint64_t id, ObjectType type, vsg::ref_ptr<vsg::Node> prototype) :
    id_(id),
    type_(type),
    transform_(vsg::MatrixTransform::create()),
    prototype_(std::move(prototype))
{
    if (prototype_) transform_->addChild(prototype_);
}

void SceneObject::init(vsg::ref_ptr<vsg::Group> parent)
{
    if (parent && transform_) parent->addChild(transform_);
}

void SceneObject::update(const ObjectState& state)
{
    if (!transform_) return;
    transform_->matrix = vsg::translate(state.position) * vsg::scale(state.scale);
}

} // namespace vsgthreading
