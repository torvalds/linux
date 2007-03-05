#ifndef _GPIO_KEYS_H
#define _GPIO_KEYS_H

struct gpio_keys_button {
	/* Configuration parameters */
	int keycode;
	int gpio;
	int active_low;
	char *desc;
};

struct gpio_keys_platform_data {
	struct gpio_keys_button *buttons;
	int nbuttons;
};

#endif
