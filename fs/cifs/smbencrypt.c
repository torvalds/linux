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
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include "cifs_unicode.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "md5.h"
#include "cifs_debug.h"
#include "cifsencrypt.h"

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

/*The following definitions come from  libsmb/smbencrypt.c  */

void SMBencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24);
void E_md4hash(const unsigned char *passwd, unsigned char *p16);
static void SMBOWFencrypt(unsigned char passwd[16], unsigned char *c8,
		   unsigned char p24[24]);
void SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24);

/*
   This implements the X/Open SMB password encryption
   It takes a password, a 8 byte "crypt key" and puts 24 bytes of
   encrypted password into p24 */
/* Note that password must be uppercased and null terminated */
void
SMBencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24)
{
	unsigned char p14[15], p21[21];

	memset(p21, '\0', 21);
	memset(p14, '\0', 14);
	strncpy((char *) p14, (char *) passwd, 14);

/*	strupper((char *)p14); *//* BB at least uppercase the easy range */
	E_P16(p14, p21);

	SMBOWFencrypt(p21, c8, p24);

	memset(p14, 0, 15);
	memset(p21, 0, 21);
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

void
E_md4hash(const unsigned char *passwd, unsigned char *p16)
{
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

	mdfour(p16, (unsigned char *) wpwd, len);
	memset(wpwd, 0, 129 * 2);
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

/* Does the des encryption from the NT or LM MD4 hash. */
static void
SMBOWFencrypt(unsigned char passwd[16], unsigned char *c8,
	      unsigned char p24[24])
{
	unsigned char p21[21];

	memset(p21, '\0', 21);

	memcpy(p21, passwd, 16);
	E_P24(p21, c8, p24);
}

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

void
SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24)
{
	unsigned char p21[21];

	memset(p21, '\0', 21);

	E_md4hash(passwd, p21);
	SMBOWFencrypt(p21, c8, p24);
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
