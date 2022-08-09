/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver model for leds and led triggers
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005 Richard Purdie <rpurdie@openedhand.com>
 */
#ifndef __LINUX_LEDS_H_INCLUDED
#define __LINUX_LEDS_H_INCLUDED

#include <dt-bindings/leds/common.h>
#include <linux/device.h>
#include <linux/kernfs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct device;
struct led_pattern;
struct device_node;
/*
 * LED Core
 */

/* This is obsolete/useless. We now support variable maximum brightness. */
enum led_brightness {
	LED_OFF		= 0,
	LED_ON		= 1,
	LED_HALF	= 127,
	LED_FULL	= 255,
};

enum led_default_state {
	LEDS_DEFSTATE_OFF	= 0,
	LEDS_DEFSTATE_ON	= 1,
	LEDS_DEFSTATE_KEEP	= 2,
};

struct led_init_data {
	/* device fwnode handle */
	struct fwnode_handle *fwnode;
	/*
	 * default <color:function> tuple, for backward compatibility
	 * with in-driver hard-coded LED names used as a fallback when
	 * DT "label" property is absent; it should be set to NULL
	 * in new LED class drivers.
	 */
	const char *default_label;
	/*
	 * string to be used for devicename section of LED class device
	 * either for label based LED name composition path or for fwnode
	 * based when devname_mandatory is true
	 */
	const char *devicename;
	/*
	 * indicates if LED name should always comprise devicename section;
	 * only LEDs exposed by drivers of hot-pluggable devices should
	 * set it to true
	 */
	bool devname_mandatory;
};

struct led_hw_trigger_type {
	int dummy;
};

struct led_classdev {
	const char		*name;
	unsigned int brightness;
	unsigned int max_brightness;
	int			 flags;

	/* Lower 16 bits reflect status */
#define LED_SUSPENDED		BIT(0)
#define LED_UNREGISTERING	BIT(1)
	/* Upper 16 bits reflect control information */
#define LED_CORE_SUSPENDRESUME	BIT(16)
#define LED_SYSFS_DISABLE	BIT(17)
#define LED_DEV_CAP_FLASH	BIT(18)
#define LED_HW_PLUGGABLE	BIT(19)
#define LED_PANIC_INDICATOR	BIT(20)
#define LED_BRIGHT_HW_CHANGED	BIT(21)
#define LED_RETAIN_AT_SHUTDOWN	BIT(22)
#define LED_INIT_DEFAULT_TRIGGER BIT(23)

	/* set_brightness_work / blink_timer flags, atomic, private. */
	unsigned long		work_flags;

#define LED_BLINK_SW			0
#define LED_BLINK_ONESHOT		1
#define LED_BLINK_ONESHOT_STOP		2
#define LED_BLINK_INVERT		3
#define LED_BLINK_BRIGHTNESS_CHANGE 	4
#define LED_BLINK_DISABLE		5

	/* Set LED brightness level
	 * Must not sleep. Use brightness_set_blocking for drivers
	 * that can sleep while setting brightness.
	 */
	void		(*brightness_set)(struct led_classdev *led_cdev,
					  enum led_brightness brightness);
	/*
	 * Set LED brightness level immediately - it can block the caller for
	 * the time required for accessing a LED device register.
	 */
	int (*brightness_set_blocking)(struct led_classdev *led_cdev,
				       enum led_brightness brightness);
	/* Get LED brightness level */
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);

	/*
	 * Activate hardware accelerated blink, delays are in milliseconds
	 * and if both are zero then a sensible default should be chosen.
	 * The call should adjust the timings in that case and if it can't
	 * match the values specified exactly.
	 * Deactivate blinking again when the brightness is set to LED_OFF
	 * via the brightness_set() callback.
	 */
	int		(*blink_set)(struct led_classdev *led_cdev,
				     unsigned long *delay_on,
				     unsigned long *delay_off);

	int (*pattern_set)(struct led_classdev *led_cdev,
			   struct led_pattern *pattern, u32 len, int repeat);
	int (*pattern_clear)(struct led_classdev *led_cdev);

	struct device		*dev;
	const struct attribute_group	**groups;

	struct list_head	 node;			/* LED Device list */
	const char		*default_trigger;	/* Trigger to use */

	unsigned long		 blink_delay_on, blink_delay_off;
	struct timer_list	 blink_timer;
	int			 blink_brightness;
	int			 new_blink_brightness;
	void			(*flash_resume)(struct led_classdev *led_cdev);

	struct work_struct	set_brightness_work;
	int			delayed_set_value;

#ifdef CONFIG_LEDS_TRIGGERS
	/* Protects the trigger data below */
	struct rw_semaphore	 trigger_lock;

	struct led_trigger	*trigger;
	struct list_head	 trig_list;
	void			*trigger_data;
	/* true if activated - deactivate routine uses it to do cleanup */
	bool			activated;

	/* LEDs that have private triggers have this set */
	struct led_hw_trigger_type	*trigger_type;
#endif

#ifdef CONFIG_LEDS_BRIGHTNESS_HW_CHANGED
	int			 brightness_hw_changed;
	struct kernfs_node	*brightness_hw_changed_kn;
#endif

	/* Ensures consistent access to the LED Flash Class device */
	struct mutex		led_access;
};

/**
 * led_classdev_register_ext - register a new object of LED class with
 *			       init data
 * @parent: LED controller device this LED is driven by
 * @led_cdev: the led_classdev structure for this device
 * @init_data: the LED class device initialization data
 *
 * Register a new object of LED class, with name derived from init_data.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_classdev_register_ext(struct device *parent,
				     struct led_classdev *led_cdev,
				     struct led_init_data *init_data);

/**
 * led_classdev_register - register a new object of LED class
 * @parent: LED controller device this LED is driven by
 * @led_cdev: the led_classdev structure for this device
 *
 * Register a new object of LED class, with name derived from the name property
 * of passed led_cdev argument.
 *
 * Returns: 0 on success or negative error value on failure
 */
static inline int led_classdev_register(struct device *parent,
					struct led_classdev *led_cdev)
{
	return led_classdev_register_ext(parent, led_cdev, NULL);
}

int devm_led_classdev_register_ext(struct device *parent,
					  struct led_classdev *led_cdev,
					  struct led_init_data *init_data);

static inline int devm_led_classdev_register(struct device *parent,
					     struct led_classdev *led_cdev)
{
	return devm_led_classdev_register_ext(parent, led_cdev, NULL);
}
void led_classdev_unregister(struct led_classdev *led_cdev);
void devm_led_classdev_unregister(struct device *parent,
				  struct led_classdev *led_cdev);
void led_classdev_suspend(struct led_classdev *led_cdev);
void led_classdev_resume(struct led_classdev *led_cdev);

extern struct led_classdev *of_led_get(struct device_node *np, int index);
extern void led_put(struct led_classdev *led_cdev);
struct led_classdev *__must_check devm_of_led_get(struct device *dev,
						  int index);

/**
 * led_blink_set - set blinking with software fallback
 * @led_cdev: the LED to start blinking
 * @delay_on: the time it should be on (in ms)
 * @delay_off: the time it should ble off (in ms)
 *
 * This function makes the LED blink, attempting to use the
 * hardware acceleration if possible, but falling back to
 * software blinking if there is no hardware blinking or if
 * the LED refuses the passed values.
 *
 * Note that if software blinking is active, simply calling
 * led_cdev->brightness_set() will not stop the blinking,
 * use led_classdev_brightness_set() instead.
 */
void led_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on,
		   unsigned long *delay_off);
/**
 * led_blink_set_oneshot - do a oneshot software blink
 * @led_cdev: the LED to start blinking
 * @delay_on: the time it should be on (in ms)
 * @delay_off: the time it should ble off (in ms)
 * @invert: blink off, then on, leaving the led on
 *
 * This function makes the LED blink one time for delay_on +
 * delay_off time, ignoring the request if another one-shot
 * blink is already in progress.
 *
 * If invert is set, led blinks for delay_off first, then for
 * delay_on and leave the led on after the on-off cycle.
 */
void led_blink_set_oneshot(struct led_classdev *led_cdev,
			   unsigned long *delay_on, unsigned long *delay_off,
			   int invert);
/**
 * led_set_brightness - set LED brightness
 * @led_cdev: the LED to set
 * @brightness: the brightness to set it to
 *
 * Set an LED's brightness, and, if necessary, cancel the
 * software blink timer that implements blinking when the
 * hardware doesn't. This function is guaranteed not to sleep.
 */
void led_set_brightness(struct led_classdev *led_cdev, unsigned int brightness);

/**
 * led_set_brightness_sync - set LED brightness synchronously
 * @led_cdev: the LED to set
 * @value: the brightness to set it to
 *
 * Set an LED's brightness immediately. This function will block
 * the caller for the time required for accessing device registers,
 * and it can sleep.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_set_brightness_sync(struct led_classdev *led_cdev, unsigned int value);

/**
 * led_update_brightness - update LED brightness
 * @led_cdev: the LED to query
 *
 * Get an LED's current brightness and update led_cdev->brightness
 * member with the obtained value.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_update_brightness(struct led_classdev *led_cdev);

/**
 * led_get_default_pattern - return default pattern
 *
 * @led_cdev: the LED to get default pattern for
 * @size:     pointer for storing the number of elements in returned array,
 *            modified only if return != NULL
 *
 * Return:    Allocated array of integers with default pattern from device tree
 *            or NULL.  Caller is responsible for kfree().
 */
u32 *led_get_default_pattern(struct led_classdev *led_cdev, unsigned int *size);

/**
 * led_sysfs_disable - disable LED sysfs interface
 * @led_cdev: the LED to set
 *
 * Disable the led_cdev's sysfs interface.
 */
void led_sysfs_disable(struct led_classdev *led_cdev);

/**
 * led_sysfs_enable - enable LED sysfs interface
 * @led_cdev: the LED to set
 *
 * Enable the led_cdev's sysfs interface.
 */
void led_sysfs_enable(struct led_classdev *led_cdev);

/**
 * led_compose_name - compose LED class device name
 * @dev: LED controller device object
 * @init_data: the LED class device initialization data
 * @led_classdev_name: composed LED class device name
 *
 * Create LED class device name basing on the provided init_data argument.
 * The name can have <devicename:color:function> or <color:function>.
 * form, depending on the init_data configuration.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_compose_name(struct device *dev, struct led_init_data *init_data,
		     char *led_classdev_name);

/**
 * led_sysfs_is_disabled - check if LED sysfs interface is disabled
 * @led_cdev: the LED to query
 *
 * Returns: true if the led_cdev's sysfs interface is disabled.
 */
static inline bool led_sysfs_is_disabled(struct led_classdev *led_cdev)
{
	return led_cdev->flags & LED_SYSFS_DISABLE;
}

/*
 * LED Triggers
 */
/* Registration functions for simple triggers */
#define DEFINE_LED_TRIGGER(x)		static struct led_trigger *x;
#define DEFINE_LED_TRIGGER_GLOBAL(x)	struct led_trigger *x;

#ifdef CONFIG_LEDS_TRIGGERS

#define TRIG_NAME_MAX 50

struct led_trigger {
	/* Trigger Properties */
	const char	 *name;
	int		(*activate)(struct led_classdev *led_cdev);
	void		(*deactivate)(struct led_classdev *led_cdev);

	/* LED-private triggers have this set */
	struct led_hw_trigger_type *trigger_type;

	/* LEDs under control by this trigger (for simple triggers) */
	rwlock_t	  leddev_list_lock;
	struct list_head  led_cdevs;

	/* Link to next registered trigger */
	struct list_head  next_trig;

	const struct attribute_group **groups;
};

/*
 * Currently the attributes in struct led_trigger::groups are added directly to
 * the LED device. As this might change in the future, the following
 * macros abstract getting the LED device and its trigger_data from the dev
 * parameter passed to the attribute accessor functions.
 */
#define led_trigger_get_led(dev)	((struct led_classdev *)dev_get_drvdata((dev)))
#define led_trigger_get_drvdata(dev)	(led_get_trigger_data(led_trigger_get_led(dev)))

/* Registration functions for complex triggers */
int led_trigger_register(struct led_trigger *trigger);
void led_trigger_unregister(struct led_trigger *trigger);
int devm_led_trigger_register(struct device *dev,
				     struct led_trigger *trigger);

void led_trigger_register_simple(const char *name,
				struct led_trigger **trigger);
void led_trigger_unregister_simple(struct led_trigger *trigger);
void led_trigger_event(struct led_trigger *trigger,  enum led_brightness event);
void led_trigger_blink(struct led_trigger *trigger, unsigned long *delay_on,
		       unsigned long *delay_off);
void led_trigger_blink_oneshot(struct led_trigger *trigger,
			       unsigned long *delay_on,
			       unsigned long *delay_off,
			       int invert);
void led_trigger_set_default(struct led_classdev *led_cdev);
int led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trigger);
void led_trigger_remove(struct led_classdev *led_cdev);

static inline void led_set_trigger_data(struct led_classdev *led_cdev,
					void *trigger_data)
{
	led_cdev->trigger_data = trigger_data;
}

static inline void *led_get_trigger_data(struct led_classdev *led_cdev)
{
	return led_cdev->trigger_data;
}

/**
 * led_trigger_rename_static - rename a trigger
 * @name: the new trigger name
 * @trig: the LED trigger to rename
 *
 * Change a LED trigger name by copying the string passed in
 * name into current trigger name, which MUST be large
 * enough for the new string.
 *
 * Note that name must NOT point to the same string used
 * during LED registration, as that could lead to races.
 *
 * This is meant to be used on triggers with statically
 * allocated name.
 */
void led_trigger_rename_static(const char *name, struct led_trigger *trig);

#define module_led_trigger(__led_trigger) \
	module_driver(__led_trigger, led_trigger_register, \
		      led_trigger_unregister)

#else

/* Trigger has no members */
struct led_trigger {};

/* Trigger inline empty functions */
static inline void led_trigger_register_simple(const char *name,
					struct led_trigger **trigger) {}
static inline void led_trigger_unregister_simple(struct led_trigger *trigger) {}
static inline void led_trigger_event(struct led_trigger *trigger,
				enum led_brightness event) {}
static inline void led_trigger_blink(struct led_trigger *trigger,
				      unsigned long *delay_on,
				      unsigned long *delay_off) {}
static inline void led_trigger_blink_oneshot(struct led_trigger *trigger,
				      unsigned long *delay_on,
				      unsigned long *delay_off,
				      int invert) {}
static inline void led_trigger_set_default(struct led_classdev *led_cdev) {}
static inline int led_trigger_set(struct led_classdev *led_cdev,
				  struct led_trigger *trigger)
{
	return 0;
}

static inline void led_trigger_remove(struct led_classdev *led_cdev) {}
static inline void led_set_trigger_data(struct led_classdev *led_cdev) {}
static inline void *led_get_trigger_data(struct led_classdev *led_cdev)
{
	return NULL;
}

#endif /* CONFIG_LEDS_TRIGGERS */

/* Trigger specific functions */
#ifdef CONFIG_LEDS_TRIGGER_DISK
void ledtrig_disk_activity(bool write);
#else
static inline void ledtrig_disk_activity(bool write) {}
#endif

#ifdef CONFIG_LEDS_TRIGGER_MTD
void ledtrig_mtd_activity(void);
#else
static inline void ledtrig_mtd_activity(void) {}
#endif

#if defined(CONFIG_LEDS_TRIGGER_CAMERA) || defined(CONFIG_LEDS_TRIGGER_CAMERA_MODULE)
void ledtrig_flash_ctrl(bool on);
void ledtrig_torch_ctrl(bool on);
#else
static inline void ledtrig_flash_ctrl(bool on) {}
static inline void ledtrig_torch_ctrl(bool on) {}
#endif

/*
 * Generic LED platform data for describing LED names and default triggers.
 */
struct led_info {
	const char	*name;
	const char	*default_trigger;
	int		flags;
};

struct led_platform_data {
	int		num_leds;
	struct led_info	*leds;
};

struct led_properties {
	u32		color;
	bool		color_present;
	const char	*function;
	u32		func_enum;
	bool		func_enum_present;
	const char	*label;
};

struct gpio_desc;
typedef int (*gpio_blink_set_t)(struct gpio_desc *desc, int state,
				unsigned long *delay_on,
				unsigned long *delay_off);

/* For the leds-gpio driver */
struct gpio_led {
	const char *name;
	const char *default_trigger;
	unsigned 	gpio;
	unsigned	active_low : 1;
	unsigned	retain_state_suspended : 1;
	unsigned	panic_indicator : 1;
	unsigned	default_state : 2;
	unsigned	retain_state_shutdown : 1;
	/* default_state should be one of LEDS_GPIO_DEFSTATE_(ON|OFF|KEEP) */
	struct gpio_desc *gpiod;
};
#define LEDS_GPIO_DEFSTATE_OFF		LEDS_DEFSTATE_OFF
#define LEDS_GPIO_DEFSTATE_ON		LEDS_DEFSTATE_ON
#define LEDS_GPIO_DEFSTATE_KEEP		LEDS_DEFSTATE_KEEP

struct gpio_led_platform_data {
	int 		num_leds;
	const struct gpio_led *leds;

#define GPIO_LED_NO_BLINK_LOW	0	/* No blink GPIO state low */
#define GPIO_LED_NO_BLINK_HIGH	1	/* No blink GPIO state high */
#define GPIO_LED_BLINK		2	/* Please, blink */
	gpio_blink_set_t	gpio_blink_set;
};

#ifdef CONFIG_NEW_LEDS
struct platform_device *gpio_led_register_device(
		int id, const struct gpio_led_platform_data *pdata);
#else
static inline struct platform_device *gpio_led_register_device(
		int id, const struct gpio_led_platform_data *pdata)
{
	return 0;
}
#endif

enum cpu_led_event {
	CPU_LED_IDLE_START,	/* CPU enters idle */
	CPU_LED_IDLE_END,	/* CPU idle ends */
	CPU_LED_START,		/* Machine starts, especially resume */
	CPU_LED_STOP,		/* Machine stops, especially suspend */
	CPU_LED_HALTED,		/* Machine shutdown */
};
#ifdef CONFIG_LEDS_TRIGGER_CPU
void ledtrig_cpu(enum cpu_led_event evt);
#else
static inline void ledtrig_cpu(enum cpu_led_event evt)
{
	return;
}
#endif

#ifdef CONFIG_LEDS_BRIGHTNESS_HW_CHANGED
void led_classdev_notify_brightness_hw_changed(
	struct led_classdev *led_cdev, unsigned int brightness);
#else
static inline void led_classdev_notify_brightness_hw_changed(
	struct led_classdev *led_cdev, enum led_brightness brightness) { }
#endif

/**
 * struct led_pattern - pattern interval settings
 * @delta_t: pattern interval delay, in milliseconds
 * @brightness: pattern interval brightness
 */
struct led_pattern {
	u32 delta_t;
	int brightness;
};

enum led_audio {
	LED_AUDIO_MUTE,		/* master mute LED */
	LED_AUDIO_MICMUTE,	/* mic mute LED */
	NUM_AUDIO_LEDS
};

#if IS_ENABLED(CONFIG_LEDS_TRIGGER_AUDIO)
enum led_brightness ledtrig_audio_get(enum led_audio type);
void ledtrig_audio_set(enum led_audio type, enum led_brightness state);
#else
static inline enum led_brightness ledtrig_audio_get(enum led_audio type)
{
	return LED_OFF;
}
static inline void ledtrig_audio_set(enum led_audio type,
				     enum led_brightness state)
{
}
#endif

#endif		/* __LINUX_LEDS_H_INCLUDED */
