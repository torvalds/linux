/*
 * Copyright (c) 2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifndef __RFKILL_GPIO_H
#define __RFKILL_GPIO_H

#include <linux/types.h>
#include <linux/rfkill.h>

#define RFKILL_RK_GPIO_NAME_SIZE   64

struct rfkill_rk_iomux {
    char    *name;
    int     fgpio;
    int     fmux;
};

struct rfkill_rk_gpio {
    int     io;
    char    name[RFKILL_RK_GPIO_NAME_SIZE];
    int     enable; // disable = !enable
    struct rfkill_rk_iomux  iomux;
};

struct rfkill_rk_irq {
    char                    name[RFKILL_RK_GPIO_NAME_SIZE];
    struct rfkill_rk_gpio   gpio;
    int                     irq;
};

/**
 * struct rfkill_rk_platform_data - platform data for rfkill gpio device.
 * for unused gpio's, the expected value is -1.
 * @name:               name for the gpio rf kill instance
 * @reset_gpio:         GPIO which is used for reseting rfkill switch
 * @shutdown_gpio:      GPIO which is used for shutdown of rfkill switch
 */

struct rfkill_rk_platform_data {
    char                    *name;
    enum rfkill_type        type;
    struct rfkill_rk_gpio   poweron_gpio;
    struct rfkill_rk_gpio   reset_gpio;
    struct rfkill_rk_gpio   wake_gpio;      // Host wake or sleep BT
    struct rfkill_rk_irq    wake_host_irq;  // BT wakeup host
    struct rfkill_rk_gpio   rts_gpio;
};

#endif /* __RFKILL_GPIO_H */

