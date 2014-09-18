#ifndef __GPIO_EM_H__
#define __GPIO_EM_H__

struct gpio_em_config {
	unsigned int gpio_base;
	unsigned int irq_base;
	unsigned int number_of_pins;
	const char *pctl_name;
};

#endif /* __GPIO_EM_H__ */
