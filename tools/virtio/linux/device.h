#ifndef LINUX_DEVICE_H

struct device {
	void *parent;
};

struct device_driver {
	const char *name;
};
#endif
