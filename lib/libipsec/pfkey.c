/*	$KAME: pfkey.c,v 1.46 2003/08/26 03:37:06 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/pfkeyv2.h>
#include <netipsec/key_var.h>
#include <netinet/in.h>
#include <netipsec/ipsec.h>

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "ipsec_strerror.h"
#include "libpfkey.h"

#define CALLOC(size, cast) (cast)calloc(1, (size))

static int findsupportedmap(int);
static int setsupportedmap(struct sadb_supported *);
static struct sadb_alg *findsupportedalg(u_int, u_int);
static int pfkey_send_x1(int, u_int, u_int, u_int, struct sockaddr *,
	struct sockaddr *, u_int32_t, u_int32_t, u_int, caddr_t,
	u_int, u_int, u_int, u_int, u_int, u_int32_t, u_int32_t,
	u_int32_t, u_int32_t, u_int32_t);
static int pfkey_send_x2(int, u_int, u_int, u_int,
	struct sockaddr *, struct sockaddr *, u_int32_t);
static int pfkey_send_x3(int, u_int, u_int);
static int pfkey_send_x4(int, u_int, struct sockaddr *, u_int,
	struct sockaddr *, u_int, u_int, u_int64_t, u_int64_t,
	char *, int, u_int32_t);
static int pfkey_send_x5(int, u_int, u_int32_t);

static caddr_t pfkey_setsadbmsg(caddr_t, caddr_t, u_int, u_int,
	u_int, u_int32_t, pid_t);
static caddr_t pfkey_setsadbsa(caddr_t, caddr_t, u_int32_t, u_int,
	u_int, u_int, u_int32_t);
static caddr_t pfkey_setsadbxreplay(caddr_t, caddr_t, uint32_t);
static caddr_t pfkey_setsadbaddr(caddr_t, caddr_t, u_int,
	struct sockaddr *, u_int, u_int);
static caddr_t pfkey_setsadbkey(caddr_t, caddr_t, u_int, caddr_t, u_int);
static caddr_t pfkey_setsadblifetime(caddr_t, caddr_t, u_int, u_int32_t,
	u_int32_t, u_int32_t, u_int32_t);
static caddr_t pfkey_setsadbxsa2(caddr_t, caddr_t, u_int32_t, u_int32_t);

/*
 * make and search supported algorithm structure.
 */
static struct sadb_supported *ipsec_supported[] = { NULL, NULL, NULL, NULL };

static int supported_map[] = {
	SADB_SATYPE_AH,
	SADB_SATYPE_ESP,
	SADB_X_SATYPE_IPCOMP,
	SADB_X_SATYPE_TCPSIGNATURE
};

static int
findsupportedmap(satype)
	int satype;
{
	int i;

	for (i = 0; i < sizeof(supported_map)/sizeof(supported_map[0]); i++)
		if (supported_map[i] == satype)
			return i;
	return -1;
}

static struct sadb_alg *
findsupportedalg(satype, alg_id)
	u_int satype, alg_id;
{
	int algno;
	int tlen;
	caddr_t p;

	/* validity check */
	algno = findsupportedmap(satype);
	if (algno == -1) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return NULL;
	}
	if (ipsec_supported[algno] == NULL) {
		__ipsec_errcode = EIPSEC_DO_GET_SUPP_LIST;
		return NULL;
	}

	tlen = ipsec_supported[algno]->sadb_supported_len
		- sizeof(struct sadb_supported);
	p = (caddr_t)(ipsec_supported[algno] + 1);
	while (tlen > 0) {
		if (tlen < sizeof(struct sadb_alg)) {
			/* invalid format */
			break;
		}
		if (((struct sadb_alg *)p)->sadb_alg_id == alg_id)
			return (struct sadb_alg *)p;

		tlen -= sizeof(struct sadb_alg);
		p += sizeof(struct sadb_alg);
	}

	__ipsec_errcode = EIPSEC_NOT_SUPPORTED;
	return NULL;
}

static int
setsupportedmap(sup)
	struct sadb_supported *sup;
{
	struct sadb_supported **ipsup;

	switch (sup->sadb_supported_exttype) {
	case SADB_EXT_SUPPORTED_AUTH:
		ipsup = &ipsec_supported[findsupportedmap(SADB_SATYPE_AH)];
		break;
	case SADB_EXT_SUPPORTED_ENCRYPT:
		ipsup = &ipsec_supported[findsupportedmap(SADB_SATYPE_ESP)];
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_SATYPE;
		return -1;
	}

	if (*ipsup)
		free(*ipsup);

	*ipsup = malloc(sup->sadb_supported_len);
	if (!*ipsup) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	memcpy(*ipsup, sup, sup->sadb_supported_len);

	return 0;
}

/*
 * check key length against algorithm specified.
 * This function is called with SADB_EXT_SUPPORTED_{AUTH,ENCRYPT} as the
 * augument, and only calls to ipsec_check_keylen2();
 * keylen is the unit of bit.
 * OUT:
 *	-1: invalid.
 *	 0: valid.
 */
int
ipsec_check_keylen(supported, alg_id, keylen)
	u_int supported;
	u_int alg_id;
	u_int keylen;
{
	int satype;

	/* validity check */
	switch (supported) {
	case SADB_EXT_SUPPORTED_AUTH:
		satype = SADB_SATYPE_AH;
		break;
	case SADB_EXT_SUPPORTED_ENCRYPT:
		satype = SADB_SATYPE_ESP;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	return ipsec_check_keylen2(satype, alg_id, keylen);
}

/*
 * check key length against algorithm specified.
 * satype is one of satype defined at pfkeyv2.h.
 * keylen is the unit of bit.
 * OUT:
 *	-1: invalid.
 *	 0: valid.
 */
int
ipsec_check_keylen2(satype, alg_id, keylen)
	u_int satype;
	u_int alg_id;
	u_int keylen;
{
	struct sadb_alg *alg;

	alg = findsupportedalg(satype, alg_id);
	if (!alg)
		return -1;

	if (keylen < alg->sadb_alg_minbits || keylen > alg->sadb_alg_maxbits) {
		__ipsec_errcode = EIPSEC_INVAL_KEYLEN;
		return -1;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

/*
 * get max/min key length against algorithm specified.
 * satype is one of satype defined at pfkeyv2.h.
 * keylen is the unit of bit.
 * OUT:
 *	-1: invalid.
 *	 0: valid.
 */
int
ipsec_get_keylen(supported, alg_id, alg0)
	u_int supported, alg_id;
	struct sadb_alg *alg0;
{
	struct sadb_alg *alg;
	u_int satype;

	/* validity check */
	if (!alg0) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	switch (supported) {
	case SADB_EXT_SUPPORTED_AUTH:
		satype = SADB_SATYPE_AH;
		break;
	case SADB_EXT_SUPPORTED_ENCRYPT:
		satype = SADB_SATYPE_ESP;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	alg = findsupportedalg(satype, alg_id);
	if (!alg)
		return -1;

	memcpy(alg0, alg, sizeof(*alg0));

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

/*
 * set the rate for SOFT lifetime against HARD one.
 * If rate is more than 100 or equal to zero, then set to 100.
 */
static u_int soft_lifetime_allocations_rate = PFKEY_SOFT_LIFETIME_RATE;
static u_int soft_lifetime_bytes_rate = PFKEY_SOFT_LIFETIME_RATE;
static u_int soft_lifetime_addtime_rate = PFKEY_SOFT_LIFETIME_RATE;
static u_int soft_lifetime_usetime_rate = PFKEY_SOFT_LIFETIME_RATE;

u_int
pfkey_set_softrate(type, rate)
	u_int type, rate;
{
	__ipsec_errcode = EIPSEC_NO_ERROR;

	if (rate > 100 || rate == 0)
		rate = 100;

	switch (type) {
	case SADB_X_LIFETIME_ALLOCATIONS:
		soft_lifetime_allocations_rate = rate;
		return 0;
	case SADB_X_LIFETIME_BYTES:
		soft_lifetime_bytes_rate = rate;
		return 0;
	case SADB_X_LIFETIME_ADDTIME:
		soft_lifetime_addtime_rate = rate;
		return 0;
	case SADB_X_LIFETIME_USETIME:
		soft_lifetime_usetime_rate = rate;
		return 0;
	}

	__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
	return 1;
}

/*
 * get current rate for SOFT lifetime against HARD one.
 * ATTENTION: ~0 is returned if invalid type was passed.
 */
u_int
pfkey_get_softrate(type)
	u_int type;
{
	switch (type) {
	case SADB_X_LIFETIME_ALLOCATIONS:
		return soft_lifetime_allocations_rate;
	case SADB_X_LIFETIME_BYTES:
		return soft_lifetime_bytes_rate;
	case SADB_X_LIFETIME_ADDTIME:
		return soft_lifetime_addtime_rate;
	case SADB_X_LIFETIME_USETIME:
		return soft_lifetime_usetime_rate;
	}

	return ~0;
}

/*
 * sending SADB_GETSPI message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_getspi(so, satype, mode, src, dst, min, max, reqid, seq)
	int so;
	u_int satype, mode;
	struct sockaddr *src, *dst;
	u_int32_t min, max, reqid, seq;
{
	struct sadb_msg *newmsg;
	caddr_t ep;
	int len;
	int need_spirange = 0;
	caddr_t p;
	int plen;

	/* validity check */
	if (src == NULL || dst == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}
	if (src->sa_family != dst->sa_family) {
		__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
		return -1;
	}
	if (min > max || (min > 0 && min <= 255)) {
		__ipsec_errcode = EIPSEC_INVAL_SPI;
		return -1;
	}
	switch (src->sa_family) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_FAMILY;
		return -1;
	}

	/* create new sadb_msg to send. */
	len = sizeof(struct sadb_msg)
		+ sizeof(struct sadb_x_sa2)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(dst->sa_len);

	if (min > 255 && max < ~0) {
		need_spirange++;
		len += sizeof(struct sadb_spirange);
	}

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, SADB_GETSPI,
	    len, satype, seq, getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}

	p = pfkey_setsadbxsa2(p, ep, mode, reqid);
	if (!p) {
		free(newmsg);
		return -1;
	}

	/* set sadb_address for source */
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_SRC, src, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}

	/* set sadb_address for destination */
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_DST, dst, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}

	/* proccessing spi range */
	if (need_spirange) {
		struct sadb_spirange spirange;

		if (p + sizeof(spirange) > ep) {
			free(newmsg);
			return -1;
		}

		memset(&spirange, 0, sizeof(spirange));
		spirange.sadb_spirange_len = PFKEY_UNIT64(sizeof(spirange));
		spirange.sadb_spirange_exttype = SADB_EXT_SPIRANGE;
		spirange.sadb_spirange_min = min;
		spirange.sadb_spirange_max = max;

		memcpy(p, &spirange, sizeof(spirange));

		p += sizeof(spirange);
	}
	if (p != ep) {
		free(newmsg);
		return -1;
	}

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/*
 * sending SADB_UPDATE message to the kernel.
 * The length of key material is a_keylen + e_keylen.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_update(so, satype, mode, src, dst, spi, reqid, wsize,
		keymat, e_type, e_keylen, a_type, a_keylen, flags,
		l_alloc, l_bytes, l_addtime, l_usetime, seq)
	int so;
	u_int satype, mode, wsize;
	struct sockaddr *src, *dst;
	u_int32_t spi, reqid;
	caddr_t keymat;
	u_int e_type, e_keylen, a_type, a_keylen, flags;
	u_int32_t l_alloc;
	u_int64_t l_bytes, l_addtime, l_usetime;
	u_int32_t seq;
{
	int len;
	if ((len = pfkey_send_x1(so, SADB_UPDATE, satype, mode, src, dst, spi,
			reqid, wsize,
			keymat, e_type, e_keylen, a_type, a_keylen, flags,
			l_alloc, l_bytes, l_addtime, l_usetime, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_ADD message to the kernel.
 * The length of key material is a_keylen + e_keylen.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_add(so, satype, mode, src, dst, spi, reqid, wsize,
		keymat, e_type, e_keylen, a_type, a_keylen, flags,
		l_alloc, l_bytes, l_addtime, l_usetime, seq)
	int so;
	u_int satype, mode, wsize;
	struct sockaddr *src, *dst;
	u_int32_t spi, reqid;
	caddr_t keymat;
	u_int e_type, e_keylen, a_type, a_keylen, flags;
	u_int32_t l_alloc;
	u_int64_t l_bytes, l_addtime, l_usetime;
	u_int32_t seq;
{
	int len;
	if ((len = pfkey_send_x1(so, SADB_ADD, satype, mode, src, dst, spi,
			reqid, wsize,
			keymat, e_type, e_keylen, a_type, a_keylen, flags,
			l_alloc, l_bytes, l_addtime, l_usetime, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_DELETE message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_delete(so, satype, mode, src, dst, spi)
	int so;
	u_int satype, mode;
	struct sockaddr *src, *dst;
	u_int32_t spi;
{
	int len;
	if ((len = pfkey_send_x2(so, SADB_DELETE, satype, mode, src, dst, spi)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_DELETE without spi to the kernel.  This is
 * the "delete all" request (an extension also present in
 * Solaris).
 *
 * OUT:
 *	positive: success and return length sent
 *	-1	: error occured, and set errno
 */
int
pfkey_send_delete_all(so, satype, mode, src, dst)
	int so;
	u_int satype, mode;
	struct sockaddr *src, *dst;
{
	struct sadb_msg *newmsg;
	int len;
	caddr_t p;
	int plen;
	caddr_t ep;

	/* validity check */
	if (src == NULL || dst == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}
	if (src->sa_family != dst->sa_family) {
		__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
		return -1;
	}
	switch (src->sa_family) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_FAMILY;
		return -1;
	}

	/* create new sadb_msg to reply. */
	len = sizeof(struct sadb_msg)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(dst->sa_len);

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, SADB_DELETE, len, satype, 0,
	    getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_SRC, src, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_DST, dst, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p || p != ep) {
		free(newmsg);
		return -1;
	}

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/*
 * sending SADB_GET message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_get(so, satype, mode, src, dst, spi)
	int so;
	u_int satype, mode;
	struct sockaddr *src, *dst;
	u_int32_t spi;
{
	int len;
	if ((len = pfkey_send_x2(so, SADB_GET, satype, mode, src, dst, spi)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_REGISTER message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_register(so, satype)
	int so;
	u_int satype;
{
	int len, algno;

	if (satype == SADB_SATYPE_UNSPEC) {
		for (algno = 0;
		     algno < sizeof(supported_map)/sizeof(supported_map[0]);
		     algno++) {
			if (ipsec_supported[algno]) {
				free(ipsec_supported[algno]);
				ipsec_supported[algno] = NULL;
			}
		}
	} else {
		algno = findsupportedmap(satype);
		if (algno == -1) {
			__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
			return -1;
		}

		if (ipsec_supported[algno]) {
			free(ipsec_supported[algno]);
			ipsec_supported[algno] = NULL;
		}
	}

	if ((len = pfkey_send_x3(so, SADB_REGISTER, satype)) < 0)
		return -1;

	return len;
}

/*
 * receiving SADB_REGISTER message from the kernel, and copy buffer for
 * sadb_supported returned into ipsec_supported.
 * OUT:
 *	 0: success and return length sent.
 *	-1: error occured, and set errno.
 */
int
pfkey_recv_register(so)
	int so;
{
	pid_t pid = getpid();
	struct sadb_msg *newmsg;
	int error = -1;

	/* receive message */
	for (;;) {
		if ((newmsg = pfkey_recv(so)) == NULL)
			return -1;
		if (newmsg->sadb_msg_type == SADB_REGISTER &&
		    newmsg->sadb_msg_pid == pid)
			break;
		free(newmsg);
	}

	/* check and fix */
	newmsg->sadb_msg_len = PFKEY_UNUNIT64(newmsg->sadb_msg_len);

	error = pfkey_set_supported(newmsg, newmsg->sadb_msg_len);
	free(newmsg);

	if (error == 0)
		__ipsec_errcode = EIPSEC_NO_ERROR;

	return error;
}

/*
 * receiving SADB_REGISTER message from the kernel, and copy buffer for
 * sadb_supported returned into ipsec_supported.
 * NOTE: sadb_msg_len must be host order.
 * IN:
 *	tlen: msg length, it's to makeing sure.
 * OUT:
 *	 0: success and return length sent.
 *	-1: error occured, and set errno.
 */
int
pfkey_set_supported(msg, tlen)
	struct sadb_msg *msg;
	int tlen;
{
	struct sadb_supported *sup;
	caddr_t p;
	caddr_t ep;

	/* validity */
	if (msg->sadb_msg_len != tlen) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	p = (caddr_t)msg;
	ep = p + tlen;

	p += sizeof(struct sadb_msg);

	while (p < ep) {
		sup = (struct sadb_supported *)p;
		if (ep < p + sizeof(*sup) ||
		    PFKEY_EXTLEN(sup) < sizeof(*sup) ||
		    ep < p + sup->sadb_supported_len) {
			/* invalid format */
			break;
		}

		switch (sup->sadb_supported_exttype) {
		case SADB_EXT_SUPPORTED_AUTH:
		case SADB_EXT_SUPPORTED_ENCRYPT:
			break;
		default:
			__ipsec_errcode = EIPSEC_INVAL_SATYPE;
			return -1;
		}

		/* fixed length */
		sup->sadb_supported_len = PFKEY_EXTLEN(sup);

		/* set supported map */
		if (setsupportedmap(sup) != 0)
			return -1;

		p += sup->sadb_supported_len;
	}

	if (p != ep) {
		__ipsec_errcode = EIPSEC_INVAL_SATYPE;
		return -1;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;

	return 0;
}

/*
 * sending SADB_FLUSH message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_flush(so, satype)
	int so;
	u_int satype;
{
	int len;

	if ((len = pfkey_send_x3(so, SADB_FLUSH, satype)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_DUMP message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_dump(so, satype)
	int so;
	u_int satype;
{
	int len;

	if ((len = pfkey_send_x3(so, SADB_DUMP, satype)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_PROMISC message to the kernel.
 * NOTE that this function handles promisc mode toggle only.
 * IN:
 *	flag:	set promisc off if zero, set promisc on if non-zero.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 *	0     : error occured, and set errno.
 *	others: a pointer to new allocated buffer in which supported
 *	        algorithms is.
 */
int
pfkey_send_promisc_toggle(so, flag)
	int so;
	int flag;
{
	int len;

	if ((len = pfkey_send_x3(so, SADB_X_PROMISC, (flag ? 1 : 0))) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDADD message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdadd(so, src, prefs, dst, prefd, proto, policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if ((len = pfkey_send_x4(so, SADB_X_SPDADD,
				src, prefs, dst, prefd, proto,
				0, 0,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDADD message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdadd2(so, src, prefs, dst, prefd, proto, ltime, vtime,
		policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	u_int64_t ltime, vtime;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if ((len = pfkey_send_x4(so, SADB_X_SPDADD,
				src, prefs, dst, prefd, proto,
				ltime, vtime,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDUPDATE message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdupdate(so, src, prefs, dst, prefd, proto, policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if ((len = pfkey_send_x4(so, SADB_X_SPDUPDATE,
				src, prefs, dst, prefd, proto,
				0, 0,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDUPDATE message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdupdate2(so, src, prefs, dst, prefd, proto, ltime, vtime,
		policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	u_int64_t ltime, vtime;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if ((len = pfkey_send_x4(so, SADB_X_SPDUPDATE,
				src, prefs, dst, prefd, proto,
				ltime, vtime,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDDELETE message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spddelete(so, src, prefs, dst, prefd, proto, policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if (policylen != sizeof(struct sadb_x_policy)) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	if ((len = pfkey_send_x4(so, SADB_X_SPDDELETE,
				src, prefs, dst, prefd, proto,
				0, 0,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDDELETE message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spddelete2(so, spid)
	int so;
	u_int32_t spid;
{
	int len;

	if ((len = pfkey_send_x5(so, SADB_X_SPDDELETE2, spid)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDGET message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdget(so, spid)
	int so;
	u_int32_t spid;
{
	int len;

	if ((len = pfkey_send_x5(so, SADB_X_SPDGET, spid)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_X_SPDSETIDX message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdsetidx(so, src, prefs, dst, prefd, proto, policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int prefs, prefd, proto;
	caddr_t policy;
	int policylen;
	u_int32_t seq;
{
	int len;

	if (policylen != sizeof(struct sadb_x_policy)) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	if ((len = pfkey_send_x4(so, SADB_X_SPDSETIDX,
				src, prefs, dst, prefd, proto,
				0, 0,
				policy, policylen, seq)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_SPDFLUSH message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spdflush(so)
	int so;
{
	int len;

	if ((len = pfkey_send_x3(so, SADB_X_SPDFLUSH, SADB_SATYPE_UNSPEC)) < 0)
		return -1;

	return len;
}

/*
 * sending SADB_SPDDUMP message to the kernel.
 * OUT:
 *	positive: success and return length sent.
 *	-1	: error occured, and set errno.
 */
int
pfkey_send_spddump(so)
	int so;
{
	int len;

	if ((len = pfkey_send_x3(so, SADB_X_SPDDUMP, SADB_SATYPE_UNSPEC)) < 0)
		return -1;

	return len;
}

/* sending SADB_ADD or SADB_UPDATE message to the kernel */
static int
pfkey_send_x1(so, type, satype, mode, src, dst, spi, reqid, wsize,
		keymat, e_type, e_keylen, a_type, a_keylen, flags,
		l_alloc, l_bytes, l_addtime, l_usetime, seq)
	int so;
	u_int type, satype, mode;
	struct sockaddr *src, *dst;
	u_int32_t spi, reqid;
	u_int wsize;
	caddr_t keymat;
	u_int e_type, e_keylen, a_type, a_keylen, flags;
	u_int32_t l_alloc, l_bytes, l_addtime, l_usetime, seq;
{
	struct sadb_msg *newmsg;
	int len;
	caddr_t p;
	int plen;
	caddr_t ep;

	/* validity check */
	if (src == NULL || dst == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}
	if (src->sa_family != dst->sa_family) {
		__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
		return -1;
	}
	switch (src->sa_family) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_FAMILY;
		return -1;
	}

	switch (satype) {
	case SADB_SATYPE_ESP:
		if (e_type == SADB_EALG_NONE) {
			__ipsec_errcode = EIPSEC_NO_ALGS;
			return -1;
		}
		break;
	case SADB_SATYPE_AH:
		if (e_type != SADB_EALG_NONE) {
			__ipsec_errcode = EIPSEC_INVAL_ALGS;
			return -1;
		}
		if (a_type == SADB_AALG_NONE) {
			__ipsec_errcode = EIPSEC_NO_ALGS;
			return -1;
		}
		break;
	case SADB_X_SATYPE_IPCOMP:
		if (e_type == SADB_X_CALG_NONE) {
			__ipsec_errcode = EIPSEC_INVAL_ALGS;
			return -1;
		}
		if (a_type != SADB_AALG_NONE) {
			__ipsec_errcode = EIPSEC_NO_ALGS;
			return -1;
		}
		break;
	case SADB_X_SATYPE_TCPSIGNATURE:
		if (e_type != SADB_EALG_NONE) {
			__ipsec_errcode = EIPSEC_INVAL_ALGS;
			return -1;
		}
		if (a_type != SADB_X_AALG_TCP_MD5) {
			__ipsec_errcode = EIPSEC_INVAL_ALGS;
			return -1;
		}
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_SATYPE;
		return -1;
	}

	/* create new sadb_msg to reply. */
	len = sizeof(struct sadb_msg)
		+ sizeof(struct sadb_sa)
		+ sizeof(struct sadb_x_sa2)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(dst->sa_len)
		+ sizeof(struct sadb_lifetime)
		+ sizeof(struct sadb_lifetime);

	if (wsize > UINT8_MAX) {
		if (wsize > (UINT32_MAX - 32) >> 3) {
			__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
			return (-1);
		}
		len += sizeof(struct sadb_x_sa_replay);
	}
	if (e_type != SADB_EALG_NONE)
		len += (sizeof(struct sadb_key) + PFKEY_ALIGN8(e_keylen));
	if (a_type != SADB_AALG_NONE)
		len += (sizeof(struct sadb_key) + PFKEY_ALIGN8(a_keylen));

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, type, len,
	                     satype, seq, getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbsa(p, ep, spi, wsize, a_type, e_type, flags);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbxsa2(p, ep, mode, reqid);
	if (!p) {
		free(newmsg);
		return -1;
	}
	if (wsize > UINT8_MAX) {
		p = pfkey_setsadbxreplay(p, ep, wsize);
		if (!p) {
			free(newmsg);
			return (-1);
		}
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_SRC, src, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_DST, dst, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}

	if (e_type != SADB_EALG_NONE) {
		p = pfkey_setsadbkey(p, ep, SADB_EXT_KEY_ENCRYPT,
		                   keymat, e_keylen);
		if (!p) {
			free(newmsg);
			return -1;
		}
	}
	if (a_type != SADB_AALG_NONE) {
		p = pfkey_setsadbkey(p, ep, SADB_EXT_KEY_AUTH,
		                   keymat + e_keylen, a_keylen);
		if (!p) {
			free(newmsg);
			return -1;
		}
	}

	/* set sadb_lifetime for destination */
	p = pfkey_setsadblifetime(p, ep, SADB_EXT_LIFETIME_HARD,
			l_alloc, l_bytes, l_addtime, l_usetime);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadblifetime(p, ep, SADB_EXT_LIFETIME_SOFT,
			l_alloc, l_bytes, l_addtime, l_usetime);
	if (!p || p != ep) {
		free(newmsg);
		return -1;
	}

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/* sending SADB_DELETE or SADB_GET message to the kernel */
static int
pfkey_send_x2(so, type, satype, mode, src, dst, spi)
	int so;
	u_int type, satype, mode;
	struct sockaddr *src, *dst;
	u_int32_t spi;
{
	struct sadb_msg *newmsg;
	int len;
	caddr_t p;
	int plen;
	caddr_t ep;

	/* validity check */
	if (src == NULL || dst == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}
	if (src->sa_family != dst->sa_family) {
		__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
		return -1;
	}
	switch (src->sa_family) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_FAMILY;
		return -1;
	}

	/* create new sadb_msg to reply. */
	len = sizeof(struct sadb_msg)
		+ sizeof(struct sadb_sa)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(dst->sa_len);

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, type, len, satype, 0,
	    getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbsa(p, ep, spi, 0, 0, 0, 0);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_SRC, src, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_DST, dst, plen,
	    IPSEC_ULPROTO_ANY);
	if (!p || p != ep) {
		free(newmsg);
		return -1;
	}

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/*
 * sending SADB_REGISTER, SADB_FLUSH, SADB_DUMP or SADB_X_PROMISC message
 * to the kernel
 */
static int
pfkey_send_x3(so, type, satype)
	int so;
	u_int type, satype;
{
	struct sadb_msg *newmsg;
	int len;
	caddr_t p;
	caddr_t ep;

	/* validity check */
	switch (type) {
	case SADB_X_PROMISC:
		if (satype != 0 && satype != 1) {
			__ipsec_errcode = EIPSEC_INVAL_SATYPE;
			return -1;
		}
		break;
	default:
		switch (satype) {
		case SADB_SATYPE_UNSPEC:
		case SADB_SATYPE_AH:
		case SADB_SATYPE_ESP:
		case SADB_X_SATYPE_IPCOMP:
		case SADB_X_SATYPE_TCPSIGNATURE:
			break;
		default:
			__ipsec_errcode = EIPSEC_INVAL_SATYPE;
			return -1;
		}
	}

	/* create new sadb_msg to send. */
	len = sizeof(struct sadb_msg);

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, type, len, satype, 0,
	    getpid());
	if (!p || p != ep) {
		free(newmsg);
		return -1;
	}

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/* sending SADB_X_SPDADD message to the kernel */
static int
pfkey_send_x4(so, type, src, prefs, dst, prefd, proto,
		ltime, vtime, policy, policylen, seq)
	int so;
	struct sockaddr *src, *dst;
	u_int type, prefs, prefd, proto;
	u_int64_t ltime, vtime;
	char *policy;
	int policylen;
	u_int32_t seq;
{
	struct sadb_msg *newmsg;
	int len;
	caddr_t p;
	int plen;
	caddr_t ep;

	/* validity check */
	if (src == NULL || dst == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}
	if (src->sa_family != dst->sa_family) {
		__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
		return -1;
	}

	switch (src->sa_family) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		__ipsec_errcode = EIPSEC_INVAL_FAMILY;
		return -1;
	}
	if (prefs > plen || prefd > plen) {
		__ipsec_errcode = EIPSEC_INVAL_PREFIXLEN;
		return -1;
	}

	/* create new sadb_msg to reply. */
	len = sizeof(struct sadb_msg)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_address)
		+ PFKEY_ALIGN8(src->sa_len)
		+ sizeof(struct sadb_lifetime)
		+ policylen;

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, type, len,
	    SADB_SATYPE_UNSPEC, seq, getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_SRC, src, prefs, proto);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadbaddr(p, ep, SADB_EXT_ADDRESS_DST, dst, prefd, proto);
	if (!p) {
		free(newmsg);
		return -1;
	}
	p = pfkey_setsadblifetime(p, ep, SADB_EXT_LIFETIME_HARD,
			0, 0, ltime, vtime);
	if (!p || p + policylen != ep) {
		free(newmsg);
		return -1;
	}
	memcpy(p, policy, policylen);

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/* sending SADB_X_SPDGET or SADB_X_SPDDELETE message to the kernel */
static int
pfkey_send_x5(so, type, spid)
	int so;
	u_int type;
	u_int32_t spid;
{
	struct sadb_msg *newmsg;
	struct sadb_x_policy xpl;
	int len;
	caddr_t p;
	caddr_t ep;

	/* create new sadb_msg to reply. */
	len = sizeof(struct sadb_msg)
		+ sizeof(xpl);

	if ((newmsg = CALLOC(len, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}
	ep = ((caddr_t)newmsg) + len;

	p = pfkey_setsadbmsg((caddr_t)newmsg, ep, type, len,
	    SADB_SATYPE_UNSPEC, 0, getpid());
	if (!p) {
		free(newmsg);
		return -1;
	}

	if (p + sizeof(xpl) != ep) {
		free(newmsg);
		return -1;
	}
	memset(&xpl, 0, sizeof(xpl));
	xpl.sadb_x_policy_len = PFKEY_UNIT64(sizeof(xpl));
	xpl.sadb_x_policy_exttype = SADB_X_EXT_POLICY;
	xpl.sadb_x_policy_id = spid;
	memcpy(p, &xpl, sizeof(xpl));

	/* send message */
	len = pfkey_send(so, newmsg, len);
	free(newmsg);

	if (len < 0)
		return -1;

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/*
 * open a socket.
 * OUT:
 *	-1: fail.
 *	others : success and return value of socket.
 */
int
pfkey_open(void)
{
	int so;
	int bufsiz_current, bufsiz_wanted;
	int ret;
	socklen_t len;

	if ((so = socket(PF_KEY, SOCK_RAW, PF_KEY_V2)) < 0) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}

	/*
	 * This is a temporary workaround for KAME PR 154.
	 * Don't really care even if it fails.
	 */
	/* Try to have 128k. If we have more, do not lower it. */
	bufsiz_wanted = 128 * 1024;
	len = sizeof(bufsiz_current);
	ret = getsockopt(so, SOL_SOCKET, SO_SNDBUF,
		&bufsiz_current, &len);
	if ((ret < 0) || (bufsiz_current < bufsiz_wanted))
		(void)setsockopt(so, SOL_SOCKET, SO_SNDBUF,
			&bufsiz_wanted, sizeof(bufsiz_wanted));

	/* Try to have have at least 2MB. If we have more, do not lower it. */
	bufsiz_wanted = 2 * 1024 * 1024;
	len = sizeof(bufsiz_current);
	ret = getsockopt(so, SOL_SOCKET, SO_RCVBUF,
		&bufsiz_current, &len);
	if (ret < 0)
		bufsiz_current = 128 * 1024;

	for (; bufsiz_wanted > bufsiz_current; bufsiz_wanted /= 2) {
		if (setsockopt(so, SOL_SOCKET, SO_RCVBUF,
				&bufsiz_wanted, sizeof(bufsiz_wanted)) == 0)
			break;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return so;
}

/*
 * close a socket.
 * OUT:
 *	 0: success.
 *	-1: fail.
 */
void
pfkey_close(so)
	int so;
{
	(void)close(so);

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return;
}

/*
 * receive sadb_msg data, and return pointer to new buffer allocated.
 * Must free this buffer later.
 * OUT:
 *	NULL	: error occured.
 *	others	: a pointer to sadb_msg structure.
 *
 * XXX should be rewritten to pass length explicitly
 */
struct sadb_msg *
pfkey_recv(so)
	int so;
{
	struct sadb_msg buf, *newmsg;
	int len, reallen;

	while ((len = recv(so, (caddr_t)&buf, sizeof(buf), MSG_PEEK)) < 0) {
		if (errno == EINTR)
			continue;
		__ipsec_set_strerror(strerror(errno));
		return NULL;
	}

	if (len < sizeof(buf)) {
		recv(so, (caddr_t)&buf, sizeof(buf), 0);
		__ipsec_errcode = EIPSEC_MAX;
		return NULL;
	}

	/* read real message */
	reallen = PFKEY_UNUNIT64(buf.sadb_msg_len);
	if ((newmsg = CALLOC(reallen, struct sadb_msg *)) == NULL) {
		__ipsec_set_strerror(strerror(errno));
		return NULL;
	}

	while ((len = recv(so, (caddr_t)newmsg, reallen, 0)) < 0) {
		if (errno == EINTR)
			continue;
		__ipsec_set_strerror(strerror(errno));
		free(newmsg);
		return NULL;
	}

	if (len != reallen) {
		__ipsec_errcode = EIPSEC_SYSTEM_ERROR;
		free(newmsg);
		return NULL;
	}

	/* don't trust what the kernel says, validate! */
	if (PFKEY_UNUNIT64(newmsg->sadb_msg_len) != len) {
		__ipsec_errcode = EIPSEC_SYSTEM_ERROR;
		free(newmsg);
		return NULL;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return newmsg;
}

/*
 * send message to a socket.
 * OUT:
 *	 others: success and return length sent.
 *	-1     : fail.
 */
int
pfkey_send(so, msg, len)
	int so;
	struct sadb_msg *msg;
	int len;
{
	if ((len = send(so, (caddr_t)msg, len, 0)) < 0) {
		__ipsec_set_strerror(strerror(errno));
		return -1;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return len;
}

/*
 * %%% Utilities
 * NOTE: These functions are derived from netkey/key.c in KAME.
 */
/*
 * set the pointer to each header in this message buffer.
 * IN:	msg: pointer to message buffer.
 *	mhp: pointer to the buffer initialized like below:
 *		caddr_t mhp[SADB_EXT_MAX + 1];
 * OUT:	-1: invalid.
 *	 0: valid.
 *
 * XXX should be rewritten to obtain length explicitly
 */
int
pfkey_align(msg, mhp)
	struct sadb_msg *msg;
	caddr_t *mhp;
{
	struct sadb_ext *ext;
	int i;
	caddr_t p;
	caddr_t ep;	/* XXX should be passed from upper layer */

	/* validity check */
	if (msg == NULL || mhp == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	/* initialize */
	for (i = 0; i < SADB_EXT_MAX + 1; i++)
		mhp[i] = NULL;

	mhp[0] = (caddr_t)msg;

	/* initialize */
	p = (caddr_t) msg;
	ep = p + PFKEY_UNUNIT64(msg->sadb_msg_len);

	/* skip base header */
	p += sizeof(struct sadb_msg);

	while (p < ep) {
		ext = (struct sadb_ext *)p;
		if (ep < p + sizeof(*ext) || PFKEY_EXTLEN(ext) < sizeof(*ext) ||
		    ep < p + PFKEY_EXTLEN(ext)) {
			/* invalid format */
			break;
		}

		/* duplicate check */
		/* XXX Are there duplication either KEY_AUTH or KEY_ENCRYPT ?*/
		if (mhp[ext->sadb_ext_type] != NULL) {
			__ipsec_errcode = EIPSEC_INVAL_EXTTYPE;
			return -1;
		}

		/* set pointer */
		switch (ext->sadb_ext_type) {
		case SADB_EXT_SA:
		case SADB_EXT_LIFETIME_CURRENT:
		case SADB_EXT_LIFETIME_HARD:
		case SADB_EXT_LIFETIME_SOFT:
		case SADB_EXT_ADDRESS_SRC:
		case SADB_EXT_ADDRESS_DST:
		case SADB_EXT_ADDRESS_PROXY:
		case SADB_EXT_KEY_AUTH:
			/* XXX should to be check weak keys. */
		case SADB_EXT_KEY_ENCRYPT:
			/* XXX should to be check weak keys. */
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
		case SADB_EXT_SENSITIVITY:
		case SADB_EXT_PROPOSAL:
		case SADB_EXT_SUPPORTED_AUTH:
		case SADB_EXT_SUPPORTED_ENCRYPT:
		case SADB_EXT_SPIRANGE:
		case SADB_X_EXT_POLICY:
		case SADB_X_EXT_SA2:
		case SADB_X_EXT_NAT_T_TYPE:
		case SADB_X_EXT_NAT_T_SPORT:
		case SADB_X_EXT_NAT_T_DPORT:
		case SADB_X_EXT_NAT_T_OAI:
		case SADB_X_EXT_NAT_T_OAR:
		case SADB_X_EXT_NAT_T_FRAG:
		case SADB_X_EXT_SA_REPLAY:
		case SADB_X_EXT_NEW_ADDRESS_SRC:
		case SADB_X_EXT_NEW_ADDRESS_DST:
			mhp[ext->sadb_ext_type] = (caddr_t)ext;
			break;
		default:
			__ipsec_errcode = EIPSEC_INVAL_EXTTYPE;
			return -1;
		}

		p += PFKEY_EXTLEN(ext);
	}

	if (p != ep) {
		__ipsec_errcode = EIPSEC_INVAL_SADBMSG;
		return -1;
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

/*
 * check basic usage for sadb_msg,
 * NOTE: This routine is derived from netkey/key.c in KAME.
 * IN:	msg: pointer to message buffer.
 *	mhp: pointer to the buffer initialized like below:
 *
 *		caddr_t mhp[SADB_EXT_MAX + 1];
 *
 * OUT:	-1: invalid.
 *	 0: valid.
 */
int
pfkey_check(mhp)
	caddr_t *mhp;
{
	struct sadb_msg *msg;

	/* validity check */
	if (mhp == NULL || mhp[0] == NULL) {
		__ipsec_errcode = EIPSEC_INVAL_ARGUMENT;
		return -1;
	}

	msg = (struct sadb_msg *)mhp[0];

	/* check version */
	if (msg->sadb_msg_version != PF_KEY_V2) {
		__ipsec_errcode = EIPSEC_INVAL_VERSION;
		return -1;
	}

	/* check type */
	if (msg->sadb_msg_type > SADB_MAX) {
		__ipsec_errcode = EIPSEC_INVAL_MSGTYPE;
		return -1;
	}

	/* check SA type */
	switch (msg->sadb_msg_satype) {
	case SADB_SATYPE_UNSPEC:
		switch (msg->sadb_msg_type) {
		case SADB_GETSPI:
		case SADB_UPDATE:
		case SADB_ADD:
		case SADB_DELETE:
		case SADB_GET:
		case SADB_ACQUIRE:
		case SADB_EXPIRE:
			__ipsec_errcode = EIPSEC_INVAL_SATYPE;
			return -1;
		}
		break;
	case SADB_SATYPE_ESP:
	case SADB_SATYPE_AH:
	case SADB_X_SATYPE_IPCOMP:
	case SADB_X_SATYPE_TCPSIGNATURE:
		switch (msg->sadb_msg_type) {
		case SADB_X_SPDADD:
		case SADB_X_SPDDELETE:
		case SADB_X_SPDGET:
		case SADB_X_SPDDUMP:
		case SADB_X_SPDFLUSH:
			__ipsec_errcode = EIPSEC_INVAL_SATYPE;
			return -1;
		}
		break;
	case SADB_SATYPE_RSVP:
	case SADB_SATYPE_OSPFV2:
	case SADB_SATYPE_RIPV2:
	case SADB_SATYPE_MIP:
		__ipsec_errcode = EIPSEC_NOT_SUPPORTED;
		return -1;
	case 1:	/* XXX: What does it do ? */
		if (msg->sadb_msg_type == SADB_X_PROMISC)
			break;
		/*FALLTHROUGH*/
	default:
		__ipsec_errcode = EIPSEC_INVAL_SATYPE;
		return -1;
	}

	/* check field of upper layer protocol and address family */
	if (mhp[SADB_EXT_ADDRESS_SRC] != NULL
	 && mhp[SADB_EXT_ADDRESS_DST] != NULL) {
		struct sadb_address *src0, *dst0;

		src0 = (struct sadb_address *)(mhp[SADB_EXT_ADDRESS_SRC]);
		dst0 = (struct sadb_address *)(mhp[SADB_EXT_ADDRESS_DST]);

		if (src0->sadb_address_proto != dst0->sadb_address_proto) {
			__ipsec_errcode = EIPSEC_PROTO_MISMATCH;
			return -1;
		}

		if (PFKEY_ADDR_SADDR(src0)->sa_family
		 != PFKEY_ADDR_SADDR(dst0)->sa_family) {
			__ipsec_errcode = EIPSEC_FAMILY_MISMATCH;
			return -1;
		}

		switch (PFKEY_ADDR_SADDR(src0)->sa_family) {
		case AF_INET:
		case AF_INET6:
			break;
		default:
			__ipsec_errcode = EIPSEC_INVAL_FAMILY;
			return -1;
		}

		/*
		 * prefixlen == 0 is valid because there must be the case
		 * all addresses are matched.
		 */
	}

	__ipsec_errcode = EIPSEC_NO_ERROR;
	return 0;
}

/*
 * set data into sadb_msg.
 * `buf' must has been allocated sufficiently.
 */
static caddr_t
pfkey_setsadbmsg(buf, lim, type, tlen, satype, seq, pid)
	caddr_t buf;
	caddr_t lim;
	u_int type, satype;
	u_int tlen;
	u_int32_t seq;
	pid_t pid;
{
	struct sadb_msg *p;
	u_int len;

	p = (struct sadb_msg *)buf;
	len = sizeof(struct sadb_msg);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_msg_version = PF_KEY_V2;
	p->sadb_msg_type = type;
	p->sadb_msg_errno = 0;
	p->sadb_msg_satype = satype;
	p->sadb_msg_len = PFKEY_UNIT64(tlen);
	p->sadb_msg_reserved = 0;
	p->sadb_msg_seq = seq;
	p->sadb_msg_pid = (u_int32_t)pid;

	return(buf + len);
}

/*
 * copy secasvar data into sadb_address.
 * `buf' must has been allocated sufficiently.
 */
static caddr_t
pfkey_setsadbsa(buf, lim, spi, wsize, auth, enc, flags)
	caddr_t buf;
	caddr_t lim;
	u_int32_t spi, flags;
	u_int wsize, auth, enc;
{
	struct sadb_sa *p;
	u_int len;

	p = (struct sadb_sa *)buf;
	len = sizeof(struct sadb_sa);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_sa_len = PFKEY_UNIT64(len);
	p->sadb_sa_exttype = SADB_EXT_SA;
	p->sadb_sa_spi = spi;
	p->sadb_sa_replay = wsize > UINT8_MAX ? UINT8_MAX: wsize;
	p->sadb_sa_state = SADB_SASTATE_LARVAL;
	p->sadb_sa_auth = auth;
	p->sadb_sa_encrypt = enc;
	p->sadb_sa_flags = flags;

	return(buf + len);
}

/*
 * Set data into sadb_x_sa_replay.
 * `buf' must has been allocated sufficiently.
 */
static caddr_t
pfkey_setsadbxreplay(caddr_t buf, caddr_t lim, uint32_t wsize)
{
	struct sadb_x_sa_replay *p;
	u_int len;

	p = (struct sadb_x_sa_replay *)buf;
	len = sizeof(struct sadb_x_sa_replay);

	if (buf + len > lim)
		return (NULL);

	memset(p, 0, len);
	p->sadb_x_sa_replay_len = PFKEY_UNIT64(len);
	p->sadb_x_sa_replay_exttype = SADB_X_EXT_SA_REPLAY;
	/* Convert wsize from bytes to number of packets. */
	p->sadb_x_sa_replay_replay = wsize << 3;

	return (buf + len);
}

/*
 * set data into sadb_address.
 * `buf' must has been allocated sufficiently.
 * prefixlen is in bits.
 */
static caddr_t
pfkey_setsadbaddr(buf, lim, exttype, saddr, prefixlen, ul_proto)
	caddr_t buf;
	caddr_t lim;
	u_int exttype;
	struct sockaddr *saddr;
	u_int prefixlen;
	u_int ul_proto;
{
	struct sadb_address *p;
	u_int len;

	p = (struct sadb_address *)buf;
	len = sizeof(struct sadb_address) + PFKEY_ALIGN8(saddr->sa_len);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_address_len = PFKEY_UNIT64(len);
	p->sadb_address_exttype = exttype & 0xffff;
	p->sadb_address_proto = ul_proto & 0xff;
	p->sadb_address_prefixlen = prefixlen;
	p->sadb_address_reserved = 0;

	memcpy(p + 1, saddr, saddr->sa_len);

	return(buf + len);
}

/*
 * set sadb_key structure after clearing buffer with zero.
 * OUT: the pointer of buf + len.
 */
static caddr_t
pfkey_setsadbkey(buf, lim, type, key, keylen)
	caddr_t buf;
	caddr_t lim;
	caddr_t key;
	u_int type, keylen;
{
	struct sadb_key *p;
	u_int len;

	p = (struct sadb_key *)buf;
	len = sizeof(struct sadb_key) + PFKEY_ALIGN8(keylen);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_key_len = PFKEY_UNIT64(len);
	p->sadb_key_exttype = type;
	p->sadb_key_bits = keylen << 3;
	p->sadb_key_reserved = 0;

	memcpy(p + 1, key, keylen);

	return buf + len;
}

/*
 * set sadb_lifetime structure after clearing buffer with zero.
 * OUT: the pointer of buf + len.
 */
static caddr_t
pfkey_setsadblifetime(buf, lim, type, l_alloc, l_bytes, l_addtime, l_usetime)
	caddr_t buf;
	caddr_t lim;
	u_int type;
	u_int32_t l_alloc, l_bytes, l_addtime, l_usetime;
{
	struct sadb_lifetime *p;
	u_int len;

	p = (struct sadb_lifetime *)buf;
	len = sizeof(struct sadb_lifetime);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_lifetime_len = PFKEY_UNIT64(len);
	p->sadb_lifetime_exttype = type;

	switch (type) {
	case SADB_EXT_LIFETIME_SOFT:
		p->sadb_lifetime_allocations
			= (l_alloc * soft_lifetime_allocations_rate) /100;
		p->sadb_lifetime_bytes
			= (l_bytes * soft_lifetime_bytes_rate) /100;
		p->sadb_lifetime_addtime
			= (l_addtime * soft_lifetime_addtime_rate) /100;
		p->sadb_lifetime_usetime
			= (l_usetime * soft_lifetime_usetime_rate) /100;
		break;
	case SADB_EXT_LIFETIME_HARD:
		p->sadb_lifetime_allocations = l_alloc;
		p->sadb_lifetime_bytes = l_bytes;
		p->sadb_lifetime_addtime = l_addtime;
		p->sadb_lifetime_usetime = l_usetime;
		break;
	}

	return buf + len;
}

/*
 * copy secasvar data into sadb_address.
 * `buf' must has been allocated sufficiently.
 */
static caddr_t
pfkey_setsadbxsa2(buf, lim, mode0, reqid)
	caddr_t buf;
	caddr_t lim;
	u_int32_t mode0;
	u_int32_t reqid;
{
	struct sadb_x_sa2 *p;
	u_int8_t mode = mode0 & 0xff;
	u_int len;

	p = (struct sadb_x_sa2 *)buf;
	len = sizeof(struct sadb_x_sa2);

	if (buf + len > lim)
		return NULL;

	memset(p, 0, len);
	p->sadb_x_sa2_len = PFKEY_UNIT64(len);
	p->sadb_x_sa2_exttype = SADB_X_EXT_SA2;
	p->sadb_x_sa2_mode = mode;
	p->sadb_x_sa2_reqid = reqid;

	return(buf + len);
}
