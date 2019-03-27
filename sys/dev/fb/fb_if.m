#include <sys/bus.h>
#include <sys/fbio.h>

INTERFACE fb;

METHOD int pin_max {
	device_t dev;
	int *npins;
};

METHOD struct fb_info * getinfo {
	device_t dev;
};
