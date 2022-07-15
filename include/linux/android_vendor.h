/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * android_vendor.h - Android vendor data
 *
 * Copyright 2020 Google LLC
 *
 * These macros are to be used to reserve space in kernel data structures
 * for use by vendor modules.
 *
 * These macros should be used before the kernel abi is "frozen".
 * Fields can be added to various kernel structures that need space
 * for functionality implemented in vendor modules. The use of
 * these fields is vendor specific.
 */
#ifndef _ANDROID_VENDOR_H
#define _ANDROID_VENDOR_H

/*
 * ANDROID_VENDOR_DATA
 *   Reserve some "padding" in a structure for potential future use.
 *   This normally placed at the end of a structure.
 *   number: the "number" of the padding variable in the structure.  Start with
 *   1 and go up.
 *
 * ANDROID_VENDOR_DATA_ARRAY
 *   Same as ANDROID_VENDOR_DATA but allocates an array of u64 with
 *   the specified size
 */
#ifdef CONFIG_ANDROID_VENDOR_OEM_DATA
#define ANDROID_VENDOR_DATA(n)		u64 android_vendor_data##n
#define ANDROID_VENDOR_DATA_ARRAY(n, s)	u64 android_vendor_data##n[s]

#define ANDROID_OEM_DATA(n)		u64 android_oem_data##n
#define ANDROID_OEM_DATA_ARRAY(n, s)	u64 android_oem_data##n[s]

#define android_init_vendor_data(p, n) \
	memset(&p->android_vendor_data##n, 0, sizeof(p->android_vendor_data##n))
#define android_init_oem_data(p, n) \
	memset(&p->android_oem_data##n, 0, sizeof(p->android_oem_data##n))
#else
#define ANDROID_VENDOR_DATA(n)
#define ANDROID_VENDOR_DATA_ARRAY(n, s)
#define ANDROID_OEM_DATA(n)
#define ANDROID_OEM_DATA_ARRAY(n, s)

#define android_init_vendor_data(p, n)
#define android_init_oem_data(p, n)
#endif

#endif /* _ANDROID_VENDOR_H */
