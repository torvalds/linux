/* Public domain. */

#ifndef _LINUX_POWER_SUPPLY_H
#define _LINUX_POWER_SUPPLY_H

static inline int
power_supply_is_system_supplied(void)
{
	extern int hw_power;
	return hw_power;
}

#endif
