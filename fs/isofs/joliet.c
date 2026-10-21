// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/isofs/joliet.c
 *
 *  (C) 1996 Gordon Chaffee
 *
 *  Joliet: Microsoft's Unicode extensions to iso9660
 */

#include <linux/types.h>
#include <linux/nls.h>
#include "isofs.h"

/*
 * Convert Unicode 16 to UTF-8 or ASCII.
 */
static int
uni16_to_x8(unsigned char *ascii, __be16 *uni, int len, struct nls_table *nls,
	    int outsize)
{
	__be16 *ip, ch;
	unsigned char *op, *end;

	ip = uni;
	op = ascii;
	end = ascii + outsize - 1;	/* leave room for the terminator */

	while ((ch = get_unaligned(ip)) && len) {
		int llen;

		if (op >= end)
			break;
		llen = nls->uni2char(be16_to_cpu(ch), op, end - op);
		if (llen > 0)
			op += llen;
		else if (llen == -ENAMETOOLONG)
			break;
		else
			*op++ = '?';
		ip++;

		len--;
	}
	*op = 0;
	return (op - ascii);
}

/*
 * Convert the Joliet name of @de into @outname, a buffer of @outsize bytes.
 * The result is at most @outsize - 1 bytes long; a longer name is cut at a
 * character boundary.
 */
int
get_joliet_filename(struct iso_directory_record *de, unsigned char *outname,
		    int outsize, struct inode *inode)
{
	struct nls_table *nls;
	int len = 0;

	nls = ISOFS_SB(inode->i_sb)->s_nls_iocharset;

	if (!nls) {
		len = utf16s_to_utf8s((const wchar_t *) de->name,
				de->name_len[0] >> 1, UTF16_BIG_ENDIAN,
				outname, outsize - 1);
	} else {
		len = uni16_to_x8(outname, (__be16 *) de->name,
				de->name_len[0] >> 1, nls, outsize);
	}
	if ((len > 2) && (outname[len-2] == ';') && (outname[len-1] == '1'))
		len -= 2;

	/*
	 * Windows doesn't like periods at the end of a name,
	 * so neither do we
	 */
	while (len >= 2 && (outname[len-1] == '.'))
		len--;

	return len;
}
