/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_unicode.h"
#include "jfs_debug.h"

/*
 * NAME:	jfs_strfromUCS()
 *
 * FUNCTION:	Convert little-endian unicode string to character string
 *
 */
int jfs_strfromUCS_le(char *to, const __le16 * from,
		      int len, struct nls_table *codepage)
{
	int i;
	int outlen = 0;
	static int warn_again = 5;	/* Only warn up to 5 times total */
	int warn = !!warn_again;	/* once per string */

	if (codepage) {
		for (i = 0; (i < len) && from[i]; i++) {
			int charlen;
			charlen =
			    codepage->uni2char(le16_to_cpu(from[i]),
					       &to[outlen],
					       NLS_MAX_CHARSET_SIZE);
			if (charlen > 0)
				outlen += charlen;
			else
				to[outlen++] = '?';
		}
	} else {
		for (i = 0; (i < len) && from[i]; i++) {
			if (unlikely(le16_to_cpu(from[i]) & 0xff00)) {
				to[i] = '?';
				if (unlikely(warn)) {
					warn--;
					warn_again--;
					printk(KERN_ERR
			"non-latin1 character 0x%x found in JFS file name\n", 
		       			       le16_to_cpu(from[i]));
					printk(KERN_ERR
				"mount with iocharset=utf8 to access\n");
				}

			}
			else
				to[i] = (char) (le16_to_cpu(from[i]));
		}
		outlen = i;
	}
	to[outlen] = 0;
	return outlen;
}

/*
 * NAME:	jfs_strtoUCS()
 *
 * FUNCTION:	Convert character string to unicode string
 *
 */
static int jfs_strtoUCS(wchar_t * to, const unsigned char *from, int len,
		struct nls_table *codepage)
{
	int charlen;
	int i;

	if (codepage) {
		for (i = 0; len && *from; i++, from += charlen, len -= charlen)
		{
			charlen = codepage->char2uni(from, len, &to[i]);
			if (charlen < 1) {
				jfs_err("jfs_strtoUCS: char2uni returned %d.",
					charlen);
				jfs_err("charset = %s, char = 0x%x",
					codepage->charset, *from);
				return charlen;
			}
		}
	} else {
		for (i = 0; (i < len) && from[i]; i++)
			to[i] = (wchar_t) from[i];
	}

	to[i] = 0;
	return i;
}

/*
 * NAME:	get_UCSname()
 *
 * FUNCTION:	Allocate and translate to unicode string
 *
 */
int get_UCSname(struct component_name * uniName, struct dentry *dentry)
{
	struct nls_table *nls_tab = JFS_SBI(dentry->d_sb)->nls_tab;
	int length = dentry->d_name.len;

	if (length > JFS_NAME_MAX)
		return -ENAMETOOLONG;

	uniName->name =
	    kmalloc((length + 1) * sizeof(wchar_t), GFP_NOFS);

	if (uniName->name == NULL)
		return -ENOSPC;

	uniName->namlen = jfs_strtoUCS(uniName->name, dentry->d_name.name,
				       length, nls_tab);

	if (uniName->namlen < 0) {
		kfree(uniName->name);
		return uniName->namlen;
	}

	return 0;
}
