#ifndef LINUX_PLATFORM_DATA_TDA9950_H
#define LINUX_PLATFORM_DATA_TDA9950_H

struct device;

struct tda9950_glue {
	struct device *parent;
	unsigned long irq_flags;
	void *data;
	int (*init)(void *);
	void (*exit)(void *);
	int (*open)(void *);
	void (*release)(void *);
};

#endif
