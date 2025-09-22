/* Public domain. */

#ifndef _LINUX_BACKLIGHT_H
#define _LINUX_BACKLIGHT_H

#include <sys/task.h>
#include <linux/fb.h>

struct backlight_device;
struct device;

struct backlight_properties {
	int type;
#define BACKLIGHT_RAW		0
#define BACKLIGHT_FIRMWARE	1
#define BACKLIGHT_PLATFORM	2
	int max_brightness;
	int brightness;
	int power;
#define BACKLIGHT_POWER_ON	0
#define BACKLIGHT_POWER_OFF	1
	int scale;
#define BACKLIGHT_SCALE_LINEAR	0
	int state;
#define BL_CORE_SUSPENDED	0x00000001
};

struct backlight_ops {
	int options;
#define BL_CORE_SUSPENDRESUME	0x00000001
	int (*update_status)(struct backlight_device *);
	int (*get_brightness)(struct backlight_device *);
};

struct backlight_device {
	const struct backlight_ops *ops;
	struct backlight_properties props;
	struct task task;
	void *data;
	SLIST_ENTRY(backlight_device) next;
	const char *name;
};

static inline void *
bl_get_data(struct backlight_device *bd)
{
	return bd->data;
}

static inline int
backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

#define BACKLIGHT_UPDATE_HOTKEY	0

struct backlight_device *backlight_device_register(const char *, void *,
    void *, const struct backlight_ops *, const struct backlight_properties *);
void backlight_device_unregister(struct backlight_device *);

static inline struct backlight_device *
devm_backlight_device_register(void *dev, const char *name, void *parent,
    void *data, const struct backlight_ops *bo,
    const struct backlight_properties *bp)
{
	return backlight_device_register(name, dev, data, bo, bp);
}

static inline void
backlight_update_status(struct backlight_device *bd)
{
	bd->ops->update_status(bd);
}

static inline void
backlight_force_update(struct backlight_device *bd, int reason)
{
	bd->props.brightness = bd->ops->get_brightness(bd);
}

static inline void
backlight_device_set_brightness(struct backlight_device *bd, int level)
{
	if (level > bd->props.max_brightness)
		return;
	bd->props.brightness = level;
	bd->ops->update_status(bd);
}

void backlight_schedule_update_status(struct backlight_device *);

int backlight_enable(struct backlight_device *);
int backlight_disable(struct backlight_device *);

static inline struct backlight_device *
devm_of_find_backlight(struct device *dev)
{
	return NULL;
}

struct backlight_device *backlight_device_get_by_name(const char *);

#endif
