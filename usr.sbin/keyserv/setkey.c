/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#ifndef lint
#if 0
static char sccsid[] = "@(#)setkey.c	1.11	94/04/25 SMI";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * Do the real work of the keyserver.
 * Store secret keys. Compute common keys,
 * and use them to decrypt and encrypt DES keys.
 * Cache the common keys, so the expensive computation is avoided.
 */
#include <mp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpc/des_crypt.h>
#include <rpc/des.h>
#include <sys/errno.h>
#include "keyserv.h"

static MINT *MODULUS;
static char *fetchsecretkey( uid_t );
static void writecache( char *, char *, des_block * );
static int readcache( char *, char *, des_block * );
static void extractdeskey( MINT *, des_block * );
static int storesecretkey( uid_t, keybuf );
static keystatus pk_crypt( uid_t, char *, netobj *, des_block *, int);
static int nodefaultkeys = 0;


/*
 * prohibit the nobody key on this machine k (the -d flag)
 */
void
pk_nodefaultkeys()
{
	nodefaultkeys = 1;
}

/*
 * Set the modulus for all our Diffie-Hellman operations
 */
void
setmodulus(modx)
	char *modx;
{
	MODULUS = mp_xtom(modx);
}

/*
 * Set the secretkey key for this uid
 */
keystatus
pk_setkey(uid, skey)
	uid_t uid;
	keybuf skey;
{
	if (!storesecretkey(uid, skey)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

/*
 * Encrypt the key using the public key associated with remote_name and the
 * secret key associated with uid.
 */
keystatus
pk_encrypt(uid, remote_name, remote_key, key)
	uid_t uid;
	char *remote_name;
	netobj	*remote_key;
	des_block *key;
{
	return (pk_crypt(uid, remote_name, remote_key, key, DES_ENCRYPT));
}

/*
 * Decrypt the key using the public key associated with remote_name and the
 * secret key associated with uid.
 */
keystatus
pk_decrypt(uid, remote_name, remote_key, key)
	uid_t uid;
	char *remote_name;
	netobj *remote_key;
	des_block *key;
{
	return (pk_crypt(uid, remote_name, remote_key, key, DES_DECRYPT));
}

static int store_netname( uid_t, key_netstarg * );
static int fetch_netname( uid_t, key_netstarg * );

keystatus
pk_netput(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	if (!store_netname(uid, netstore)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

keystatus
pk_netget(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	if (!fetch_netname(uid, netstore)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}


/*
 * Do the work of pk_encrypt && pk_decrypt
 */
static keystatus
pk_crypt(uid, remote_name, remote_key, key, mode)
	uid_t uid;
	char *remote_name;
	netobj *remote_key;
	des_block *key;
	int mode;
{
	char *xsecret;
	char xpublic[1024];
	char xsecret_hold[1024];
	des_block deskey;
	int err;
	MINT *public;
	MINT *secret;
	MINT *common;
	char zero[8];

	xsecret = fetchsecretkey(uid);
	if (xsecret == NULL || xsecret[0] == 0) {
		memset(zero, 0, sizeof (zero));
		xsecret = xsecret_hold;
		if (nodefaultkeys)
			return (KEY_NOSECRET);

		if (!getsecretkey("nobody", xsecret, zero) || xsecret[0] == 0) {
			return (KEY_NOSECRET);
		}
	}
	if (remote_key) {
		memcpy(xpublic, remote_key->n_bytes, remote_key->n_len);
	} else {
		bzero((char *)&xpublic, sizeof(xpublic));
		if (!getpublickey(remote_name, xpublic)) {
			if (nodefaultkeys || !getpublickey("nobody", xpublic))
				return (KEY_UNKNOWN);
		}
	}

	if (!readcache(xpublic, xsecret, &deskey)) {
		public = mp_xtom(xpublic);
		secret = mp_xtom(xsecret);
		/* Sanity Check on public and private keys */
		if ((public == NULL) || (secret == NULL))
			return (KEY_SYSTEMERR);

		common = mp_itom(0);
		mp_pow(public, secret, MODULUS, common);
		extractdeskey(common, &deskey);
		writecache(xpublic, xsecret, &deskey);
		mp_mfree(secret);
		mp_mfree(public);
		mp_mfree(common);
	}
	err = ecb_crypt((char *)&deskey, (char *)key, sizeof (des_block),
		DES_HW | mode);
	if (DES_FAILED(err)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

keystatus
pk_get_conv_key(uid, xpublic, result)
	uid_t uid;
	keybuf xpublic;
	cryptkeyres *result;
{
	char *xsecret;
	char xsecret_hold[1024];
	MINT *public;
	MINT *secret;
	MINT *common;
	char zero[8];


	xsecret = fetchsecretkey(uid);

	if (xsecret == NULL || xsecret[0] == 0) {
		memset(zero, 0, sizeof (zero));
		xsecret = xsecret_hold;
		if (nodefaultkeys)
			return (KEY_NOSECRET);

		if (!getsecretkey("nobody", xsecret, zero) ||
			xsecret[0] == 0)
			return (KEY_NOSECRET);
	}

	if (!readcache(xpublic, xsecret, &result->cryptkeyres_u.deskey)) {
		public = mp_xtom(xpublic);
		secret = mp_xtom(xsecret);
		/* Sanity Check on public and private keys */
		if ((public == NULL) || (secret == NULL))
			return (KEY_SYSTEMERR);

		common = mp_itom(0);
		mp_pow(public, secret, MODULUS, common);
		extractdeskey(common, &result->cryptkeyres_u.deskey);
		writecache(xpublic, xsecret, &result->cryptkeyres_u.deskey);
		mp_mfree(secret);
		mp_mfree(public);
		mp_mfree(common);
	}

	return (KEY_SUCCESS);
}

/*
 * Choose middle 64 bits of the common key to use as our des key, possibly
 * overwriting the lower order bits by setting parity.
 */
static void
extractdeskey(ck, deskey)
	MINT *ck;
	des_block *deskey;
{
	MINT *a;
	short r;
	int i;
	short base = (1 << 8);
	char *k;

	a = mp_itom(0);
#ifdef SOLARIS_MP
	_mp_move(ck, a);
#else
	mp_move(ck, a);
#endif
	for (i = 0; i < ((KEYSIZE - 64) / 2) / 8; i++) {
		mp_sdiv(a, base, a, &r);
	}
	k = deskey->c;
	for (i = 0; i < 8; i++) {
		mp_sdiv(a, base, a, &r);
		*k++ = r;
	}
	mp_mfree(a);
	des_setparity((char *)deskey);
}

/*
 * Key storage management
 */

#define	KEY_ONLY 0
#define	KEY_NAME 1
struct secretkey_netname_list {
	uid_t uid;
	key_netstarg keynetdata;
	u_char sc_flag;
	struct secretkey_netname_list *next;
};



static struct secretkey_netname_list *g_secretkey_netname;

/*
 * Store the keys and netname for this uid
 */
static int
store_netname(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	struct secretkey_netname_list *new;
	struct secretkey_netname_list **l;

	for (l = &g_secretkey_netname; *l != NULL && (*l)->uid != uid;
			l = &(*l)->next) {
	}
	if (*l == NULL) {
		new = (struct secretkey_netname_list *)malloc(sizeof (*new));
		if (new == NULL) {
			return (0);
		}
		new->uid = uid;
		new->next = NULL;
		*l = new;
	} else {
		new = *l;
		if (new->keynetdata.st_netname)
			(void) free (new->keynetdata.st_netname);
	}
	memcpy(new->keynetdata.st_priv_key, netstore->st_priv_key,
		HEXKEYBYTES);
	memcpy(new->keynetdata.st_pub_key, netstore->st_pub_key, HEXKEYBYTES);

	if (netstore->st_netname)
		new->keynetdata.st_netname = strdup(netstore->st_netname);
	else
		new->keynetdata.st_netname = (char *)NULL;
	new->sc_flag = KEY_NAME;
	return (1);

}

/*
 * Fetch the keys and netname for this uid
 */

static int
fetch_netname(uid, key_netst)
	uid_t uid;
	struct key_netstarg *key_netst;
{
	struct secretkey_netname_list *l;

	for (l = g_secretkey_netname; l != NULL; l = l->next) {
		if ((l->uid == uid) && (l->sc_flag == KEY_NAME)){

			memcpy(key_netst->st_priv_key,
				l->keynetdata.st_priv_key, HEXKEYBYTES);

			memcpy(key_netst->st_pub_key,
				l->keynetdata.st_pub_key, HEXKEYBYTES);

			if (l->keynetdata.st_netname)
				key_netst->st_netname =
					strdup(l->keynetdata.st_netname);
			else
				key_netst->st_netname = NULL;
		return (1);
		}
	}

	return (0);
}

static char *
fetchsecretkey(uid)
	uid_t uid;
{
	struct secretkey_netname_list *l;

	for (l = g_secretkey_netname; l != NULL; l = l->next) {
		if (l->uid == uid) {
			return (l->keynetdata.st_priv_key);
		}
	}
	return (NULL);
}

/*
 * Store the secretkey for this uid
 */
static int
storesecretkey(uid, key)
	uid_t uid;
	keybuf key;
{
	struct secretkey_netname_list *new;
	struct secretkey_netname_list **l;

	for (l = &g_secretkey_netname; *l != NULL && (*l)->uid != uid;
			l = &(*l)->next) {
	}
	if (*l == NULL) {
		new = (struct secretkey_netname_list *) malloc(sizeof (*new));
		if (new == NULL) {
			return (0);
		}
		new->uid = uid;
		new->sc_flag = KEY_ONLY;
		memset(new->keynetdata.st_pub_key, 0, HEXKEYBYTES);
		new->keynetdata.st_netname = NULL;
		new->next = NULL;
		*l = new;
	} else {
		new = *l;
	}

	memcpy(new->keynetdata.st_priv_key, key,
		HEXKEYBYTES);
	return (1);
}

static int
hexdigit(val)
	int val;
{
	return ("0123456789abcdef"[val]);
}

void
bin2hex(bin, hex, size)
	unsigned char *bin;
	unsigned char *hex;
	int size;
{
	int i;

	for (i = 0; i < size; i++) {
		*hex++ = hexdigit(*bin >> 4);
		*hex++ = hexdigit(*bin++ & 0xf);
	}
}

static int
hexval(dig)
	char dig;
{
	if ('0' <= dig && dig <= '9') {
		return (dig - '0');
	} else if ('a' <= dig && dig <= 'f') {
		return (dig - 'a' + 10);
	} else if ('A' <= dig && dig <= 'F') {
		return (dig - 'A' + 10);
	} else {
		return (-1);
	}
}

void
hex2bin(hex, bin, size)
	unsigned char *hex;
	unsigned char *bin;
	int size;
{
	int i;

	for (i = 0; i < size; i++) {
		*bin = hexval(*hex++) << 4;
		*bin++ |= hexval(*hex++);
	}
}

/*
 * Exponential caching management
 */
struct cachekey_list {
	keybuf secret;
	keybuf public;
	des_block deskey;
	struct cachekey_list *next;
};
static struct cachekey_list *g_cachedkeys;

/*
 * cache result of expensive multiple precision exponential operation
 */
static void
writecache(pub, sec, deskey)
	char *pub;
	char *sec;
	des_block *deskey;
{
	struct cachekey_list *new;

	new = (struct cachekey_list *) malloc(sizeof (struct cachekey_list));
	if (new == NULL) {
		return;
	}
	memcpy(new->public, pub, sizeof (keybuf));
	memcpy(new->secret, sec, sizeof (keybuf));
	new->deskey = *deskey;
	new->next = g_cachedkeys;
	g_cachedkeys = new;
}

/*
 * Try to find the common key in the cache
 */
static int
readcache(pub, sec, deskey)
	char *pub;
	char *sec;
	des_block *deskey;
{
	struct cachekey_list *found;
	register struct cachekey_list **l;

#define	cachehit(pub, sec, list)	\
		(memcmp(pub, (list)->public, sizeof (keybuf)) == 0 && \
		memcmp(sec, (list)->secret, sizeof (keybuf)) == 0)

	for (l = &g_cachedkeys; (*l) != NULL && !cachehit(pub, sec, *l);
		l = &(*l)->next)
		;
	if ((*l) == NULL) {
		return (0);
	}
	found = *l;
	(*l) = (*l)->next;
	found->next = g_cachedkeys;
	g_cachedkeys = found;
	*deskey = found->deskey;
	return (1);
}
