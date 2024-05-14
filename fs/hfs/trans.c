/*
 *  linux/fs/hfs/trans.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains routines for converting between the Macintosh
 * character set and various other encodings.  This includes dealing
 * with ':' vs. '/' as the path-element separator.
 */

#include <linux/types.h>
#include <linux/nls.h>

#include "hfs_fs.h"

/*================ Global functions ================*/

/*
 * hfs_mac2asc()
 *
 * Given a 'Pascal String' (a string preceded by a length byte) in
 * the Macintosh character set produce the corresponding filename using
 * the 'trivial' name-mangling scheme, returning the length of the
 * mangled filename.  Note that the output string is not NULL
 * terminated.
 *
 * The name-mangling works as follows:
 * The character '/', which is illegal in Linux filenames is replaced
 * by ':' which never appears in HFS filenames.	 All other characters
 * are passed unchanged from input to output.
 */
int hfs_mac2asc(struct super_block *sb, char *out, const struct hfs_name *in)
{
	struct nls_table *nls_disk = HFS_SB(sb)->nls_disk;
	struct nls_table *nls_io = HFS_SB(sb)->nls_io;
	const char *src;
	char *dst;
	int srclen, dstlen, size;

	src = in->name;
	srclen = in->len;
	if (srclen > HFS_NAMELEN)
		srclen = HFS_NAMELEN;
	dst = out;
	dstlen = HFS_MAX_NAMELEN;
	if (nls_io) {
		wchar_t ch;

		while (srclen > 0) {
			if (nls_disk) {
				size = nls_disk->char2uni(src, srclen, &ch);
				if (size <= 0) {
					ch = '?';
					size = 1;
				}
				src += size;
				srclen -= size;
			} else {
				ch = *src++;
				srclen--;
			}
			if (ch == '/')
				ch = ':';
			size = nls_io->uni2char(ch, dst, dstlen);
			if (size < 0) {
				if (size == -ENAMETOOLONG)
					goto out;
				*dst = '?';
				size = 1;
			}
			dst += size;
			dstlen -= size;
		}
	} else {
		char ch;

		while (--srclen >= 0)
			*dst++ = (ch = *src++) == '/' ? ':' : ch;
	}
out:
	return dst - out;
}

/*
 * hfs_asc2mac()
 *
 * Given an ASCII string (not null-terminated) and its length,
 * generate the corresponding filename in the Macintosh character set
 * using the 'trivial' name-mangling scheme, returning the length of
 * the mangled filename.  Note that the output string is not NULL
 * terminated.
 *
 * This routine is a inverse to hfs_mac2triv().
 * A ':' is replaced by a '/'.
 */
void hfs_asc2mac(struct super_block *sb, struct hfs_name *out, const struct qstr *in)
{
	struct nls_table *nls_disk = HFS_SB(sb)->nls_disk;
	struct nls_table *nls_io = HFS_SB(sb)->nls_io;
	const char *src;
	char *dst;
	int srclen, dstlen, size;

	src = in->name;
	srclen = in->len;
	dst = out->name;
	dstlen = HFS_NAMELEN;
	if (nls_io) {
		wchar_t ch;

		while (srclen > 0 && dstlen > 0) {
			size = nls_io->char2uni(src, srclen, &ch);
			if (size < 0) {
				ch = '?';
				size = 1;
			}
			src += size;
			srclen -= size;
			if (ch == ':')
				ch = '/';
			if (nls_disk) {
				size = nls_disk->uni2char(ch, dst, dstlen);
				if (size < 0) {
					if (size == -ENAMETOOLONG)
						goto out;
					*dst = '?';
					size = 1;
				}
				dst += size;
				dstlen -= size;
			} else {
				*dst++ = ch > 0xff ? '?' : ch;
				dstlen--;
			}
		}
	} else {
		char ch;

		if (dstlen > srclen)
			dstlen = srclen;
		while (--dstlen >= 0)
			*dst++ = (ch = *src++) == ':' ? '/' : ch;
	}
out:
	out->len = dst - (char *)out->name;
	dstlen = HFS_NAMELEN - out->len;
	while (--dstlen >= 0)
		*dst++ = 0;
}
