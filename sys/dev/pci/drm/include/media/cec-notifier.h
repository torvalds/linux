/* Public domain. */

#ifndef _MEDIA_CEC_NOTIFIER_H
#define _MEDIA_CEC_NOTIFIER_H

#include <linux/debugfs.h> /* via media/cec.h */

struct cec_notifier;

struct cec_connector_info {
};

static inline void
cec_notifier_set_phys_addr_from_edid(struct cec_notifier *cn,
    const struct edid *edid)
{
}

static inline void
cec_notifier_phys_addr_invalidate(struct cec_notifier *cn)
{
}

static inline void
cec_fill_conn_info_from_drm(struct cec_connector_info *ci,
    const struct drm_connector *c)
{
}

static inline struct cec_notifier *
cec_notifier_conn_register(struct device *dev, const char *port,
    const struct cec_connector_info *ci)
{
	return (void *)1;
}

static inline void
cec_notifier_conn_unregister(struct cec_notifier *cn)
{
}

static inline void
cec_notifier_set_phys_addr(struct cec_notifier *cn, uint16_t paddr)
{
}
#endif
