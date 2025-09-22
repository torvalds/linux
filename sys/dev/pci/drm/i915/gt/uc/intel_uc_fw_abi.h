/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef _INTEL_UC_FW_ABI_H
#define _INTEL_UC_FW_ABI_H

#include <linux/types.h>
#include <linux/build_bug.h>

/**
 * DOC: Firmware Layout
 *
 * The GuC/HuC firmware layout looks like this::
 *
 *      +======================================================================+
 *      |  Firmware blob                                                       |
 *      +===============+===============+============+============+============+
 *      |  CSS header   |     uCode     |  RSA key   |  modulus   |  exponent  |
 *      +===============+===============+============+============+============+
 *       <-header size->                 <---header size continued ----------->
 *       <--- size ----------------------------------------------------------->
 *                                       <-key size->
 *                                                    <-mod size->
 *                                                                 <-exp size->
 *
 * The firmware may or may not have modulus key and exponent data. The header,
 * uCode and RSA signature are must-have components that will be used by driver.
 * Length of each components, which is all in dwords, can be found in header.
 * In the case that modulus and exponent are not present in fw, a.k.a truncated
 * image, the length value still appears in header.
 *
 * Driver will do some basic fw size validation based on the following rules:
 *
 * 1. Header, uCode and RSA are must-have components.
 * 2. All firmware components, if they present, are in the sequence illustrated
 *    in the layout table above.
 * 3. Length info of each component can be found in header, in dwords.
 * 4. Modulus and exponent key are not required by driver. They may not appear
 *    in fw. So driver will load a truncated firmware in this case.
 *
 * Starting from DG2, the HuC is loaded by the GSC instead of i915. The GSC
 * firmware performs all the required integrity checks, we just need to check
 * the version. Note that the header for GSC-managed blobs is different from the
 * CSS used for dma-loaded firmwares.
 */

struct uc_css_header {
	u32 module_type;
	/*
	 * header_size includes all non-uCode bits, including css_header, rsa
	 * key, modulus key and exponent data.
	 */
	u32 header_size_dw;
	u32 header_version;
	u32 module_id;
	u32 module_vendor;
	u32 date;
#define CSS_DATE_DAY			(0xFF << 0)
#define CSS_DATE_MONTH			(0xFF << 8)
#define CSS_DATE_YEAR			(0xFFFF << 16)
	u32 size_dw; /* uCode plus header_size_dw */
	u32 key_size_dw;
	u32 modulus_size_dw;
	u32 exponent_size_dw;
	u32 time;
#define CSS_TIME_HOUR			(0xFF << 0)
#define CSS_DATE_MIN			(0xFF << 8)
#define CSS_DATE_SEC			(0xFFFF << 16)
	char username[8];
	char buildnumber[12];
	u32 sw_version;
#define CSS_SW_VERSION_UC_MAJOR		(0xFF << 16)
#define CSS_SW_VERSION_UC_MINOR		(0xFF << 8)
#define CSS_SW_VERSION_UC_PATCH		(0xFF << 0)
	u32 vf_version;
	u32 reserved0[12];
	union {
		u32 private_data_size; /* only applies to GuC */
		u32 reserved1;
	};
	u32 header_info;
} __packed;
#ifdef notyet
static_assert(sizeof(struct uc_css_header) == 128);
#endif

#endif /* _INTEL_UC_FW_ABI_H */
