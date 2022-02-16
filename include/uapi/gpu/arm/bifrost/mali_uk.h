/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010, 2012-2015, 2018, 2020-2022 ARM Limited. All rights reserved.
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

/**
 * DOC: Types and definitions that are common across OSs for both the user
 *      and kernel side of the User-Kernel interface.
 */

#ifndef _UAPI_UK_H_
#define _UAPI_UK_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * DOC: uk_api User-Kernel Interface API
 *
 * The User-Kernel Interface abstracts the communication mechanism between the user and kernel-side code of device
 * drivers developed as part of the Midgard DDK. Currently that includes the Base driver.
 *
 * It exposes an OS independent API to user-side code (UKU) which routes functions calls to an OS-independent
 * kernel-side API (UKK) via an OS-specific communication mechanism.
 *
 * This API is internal to the Midgard DDK and is not exposed to any applications.
 *
 */

/**
 * enum uk_client_id - These are identifiers for kernel-side drivers
 * implementing a UK interface, aka UKK clients.
 * @UK_CLIENT_MALI_T600_BASE: Value used to identify the Base driver UK client.
 * @UK_CLIENT_COUNT:          The number of uk clients supported. This must be
 *                            the last member of the enum
 *
 * The UK module maps this to an OS specific device name, e.g. "gpu_base" -> "GPU0:". Specify this
 * identifier to select a UKK client to the uku_open() function.
 *
 * When a new UKK client driver is created a new identifier needs to be added to the uk_client_id
 * enumeration and the uku_open() implemenation for the various OS ports need to be updated to
 * provide a mapping of the identifier to the OS specific device name.
 *
 */
enum uk_client_id {
	UK_CLIENT_MALI_T600_BASE,
	UK_CLIENT_COUNT
};

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _UAPI_UK_H_ */
