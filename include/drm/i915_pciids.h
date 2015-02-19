/*
 * Copyright 2013 Intel Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _I915_PCIIDS_H
#define _I915_PCIIDS_H

/*
 * A pci_device_id struct {
 *	__u32 vendor, device;
 *      __u32 subvendor, subdevice;
 *	__u32 class, class_mask;
 *	kernel_ulong_t driver_data;
 * };
 * Don't use C99 here because "class" is reserved and we want to
 * give userspace flexibility.
 */
#define INTEL_VGA_DEVICE(id, info) {		\
	0x8086,	id,				\
	~0, ~0,					\
	0x030000, 0xff0000,			\
	(unsigned long) info }

#define INTEL_QUANTA_VGA_DEVICE(info) {		\
	0x8086,	0x16a,				\
	0x152d,	0x8990,				\
	0x030000, 0xff0000,			\
	(unsigned long) info }

#define INTEL_I830_IDS(info)				\
	INTEL_VGA_DEVICE(0x3577, info)

#define INTEL_I845G_IDS(info)				\
	INTEL_VGA_DEVICE(0x2562, info)

#define INTEL_I85X_IDS(info)				\
	INTEL_VGA_DEVICE(0x3582, info), /* I855_GM */ \
	INTEL_VGA_DEVICE(0x358e, info)

#define INTEL_I865G_IDS(info)				\
	INTEL_VGA_DEVICE(0x2572, info) /* I865_G */

#define INTEL_I915G_IDS(info)				\
	INTEL_VGA_DEVICE(0x2582, info), /* I915_G */ \
	INTEL_VGA_DEVICE(0x258a, info)  /* E7221_G */

#define INTEL_I915GM_IDS(info)				\
	INTEL_VGA_DEVICE(0x2592, info) /* I915_GM */

#define INTEL_I945G_IDS(info)				\
	INTEL_VGA_DEVICE(0x2772, info) /* I945_G */

#define INTEL_I945GM_IDS(info)				\
	INTEL_VGA_DEVICE(0x27a2, info), /* I945_GM */ \
	INTEL_VGA_DEVICE(0x27ae, info)  /* I945_GME */

#define INTEL_I965G_IDS(info)				\
	INTEL_VGA_DEVICE(0x2972, info), /* I946_GZ */	\
	INTEL_VGA_DEVICE(0x2982, info),	/* G35_G */	\
	INTEL_VGA_DEVICE(0x2992, info),	/* I965_Q */	\
	INTEL_VGA_DEVICE(0x29a2, info)	/* I965_G */

#define INTEL_G33_IDS(info)				\
	INTEL_VGA_DEVICE(0x29b2, info), /* Q35_G */ \
	INTEL_VGA_DEVICE(0x29c2, info),	/* G33_G */ \
	INTEL_VGA_DEVICE(0x29d2, info)	/* Q33_G */

#define INTEL_I965GM_IDS(info)				\
	INTEL_VGA_DEVICE(0x2a02, info),	/* I965_GM */ \
	INTEL_VGA_DEVICE(0x2a12, info)  /* I965_GME */

#define INTEL_GM45_IDS(info)				\
	INTEL_VGA_DEVICE(0x2a42, info) /* GM45_G */

#define INTEL_G45_IDS(info)				\
	INTEL_VGA_DEVICE(0x2e02, info), /* IGD_E_G */ \
	INTEL_VGA_DEVICE(0x2e12, info), /* Q45_G */ \
	INTEL_VGA_DEVICE(0x2e22, info), /* G45_G */ \
	INTEL_VGA_DEVICE(0x2e32, info), /* G41_G */ \
	INTEL_VGA_DEVICE(0x2e42, info), /* B43_G */ \
	INTEL_VGA_DEVICE(0x2e92, info)	/* B43_G.1 */

#define INTEL_PINEVIEW_IDS(info)			\
	INTEL_VGA_DEVICE(0xa001, info),			\
	INTEL_VGA_DEVICE(0xa011, info)

#define INTEL_IRONLAKE_D_IDS(info) \
	INTEL_VGA_DEVICE(0x0042, info)

#define INTEL_IRONLAKE_M_IDS(info) \
	INTEL_VGA_DEVICE(0x0046, info)

#define INTEL_SNB_D_IDS(info) \
	INTEL_VGA_DEVICE(0x0102, info), \
	INTEL_VGA_DEVICE(0x0112, info), \
	INTEL_VGA_DEVICE(0x0122, info), \
	INTEL_VGA_DEVICE(0x010A, info)

#define INTEL_SNB_M_IDS(info) \
	INTEL_VGA_DEVICE(0x0106, info), \
	INTEL_VGA_DEVICE(0x0116, info), \
	INTEL_VGA_DEVICE(0x0126, info)

#define INTEL_IVB_M_IDS(info) \
	INTEL_VGA_DEVICE(0x0156, info), /* GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0166, info)  /* GT2 mobile */

#define INTEL_IVB_D_IDS(info) \
	INTEL_VGA_DEVICE(0x0152, info), /* GT1 desktop */ \
	INTEL_VGA_DEVICE(0x0162, info), /* GT2 desktop */ \
	INTEL_VGA_DEVICE(0x015a, info), /* GT1 server */ \
	INTEL_VGA_DEVICE(0x016a, info)  /* GT2 server */

#define INTEL_IVB_Q_IDS(info) \
	INTEL_QUANTA_VGA_DEVICE(info) /* Quanta transcode */

#define INTEL_HSW_D_IDS(info) \
	INTEL_VGA_DEVICE(0x0402, info), /* GT1 desktop */ \
	INTEL_VGA_DEVICE(0x0412, info), /* GT2 desktop */ \
	INTEL_VGA_DEVICE(0x0422, info), /* GT3 desktop */ \
	INTEL_VGA_DEVICE(0x040a, info), /* GT1 server */ \
	INTEL_VGA_DEVICE(0x041a, info), /* GT2 server */ \
	INTEL_VGA_DEVICE(0x042a, info), /* GT3 server */ \
	INTEL_VGA_DEVICE(0x040B, info), /* GT1 reserved */ \
	INTEL_VGA_DEVICE(0x041B, info), /* GT2 reserved */ \
	INTEL_VGA_DEVICE(0x042B, info), /* GT3 reserved */ \
	INTEL_VGA_DEVICE(0x040E, info), /* GT1 reserved */ \
	INTEL_VGA_DEVICE(0x041E, info), /* GT2 reserved */ \
	INTEL_VGA_DEVICE(0x042E, info), /* GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0C02, info), /* SDV GT1 desktop */ \
	INTEL_VGA_DEVICE(0x0C12, info), /* SDV GT2 desktop */ \
	INTEL_VGA_DEVICE(0x0C22, info), /* SDV GT3 desktop */ \
	INTEL_VGA_DEVICE(0x0C0A, info), /* SDV GT1 server */ \
	INTEL_VGA_DEVICE(0x0C1A, info), /* SDV GT2 server */ \
	INTEL_VGA_DEVICE(0x0C2A, info), /* SDV GT3 server */ \
	INTEL_VGA_DEVICE(0x0C0B, info), /* SDV GT1 reserved */ \
	INTEL_VGA_DEVICE(0x0C1B, info), /* SDV GT2 reserved */ \
	INTEL_VGA_DEVICE(0x0C2B, info), /* SDV GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0C0E, info), /* SDV GT1 reserved */ \
	INTEL_VGA_DEVICE(0x0C1E, info), /* SDV GT2 reserved */ \
	INTEL_VGA_DEVICE(0x0C2E, info), /* SDV GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0A02, info), /* ULT GT1 desktop */ \
	INTEL_VGA_DEVICE(0x0A12, info), /* ULT GT2 desktop */ \
	INTEL_VGA_DEVICE(0x0A22, info), /* ULT GT3 desktop */ \
	INTEL_VGA_DEVICE(0x0A0A, info), /* ULT GT1 server */ \
	INTEL_VGA_DEVICE(0x0A1A, info), /* ULT GT2 server */ \
	INTEL_VGA_DEVICE(0x0A2A, info), /* ULT GT3 server */ \
	INTEL_VGA_DEVICE(0x0A0B, info), /* ULT GT1 reserved */ \
	INTEL_VGA_DEVICE(0x0A1B, info), /* ULT GT2 reserved */ \
	INTEL_VGA_DEVICE(0x0A2B, info), /* ULT GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0D02, info), /* CRW GT1 desktop */ \
	INTEL_VGA_DEVICE(0x0D12, info), /* CRW GT2 desktop */ \
	INTEL_VGA_DEVICE(0x0D22, info), /* CRW GT3 desktop */ \
	INTEL_VGA_DEVICE(0x0D0A, info), /* CRW GT1 server */ \
	INTEL_VGA_DEVICE(0x0D1A, info), /* CRW GT2 server */ \
	INTEL_VGA_DEVICE(0x0D2A, info), /* CRW GT3 server */ \
	INTEL_VGA_DEVICE(0x0D0B, info), /* CRW GT1 reserved */ \
	INTEL_VGA_DEVICE(0x0D1B, info), /* CRW GT2 reserved */ \
	INTEL_VGA_DEVICE(0x0D2B, info), /* CRW GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0D0E, info), /* CRW GT1 reserved */ \
	INTEL_VGA_DEVICE(0x0D1E, info), /* CRW GT2 reserved */ \
	INTEL_VGA_DEVICE(0x0D2E, info)  /* CRW GT3 reserved */ \

#define INTEL_HSW_M_IDS(info) \
	INTEL_VGA_DEVICE(0x0406, info), /* GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0416, info), /* GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0426, info), /* GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0C06, info), /* SDV GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0C16, info), /* SDV GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0C26, info), /* SDV GT3 mobile */ \
	INTEL_VGA_DEVICE(0x0A06, info), /* ULT GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0A16, info), /* ULT GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0A26, info), /* ULT GT3 mobile */ \
	INTEL_VGA_DEVICE(0x0A0E, info), /* ULX GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0A1E, info), /* ULX GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0A2E, info), /* ULT GT3 reserved */ \
	INTEL_VGA_DEVICE(0x0D06, info), /* CRW GT1 mobile */ \
	INTEL_VGA_DEVICE(0x0D16, info), /* CRW GT2 mobile */ \
	INTEL_VGA_DEVICE(0x0D26, info)  /* CRW GT3 mobile */

#define INTEL_VLV_M_IDS(info) \
	INTEL_VGA_DEVICE(0x0f30, info), \
	INTEL_VGA_DEVICE(0x0f31, info), \
	INTEL_VGA_DEVICE(0x0f32, info), \
	INTEL_VGA_DEVICE(0x0f33, info), \
	INTEL_VGA_DEVICE(0x0157, info)

#define INTEL_VLV_D_IDS(info) \
	INTEL_VGA_DEVICE(0x0155, info)

#define _INTEL_BDW_M(gt, id, info) \
	INTEL_VGA_DEVICE((((gt) - 1) << 4) | (id), info)
#define _INTEL_BDW_D(gt, id, info) \
	INTEL_VGA_DEVICE((((gt) - 1) << 4) | (id), info)

#define _INTEL_BDW_M_IDS(gt, info) \
	_INTEL_BDW_M(gt, 0x1602, info), /* ULT */ \
	_INTEL_BDW_M(gt, 0x1606, info), /* ULT */ \
	_INTEL_BDW_M(gt, 0x160B, info), /* Iris */ \
	_INTEL_BDW_M(gt, 0x160E, info) /* ULX */

#define _INTEL_BDW_D_IDS(gt, info) \
	_INTEL_BDW_D(gt, 0x160A, info), /* Server */ \
	_INTEL_BDW_D(gt, 0x160D, info) /* Workstation */

#define INTEL_BDW_GT12M_IDS(info) \
	_INTEL_BDW_M_IDS(1, info), \
	_INTEL_BDW_M_IDS(2, info)

#define INTEL_BDW_GT12D_IDS(info) \
	_INTEL_BDW_D_IDS(1, info), \
	_INTEL_BDW_D_IDS(2, info)

#define INTEL_BDW_GT3M_IDS(info) \
	_INTEL_BDW_M_IDS(3, info)

#define INTEL_BDW_GT3D_IDS(info) \
	_INTEL_BDW_D_IDS(3, info)

#define INTEL_BDW_RSVDM_IDS(info) \
	_INTEL_BDW_M_IDS(4, info)

#define INTEL_BDW_RSVDD_IDS(info) \
	_INTEL_BDW_D_IDS(4, info)

#define INTEL_BDW_M_IDS(info) \
	INTEL_BDW_GT12M_IDS(info), \
	INTEL_BDW_GT3M_IDS(info), \
	INTEL_BDW_RSVDM_IDS(info)

#define INTEL_BDW_D_IDS(info) \
	INTEL_BDW_GT12D_IDS(info), \
	INTEL_BDW_GT3D_IDS(info), \
	INTEL_BDW_RSVDD_IDS(info)

#define INTEL_CHV_IDS(info) \
	INTEL_VGA_DEVICE(0x22b0, info), \
	INTEL_VGA_DEVICE(0x22b1, info), \
	INTEL_VGA_DEVICE(0x22b2, info), \
	INTEL_VGA_DEVICE(0x22b3, info)

#define INTEL_SKL_IDS(info) \
	INTEL_VGA_DEVICE(0x1916, info), /* ULT GT2 */ \
	INTEL_VGA_DEVICE(0x1906, info), /* ULT GT1 */ \
	INTEL_VGA_DEVICE(0x1926, info), /* ULT GT3 */ \
	INTEL_VGA_DEVICE(0x1921, info), /* ULT GT2F */ \
	INTEL_VGA_DEVICE(0x190E, info), /* ULX GT1 */ \
	INTEL_VGA_DEVICE(0x191E, info), /* ULX GT2 */ \
	INTEL_VGA_DEVICE(0x1912, info), /* DT  GT2 */ \
	INTEL_VGA_DEVICE(0x1902, info), /* DT  GT1 */ \
	INTEL_VGA_DEVICE(0x191B, info), /* Halo GT2 */ \
	INTEL_VGA_DEVICE(0x192B, info), /* Halo GT3 */ \
	INTEL_VGA_DEVICE(0x190B, info), /* Halo GT1 */ \
	INTEL_VGA_DEVICE(0x191A, info), /* SRV GT2 */ \
	INTEL_VGA_DEVICE(0x192A, info), /* SRV GT3 */ \
	INTEL_VGA_DEVICE(0x190A, info), /* SRV GT1 */ \
	INTEL_VGA_DEVICE(0x191D, info)  /* WKS GT2 */

#endif /* _I915_PCIIDS_H */
