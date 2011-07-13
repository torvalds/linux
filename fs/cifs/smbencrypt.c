/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Modified by Jeremy Allison 1995.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Modified by Steve French (sfrench@us.ibm.com) 2002-2003

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/random.h>
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
	int rc;
	unsigned char key2[8];
	struct crypto_blkcipher *tfm_des;
	struct scatterlist sgin, sgout;
	struct blkcipher_desc desc;

	str_to_key(key, key2);

	tfm_des = crypto_alloc_blkcipher("ecb(des)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm_des)) {
		rc = PTR_ERR(tfm_des);
		cERROR(1, "could not allocate des crypto API\n");
		goto smbhash_err;
	}

	desc.tfm = tfm_des;

	crypto_blkcipher_setkey(tfm_des, key2, 8);

	sg_init_one(&sgin, in, 8);
	sg_init_one(&sgout, out, 8);

	rc = crypto_blkcipher_encrypt(&desc, &sgout, &sgin, 8);
	if (rc)
		cERROR(1, "could not encrypt crypt key rc: %d\n", rc);

	crypto_free_blkcipher(tfm_des);
smbhash_err:
	return rc;
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
	unsigned int size;
	struct crypto_shash *md4;
	struct sdesc *sdescmd4;

	md4 = crypto_alloc_shash("md4", 0, 0);
	if (IS_ERR(md4)) {
		rc = PTR_ERR(md4);
		cERROR(1, "%s: Crypto md4 allocation error %d\n", __func__, rc);
		return rc;
	}
	size = sizeof(struct shash_desc) + crypto_shash_descsize(md4);
	sdescmd4 = kmalloc(size, GFP_KERNEL);
	if (!sdescmd4) {
		rc = -ENOMEM;
		cERROR(1, "%s: Memory allocation failure\n", __func__);
		goto mdfour_err;
	}
	sdescmd4->shash.tfm = md4;
	sdescmd4->shash.flags = 0x0;

	rc = crypto_shash_init(&sdescmd4->shash);
	if (rc) {
		cERROR(1, "%s: Could not init md4 shash\n", __func__);
		goto mdfour_err;
	}
	crypto_shash_update(&sdescmd4->shash, link_str, link_len);
	rc = crypto_shash_final(&sdescmd4->shash, md4_hash);

mdfour_err:
	crypto_free_shash(md4);
	kfree(sdescmd4);

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

/* Routines for Windows NT MD4 Hash functions. */
static int
_my_wcslen(__u16 *str)
{
	int len = 0;
	while (*str++ != 0)
		len++;
	return len;
}

/*
 * Convert a string into an NT UNICODE string.
 * Note that regardless of processor type
 * this must be in intel (little-endian)
 * format.
 */

static int
_my_mbstowcs(__u16 *dst, const unsigned char *src, int len)
{	/* BB not a very good conversion routine - change/fix */
	int i;
	__u16 val;

	for (i = 0; i < len; i++) {
		val = *src;
		SSVAL(dst, 0, val);
		dst++;
		src++;
		if (val == 0)
			break;
	}
	return i;
}

/*
 * Creates the MD4 Hash of the users password in NT UNICODE.
 */

int
E_md4hash(const unsigned char *passwd, unsigned char *p16)
{
	int rc;
	int len;
	__u16 wpwd[129];

	/* Password cannot be longer than 128 characters */
	if (passwd) {
		len = strlen((char *) passwd);
		if (len > 128)
			len = 128;

		/* Password must be converted to NT unicode */
		_my_mbstowcs(wpwd, passwd, len);
	} else
		len = 0;

	wpwd[len] = 0;	/* Ensure string is null terminated */
	/* Calculate length in bytes */
	len = _my_wcslen(wpwd) * sizeof(__u16);

	rc = mdfour(p16, (unsigned char *) wpwd, len);
	memset(wpwd, 0, 129 * 2);

	return rc;
}

#if 0 /* currently unused */
/* Does both the NT and LM owfs of a user's password */
static void
nt_lm_owf_gen(char *pwd, unsigned char nt_p16[16], unsigned char p16[16])
{
	char passwd[514];

	memset(passwd, '\0', 514);
	if (strlen(pwd) < 513)
		strcpy(passwd, pwd);
	else
		memcpy(passwd, pwd, 512);
	/* Calculate the MD4 hash (NT compatible) of the password */
	memset(nt_p16, '\0', 16);
	E_md4hash(passwd, nt_p16);

	/* Mangle the passwords into Lanman format */
	passwd[14] = '\0';
/*	strupper(passwd); */

	/* Calculate the SMB (lanman) hash functions of the password */

	memset(p16, '\0', 16);
	E_P16((unsigned char *) passwd, (unsigned char *) p16);

	/* clear out local copy of user's password (just being paranoid). */
	memset(passwd, '\0', sizeof(passwd));
}
#endif

/* Does the NTLMv2 owfs of a user's password */
#if 0  /* function not needed yet - but will be soon */
static void
ntv2_owf_gen(const unsigned char owf[16], const char *user_n,
		const char *domain_n, unsigned char kr_buf[16],
		const struct nls_table *nls_codepage)
{
	wchar_t *user_u;
	wchar_t *dom_u;
	int user_l, domain_l;
	struct HMACMD5Context ctx;

	/* might as well do one alloc to hold both (user_u and dom_u) */
	user_u = kmalloc(2048 * sizeof(wchar_t), GFP_KERNEL);
	if (user_u == NULL)
		return;
	dom_u = user_u + 1024;

	/* push_ucs2(NULL, user_u, user_n, (user_l+1)*2,
			STR_UNICODE|STR_NOALIGN|STR_TERMINATE|STR_UPPER);
	   push_ucs2(NULL, dom_u, domain_n, (domain_l+1)*2,
			STR_UNICODE|STR_NOALIGN|STR_TERMINATE|STR_UPPER); */

	/* BB user and domain may need to be uppercased */
	user_l = cifs_strtoUCS(user_u, user_n, 511, nls_codepage);
	domain_l = cifs_strtoUCS(dom_u, domain_n, 511, nls_codepage);

	user_l++;		/* trailing null */
	domain_l++;

	hmac_md5_init_limK_to_64(owf, 16, &ctx);
	hmac_md5_update((const unsigned char *) user_u, user_l * 2, &ctx);
	hmac_md5_update((const unsigned char *) dom_u, domain_l * 2, &ctx);
	hmac_md5_final(kr_buf, &ctx);

	kfree(user_u);
}
#endif

/* Does the des encryption from the FIRST 8 BYTES of the NT or LM MD4 hash. */
#if 0 /* currently unused */
static void
NTLMSSPOWFencrypt(unsigned char passwd[8],
		  unsigned char *ntlmchalresp, unsigned char p24[24])
{
	unsigned char p21[21];

	memset(p21, '\0', 21);
	memcpy(p21, passwd, 8);
	memset(p21 + 8, 0xbd, 8);

	E_P24(p21, ntlmchalresp, p24);
}
#endif

/* Does the NT MD4 hash then des encryption. */
int
SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24)
{
	int rc;
	unsigned char p16[16], p21[21];

	memset(p16, '\0', 16);
	memset(p21, '\0', 21);

	rc = E_md4hash(passwd, p16);
	if (rc) {
		cFYI(1, "%s Can't generate NT hash, error: %d", __func__, rc);
		return rc;
	}
	memcpy(p21, p16, 16);
	rc = E_P24(p21, c8, p24);
	return rc;
}


/* Does the md5 encryption from the NT hash for NTLMv2. */
/* These routines will be needed later */
#if 0
static void
SMBOWFencrypt_ntv2(const unsigned char kr[16],
		   const struct data_blob *srv_chal,
		   const struct data_blob *cli_chal, unsigned char resp_buf[16])
{
	struct HMACMD5Context ctx;

	hmac_md5_init_limK_to_64(kr, 16, &ctx);
	hmac_md5_update(srv_chal->data, srv_chal->length, &ctx);
	hmac_md5_update(cli_chal->data, cli_chal->length, &ctx);
	hmac_md5_final(resp_buf, &ctx);
}

static void
SMBsesskeygen_ntv2(const unsigned char kr[16],
		   const unsigned char *nt_resp, __u8 sess_key[16])
{
	struct HMACMD5Context ctx;

	hmac_md5_init_limK_to_64(kr, 16, &ctx);
	hmac_md5_update(nt_resp, 16, &ctx);
	hmac_md5_final((unsigned char *) sess_key, &ctx);
}

static void
SMBsesskeygen_ntv1(const unsigned char kr[16],
		   const unsigned char *nt_resp, __u8 sess_key[16])
{
	mdfour((unsigned char *) sess_key, (unsigned char *) kr, 16);
}
#endif
