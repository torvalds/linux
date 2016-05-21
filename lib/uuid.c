/*
 * Unified UUID/GUID definition
 *
 * Copyright (C) 2009, Intel Corp.
 *	Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/uuid.h>
#include <linux/random.h>

const u8 uuid_le_index[16] = {3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
EXPORT_SYMBOL(uuid_le_index);
const u8 uuid_be_index[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
EXPORT_SYMBOL(uuid_be_index);

/***************************************************************
 * Random UUID interface
 *
 * Used here for a Boot ID, but can be useful for other kernel
 * drivers.
 ***************************************************************/

/*
 * Generate random UUID
 */
void generate_random_uuid(unsigned char uuid[16])
{
	get_random_bytes(uuid, 16);
	/* Set UUID version to 4 --- truly random generation */
	uuid[6] = (uuid[6] & 0x0F) | 0x40;
	/* Set the UUID variant to DCE */
	uuid[8] = (uuid[8] & 0x3F) | 0x80;
}
EXPORT_SYMBOL(generate_random_uuid);

static void __uuid_gen_common(__u8 b[16])
{
	prandom_bytes(b, 16);
	/* reversion 0b10 */
	b[8] = (b[8] & 0x3F) | 0x80;
}

void uuid_le_gen(uuid_le *lu)
{
	__uuid_gen_common(lu->b);
	/* version 4 : random generation */
	lu->b[7] = (lu->b[7] & 0x0F) | 0x40;
}
EXPORT_SYMBOL_GPL(uuid_le_gen);

void uuid_be_gen(uuid_be *bu)
{
	__uuid_gen_common(bu->b);
	/* version 4 : random generation */
	bu->b[6] = (bu->b[6] & 0x0F) | 0x40;
}
EXPORT_SYMBOL_GPL(uuid_be_gen);

/**
  * uuid_is_valid - checks if UUID string valid
  * @uuid:	UUID string to check
  *
  * Description:
  * It checks if the UUID string is following the format:
  *	xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  * where x is a hex digit.
  *
  * Return: true if input is valid UUID string.
  */
bool uuid_is_valid(const char *uuid)
{
	unsigned int i;

	for (i = 0; i < UUID_STRING_LEN; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (uuid[i] != '-')
				return false;
		} else if (!isxdigit(uuid[i])) {
			return false;
		}
	}

	return true;
}
EXPORT_SYMBOL(uuid_is_valid);

static int __uuid_to_bin(const char *uuid, __u8 b[16], const u8 ei[16])
{
	static const u8 si[16] = {0,2,4,6,9,11,14,16,19,21,24,26,28,30,32,34};
	unsigned int i;

	if (!uuid_is_valid(uuid))
		return -EINVAL;

	for (i = 0; i < 16; i++) {
		int hi = hex_to_bin(uuid[si[i]] + 0);
		int lo = hex_to_bin(uuid[si[i]] + 1);

		b[ei[i]] = (hi << 4) | lo;
	}

	return 0;
}

int uuid_le_to_bin(const char *uuid, uuid_le *u)
{
	return __uuid_to_bin(uuid, u->b, uuid_le_index);
}
EXPORT_SYMBOL(uuid_le_to_bin);

int uuid_be_to_bin(const char *uuid, uuid_be *u)
{
	return __uuid_to_bin(uuid, u->b, uuid_be_index);
}
EXPORT_SYMBOL(uuid_be_to_bin);
