/* Public domain. */

#include <drm/drm_device.h>
#include <drm/drm_connector.h>

int
drm_connector_attach_content_protection_property(struct drm_connector *connector,
    bool hdcp_content_type)
{
	return -ENOSYS;
}

int
drm_hdcp_check_ksvs_revoked(struct drm_device *drm_dev, u8 *ksvs, u32 ksv_count)
{
	STUB();
	return -ENOSYS;
}

void
drm_hdcp_update_content_protection(struct drm_connector *connector, u64 val)
{
	STUB();
}
