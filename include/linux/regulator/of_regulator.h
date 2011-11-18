/*
 * OpenFirmware regulator support routines
 *
 */

#ifndef __LINUX_OF_REG_H
#define __LINUX_OF_REG_H

#if defined(CONFIG_OF)
extern struct regulator_init_data
	*of_get_regulator_init_data(struct device *dev);
#else
static inline struct regulator_init_data
	*of_get_regulator_init_data(struct device *dev)
{
	return NULL;
}
#endif /* CONFIG_OF */

#endif /* __LINUX_OF_REG_H */
