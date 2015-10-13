#ifndef __LINUX_GPIO_MACHINE_H
#define __LINUX_GPIO_MACHINE_H

#include <linux/types.h>
#include <linux/list.h>

enum gpio_lookup_flags {
	GPIO_ACTIVE_HIGH = (0 << 0),
	GPIO_ACTIVE_LOW = (1 << 0),
	GPIO_OPEN_DRAIN = (1 << 1),
	GPIO_OPEN_SOURCE = (1 << 2),
};

/**
 * struct gpiod_lookup - lookup table
 * @chip_label: name of the chip the GPIO belongs to
 * @chip_hwnum: hardware number (i.e. relative to the chip) of the GPIO
 * @con_id: name of the GPIO from the device's point of view
 * @idx: index of the GPIO in case several GPIOs share the same name
 * @flags: mask of GPIO_* values
 *
 * gpiod_lookup is a lookup table for associating GPIOs to specific devices and
 * functions using platform data.
 */
struct gpiod_lookup {
	const char *chip_label;
	u16 chip_hwnum;
	const char *con_id;
	unsigned int idx;
	enum gpio_lookup_flags flags;
};

struct gpiod_lookup_table {
	struct list_head list;
	const char *dev_id;
	struct gpiod_lookup table[];
};

/*
 * Simple definition of a single GPIO under a con_id
 */
#define GPIO_LOOKUP(_chip_label, _chip_hwnum, _con_id, _flags) \
	GPIO_LOOKUP_IDX(_chip_label, _chip_hwnum, _con_id, 0, _flags)

/*
 * Use this macro if you need to have several GPIOs under the same con_id.
 * Each GPIO needs to use a different index and can be accessed using
 * gpiod_get_index()
 */
#define GPIO_LOOKUP_IDX(_chip_label, _chip_hwnum, _con_id, _idx, _flags)  \
{                                                                         \
	.chip_label = _chip_label,                                        \
	.chip_hwnum = _chip_hwnum,                                        \
	.con_id = _con_id,                                                \
	.idx = _idx,                                                      \
	.flags = _flags,                                                  \
}

void gpiod_add_lookup_table(struct gpiod_lookup_table *table);
void gpiod_remove_lookup_table(struct gpiod_lookup_table *table);

#endif /* __LINUX_GPIO_MACHINE_H */
