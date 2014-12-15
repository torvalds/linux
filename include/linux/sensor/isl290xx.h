#ifndef __LINUX_ISL290XX_H__
#define __LINUX_ISL290XX_H__

struct isl290xx_platform_data {
	char *regulator_name;
	unsigned int resolution;
	unsigned int range;
	int irq_gpio_number;
};

#endif

