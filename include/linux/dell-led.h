#ifndef __DELL_LED_H__
#define __DELL_LED_H__

enum {
	DELL_LED_MICMUTE,
};

int dell_app_wmi_led_set(int whichled, int on);

#endif
