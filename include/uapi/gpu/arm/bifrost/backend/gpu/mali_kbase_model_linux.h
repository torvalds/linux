/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

/*
 * Dummy Model interface
 */

#ifndef _UAPI_KBASE_MODEL_LINUX_H_
#define _UAPI_KBASE_MODEL_LINUX_H_

/* Generic model IRQs */
#define MODEL_LINUX_JOB_IRQ (0x1 << 0)
#define MODEL_LINUX_GPU_IRQ (0x1 << 1)
#define MODEL_LINUX_MMU_IRQ (0x1 << 2)

#define MODEL_LINUX_IRQ_MASK (MODEL_LINUX_JOB_IRQ | MODEL_LINUX_GPU_IRQ | MODEL_LINUX_MMU_IRQ)

#endif /* _UAPI_KBASE_MODEL_LINUX_H_ */
