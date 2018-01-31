/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __GPS_H__
#define __GPS_H__

//CC&C Johnson.Wang
//mtk gps device
static struct mt3326_gps_hardware {
        int (*ext_power_on)(int);
        int (*ext_power_off)(int);
};

static struct mt3326_gps_hardware mt3326_gps_hw = {
        .ext_power_on =  NULL,
        .ext_power_off = NULL,
};

static struct platform_device mt3326_device_gps = {
        .name           = "mt3326-gps",
        .id             = -1,
        .dev            = {
        .platform_data  = &mt3326_gps_hw,
        },
};

#endif // __GPS_H__
