/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cifs_unicode:  Unicode kernel case support
 *
 * Function:
 *     Convert a unicode character to upper or lower case using
 *     compressed tables.
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2009
 *
 * Notes:
 *     These APIs are based on the C library functions.  The semantics
 *     should match the C functions but with expanded size operands.
 *
 *     The upper/lower functions are based on a table created by mkupr.
 *     This is a compressed table of upper and lower case conversion.
 */
#ifndef _CIFS_UNICODE_H
#define _CIFS_UNICODE_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/nls.h>
#include "../../nls/nls_ucs2_utils.h"

/*
 * Macs use an older "SFM" mapping of the symbols above. Fortunately it does
 * not conflict (although almost does) with the mapping above.
 */

#define SFM_DOUBLEQUOTE ((__u16) 0xF020)
#define SFM_ASTERISK    ((__u16) 0xF021)
#define SFM_QUESTION    ((__u16) 0xF025)
#define SFM_COLON       ((__u16) 0xF022)
#define SFM_GRTRTHAN    ((__u16) 0xF024)
#define SFM_LESSTHAN    ((__u16) 0xF023)
#define SFM_PIPE        ((__u16) 0xF027)
#define SFM_SLASH       ((__u16) 0xF026)
#define SFM_SPACE	((__u16) 0xF028)
#define SFM_PERIOD	((__u16) 0xF029)

/*
 * Mapping mechanism to use when one of the seven reserved characters is
 * encountered.  We can only map using one of the mechanisms at a time
 * since otherwise readdir could return directory entries which we would
 * not be able to open
 *
 * NO_MAP_UNI_RSVD  = do not perform any remapping of the character
 * SFM_MAP_UNI_RSVD = map reserved characters using SFM scheme (MAC compatible)
 * SFU_MAP_UNI_RSVD = map reserved characters ala SFU ("mapchars" option)
 *
 */
#define NO_MAP_UNI_RSVD		0
#define SFM_MAP_UNI_RSVD	1
#define SFU_MAP_UNI_RSVD	2

#ifdef __KERNEL__
int cifs_from_utf16(char *to, const __le16 *from, int tolen, int fromlen,
		    const struct nls_table *cp, int map_type);
int cifs_utf16_bytes(const __le16 *from, int maxbytes,
		     const struct nls_table *codepage);
int cifs_strtoUTF16(__le16 *, const char *, int, const struct nls_table *);
char *cifs_strndup_from_utf16(const char *src, const int maxlen,
			      const bool is_unicode,
			      const struct nls_table *codepage);
extern int cifsConvertToUTF16(__le16 *target, const char *source, int maxlen,
			      const struct nls_table *cp, int mapChars);
extern int cifs_remap(struct cifs_sb_info *cifs_sb);
extern __le16 *cifs_strndup_to_utf16(const char *src, const int maxlen,
				     int *utf16_len, const struct nls_table *cp,
				     int remap);
#endif

wchar_t cifs_toupper(wchar_t in);

#endif /* _CIFS_UNICODE_H */
