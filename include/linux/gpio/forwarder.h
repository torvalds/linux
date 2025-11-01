/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GPIO_FORWARDER_H
#define __LINUX_GPIO_FORWARDER_H

struct gpio_desc;
struct gpio_chip;
struct gpiochip_fwd;

struct gpiochip_fwd *devm_gpiochip_fwd_alloc(struct device *dev,
					     unsigned int ngpios);
int gpiochip_fwd_desc_add(struct gpiochip_fwd *fwd,
			  struct gpio_desc *desc, unsigned int offset);
void gpiochip_fwd_desc_free(struct gpiochip_fwd *fwd, unsigned int offset);
int gpiochip_fwd_register(struct gpiochip_fwd *fwd, void *data);

struct gpio_chip *gpiochip_fwd_get_gpiochip(struct gpiochip_fwd *fwd);

void *gpiochip_fwd_get_data(struct gpiochip_fwd *fwd);

int gpiochip_fwd_gpio_request(struct gpiochip_fwd *fwd, unsigned int offset);
int gpiochip_fwd_gpio_get_direction(struct gpiochip_fwd *fwd,
				    unsigned int offset);
int gpiochip_fwd_gpio_direction_input(struct gpiochip_fwd *fwd,
				      unsigned int offset);
int gpiochip_fwd_gpio_direction_output(struct gpiochip_fwd *fwd,
				       unsigned int offset,
				       int value);
int gpiochip_fwd_gpio_get(struct gpiochip_fwd *fwd, unsigned int offset);
int gpiochip_fwd_gpio_get_multiple(struct gpiochip_fwd *fwd,
				   unsigned long *mask,
				   unsigned long *bits);
int gpiochip_fwd_gpio_set(struct gpiochip_fwd *fwd, unsigned int offset,
			  int value);
int gpiochip_fwd_gpio_set_multiple(struct gpiochip_fwd *fwd,
				   unsigned long *mask,
				   unsigned long *bits);
int gpiochip_fwd_gpio_set_config(struct gpiochip_fwd *fwd, unsigned int offset,
				 unsigned long config);
int gpiochip_fwd_gpio_to_irq(struct gpiochip_fwd *fwd, unsigned int offset);

#endif
