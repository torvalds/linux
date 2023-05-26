/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_SOCINFO_H__
#define __QCOM_SOCINFO_H__

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID		137

#define SMEM_SOCINFO_BUILD_ID_LENGTH	32
#define SMEM_SOCINFO_CHIP_ID_LENGTH	32

/* Socinfo SMEM item structure */
struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	char build_id[SMEM_SOCINFO_BUILD_ID_LENGTH];
	/* Version 2 */
	__le32 raw_id;
	__le32 raw_ver;
	/* Version 3 */
	__le32 hw_plat;
	/* Version 4 */
	__le32 plat_ver;
	/* Version 5 */
	__le32 accessory_chip;
	/* Version 6 */
	__le32 hw_plat_subtype;
	/* Version 7 */
	__le32 pmic_model;
	__le32 pmic_die_rev;
	/* Version 8 */
	__le32 pmic_model_1;
	__le32 pmic_die_rev_1;
	__le32 pmic_model_2;
	__le32 pmic_die_rev_2;
	/* Version 9 */
	__le32 foundry_id;
	/* Version 10 */
	__le32 serial_num;
	/* Version 11 */
	__le32 num_pmics;
	__le32 pmic_array_offset;
	/* Version 12 */
	__le32 chip_family;
	__le32 raw_device_family;
	__le32 raw_device_num;
	/* Version 13 */
	__le32 nproduct_id;
	char chip_id[SMEM_SOCINFO_CHIP_ID_LENGTH];
	/* Version 14 */
	__le32 num_clusters;
	__le32 ncluster_array_offset;
	__le32 num_defective_parts;
	__le32 ndefective_parts_array_offset;
	/* Version 15 */
	__le32 nmodem_supported;
	/* Version 16 */
	__le32  feature_code;
	__le32  pcode;
	__le32  npartnamemap_offset;
	__le32  nnum_partname_mapping;
	/* Version 17 */
	__le32 oem_variant;
};

#endif
