/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MAX17047_BATTERY_H_
#define __MAX17047_BATTERY_H_

struct max17047_platform_data {
        int (*battery_online)(void);
        int (*charger_online)(void);
        int (*charger_enable)(void);
};

#endif /* __MAX17047_BATTERY_H_ */
