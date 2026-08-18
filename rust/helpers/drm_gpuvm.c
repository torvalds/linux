// SPDX-License-Identifier: GPL-2.0 or MIT

#ifdef CONFIG_RUST_DRM_GPUVM

#include <drm/drm_gpuvm.h>

__rust_helper
struct drm_gpuvm_bo *rust_helper_drm_gpuvm_bo_get(struct drm_gpuvm_bo *vm_bo)
{
	return drm_gpuvm_bo_get(vm_bo);
}

__rust_helper
struct drm_gpuvm *rust_helper_drm_gpuvm_get(struct drm_gpuvm *obj)
{
	return drm_gpuvm_get(obj);
}

__rust_helper
bool rust_helper_drm_gpuvm_is_extobj(struct drm_gpuvm *gpuvm,
				     struct drm_gem_object *obj)
{
	return drm_gpuvm_is_extobj(gpuvm, obj);
}

#endif // CONFIG_RUST_DRM_GPUVM
