// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aug 8, 2011 Bob Pearson with help from Joakim Tjernlund and George Spelvin
 * cleaned up code to current version of sparse and added the slicing-by-8
 * algorithm to the closely similar existing slicing-by-4 algorithm.
 *
 * Oct 15, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Nicer crc32 functions/docs submitted by linux@horizon.com.  Thanks!
 * Code was from the public domain, copyright abandoned.  Code was
 * subsequently included in the kernel, thus was re-licensed under the
 * GNU GPL v2.
 *
 * Oct 12, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Same crc32 function was used in 5 other places in the kernel.
 * I made one version, and deleted the others.
 * There are various incantations of crc32().  Some use a seed of 0 or ~0.
 * Some xor at the end with ~0.  The generic crc32() function takes
 * seed as an argument, and doesn't xor at the end.  Then individual
 * users can do whatever they need.
 *   drivers/net/smc9194.c uses seed ~0, doesn't xor with ~0.
 *   fs/jffs2 uses seed 0, doesn't xor with ~0.
 *   fs/partitions/efi.c uses seed ~0, xor's with ~0.
 */

/* see: Documentation/staging/crc32.rst for a description of algorithms */

#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/types.h>

#include "crc32table.h"

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@dell.com>");
MODULE_DESCRIPTION("Various CRC32 calculations");
MODULE_LICENSE("GPL");

u32 crc32_le_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32table_le[(crc & 255) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32_le_base);

u32 crc32c_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc >> 8) ^ crc32ctable_le[(crc & 255) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32c_base);

u32 crc32_be_base(u32 crc, const u8 *p, size_t len)
{
	while (len--)
		crc = (crc << 8) ^ crc32table_be[(crc >> 24) ^ *p++];
	return crc;
}
EXPORT_SYMBOL(crc32_be_base);
