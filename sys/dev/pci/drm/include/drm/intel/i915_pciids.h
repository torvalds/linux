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
#define INTEL_VGA_DEVICE(id, info) { \
	0x8086,	id, \
	~0, ~0, \
	0x030000, 0xff0000, \
	(unsigned long) info }

#define INTEL_QUANTA_VGA_DEVICE(info) { \
	0x8086,	0x16a, \
	0x152d,	0x8990, \
	0x030000, 0xff0000, \
	(unsigned long) info }

#define INTEL_I810_IDS(MACRO__, ...) \
	MACRO__(0x7121, ## __VA_ARGS__), /* I810 */ \
	MACRO__(0x7123, ## __VA_ARGS__), /* I810_DC100 */ \
	MACRO__(0x7125, ## __VA_ARGS__)  /* I810_E */

#define INTEL_I815_IDS(MACRO__, ...) \
	MACRO__(0x1132, ## __VA_ARGS__)  /* I815*/

#define INTEL_I830_IDS(MACRO__, ...) \
	MACRO__(0x3577, ## __VA_ARGS__)

#define INTEL_I845G_IDS(MACRO__, ...) \
	MACRO__(0x2562, ## __VA_ARGS__)

#define INTEL_I85X_IDS(MACRO__, ...) \
	MACRO__(0x3582, ## __VA_ARGS__), /* I855_GM */ \
	MACRO__(0x358e, ## __VA_ARGS__)

#define INTEL_I865G_IDS(MACRO__, ...) \
	MACRO__(0x2572, ## __VA_ARGS__) /* I865_G */

#define INTEL_I915G_IDS(MACRO__, ...) \
	MACRO__(0x2582, ## __VA_ARGS__), /* I915_G */ \
	MACRO__(0x258a, ## __VA_ARGS__)  /* E7221_G */

#define INTEL_I915GM_IDS(MACRO__, ...) \
	MACRO__(0x2592, ## __VA_ARGS__) /* I915_GM */

#define INTEL_I945G_IDS(MACRO__, ...) \
	MACRO__(0x2772, ## __VA_ARGS__) /* I945_G */

#define INTEL_I945GM_IDS(MACRO__, ...) \
	MACRO__(0x27a2, ## __VA_ARGS__), /* I945_GM */ \
	MACRO__(0x27ae, ## __VA_ARGS__)  /* I945_GME */

#define INTEL_I965G_IDS(MACRO__, ...) \
	MACRO__(0x2972, ## __VA_ARGS__), /* I946_GZ */ \
	MACRO__(0x2982, ## __VA_ARGS__),	/* G35_G */ \
	MACRO__(0x2992, ## __VA_ARGS__),	/* I965_Q */ \
	MACRO__(0x29a2, ## __VA_ARGS__)	/* I965_G */

#define INTEL_G33_IDS(MACRO__, ...) \
	MACRO__(0x29b2, ## __VA_ARGS__), /* Q35_G */ \
	MACRO__(0x29c2, ## __VA_ARGS__),	/* G33_G */ \
	MACRO__(0x29d2, ## __VA_ARGS__)	/* Q33_G */

#define INTEL_I965GM_IDS(MACRO__, ...) \
	MACRO__(0x2a02, ## __VA_ARGS__),	/* I965_GM */ \
	MACRO__(0x2a12, ## __VA_ARGS__)  /* I965_GME */

#define INTEL_GM45_IDS(MACRO__, ...) \
	MACRO__(0x2a42, ## __VA_ARGS__) /* GM45_G */

#define INTEL_G45_IDS(MACRO__, ...) \
	MACRO__(0x2e02, ## __VA_ARGS__), /* IGD_E_G */ \
	MACRO__(0x2e12, ## __VA_ARGS__), /* Q45_G */ \
	MACRO__(0x2e22, ## __VA_ARGS__), /* G45_G */ \
	MACRO__(0x2e32, ## __VA_ARGS__), /* G41_G */ \
	MACRO__(0x2e42, ## __VA_ARGS__), /* B43_G */ \
	MACRO__(0x2e92, ## __VA_ARGS__)	/* B43_G.1 */

#define INTEL_PNV_G_IDS(MACRO__, ...) \
	MACRO__(0xa001, ## __VA_ARGS__)

#define INTEL_PNV_M_IDS(MACRO__, ...) \
	MACRO__(0xa011, ## __VA_ARGS__)

#define INTEL_PNV_IDS(MACRO__, ...) \
	INTEL_PNV_G_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_PNV_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_ILK_D_IDS(MACRO__, ...) \
	MACRO__(0x0042, ## __VA_ARGS__)

#define INTEL_ILK_M_IDS(MACRO__, ...) \
	MACRO__(0x0046, ## __VA_ARGS__)

#define INTEL_ILK_IDS(MACRO__, ...) \
	INTEL_ILK_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_ILK_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_SNB_D_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0102, ## __VA_ARGS__), \
	MACRO__(0x010A, ## __VA_ARGS__)

#define INTEL_SNB_D_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0112, ## __VA_ARGS__), \
	MACRO__(0x0122, ## __VA_ARGS__)

#define INTEL_SNB_D_IDS(MACRO__, ...) \
	INTEL_SNB_D_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SNB_D_GT2_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_SNB_M_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0106, ## __VA_ARGS__)

#define INTEL_SNB_M_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0116, ## __VA_ARGS__), \
	MACRO__(0x0126, ## __VA_ARGS__)

#define INTEL_SNB_M_IDS(MACRO__, ...) \
	INTEL_SNB_M_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SNB_M_GT2_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_SNB_IDS(MACRO__, ...) \
	INTEL_SNB_D_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SNB_M_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_IVB_M_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0156, ## __VA_ARGS__) /* GT1 mobile */

#define INTEL_IVB_M_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0166, ## __VA_ARGS__) /* GT2 mobile */

#define INTEL_IVB_M_IDS(MACRO__, ...) \
	INTEL_IVB_M_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_IVB_M_GT2_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_IVB_D_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0152, ## __VA_ARGS__), /* GT1 desktop */ \
	MACRO__(0x015a, ## __VA_ARGS__)  /* GT1 server */

#define INTEL_IVB_D_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0162, ## __VA_ARGS__), /* GT2 desktop */ \
	MACRO__(0x016a, ## __VA_ARGS__)  /* GT2 server */

#define INTEL_IVB_D_IDS(MACRO__, ...) \
	INTEL_IVB_D_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_IVB_D_GT2_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_IVB_IDS(MACRO__, ...) \
	INTEL_IVB_M_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_IVB_D_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_IVB_Q_IDS(MACRO__, ...) \
	INTEL_QUANTA_VGA_DEVICE(__VA_ARGS__) /* Quanta transcode */

#define INTEL_HSW_ULT_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0A02, ## __VA_ARGS__), /* ULT GT1 desktop */ \
	MACRO__(0x0A06, ## __VA_ARGS__), /* ULT GT1 mobile */ \
	MACRO__(0x0A0A, ## __VA_ARGS__), /* ULT GT1 server */ \
	MACRO__(0x0A0B, ## __VA_ARGS__)  /* ULT GT1 reserved */

#define INTEL_HSW_ULX_GT1_IDS(MACRO__, ...) \
	MACRO__(0x0A0E, ## __VA_ARGS__) /* ULX GT1 mobile */

#define INTEL_HSW_GT1_IDS(MACRO__, ...) \
	INTEL_HSW_ULT_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_HSW_ULX_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x0402, ## __VA_ARGS__), /* GT1 desktop */ \
	MACRO__(0x0406, ## __VA_ARGS__), /* GT1 mobile */ \
	MACRO__(0x040A, ## __VA_ARGS__), /* GT1 server */ \
	MACRO__(0x040B, ## __VA_ARGS__), /* GT1 reserved */ \
	MACRO__(0x040E, ## __VA_ARGS__), /* GT1 reserved */ \
	MACRO__(0x0C02, ## __VA_ARGS__), /* SDV GT1 desktop */ \
	MACRO__(0x0C06, ## __VA_ARGS__), /* SDV GT1 mobile */ \
	MACRO__(0x0C0A, ## __VA_ARGS__), /* SDV GT1 server */ \
	MACRO__(0x0C0B, ## __VA_ARGS__), /* SDV GT1 reserved */ \
	MACRO__(0x0C0E, ## __VA_ARGS__), /* SDV GT1 reserved */ \
	MACRO__(0x0D02, ## __VA_ARGS__), /* CRW GT1 desktop */ \
	MACRO__(0x0D06, ## __VA_ARGS__), /* CRW GT1 mobile */ \
	MACRO__(0x0D0A, ## __VA_ARGS__), /* CRW GT1 server */ \
	MACRO__(0x0D0B, ## __VA_ARGS__), /* CRW GT1 reserved */ \
	MACRO__(0x0D0E, ## __VA_ARGS__)  /* CRW GT1 reserved */

#define INTEL_HSW_ULT_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0A12, ## __VA_ARGS__), /* ULT GT2 desktop */ \
	MACRO__(0x0A16, ## __VA_ARGS__), /* ULT GT2 mobile */ \
	MACRO__(0x0A1A, ## __VA_ARGS__), /* ULT GT2 server */ \
	MACRO__(0x0A1B, ## __VA_ARGS__)  /* ULT GT2 reserved */ \

#define INTEL_HSW_ULX_GT2_IDS(MACRO__, ...) \
	MACRO__(0x0A1E, ## __VA_ARGS__) /* ULX GT2 mobile */ \

#define INTEL_HSW_GT2_IDS(MACRO__, ...) \
	INTEL_HSW_ULT_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_HSW_ULX_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x0412, ## __VA_ARGS__), /* GT2 desktop */ \
	MACRO__(0x0416, ## __VA_ARGS__), /* GT2 mobile */ \
	MACRO__(0x041A, ## __VA_ARGS__), /* GT2 server */ \
	MACRO__(0x041B, ## __VA_ARGS__), /* GT2 reserved */ \
	MACRO__(0x041E, ## __VA_ARGS__), /* GT2 reserved */ \
	MACRO__(0x0C12, ## __VA_ARGS__), /* SDV GT2 desktop */ \
	MACRO__(0x0C16, ## __VA_ARGS__), /* SDV GT2 mobile */ \
	MACRO__(0x0C1A, ## __VA_ARGS__), /* SDV GT2 server */ \
	MACRO__(0x0C1B, ## __VA_ARGS__), /* SDV GT2 reserved */ \
	MACRO__(0x0C1E, ## __VA_ARGS__), /* SDV GT2 reserved */ \
	MACRO__(0x0D12, ## __VA_ARGS__), /* CRW GT2 desktop */ \
	MACRO__(0x0D16, ## __VA_ARGS__), /* CRW GT2 mobile */ \
	MACRO__(0x0D1A, ## __VA_ARGS__), /* CRW GT2 server */ \
	MACRO__(0x0D1B, ## __VA_ARGS__), /* CRW GT2 reserved */ \
	MACRO__(0x0D1E, ## __VA_ARGS__)  /* CRW GT2 reserved */

#define INTEL_HSW_ULT_GT3_IDS(MACRO__, ...) \
	MACRO__(0x0A22, ## __VA_ARGS__), /* ULT GT3 desktop */ \
	MACRO__(0x0A26, ## __VA_ARGS__), /* ULT GT3 mobile */ \
	MACRO__(0x0A2A, ## __VA_ARGS__), /* ULT GT3 server */ \
	MACRO__(0x0A2B, ## __VA_ARGS__), /* ULT GT3 reserved */ \
	MACRO__(0x0A2E, ## __VA_ARGS__)  /* ULT GT3 reserved */

#define INTEL_HSW_GT3_IDS(MACRO__, ...) \
	INTEL_HSW_ULT_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x0422, ## __VA_ARGS__), /* GT3 desktop */ \
	MACRO__(0x0426, ## __VA_ARGS__), /* GT3 mobile */ \
	MACRO__(0x042A, ## __VA_ARGS__), /* GT3 server */ \
	MACRO__(0x042B, ## __VA_ARGS__), /* GT3 reserved */ \
	MACRO__(0x042E, ## __VA_ARGS__), /* GT3 reserved */ \
	MACRO__(0x0C22, ## __VA_ARGS__), /* SDV GT3 desktop */ \
	MACRO__(0x0C26, ## __VA_ARGS__), /* SDV GT3 mobile */ \
	MACRO__(0x0C2A, ## __VA_ARGS__), /* SDV GT3 server */ \
	MACRO__(0x0C2B, ## __VA_ARGS__), /* SDV GT3 reserved */ \
	MACRO__(0x0C2E, ## __VA_ARGS__), /* SDV GT3 reserved */ \
	MACRO__(0x0D22, ## __VA_ARGS__), /* CRW GT3 desktop */ \
	MACRO__(0x0D26, ## __VA_ARGS__), /* CRW GT3 mobile */ \
	MACRO__(0x0D2A, ## __VA_ARGS__), /* CRW GT3 server */ \
	MACRO__(0x0D2B, ## __VA_ARGS__), /* CRW GT3 reserved */ \
	MACRO__(0x0D2E, ## __VA_ARGS__)  /* CRW GT3 reserved */

#define INTEL_HSW_IDS(MACRO__, ...) \
	INTEL_HSW_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_HSW_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_HSW_GT3_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_VLV_IDS(MACRO__, ...) \
	MACRO__(0x0f30, ## __VA_ARGS__), \
	MACRO__(0x0f31, ## __VA_ARGS__), \
	MACRO__(0x0f32, ## __VA_ARGS__), \
	MACRO__(0x0f33, ## __VA_ARGS__)

#define INTEL_BDW_ULT_GT1_IDS(MACRO__, ...) \
	MACRO__(0x1606, ## __VA_ARGS__), /* GT1 ULT */ \
	MACRO__(0x160B, ## __VA_ARGS__)  /* GT1 Iris */

#define INTEL_BDW_ULX_GT1_IDS(MACRO__, ...) \
	MACRO__(0x160E, ## __VA_ARGS__) /* GT1 ULX */

#define INTEL_BDW_GT1_IDS(MACRO__, ...) \
	INTEL_BDW_ULT_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_ULX_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1602, ## __VA_ARGS__), /* GT1 ULT */ \
	MACRO__(0x160A, ## __VA_ARGS__), /* GT1 Server */ \
	MACRO__(0x160D, ## __VA_ARGS__)  /* GT1 Workstation */

#define INTEL_BDW_ULT_GT2_IDS(MACRO__, ...) \
	MACRO__(0x1616, ## __VA_ARGS__), /* GT2 ULT */ \
	MACRO__(0x161B, ## __VA_ARGS__)  /* GT2 ULT */

#define INTEL_BDW_ULX_GT2_IDS(MACRO__, ...) \
	MACRO__(0x161E, ## __VA_ARGS__) /* GT2 ULX */

#define INTEL_BDW_GT2_IDS(MACRO__, ...) \
	INTEL_BDW_ULT_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_ULX_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1612, ## __VA_ARGS__), /* GT2 Halo */ \
	MACRO__(0x161A, ## __VA_ARGS__), /* GT2 Server */ \
	MACRO__(0x161D, ## __VA_ARGS__)  /* GT2 Workstation */

#define INTEL_BDW_ULT_GT3_IDS(MACRO__, ...) \
	MACRO__(0x1626, ## __VA_ARGS__), /* ULT */ \
	MACRO__(0x162B, ## __VA_ARGS__)  /* Iris */ \

#define INTEL_BDW_ULX_GT3_IDS(MACRO__, ...) \
	MACRO__(0x162E, ## __VA_ARGS__)  /* ULX */

#define INTEL_BDW_GT3_IDS(MACRO__, ...) \
	INTEL_BDW_ULT_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_ULX_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1622, ## __VA_ARGS__), /* ULT */ \
	MACRO__(0x162A, ## __VA_ARGS__), /* Server */ \
	MACRO__(0x162D, ## __VA_ARGS__)  /* Workstation */

#define INTEL_BDW_ULT_RSVD_IDS(MACRO__, ...) \
	MACRO__(0x1636, ## __VA_ARGS__), /* ULT */ \
	MACRO__(0x163B, ## __VA_ARGS__)  /* Iris */

#define INTEL_BDW_ULX_RSVD_IDS(MACRO__, ...) \
	MACRO__(0x163E, ## __VA_ARGS__) /* ULX */

#define INTEL_BDW_RSVD_IDS(MACRO__, ...) \
	INTEL_BDW_ULT_RSVD_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_ULX_RSVD_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1632, ## __VA_ARGS__), /* ULT */ \
	MACRO__(0x163A, ## __VA_ARGS__), /* Server */ \
	MACRO__(0x163D, ## __VA_ARGS__)  /* Workstation */

#define INTEL_BDW_IDS(MACRO__, ...) \
	INTEL_BDW_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_BDW_RSVD_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_CHV_IDS(MACRO__, ...) \
	MACRO__(0x22b0, ## __VA_ARGS__), \
	MACRO__(0x22b1, ## __VA_ARGS__), \
	MACRO__(0x22b2, ## __VA_ARGS__), \
	MACRO__(0x22b3, ## __VA_ARGS__)

#define INTEL_SKL_ULT_GT1_IDS(MACRO__, ...) \
	MACRO__(0x1906, ## __VA_ARGS__), /* ULT GT1 */ \
	MACRO__(0x1913, ## __VA_ARGS__)  /* ULT GT1.5 */

#define INTEL_SKL_ULX_GT1_IDS(MACRO__, ...) \
	MACRO__(0x190E, ## __VA_ARGS__), /* ULX GT1 */ \
	MACRO__(0x1915, ## __VA_ARGS__)  /* ULX GT1.5 */

#define INTEL_SKL_GT1_IDS(MACRO__, ...) \
	INTEL_SKL_ULT_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SKL_ULX_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1902, ## __VA_ARGS__), /* DT  GT1 */ \
	MACRO__(0x190A, ## __VA_ARGS__), /* SRV GT1 */ \
	MACRO__(0x190B, ## __VA_ARGS__), /* Halo GT1 */ \
	MACRO__(0x1917, ## __VA_ARGS__)  /* DT  GT1.5 */

#define INTEL_SKL_ULT_GT2_IDS(MACRO__, ...) \
	MACRO__(0x1916, ## __VA_ARGS__), /* ULT GT2 */ \
	MACRO__(0x1921, ## __VA_ARGS__)  /* ULT GT2F */

#define INTEL_SKL_ULX_GT2_IDS(MACRO__, ...) \
	MACRO__(0x191E, ## __VA_ARGS__) /* ULX GT2 */

#define INTEL_SKL_GT2_IDS(MACRO__, ...) \
	INTEL_SKL_ULT_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SKL_ULX_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x1912, ## __VA_ARGS__), /* DT  GT2 */ \
	MACRO__(0x191A, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x191B, ## __VA_ARGS__), /* Halo GT2 */ \
	MACRO__(0x191D, ## __VA_ARGS__)  /* WKS GT2 */

#define INTEL_SKL_ULT_GT3_IDS(MACRO__, ...) \
	MACRO__(0x1923, ## __VA_ARGS__), /* ULT GT3 */ \
	MACRO__(0x1926, ## __VA_ARGS__), /* ULT GT3e */ \
	MACRO__(0x1927, ## __VA_ARGS__)  /* ULT GT3e */

#define INTEL_SKL_GT3_IDS(MACRO__, ...) \
	INTEL_SKL_ULT_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x192A, ## __VA_ARGS__), /* SRV GT3 */ \
	MACRO__(0x192B, ## __VA_ARGS__), /* Halo GT3e */ \
	MACRO__(0x192D, ## __VA_ARGS__)  /* SRV GT3e */

#define INTEL_SKL_GT4_IDS(MACRO__, ...) \
	MACRO__(0x1932, ## __VA_ARGS__), /* DT GT4 */ \
	MACRO__(0x193A, ## __VA_ARGS__), /* SRV GT4e */ \
	MACRO__(0x193B, ## __VA_ARGS__), /* Halo GT4e */ \
	MACRO__(0x193D, ## __VA_ARGS__) /* WKS GT4e */

#define INTEL_SKL_IDS(MACRO__, ...) \
	INTEL_SKL_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SKL_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SKL_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_SKL_GT4_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_BXT_IDS(MACRO__, ...) \
	MACRO__(0x0A84, ## __VA_ARGS__), \
	MACRO__(0x1A84, ## __VA_ARGS__), \
	MACRO__(0x1A85, ## __VA_ARGS__), \
	MACRO__(0x5A84, ## __VA_ARGS__), /* APL HD Graphics 505 */ \
	MACRO__(0x5A85, ## __VA_ARGS__)  /* APL HD Graphics 500 */

#define INTEL_GLK_IDS(MACRO__, ...) \
	MACRO__(0x3184, ## __VA_ARGS__), \
	MACRO__(0x3185, ## __VA_ARGS__)

#define INTEL_KBL_ULT_GT1_IDS(MACRO__, ...) \
	MACRO__(0x5906, ## __VA_ARGS__), /* ULT GT1 */ \
	MACRO__(0x5913, ## __VA_ARGS__)  /* ULT GT1.5 */

#define INTEL_KBL_ULX_GT1_IDS(MACRO__, ...) \
	MACRO__(0x590E, ## __VA_ARGS__), /* ULX GT1 */ \
	MACRO__(0x5915, ## __VA_ARGS__)  /* ULX GT1.5 */

#define INTEL_KBL_GT1_IDS(MACRO__, ...) \
	INTEL_KBL_ULT_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_KBL_ULX_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x5902, ## __VA_ARGS__), /* DT  GT1 */ \
	MACRO__(0x5908, ## __VA_ARGS__), /* Halo GT1 */ \
	MACRO__(0x590A, ## __VA_ARGS__), /* SRV GT1 */ \
	MACRO__(0x590B, ## __VA_ARGS__) /* Halo GT1 */

#define INTEL_KBL_ULT_GT2_IDS(MACRO__, ...) \
	MACRO__(0x5916, ## __VA_ARGS__), /* ULT GT2 */ \
	MACRO__(0x5921, ## __VA_ARGS__)  /* ULT GT2F */

#define INTEL_KBL_ULX_GT2_IDS(MACRO__, ...) \
	MACRO__(0x591E, ## __VA_ARGS__)  /* ULX GT2 */

#define INTEL_KBL_GT2_IDS(MACRO__, ...) \
	INTEL_KBL_ULT_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_KBL_ULX_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x5912, ## __VA_ARGS__), /* DT  GT2 */ \
	MACRO__(0x5917, ## __VA_ARGS__), /* Mobile GT2 */ \
	MACRO__(0x591A, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x591B, ## __VA_ARGS__), /* Halo GT2 */ \
	MACRO__(0x591D, ## __VA_ARGS__) /* WKS GT2 */

#define INTEL_KBL_ULT_GT3_IDS(MACRO__, ...) \
	MACRO__(0x5926, ## __VA_ARGS__) /* ULT GT3 */

#define INTEL_KBL_GT3_IDS(MACRO__, ...) \
	INTEL_KBL_ULT_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x5923, ## __VA_ARGS__), /* ULT GT3 */ \
	MACRO__(0x5927, ## __VA_ARGS__) /* ULT GT3 */

#define INTEL_KBL_GT4_IDS(MACRO__, ...) \
	MACRO__(0x593B, ## __VA_ARGS__) /* Halo GT4 */

/* AML/KBL Y GT2 */
#define INTEL_AML_KBL_GT2_IDS(MACRO__, ...) \
	MACRO__(0x591C, ## __VA_ARGS__),  /* ULX GT2 */ \
	MACRO__(0x87C0, ## __VA_ARGS__) /* ULX GT2 */

/* AML/CFL Y GT2 */
#define INTEL_AML_CFL_GT2_IDS(MACRO__, ...) \
	MACRO__(0x87CA, ## __VA_ARGS__)

/* CML GT1 */
#define INTEL_CML_GT1_IDS(MACRO__, ...) \
	MACRO__(0x9BA2, ## __VA_ARGS__), \
	MACRO__(0x9BA4, ## __VA_ARGS__), \
	MACRO__(0x9BA5, ## __VA_ARGS__), \
	MACRO__(0x9BA8, ## __VA_ARGS__)

#define INTEL_CML_U_GT1_IDS(MACRO__, ...) \
	MACRO__(0x9B21, ## __VA_ARGS__), \
	MACRO__(0x9BAA, ## __VA_ARGS__), \
	MACRO__(0x9BAC, ## __VA_ARGS__)

/* CML GT2 */
#define INTEL_CML_GT2_IDS(MACRO__, ...) \
	MACRO__(0x9BC2, ## __VA_ARGS__), \
	MACRO__(0x9BC4, ## __VA_ARGS__), \
	MACRO__(0x9BC5, ## __VA_ARGS__), \
	MACRO__(0x9BC6, ## __VA_ARGS__), \
	MACRO__(0x9BC8, ## __VA_ARGS__), \
	MACRO__(0x9BE6, ## __VA_ARGS__), \
	MACRO__(0x9BF6, ## __VA_ARGS__)

#define INTEL_CML_U_GT2_IDS(MACRO__, ...) \
	MACRO__(0x9B41, ## __VA_ARGS__), \
	MACRO__(0x9BCA, ## __VA_ARGS__), \
	MACRO__(0x9BCC, ## __VA_ARGS__)

#define INTEL_CML_IDS(MACRO__, ...) \
	INTEL_CML_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CML_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CML_U_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CML_U_GT2_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_KBL_IDS(MACRO__, ...) \
	INTEL_KBL_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_KBL_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_KBL_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_KBL_GT4_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_AML_KBL_GT2_IDS(MACRO__, ## __VA_ARGS__)

/* CFL S */
#define INTEL_CFL_S_GT1_IDS(MACRO__, ...) \
	MACRO__(0x3E90, ## __VA_ARGS__), /* SRV GT1 */ \
	MACRO__(0x3E93, ## __VA_ARGS__), /* SRV GT1 */ \
	MACRO__(0x3E99, ## __VA_ARGS__)  /* SRV GT1 */

#define INTEL_CFL_S_GT2_IDS(MACRO__, ...) \
	MACRO__(0x3E91, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x3E92, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x3E96, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x3E98, ## __VA_ARGS__), /* SRV GT2 */ \
	MACRO__(0x3E9A, ## __VA_ARGS__)  /* SRV GT2 */

/* CFL H */
#define INTEL_CFL_H_GT1_IDS(MACRO__, ...) \
	MACRO__(0x3E9C, ## __VA_ARGS__)

#define INTEL_CFL_H_GT2_IDS(MACRO__, ...) \
	MACRO__(0x3E94, ## __VA_ARGS__),  /* Halo GT2 */ \
	MACRO__(0x3E9B, ## __VA_ARGS__) /* Halo GT2 */

/* CFL U GT2 */
#define INTEL_CFL_U_GT2_IDS(MACRO__, ...) \
	MACRO__(0x3EA9, ## __VA_ARGS__)

/* CFL U GT3 */
#define INTEL_CFL_U_GT3_IDS(MACRO__, ...) \
	MACRO__(0x3EA5, ## __VA_ARGS__), /* ULT GT3 */ \
	MACRO__(0x3EA6, ## __VA_ARGS__), /* ULT GT3 */ \
	MACRO__(0x3EA7, ## __VA_ARGS__), /* ULT GT3 */ \
	MACRO__(0x3EA8, ## __VA_ARGS__)  /* ULT GT3 */

#define INTEL_CFL_IDS(MACRO__, ...) \
	INTEL_CFL_S_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CFL_S_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CFL_H_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CFL_H_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CFL_U_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_CFL_U_GT3_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_AML_CFL_GT2_IDS(MACRO__, ## __VA_ARGS__)

/* WHL/CFL U GT1 */
#define INTEL_WHL_U_GT1_IDS(MACRO__, ...) \
	MACRO__(0x3EA1, ## __VA_ARGS__), \
	MACRO__(0x3EA4, ## __VA_ARGS__)

/* WHL/CFL U GT2 */
#define INTEL_WHL_U_GT2_IDS(MACRO__, ...) \
	MACRO__(0x3EA0, ## __VA_ARGS__), \
	MACRO__(0x3EA3, ## __VA_ARGS__)

/* WHL/CFL U GT3 */
#define INTEL_WHL_U_GT3_IDS(MACRO__, ...) \
	MACRO__(0x3EA2, ## __VA_ARGS__)

#define INTEL_WHL_IDS(MACRO__, ...) \
	INTEL_WHL_U_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_WHL_U_GT2_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_WHL_U_GT3_IDS(MACRO__, ## __VA_ARGS__)

/* CNL */
#define INTEL_CNL_PORT_F_IDS(MACRO__, ...) \
	MACRO__(0x5A44, ## __VA_ARGS__), \
	MACRO__(0x5A4C, ## __VA_ARGS__), \
	MACRO__(0x5A54, ## __VA_ARGS__), \
	MACRO__(0x5A5C, ## __VA_ARGS__)

#define INTEL_CNL_IDS(MACRO__, ...) \
	INTEL_CNL_PORT_F_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x5A40, ## __VA_ARGS__), \
	MACRO__(0x5A41, ## __VA_ARGS__), \
	MACRO__(0x5A42, ## __VA_ARGS__), \
	MACRO__(0x5A49, ## __VA_ARGS__), \
	MACRO__(0x5A4A, ## __VA_ARGS__), \
	MACRO__(0x5A50, ## __VA_ARGS__), \
	MACRO__(0x5A51, ## __VA_ARGS__), \
	MACRO__(0x5A52, ## __VA_ARGS__), \
	MACRO__(0x5A59, ## __VA_ARGS__), \
	MACRO__(0x5A5A, ## __VA_ARGS__)

/* ICL */
#define INTEL_ICL_PORT_F_IDS(MACRO__, ...) \
	MACRO__(0x8A50, ## __VA_ARGS__), \
	MACRO__(0x8A52, ## __VA_ARGS__), \
	MACRO__(0x8A53, ## __VA_ARGS__), \
	MACRO__(0x8A54, ## __VA_ARGS__), \
	MACRO__(0x8A56, ## __VA_ARGS__), \
	MACRO__(0x8A57, ## __VA_ARGS__), \
	MACRO__(0x8A58, ## __VA_ARGS__), \
	MACRO__(0x8A59, ## __VA_ARGS__), \
	MACRO__(0x8A5A, ## __VA_ARGS__), \
	MACRO__(0x8A5B, ## __VA_ARGS__), \
	MACRO__(0x8A5C, ## __VA_ARGS__), \
	MACRO__(0x8A70, ## __VA_ARGS__), \
	MACRO__(0x8A71, ## __VA_ARGS__)

#define INTEL_ICL_IDS(MACRO__, ...) \
	INTEL_ICL_PORT_F_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x8A51, ## __VA_ARGS__), \
	MACRO__(0x8A5D, ## __VA_ARGS__)

/* EHL */
#define INTEL_EHL_IDS(MACRO__, ...) \
	MACRO__(0x4541, ## __VA_ARGS__), \
	MACRO__(0x4551, ## __VA_ARGS__), \
	MACRO__(0x4555, ## __VA_ARGS__), \
	MACRO__(0x4557, ## __VA_ARGS__), \
	MACRO__(0x4570, ## __VA_ARGS__), \
	MACRO__(0x4571, ## __VA_ARGS__)

/* JSL */
#define INTEL_JSL_IDS(MACRO__, ...) \
	MACRO__(0x4E51, ## __VA_ARGS__), \
	MACRO__(0x4E55, ## __VA_ARGS__), \
	MACRO__(0x4E57, ## __VA_ARGS__), \
	MACRO__(0x4E61, ## __VA_ARGS__), \
	MACRO__(0x4E71, ## __VA_ARGS__)

/* TGL */
#define INTEL_TGL_GT1_IDS(MACRO__, ...) \
	MACRO__(0x9A60, ## __VA_ARGS__), \
	MACRO__(0x9A68, ## __VA_ARGS__), \
	MACRO__(0x9A70, ## __VA_ARGS__)

#define INTEL_TGL_GT2_IDS(MACRO__, ...) \
	MACRO__(0x9A40, ## __VA_ARGS__), \
	MACRO__(0x9A49, ## __VA_ARGS__), \
	MACRO__(0x9A59, ## __VA_ARGS__), \
	MACRO__(0x9A78, ## __VA_ARGS__), \
	MACRO__(0x9AC0, ## __VA_ARGS__), \
	MACRO__(0x9AC9, ## __VA_ARGS__), \
	MACRO__(0x9AD9, ## __VA_ARGS__), \
	MACRO__(0x9AF8, ## __VA_ARGS__)

#define INTEL_TGL_IDS(MACRO__, ...) \
	INTEL_TGL_GT1_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_TGL_GT2_IDS(MACRO__, ## __VA_ARGS__)

/* RKL */
#define INTEL_RKL_IDS(MACRO__, ...) \
	MACRO__(0x4C80, ## __VA_ARGS__), \
	MACRO__(0x4C8A, ## __VA_ARGS__), \
	MACRO__(0x4C8B, ## __VA_ARGS__), \
	MACRO__(0x4C8C, ## __VA_ARGS__), \
	MACRO__(0x4C90, ## __VA_ARGS__), \
	MACRO__(0x4C9A, ## __VA_ARGS__)

/* DG1 */
#define INTEL_DG1_IDS(MACRO__, ...) \
	MACRO__(0x4905, ## __VA_ARGS__), \
	MACRO__(0x4906, ## __VA_ARGS__), \
	MACRO__(0x4907, ## __VA_ARGS__), \
	MACRO__(0x4908, ## __VA_ARGS__), \
	MACRO__(0x4909, ## __VA_ARGS__)

/* ADL-S */
#define INTEL_ADLS_IDS(MACRO__, ...) \
	MACRO__(0x4680, ## __VA_ARGS__), \
	MACRO__(0x4682, ## __VA_ARGS__), \
	MACRO__(0x4688, ## __VA_ARGS__), \
	MACRO__(0x468A, ## __VA_ARGS__), \
	MACRO__(0x468B, ## __VA_ARGS__), \
	MACRO__(0x4690, ## __VA_ARGS__), \
	MACRO__(0x4692, ## __VA_ARGS__), \
	MACRO__(0x4693, ## __VA_ARGS__)

/* ADL-P */
#define INTEL_ADLP_IDS(MACRO__, ...) \
	MACRO__(0x46A0, ## __VA_ARGS__), \
	MACRO__(0x46A1, ## __VA_ARGS__), \
	MACRO__(0x46A2, ## __VA_ARGS__), \
	MACRO__(0x46A3, ## __VA_ARGS__), \
	MACRO__(0x46A6, ## __VA_ARGS__), \
	MACRO__(0x46A8, ## __VA_ARGS__), \
	MACRO__(0x46AA, ## __VA_ARGS__), \
	MACRO__(0x462A, ## __VA_ARGS__), \
	MACRO__(0x4626, ## __VA_ARGS__), \
	MACRO__(0x4628, ## __VA_ARGS__), \
	MACRO__(0x46B0, ## __VA_ARGS__), \
	MACRO__(0x46B1, ## __VA_ARGS__), \
	MACRO__(0x46B2, ## __VA_ARGS__), \
	MACRO__(0x46B3, ## __VA_ARGS__), \
	MACRO__(0x46C0, ## __VA_ARGS__), \
	MACRO__(0x46C1, ## __VA_ARGS__), \
	MACRO__(0x46C2, ## __VA_ARGS__), \
	MACRO__(0x46C3, ## __VA_ARGS__)

/* ADL-N */
#define INTEL_ADLN_IDS(MACRO__, ...) \
	MACRO__(0x46D0, ## __VA_ARGS__), \
	MACRO__(0x46D1, ## __VA_ARGS__), \
	MACRO__(0x46D2, ## __VA_ARGS__), \
	MACRO__(0x46D3, ## __VA_ARGS__), \
	MACRO__(0x46D4, ## __VA_ARGS__)

/* RPL-S */
#define INTEL_RPLS_IDS(MACRO__, ...) \
	MACRO__(0xA780, ## __VA_ARGS__), \
	MACRO__(0xA781, ## __VA_ARGS__), \
	MACRO__(0xA782, ## __VA_ARGS__), \
	MACRO__(0xA783, ## __VA_ARGS__), \
	MACRO__(0xA788, ## __VA_ARGS__), \
	MACRO__(0xA789, ## __VA_ARGS__), \
	MACRO__(0xA78A, ## __VA_ARGS__), \
	MACRO__(0xA78B, ## __VA_ARGS__)

/* RPL-U */
#define INTEL_RPLU_IDS(MACRO__, ...) \
	MACRO__(0xA721, ## __VA_ARGS__), \
	MACRO__(0xA7A1, ## __VA_ARGS__), \
	MACRO__(0xA7A9, ## __VA_ARGS__), \
	MACRO__(0xA7AC, ## __VA_ARGS__), \
	MACRO__(0xA7AD, ## __VA_ARGS__)

/* RPL-P */
#define INTEL_RPLP_IDS(MACRO__, ...) \
	MACRO__(0xA720, ## __VA_ARGS__), \
	MACRO__(0xA7A0, ## __VA_ARGS__), \
	MACRO__(0xA7A8, ## __VA_ARGS__), \
	MACRO__(0xA7AA, ## __VA_ARGS__), \
	MACRO__(0xA7AB, ## __VA_ARGS__)

/* DG2 */
#define INTEL_DG2_G10_IDS(MACRO__, ...) \
	MACRO__(0x5690, ## __VA_ARGS__), \
	MACRO__(0x5691, ## __VA_ARGS__), \
	MACRO__(0x5692, ## __VA_ARGS__), \
	MACRO__(0x56A0, ## __VA_ARGS__), \
	MACRO__(0x56A1, ## __VA_ARGS__), \
	MACRO__(0x56A2, ## __VA_ARGS__), \
	MACRO__(0x56BE, ## __VA_ARGS__), \
	MACRO__(0x56BF, ## __VA_ARGS__)

#define INTEL_DG2_G11_IDS(MACRO__, ...) \
	MACRO__(0x5693, ## __VA_ARGS__), \
	MACRO__(0x5694, ## __VA_ARGS__), \
	MACRO__(0x5695, ## __VA_ARGS__), \
	MACRO__(0x56A5, ## __VA_ARGS__), \
	MACRO__(0x56A6, ## __VA_ARGS__), \
	MACRO__(0x56B0, ## __VA_ARGS__), \
	MACRO__(0x56B1, ## __VA_ARGS__), \
	MACRO__(0x56BA, ## __VA_ARGS__), \
	MACRO__(0x56BB, ## __VA_ARGS__), \
	MACRO__(0x56BC, ## __VA_ARGS__), \
	MACRO__(0x56BD, ## __VA_ARGS__)

#define INTEL_DG2_G12_IDS(MACRO__, ...) \
	MACRO__(0x5696, ## __VA_ARGS__), \
	MACRO__(0x5697, ## __VA_ARGS__), \
	MACRO__(0x56A3, ## __VA_ARGS__), \
	MACRO__(0x56A4, ## __VA_ARGS__), \
	MACRO__(0x56B2, ## __VA_ARGS__), \
	MACRO__(0x56B3, ## __VA_ARGS__)

#define INTEL_DG2_IDS(MACRO__, ...) \
	INTEL_DG2_G10_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G11_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_DG2_G12_IDS(MACRO__, ## __VA_ARGS__)

#define INTEL_ATS_M150_IDS(MACRO__, ...) \
	MACRO__(0x56C0, ## __VA_ARGS__), \
	MACRO__(0x56C2, ## __VA_ARGS__)

#define INTEL_ATS_M75_IDS(MACRO__, ...) \
	MACRO__(0x56C1, ## __VA_ARGS__)

#define INTEL_ATS_M_IDS(MACRO__, ...) \
	INTEL_ATS_M150_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_ATS_M75_IDS(MACRO__, ## __VA_ARGS__)

/* ARL */
#define INTEL_ARL_H_IDS(MACRO__, ...) \
	MACRO__(0x7D51, ## __VA_ARGS__), \
	MACRO__(0x7DD1, ## __VA_ARGS__)

#define INTEL_ARL_U_IDS(MACRO__, ...) \
	MACRO__(0x7D41, ## __VA_ARGS__) \

#define INTEL_ARL_S_IDS(MACRO__, ...) \
	MACRO__(0x7D67, ## __VA_ARGS__), \
	MACRO__(0xB640, ## __VA_ARGS__)

#define INTEL_ARL_IDS(MACRO__, ...) \
	INTEL_ARL_H_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_ARL_U_IDS(MACRO__, ## __VA_ARGS__), \
	INTEL_ARL_S_IDS(MACRO__, ## __VA_ARGS__)

/* MTL */
#define INTEL_MTL_IDS(MACRO__, ...) \
	INTEL_ARL_IDS(MACRO__, ## __VA_ARGS__), \
	MACRO__(0x7D40, ## __VA_ARGS__), \
	MACRO__(0x7D45, ## __VA_ARGS__), \
	MACRO__(0x7D55, ## __VA_ARGS__), \
	MACRO__(0x7D60, ## __VA_ARGS__), \
	MACRO__(0x7DD5, ## __VA_ARGS__)

/* LNL */
#define INTEL_LNL_IDS(MACRO__, ...) \
	MACRO__(0x6420, ## __VA_ARGS__), \
	MACRO__(0x64A0, ## __VA_ARGS__), \
	MACRO__(0x64B0, ## __VA_ARGS__)

/* BMG */
#define INTEL_BMG_IDS(MACRO__, ...) \
	MACRO__(0xE202, ## __VA_ARGS__), \
	MACRO__(0xE20B, ## __VA_ARGS__), \
	MACRO__(0xE20C, ## __VA_ARGS__), \
	MACRO__(0xE20D, ## __VA_ARGS__), \
	MACRO__(0xE210, ## __VA_ARGS__), \
	MACRO__(0xE211, ## __VA_ARGS__), \
	MACRO__(0xE212, ## __VA_ARGS__), \
	MACRO__(0xE215, ## __VA_ARGS__), \
	MACRO__(0xE216, ## __VA_ARGS__)

#endif /* _I915_PCIIDS_H */
