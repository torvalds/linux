// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel collation handling.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 *
 * Part of this file is based on code from the NTFS-3G.
 * and is copyrighted by the respective authors below:
 * Copyright (c) 2004 Anton Altaparmakov
 * Copyright (c) 2005 Yura Pakhuchiy
 */

#include "collate.h"
#include "debug.h"
#include "ntfs.h"

#include <linux/sort.h>

static int ntfs_collate_binary(struct ntfs_volume *vol,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len)
{
	int rc;

	rc = memcmp(data1, data2, min(data1_len, data2_len));
	if (!rc && (data1_len != data2_len)) {
		if (data1_len < data2_len)
			rc = -1;
		else
			rc = 1;
	}
	return rc;
}

static int ntfs_collate_ntofs_ulong(struct ntfs_volume *vol,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len)
{
	int rc;
	u32 d1 = le32_to_cpup(data1), d2 = le32_to_cpup(data2);

	if (data1_len != data2_len || data1_len != 4)
		return -EINVAL;

	if (d1 < d2)
		rc = -1;
	else {
		if (d1 == d2)
			rc = 0;
		else
			rc = 1;
	}
	return rc;
}

/*
 * ntfs_collate_ntofs_ulongs - Which of two le32 arrays should be listed first
 * @vol: ntfs volume
 * @data1: first ulong array to collate
 * @data1_len: length in bytes of @data1
 * @data2: second ulong array to collate
 * @data2_len: length in bytes of @data2
 *
 * Returns: -1, 0 or 1 depending of how the arrays compare
 */
static int ntfs_collate_ntofs_ulongs(struct ntfs_volume *vol,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len)
{
	int len;
	const __le32 *p1 = data1, *p2 = data2;
	u32 d1, d2;

	if (data1_len != data2_len || data1_len & 3) {
		ntfs_error(vol->sb, "data1_len or data2_len not valid\n");
		return -1;
	}

	len = data1_len;
	do {
		d1 = le32_to_cpup(p1);
		p1++;
		d2 = le32_to_cpup(p2);
		p2++;
	} while (d1 == d2 && (len -= 4) > 0);
	return cmp_int(d1, d2);
}

/*
 * ntfs_collate_file_name - Which of two filenames should be listed first
 * @vol: ntfs volume
 * @data1: first filename to collate
 * @data1_len: length in bytes of @data1(unused)
 * @data2: second filename to collate
 * @data2_len: length in bytes of @data2(unused)
 */
static int ntfs_collate_file_name(struct ntfs_volume *vol,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len)
{
	int rc;

	rc = ntfs_file_compare_values(data1, data2, -EINVAL,
			IGNORE_CASE, vol->upcase, vol->upcase_len);
	if (!rc)
		rc = ntfs_file_compare_values(data1, data2,
			-EINVAL, CASE_SENSITIVE, vol->upcase, vol->upcase_len);
	return rc;
}

/*
 * ntfs_collate - collate two data items using a specified collation rule
 * @vol:	ntfs volume to which the data items belong
 * @cr:		collation rule to use when comparing the items
 * @data1:	first data item to collate
 * @data1_len:	length in bytes of @data1
 * @data2:	second data item to collate
 * @data2_len:	length in bytes of @data2
 *
 * Collate the two data items @data1 and @data2 using the collation rule @cr
 * and return -1, 0, ir 1 if @data1 is found, respectively, to collate before,
 * to match, or to collate after @data2. return -EINVAL if an error occurred.
 */
int ntfs_collate(struct ntfs_volume *vol, __le32 cr,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len)
{
	switch (le32_to_cpu(cr)) {
	case le32_to_cpu(COLLATION_BINARY):
		return ntfs_collate_binary(vol, data1, data1_len,
					   data2, data2_len);
	case le32_to_cpu(COLLATION_FILE_NAME):
		return ntfs_collate_file_name(vol, data1, data1_len,
					      data2, data2_len);
	case le32_to_cpu(COLLATION_NTOFS_ULONG):
		return ntfs_collate_ntofs_ulong(vol, data1, data1_len,
						data2, data2_len);
	case le32_to_cpu(COLLATION_NTOFS_ULONGS):
		return ntfs_collate_ntofs_ulongs(vol, data1, data1_len,
						 data2, data2_len);
	default:
		ntfs_error(vol->sb, "Unknown collation rule 0x%x",
			   le32_to_cpu(cr));
		return -EINVAL;
	}
}
