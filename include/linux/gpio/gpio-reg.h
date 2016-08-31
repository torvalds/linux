#ifndef GPIO_REG_H
#define GPIO_REG_H

struct device;

struct gpio_chip *gpio_reg_init(struct device *dev, void __iomem *reg,
	int base, int num, const char *label, u32 direction, u32 def_out,
	const char *const *names);

int gpio_reg_resume(struct gpio_chip *gc);

#endif
