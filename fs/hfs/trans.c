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

#include "hfs_fs.h"

/*================ Global functions ================*/

/*
 * hfs_mac2triv()
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
int hfs_mac2triv(char *out, const struct hfs_name *in)
{
	const char *p;
	char c;
	int i, len;

	len = in->len;
	p = in->name;
	for (i = 0; i < len; i++) {
		c = *p++;
		*out++ = c == '/' ? ':' : c;
	}
	return i;
}

/*
 * hfs_triv2mac()
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
void hfs_triv2mac(struct hfs_name *out, struct qstr *in)
{
	const char *src;
	char *dst, c;
	int i, len;

	out->len = len = min((unsigned int)HFS_NAMELEN, in->len);
	src = in->name;
	dst = out->name;
	for (i = 0; i < len; i++) {
		c = *src++;
		*dst++ = c == ':' ? '/' : c;
	}
	for (; i < HFS_NAMELEN; i++)
		*dst++ = 0;
}
