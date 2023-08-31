/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Some of the source code in this file came from fs/cifs/cifs_unicode.c
 * cifs_unicode:  Unicode kernel case support
 *
 * Function:
 *     Convert a unicode character to upper or lower case using
 *     compressed tables.
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2009
 *
 *
 * Notes:
 *     These APIs are based on the C library functions.  The semantics
 *     should match the C functions but with expanded size operands.
 *
 *     The upper/lower functions are based on a table created by mkupr.
 *     This is a compressed table of upper and lower case conversion.
 *
 */
#ifndef _SMB_UNICODE_H
#define _SMB_UNICODE_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/nls.h>
#include <linux/unicode.h>
#include "../../nls/nls_ucs2_utils.h"

#ifdef __KERNEL__
int smb_strtoUTF16(__le16 *to, const char *from, int len,
		   const struct nls_table *codepage);
char *smb_strndup_from_utf16(const char *src, const int maxlen,
			     const bool is_unicode,
			     const struct nls_table *codepage);
int smbConvertToUTF16(__le16 *target, const char *source, int srclen,
		      const struct nls_table *cp, int mapchars);
char *ksmbd_extract_sharename(struct unicode_map *um, const char *treename);
#endif

#endif /* _SMB_UNICODE_H */
