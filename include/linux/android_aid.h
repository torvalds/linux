/* include/linux/android_aid.h
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_ANDROID_AID_H
#define _LINUX_ANDROID_AID_H

/* AIDs that the kernel treats differently */
#define AID_OBSOLETE_000 KGIDT_INIT(3001)  /* was NET_BT_ADMIN */
#define AID_OBSOLETE_001 KGIDT_INIT(3002)  /* was NET_BT */
#define AID_INET         KGIDT_INIT(3003)
#define AID_NET_RAW      KGIDT_INIT(3004)
#define AID_NET_ADMIN    KGIDT_INIT(3005)
#define AID_NET_BW_STATS KGIDT_INIT(3006)  /* read bandwidth statistics */
#define AID_NET_BW_ACCT  KGIDT_INIT(3007)  /* change bandwidth statistics accounting */

#endif
