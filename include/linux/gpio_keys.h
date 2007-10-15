#ifndef _GPIO_KEYS_H
#define _GPIO_KEYS_H

struct gpio_keys_button {
	/* Configuration parameters */
	int code;		/* input event code (KEY_*, SW_*) */
	int gpio;
	int active_low;
	char *desc;
	int type;		/* input event type (EV_KEY, EV_SW) */
	int wakeup;		/* configure the button as a wake-up source */
};

struct gpio_keys_platform_data {
	struct gpio_keys_button *buttons;
	int nbuttons;
};

#endif
