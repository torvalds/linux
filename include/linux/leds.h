/*
 * Driver model for leds and led triggers
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005 Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_LEDS_H_INCLUDED
#define __LINUX_LEDS_H_INCLUDED

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct device;
/*
 * LED Core
 */

enum led_brightness {
	LED_OFF		= 0,
	LED_HALF	= 127,
	LED_FULL	= 255,
};

struct led_classdev {
	const char		*name;
	int			 brightness;
	int			 max_brightness;
	int			 flags;

	/* Lower 16 bits reflect status */
#define LED_SUSPENDED		(1 << 0)
	/* Upper 16 bits reflect control information */
#define LED_CORE_SUSPENDRESUME	(1 << 16)
#define LED_BLINK_ONESHOT	(1 << 17)
#define LED_BLINK_ONESHOT_STOP	(1 << 18)
#define LED_BLINK_INVERT	(1 << 19)

	/* Set LED brightness level */
	/* Must not sleep, use a workqueue if needed */
	void		(*brightness_set)(struct led_classdev *led_cdev,
					  enum led_brightness brightness);
	/* Get LED brightness level */
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);

	/*
	 * Activate hardware accelerated blink, delays are in milliseconds
	 * and if both are zero then a sensible default should be chosen.
	 * The call should adjust the timings in that case and if it can't
	 * match the values specified exactly.
	 * Deactivate blinking again when the brightness is set to a fixed
	 * value via the brightness_set() callback.
	 */
	int		(*blink_set)(struct led_classdev *led_cdev,
				     unsigned long *delay_on,
				     unsigned long *delay_off);

	struct device		*dev;
	struct list_head	 node;			/* LED Device list */
	const char		*default_trigger;	/* Trigger to use */

	unsigned long		 blink_delay_on, blink_delay_off;
	struct timer_list	 blink_timer;
	int			 blink_brightness;

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
#endif
};

extern int led_classdev_register(struct device *parent,
				 struct led_classdev *led_cdev);
extern void led_classdev_unregister(struct led_classdev *led_cdev);
extern void led_classdev_suspend(struct led_classdev *led_cdev);
extern void led_classdev_resume(struct led_classdev *led_cdev);

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
extern void led_blink_set(struct led_classdev *led_cdev,
			  unsigned long *delay_on,
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
extern void led_blink_set_oneshot(struct led_classdev *led_cdev,
				  unsigned long *delay_on,
				  unsigned long *delay_off,
				  int invert);
/**
 * led_set_brightness - set LED brightness
 * @led_cdev: the LED to set
 * @brightness: the brightness to set it to
 *
 * Set an LED's brightness, and, if necessary, cancel the
 * software blink timer that implements blinking when the
 * hardware doesn't.
 */
extern void led_set_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness);

/*
 * LED Triggers
 */
#ifdef CONFIG_LEDS_TRIGGERS

#define TRIG_NAME_MAX 50

struct led_trigger {
	/* Trigger Properties */
	const char	 *name;
	void		(*activate)(struct led_classdev *led_cdev);
	void		(*deactivate)(struct led_classdev *led_cdev);

	/* LEDs under control by this trigger (for simple triggers) */
	rwlock_t	  leddev_list_lock;
	struct list_head  led_cdevs;

	/* Link to next registered trigger */
	struct list_head  next_trig;
};

/* Registration functions for complex triggers */
extern int led_trigger_register(struct led_trigger *trigger);
extern void led_trigger_unregister(struct led_trigger *trigger);

/* Registration functions for simple triggers */
#define DEFINE_LED_TRIGGER(x)		static struct led_trigger *x;
#define DEFINE_LED_TRIGGER_GLOBAL(x)	struct led_trigger *x;
extern void led_trigger_register_simple(const char *name,
				struct led_trigger **trigger);
extern void led_trigger_unregister_simple(struct led_trigger *trigger);
extern void led_trigger_event(struct led_trigger *trigger,
				enum led_brightness event);
extern void led_trigger_blink(struct led_trigger *trigger,
			      unsigned long *delay_on,
			      unsigned long *delay_off);
extern void led_trigger_blink_oneshot(struct led_trigger *trigger,
				      unsigned long *delay_on,
				      unsigned long *delay_off,
				      int invert);
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
extern void led_trigger_rename_static(const char *name,
				      struct led_trigger *trig);

#else

/* Triggers aren't active - null macros */
#define DEFINE_LED_TRIGGER(x)
#define DEFINE_LED_TRIGGER_GLOBAL(x)
#define led_trigger_register_simple(x, y) do {} while(0)
#define led_trigger_unregister_simple(x) do {} while(0)
#define led_trigger_event(x, y) do {} while(0)

#endif

/* Trigger specific functions */
#ifdef CONFIG_LEDS_TRIGGER_IDE_DISK
extern void ledtrig_ide_activity(void);
#else
#define ledtrig_ide_activity() do {} while(0)
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

/* For the leds-gpio driver */
struct gpio_led {
	const char *name;
	const char *default_trigger;
	unsigned 	gpio;
	unsigned	active_low : 1;
	unsigned	retain_state_suspended : 1;
	unsigned	default_state : 2;
	/* default_state should be one of LEDS_GPIO_DEFSTATE_(ON|OFF|KEEP) */
};
#define LEDS_GPIO_DEFSTATE_OFF		0
#define LEDS_GPIO_DEFSTATE_ON		1
#define LEDS_GPIO_DEFSTATE_KEEP		2

struct gpio_led_platform_data {
	int 		num_leds;
	const struct gpio_led *leds;

#define GPIO_LED_NO_BLINK_LOW	0	/* No blink GPIO state low */
#define GPIO_LED_NO_BLINK_HIGH	1	/* No blink GPIO state high */
#define GPIO_LED_BLINK		2	/* Please, blink */
	int		(*gpio_blink_set)(unsigned gpio, int state,
					unsigned long *delay_on,
					unsigned long *delay_off);
};

struct platform_device *gpio_led_register_device(
		int id, const struct gpio_led_platform_data *pdata);

enum cpu_led_event {
	CPU_LED_IDLE_START,	/* CPU enters idle */
	CPU_LED_IDLE_END,	/* CPU idle ends */
	CPU_LED_START,		/* Machine starts, especially resume */
	CPU_LED_STOP,		/* Machine stops, especially suspend */
	CPU_LED_HALTED,		/* Machine shutdown */
};
#ifdef CONFIG_LEDS_TRIGGER_CPU
extern void ledtrig_cpu(enum cpu_led_event evt);
#else
static inline void ledtrig_cpu(enum cpu_led_event evt)
{
	return;
}
#endif

#endif		/* __LINUX_LEDS_H_INCLUDED */
