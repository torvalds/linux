/*
 * common header file for all modules.
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AML_COMMON_H
#define __AML_COMMON_H

#include "linux/amlogic/amports/vframe.h"

/* used in get_foreign_affairs()
 *         set_foreign_affairs()
 *         rst_foreign_affairs()
 */
typedef enum foreign_affairs_e {
        FOREIGN_AFFAIRS_00 = 0x00000001, // VDIN  -> VIDEO @ DDR_IF
        FOREIGN_AFFAIRS_01 = 0x00000002, // DI    -> VIDEO @ DDR_IF
        FOREIGN_AFFAIRS_02 = 0x00000004, // VIDEO -> VDIN  @ DDR_IF
        FOREIGN_AFFAIRS_03 = 0x00000008,
        FOREIGN_AFFAIRS_04 = 0x00000010,
        FOREIGN_AFFAIRS_05 = 0x00000020,
        FOREIGN_AFFAIRS_06 = 0x00000040,
        FOREIGN_AFFAIRS_07 = 0x00000080,
        FOREIGN_AFFAIRS_08 = 0x00000100,
        FOREIGN_AFFAIRS_09 = 0x00000200,
        FOREIGN_AFFAIRS_10 = 0x00000400,
        FOREIGN_AFFAIRS_11 = 0x00000800,
        FOREIGN_AFFAIRS_12 = 0x00001000,
        FOREIGN_AFFAIRS_13 = 0x00002000,
        FOREIGN_AFFAIRS_14 = 0x00004000,
        FOREIGN_AFFAIRS_15 = 0x00008000,
        FOREIGN_AFFAIRS_16 = 0x00010000,
        FOREIGN_AFFAIRS_17 = 0x00020000,
        FOREIGN_AFFAIRS_18 = 0x00040000,
        FOREIGN_AFFAIRS_19 = 0x00080000,
        FOREIGN_AFFAIRS_20 = 0x00100000,
        FOREIGN_AFFAIRS_21 = 0x00200000,
        FOREIGN_AFFAIRS_22 = 0x00400000,
        FOREIGN_AFFAIRS_23 = 0x00800000,
        FOREIGN_AFFAIRS_24 = 0x01000000,
        FOREIGN_AFFAIRS_25 = 0x02000000,
        FOREIGN_AFFAIRS_26 = 0x04000000,
        FOREIGN_AFFAIRS_27 = 0x08000000,
        FOREIGN_AFFAIRS_28 = 0x10000000,
        FOREIGN_AFFAIRS_29 = 0x20000000,
        FOREIGN_AFFAIRS_30 = 0x40000000,
        FOREIGN_AFFAIRS_31 = 0x80000000,
} foreign_affairs_t;

/* workround for DDR_IF
 * defined in video.c, used in vdin.c & deinterlace.c
 */
extern bool get_foreign_affairs(enum foreign_affairs_e foreign_affairs);
extern void set_foreign_affairs(enum foreign_affairs_e foreign_affairs);
extern void rst_foreign_affairs(enum foreign_affairs_e foreign_affairs);

#if defined(CONFIG_AM_VECM)
extern void amvecm_video_latch(void);
extern void ve_on_vs(vframe_t *vf);
#endif

#endif /* __AML_COMMON_H */

