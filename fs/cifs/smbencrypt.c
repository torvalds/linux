// SPDX-License-Identifier: GPL-2.0-or-later
/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Modified by Jeremy Allison 1995.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Modified by Steve French (sfrench@us.ibm.com) 2002-2003

*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fips.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <crypto/des.h>
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifsproto.h"

#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

/* following came from the other byteorder.h to avoid include conflicts */
#define CVAL(buf,pos) (((unsigned char *)(buf))[pos])
#define SSVALX(buf,pos,val) (CVAL(buf,pos)=(val)&0xFF,CVAL(buf,pos+1)=(val)>>8)
#define SSVAL(buf,pos,val) SSVALX((buf),(pos),((__u16)(val)))

static void
str_to_key(unsigned char *str, unsigned char *key)
{
	int i;

	key[0] = str[0] >> 1;
	key[1] = ((str[0] & 0x01) << 6) | (str[1] >> 2);
	key[2] = ((str[1] & 0x03) << 5) | (str[2] >> 3);
	key[3] = ((str[2] & 0x07) << 4) | (str[3] >> 4);
	key[4] = ((str[3] & 0x0F) << 3) | (str[4] >> 5);
	key[5] = ((str[4] & 0x1F) << 2) | (str[5] >> 6);
	key[6] = ((str[5] & 0x3F) << 1) | (str[6] >> 7);
	key[7] = str[6] & 0x7F;
	for (i = 0; i < 8; i++)
		key[i] = (key[i] << 1);
}

static int
smbhash(unsigned char *out, const unsigned char *in, unsigned char *key)
{
	unsigned char key2[8];
	struct des_ctx ctx;

	str_to_key(key, key2);

	if (fips_enabled) {
		cifs_dbg(VFS, "FIPS compliance enabled: DES not permitted\n");
		return -ENOENT;
	}

	des_expand_key(&ctx, key2, DES_KEY_SIZE);
	des_encrypt(&ctx, out, in);
	memzero_explicit(&ctx, sizeof(ctx));

	return 0;
}

static int
E_P16(unsigned char *p14, unsigned char *p16)
{
	int rc;
	unsigned char sp8[8] =
	    { 0x4b, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25 };

	rc = smbhash(p16, sp8, p14);
	if (rc)
		return rc;
	rc = smbhash(p16 + 8, sp8, p14 + 7);
	return rc;
}

static int
E_P24(unsigned char *p21, const unsigned char *c8, unsigned char *p24)
{
	int rc;

	rc = smbhash(p24, c8, p21);
	if (rc)
		return rc;
	rc = smbhash(p24 + 8, c8, p21 + 7);
	if (rc)
		return rc;
	rc = smbhash(p24 + 16, c8, p21 + 14);
	return rc;
}

/* produce a md4 message digest from data of length n bytes */
int
mdfour(unsigned char *md4_hash, unsigned char *link_str, int link_len)
{
	int rc;
	struct crypto_shash *md4 = NULL;
	struct sdesc *sdescmd4 = NULL;

	rc = cifs_alloc_hash("md4", &md4, &sdescmd4);
	if (rc)
		goto mdfour_err;

	rc = crypto_shash_init(&sdescmd4->shash);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init md4 shash\n", __func__);
		goto mdfour_err;
	}
	rc = crypto_shash_update(&sdescmd4->shash, link_str, link_len);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with link_str\n", __func__);
		goto mdfour_err;
	}
	rc = crypto_shash_final(&sdescmd4->shash, md4_hash);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate md4 hash\n", __func__);

mdfour_err:
	cifs_free_hash(&md4, &sdescmd4);
	return rc;
}

/*
   This implements the X/Open SMB password encryption
   It takes a password, a 8 byte "crypt key" and puts 24 bytes of
   encrypted password into p24 */
/* Note that password must be uppercased and null terminated */
int
SMBencrypt(unsigned char *passwd, const unsigned char *c8, unsigned char *p24)
{
	int rc;
	unsigned char p14[14], p16[16], p21[21];

	memset(p14, '\0', 14);
	memset(p16, '\0', 16);
	memset(p21, '\0', 21);

	memcpy(p14, passwd, 14);
	rc = E_P16(p14, p16);
	if (rc)
		return rc;

	memcpy(p21, p16, 16);
	rc = E_P24(p21, c8, p24);

	return rc;
}

/*
 * Creates the MD4 Hash of the users password in NT UNICODE.
 */

int
E_md4hash(const unsigned char *passwd, unsigned char *p16,
	const struct nls_table *codepage)
{
	int rc;
	int len;
	__le16 wpwd[129];

	/* Password cannot be longer than 128 characters */
	if (passwd) /* Password must be converted to NT unicode */
		len = cifs_strtoUTF16(wpwd, passwd, 128, codepage);
	else {
		len = 0;
		*wpwd = 0; /* Ensure string is null terminated */
	}

	rc = mdfour(p16, (unsigned char *) wpwd, len * sizeof(__le16));
	memzero_explicit(wpwd, sizeof(wpwd));

	return rc;
}

/* Does the NT MD4 hash then des encryption. */
int
SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24,
		const struct nls_table *codepage)
{
	int rc;
	unsigned char p16[16], p21[21];

	memset(p16, '\0', 16);
	memset(p21, '\0', 21);

	rc = E_md4hash(passwd, p16, codepage);
	if (rc) {
		cifs_dbg(FYI, "%s Can't generate NT hash, error: %d\n",
			 __func__, rc);
		return rc;
	}
	memcpy(p21, p16, 16);
	rc = E_P24(p21, c8, p24);
	return rc;
}
