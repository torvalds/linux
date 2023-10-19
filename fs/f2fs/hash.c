// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/hash.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Portions of this code from linux/fs/ext3/hash.c
 *
 * Copyright (C) 2002 by Theodore Ts'o
 */
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/pagemap.h>
#include <linux/unicode.h>

#include "f2fs.h"

/*
 * Hashing code copied from ext3
 */
#define DELTA 0x9E3779B9

static void TEA_transform(unsigned int buf[4], unsigned int const in[])
{
	__u32 sum = 0;
	__u32 b0 = buf[0], b1 = buf[1];
	__u32 a = in[0], b = in[1], c = in[2], d = in[3];
	int n = 16;

	do {
		sum += DELTA;
		b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);
		b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

static void str2hashbuf(const unsigned char *msg, size_t len,
				unsigned int *buf, int num)
{
	unsigned pad, val;
	int i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num * 4)
		len = num * 4;
	for (i = 0; i < len; i++) {
		if ((i % 4) == 0)
			val = pad;
		val = msg[i] + (val << 8);
		if ((i % 4) == 3) {
			*buf++ = val;
			val = pad;
			num--;
		}
	}
	if (--num >= 0)
		*buf++ = val;
	while (--num >= 0)
		*buf++ = pad;
}

static u32 TEA_hash_name(const u8 *p, size_t len)
{
	__u32 in[8], buf[4];

	/* Initialize the default seed for the hash checksum functions */
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	while (1) {
		str2hashbuf(p, len, in, 4);
		TEA_transform(buf, in);
		p += 16;
		if (len <= 16)
			break;
		len -= 16;
	}
	return buf[0] & ~F2FS_HASH_COL_BIT;
}

/*
 * Compute @fname->hash.  For all directories, @fname->disk_name must be set.
 * For casefolded directories, @fname->usr_fname must be set, and also
 * @fname->cf_name if the filename is valid Unicode and is not "." or "..".
 */
void f2fs_hash_filename(const struct inode *dir, struct f2fs_filename *fname)
{
	const u8 *name = fname->disk_name.name;
	size_t len = fname->disk_name.len;

	WARN_ON_ONCE(!name);

	if (is_dot_dotdot(name, len)) {
		fname->hash = 0;
		return;
	}

#if IS_ENABLED(CONFIG_UNICODE)
	if (IS_CASEFOLDED(dir)) {
		/*
		 * If the casefolded name is provided, hash it instead of the
		 * on-disk name.  If the casefolded name is *not* provided, that
		 * should only be because the name wasn't valid Unicode or was
		 * "." or "..", so fall back to treating the name as an opaque
		 * byte sequence.  Note that to handle encrypted directories,
		 * the fallback must use usr_fname (plaintext) rather than
		 * disk_name (ciphertext).
		 */
		WARN_ON_ONCE(!fname->usr_fname->name);
		if (fname->cf_name.name) {
			name = fname->cf_name.name;
			len = fname->cf_name.len;
		} else {
			name = fname->usr_fname->name;
			len = fname->usr_fname->len;
		}
		if (IS_ENCRYPTED(dir)) {
			struct qstr tmp = QSTR_INIT(name, len);

			fname->hash =
				cpu_to_le32(fscrypt_fname_siphash(dir, &tmp));
			return;
		}
	}
#endif
	fname->hash = cpu_to_le32(TEA_hash_name(name, len));
}
