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
- drm_mode_object_reference(object)
+ drm_mode_object_get(object)
|
- drm_mode_object_unreference(object)
+ drm_mode_object_put(object)
|
- drm_connector_reference(object)
+ drm_connector_get(object)
|
- drm_connector_unreference(object)
+ drm_connector_put(object)
)

@r depends on report@
expression object;
position p;
@@

(
drm_mode_object_unreference@p(object)
|
drm_mode_object_reference@p(object)
|
drm_connector_unreference@p(object)
|
drm_connector_reference@p(object)
)

@script:python depends on report@
object << r.object;
p << r.p;
@@

msg="WARNING: use get/put helpers to reference and dereference %s" % (object)
coccilib.report.print_report(p[0], msg)
