#ifndef __GPIO_ASPEED_H
#define __GPIO_ASPEED_H

struct aspeed_gpio_copro_ops {
	int (*request_access)(void *data);
	int (*release_access)(void *data);
};

int aspeed_gpio_copro_grab_gpio(struct gpio_desc *desc,
				u16 *vreg_offset, u16 *dreg_offset, u8 *bit);
int aspeed_gpio_copro_release_gpio(struct gpio_desc *desc);
int aspeed_gpio_copro_set_ops(const struct aspeed_gpio_copro_ops *ops, void *data);


#endif /* __GPIO_ASPEED_H */
