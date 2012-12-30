#ifndef __LINUX_CT36X__
#define __LINUX_CT36X__

#define CT36X_NAME	"ct36x_ts"

struct ct36x_gpio{
	int gpio;
	int active_low;
};

struct ct36x_platform_data{
	int model;

	int x_max;
	int y_max;
	
	struct ct36x_gpio rst_io;
	struct ct36x_gpio irq_io;

	int orientation[4];
};

#endif
