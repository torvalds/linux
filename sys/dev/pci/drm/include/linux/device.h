/* Public domain. */

#ifndef _LINUX_DEVICE_H
#define _LINUX_DEVICE_H

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <linux/ioport.h>
#include <linux/lockdep.h>
#include <linux/pm.h>
#include <linux/kobject.h>
#include <linux/ratelimit.h> /* dev_printk.h -> ratelimit.h */
#include <linux/module.h> /* via device/driver.h */
#include <linux/device/bus.h>

struct device_node;

struct device_driver {
	struct device *dev;
};

struct device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device *, struct device_attribute *, char *);
};

#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name

#define device_create_file(a, b)	0
#define device_remove_file(a, b)

void	*dev_get_drvdata(struct device *);
void	dev_set_drvdata(struct device *, void *);

#define dev_pm_set_driver_flags(x, y)

#define devm_kzalloc(x, y, z)	kzalloc(y, z)
#define devm_kfree(x, y)	kfree(y)

static inline int
devm_device_add_group(struct device *dev, const struct attribute_group *g)
{
	return 0;
}

#define dev_warn(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *WARNING* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_WARN(dev, fmt, arg...)					\
	WARN(1, "drm:pid%d:%s *WARNING* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_notice(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *NOTICE* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_crit(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_err(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_emerg(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *EMERGENCY* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_printk(level, dev, fmt, arg...)				\
	printf(fmt, ## arg)

#define dev_warn_ratelimited(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *WARNING* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_notice_ratelimited(dev, fmt, arg...)			\
	printf("drm:pid%d:%s *NOTICE* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_err_ratelimited(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)

#define dev_warn_once(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *WARNING* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_WARN_ONCE(dev, cond, fmt, arg...)					\
	WARN_ONCE(cond, "drm:pid%d:%s *WARNING* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_err_once(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
	
#define dev_err_probe(dev, err, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_p->ps_pid,	\
	       __func__ , ## arg), err

#ifdef DRMDEBUG
#define dev_info(dev, fmt, arg...)				\
	printf("drm: " fmt, ## arg)
#define dev_info_once(dev, fmt, arg...)				\
	printf("drm: " fmt, ## arg)
#define dev_dbg(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *DEBUG* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_dbg_once(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *DEBUG* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#define dev_dbg_ratelimited(dev, fmt, arg...)			\
	printf("drm:pid%d:%s *DEBUG* " fmt, curproc->p_p->ps_pid,	\
	    __func__ , ## arg)
#else

static inline void
dev_info(struct device *dev, const char *fmt, ...)
{
}

static inline void
dev_info_once(struct device *dev, const char *fmt, ...)
{
}

static inline void
dev_dbg(struct device *dev, const char *fmt, ...)
{
}

static inline void
dev_dbg_once(struct device *dev, const char *fmt, ...)
{
}

static inline void
dev_dbg_ratelimited(struct device *dev, const char *fmt, ...)
{
}

#endif

static inline const char *
dev_driver_string(struct device *dev)
{
	return dev->dv_cfdata->cf_driver->cd_name;
}

/* XXX return true for thunderbolt/USB4 */
#define dev_is_removable(x)	false

/* should be bus id as string, ie 0000:00:02.0 */
#define dev_name(dev)		""

static inline void
device_set_wakeup_path(struct device *dev)
{
}

#endif
