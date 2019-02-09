// SPDX-License-Identifier: GPL-2.0
///
/// Use drm_*_get() and drm_*_put() helpers instead of drm_*_reference() and
/// drm_*_unreference() helpers.
///
// Confidence: High
// Copyright: (C) 2017 NVIDIA Corporation
// Options: --no-includes --include-headers
//

virtual patch
virtual report

@depends on patch@
expression object;
@@

(
- drm_connector_reference(object)
+ drm_connector_get(object)
|
- drm_connector_unreference(object)
+ drm_connector_put(object)
|
- drm_framebuffer_reference(object)
+ drm_framebuffer_get(object)
|
- drm_framebuffer_unreference(object)
+ drm_framebuffer_put(object)
|
- drm_gem_object_reference(object)
+ drm_gem_object_get(object)
|
- drm_gem_object_unreference(object)
+ drm_gem_object_put(object)
|
- __drm_gem_object_unreference(object)
+ __drm_gem_object_put(object)
|
- drm_gem_object_unreference_unlocked(object)
+ drm_gem_object_put_unlocked(object)
|
- drm_dev_unref(object)
+ drm_dev_put(object)
)

@r depends on report@
expression object;
position p;
@@

(
drm_connector_unreference@p(object)
|
drm_connector_reference@p(object)
|
drm_framebuffer_unreference@p(object)
|
drm_framebuffer_reference@p(object)
|
drm_gem_object_unreference@p(object)
|
drm_gem_object_reference@p(object)
|
__drm_gem_object_unreference(object)
|
drm_gem_object_unreference_unlocked(object)
|
drm_dev_unref@p(object)
)

@script:python depends on report@
object << r.object;
p << r.p;
@@

msg="WARNING: use get/put helpers to reference and dereference %s" % (object)
coccilib.report.print_report(p[0], msg)
