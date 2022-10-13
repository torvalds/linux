/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_KBASE_GPU_REGMAP_JM_H_
#define _UAPI_KBASE_GPU_REGMAP_JM_H_

/* GPU control registers */

#define LATEST_FLUSH           0x038 /* (RO) Flush ID of latest clean-and-invalidate operation */

/* Job control registers */

#define JS_HEAD_LO             0x00	/* (RO) Job queue head pointer for job slot n, low word */
#define JS_HEAD_HI             0x04	/* (RO) Job queue head pointer for job slot n, high word */
#define JS_TAIL_LO             0x08	/* (RO) Job queue tail pointer for job slot n, low word */
#define JS_TAIL_HI             0x0C	/* (RO) Job queue tail pointer for job slot n, high word */
#define JS_AFFINITY_LO         0x10	/* (RO) Core affinity mask for job slot n, low word */
#define JS_AFFINITY_HI         0x14	/* (RO) Core affinity mask for job slot n, high word */
#define JS_CONFIG              0x18	/* (RO) Configuration settings for job slot n */

#define JS_HEAD_NEXT_LO        0x40	/* (RW) Next job queue head pointer for job slot n, low word */
#define JS_HEAD_NEXT_HI        0x44	/* (RW) Next job queue head pointer for job slot n, high word */
#define JS_AFFINITY_NEXT_LO    0x50	/* (RW) Next core affinity mask for job slot n, low word */
#define JS_AFFINITY_NEXT_HI    0x54	/* (RW) Next core affinity mask for job slot n, high word */
#define JS_CONFIG_NEXT         0x58	/* (RW) Next configuration settings for job slot n */
#define JS_COMMAND_NEXT        0x60	/* (RW) Next command register for job slot n */

#endif /* _UAPI_KBASE_GPU_REGMAP_JM_H_ */
