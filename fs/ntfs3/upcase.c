// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

static inline u16 upcase_unicode_char(const u16 *upcase, u16 chr)
{
	if (chr < 'a')
		return chr;

	if (chr <= 'z')
		return chr - ('a' - 'A');

	return upcase[chr];
}

/*
 * ntfs_cmp_names
 *
 * Thanks Kari Argillander <kari.argillander@gmail.com> for idea and implementation 'bothcase'
 *
 * Straight way to compare names:
 * - Case insensitive
 * - If name equals and 'bothcases' then
 * - Case sensitive
 * 'Straight way' code scans input names twice in worst case.
 * Optimized code scans input names only once.
 */
int ntfs_cmp_names(const __le16 *s1, size_t l1, const __le16 *s2, size_t l2,
		   const u16 *upcase, bool bothcase)
{
	int diff1 = 0;
	int diff2;
	size_t len = min(l1, l2);

	if (!bothcase && upcase)
		goto case_insentive;

	for (; len; s1++, s2++, len--) {
		diff1 = le16_to_cpu(*s1) - le16_to_cpu(*s2);
		if (diff1) {
			if (bothcase && upcase)
				goto case_insentive;

			return diff1;
		}
	}
	return l1 - l2;

case_insentive:
	for (; len; s1++, s2++, len--) {
		diff2 = upcase_unicode_char(upcase, le16_to_cpu(*s1)) -
			upcase_unicode_char(upcase, le16_to_cpu(*s2));
		if (diff2)
			return diff2;
	}

	diff2 = l1 - l2;
	return diff2 ? diff2 : diff1;
}

int ntfs_cmp_names_cpu(const struct cpu_str *uni1, const struct le_str *uni2,
		       const u16 *upcase, bool bothcase)
{
	const u16 *s1 = uni1->name;
	const __le16 *s2 = uni2->name;
	size_t l1 = uni1->len;
	size_t l2 = uni2->len;
	size_t len = min(l1, l2);
	int diff1 = 0;
	int diff2;

	if (!bothcase && upcase)
		goto case_insentive;

	for (; len; s1++, s2++, len--) {
		diff1 = *s1 - le16_to_cpu(*s2);
		if (diff1) {
			if (bothcase && upcase)
				goto case_insentive;

			return diff1;
		}
	}
	return l1 - l2;

case_insentive:
	for (; len; s1++, s2++, len--) {
		diff2 = upcase_unicode_char(upcase, *s1) -
			upcase_unicode_char(upcase, le16_to_cpu(*s2));
		if (diff2)
			return diff2;
	}

	diff2 = l1 - l2;
	return diff2 ? diff2 : diff1;
}
