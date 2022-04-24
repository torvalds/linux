/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2015, 2020-2022 ARM Limited. All rights reserved.
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

#ifndef _UAPI_BASE_MEM_PRIV_H_
#define _UAPI_BASE_MEM_PRIV_H_

#include <linux/types.h>

#include "mali_base_kernel.h"

#define BASE_SYNCSET_OP_MSYNC	(1U << 0)
#define BASE_SYNCSET_OP_CSYNC	(1U << 1)

/*
 * This structure describe a basic memory coherency operation.
 * It can either be:
 * @li a sync from CPU to Memory:
 *	- type = ::BASE_SYNCSET_OP_MSYNC
 *	- mem_handle = a handle to the memory object on which the operation
 *	  is taking place
 *	- user_addr = the address of the range to be synced
 *	- size = the amount of data to be synced, in bytes
 *	- offset is ignored.
 * @li a sync from Memory to CPU:
 *	- type = ::BASE_SYNCSET_OP_CSYNC
 *	- mem_handle = a handle to the memory object on which the operation
 *	  is taking place
 *	- user_addr = the address of the range to be synced
 *	- size = the amount of data to be synced, in bytes.
 *	- offset is ignored.
 */
struct basep_syncset {
	struct base_mem_handle mem_handle;
	__u64 user_addr;
	__u64 size;
	__u8 type;
	__u8 padding[7];
};

#endif /* _UAPI_BASE_MEM_PRIV_H_ */
