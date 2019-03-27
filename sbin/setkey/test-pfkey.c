/*	$FreeBSD$	*/
/*	$KAME: test-pfkey.c,v 1.4 2000/06/07 00:29:14 itojun Exp $	*/

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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/route.h>
#include <net/pfkeyv2.h>
#include <netinet/in.h>
#include <netipsec/keydb.h>
#include <netipsec/key_var.h>
#include <netipsec/key_debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

u_char m_buf[BUFSIZ];
u_int m_len;
char *pname;

void Usage(void);
int sendkeymsg(void);
void key_setsadbmsg(u_int);
void key_setsadbsens(void);
void key_setsadbprop(void);
void key_setsadbid(u_int, caddr_t);
void key_setsadblft(u_int, u_int);
void key_setspirange(void);
void key_setsadbkey(u_int, caddr_t);
void key_setsadbsa(void);
void key_setsadbaddr(u_int, u_int, caddr_t);
void key_setsadbextbuf(caddr_t, int, caddr_t, int, caddr_t, int);

void
Usage()
{
	printf("Usage:\t%s number\n", pname);
	exit(0);
}

int
main(ac, av)
	int ac;
	char **av;
{
	pname = *av;

	if (ac == 1) Usage();

	key_setsadbmsg(atoi(*(av+1)));
	sendkeymsg();

	exit(0);
}

/* %%% */
int
sendkeymsg()
{
	u_char rbuf[1024 * 32];	/* XXX: Enough ? Should I do MSG_PEEK ? */
	int so, len;

	if ((so = socket(PF_KEY, SOCK_RAW, PF_KEY_V2)) < 0) {
		perror("socket(PF_KEY)");
		goto end;
	}
#if 0
    {
#include <sys/time.h>
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(so, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("setsockopt");
		goto end;
	}
    }
#endif

	pfkey_sadump((struct sadb_msg *)m_buf);

	if ((len = send(so, m_buf, m_len, 0)) < 0) {
		perror("send");
		goto end;
	}

	if ((len = recv(so, rbuf, sizeof(rbuf), 0)) < 0) {
		perror("recv");
		goto end;
	}

	pfkey_sadump((struct sadb_msg *)rbuf);

end:
	(void)close(so);
	return(0);
}

void
key_setsadbmsg(type)
	u_int type;
{
	struct sadb_msg m_msg;

	memset(&m_msg, 0, sizeof(m_msg));
	m_msg.sadb_msg_version = PF_KEY_V2;
	m_msg.sadb_msg_type = type;
	m_msg.sadb_msg_errno = 0;
	m_msg.sadb_msg_satype = SADB_SATYPE_ESP;
#if 0
	m_msg.sadb_msg_reserved = 0;
#endif
	m_msg.sadb_msg_seq = 0;
	m_msg.sadb_msg_pid = getpid();

	m_len = sizeof(struct sadb_msg);
	memcpy(m_buf, &m_msg, m_len);

	switch (type) {
	case SADB_GETSPI:
		/*<base, address(SD), SPI range>*/
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "10.0.3.4");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "127.0.0.1");
		key_setspirange();
		/*<base, SA(*), address(SD)>*/
		break;

	case SADB_ADD:
		/* <base, SA, (lifetime(HSC),) address(SD), (address(P),)
		   key(AE), (identity(SD),) (sensitivity)> */
		key_setsadbaddr(SADB_EXT_ADDRESS_PROXY, AF_INET6, "3ffe::1");
	case SADB_UPDATE:
		key_setsadbsa();
		key_setsadblft(SADB_EXT_LIFETIME_HARD, 10);
		key_setsadblft(SADB_EXT_LIFETIME_SOFT, 5);
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "192.168.1.1");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "10.0.3.4");
		/* XXX key_setsadbkey(SADB_EXT_KEY_AUTH, "abcde"); */
		key_setsadbkey(SADB_EXT_KEY_AUTH, "1234567812345678");
		key_setsadbkey(SADB_EXT_KEY_ENCRYPT, "12345678");
		key_setsadbid(SADB_EXT_IDENTITY_SRC, "hoge1234@hoge.com");
		key_setsadbid(SADB_EXT_IDENTITY_DST, "hage5678@hage.net");
		key_setsadbsens();
		/* <base, SA, (lifetime(HSC),) address(SD), (address(P),)
		  (identity(SD),) (sensitivity)> */
		break;

	case SADB_DELETE:
		/* <base, SA(*), address(SDP)> */
		key_setsadbsa();
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "192.168.1.1");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "10.0.3.4");
		key_setsadbaddr(SADB_EXT_ADDRESS_PROXY, AF_INET6, "3ffe::1");
		/* <base, SA(*), address(SDP)> */
		break;

	case SADB_GET:
		/* <base, SA(*), address(SDP)> */
		key_setsadbsa();
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "192.168.1.1");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "10.0.3.4");
		key_setsadbaddr(SADB_EXT_ADDRESS_PROXY, AF_INET6, "3ffe::1");
		/* <base, SA, (lifetime(HSC),) address(SD), (address(P),)
		   key(AE), (identity(SD),) (sensitivity)> */
		break;

	case SADB_ACQUIRE:
		/* <base, address(SD), (address(P),) (identity(SD),)
		   (sensitivity,) proposal> */
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "192.168.1.1");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "10.0.3.4");
		key_setsadbaddr(SADB_EXT_ADDRESS_PROXY, AF_INET6, "3ffe::1");
		key_setsadbid(SADB_EXT_IDENTITY_SRC, "hoge1234@hoge.com");
		key_setsadbid(SADB_EXT_IDENTITY_DST, "hage5678@hage.net");
		key_setsadbsens();
		key_setsadbprop();
		/* <base, address(SD), (address(P),) (identity(SD),)
		   (sensitivity,) proposal> */
		break;

	case SADB_REGISTER:
		/* <base> */
		/* <base, supported> */
		break;

	case SADB_EXPIRE:
	case SADB_FLUSH:
		break;

	case SADB_DUMP:
		break;

	case SADB_X_PROMISC:
		/* <base> */
		/* <base, base(, others)> */
		break;

	case SADB_X_PCHANGE:
		break;

	/* for SPD management */
	case SADB_X_SPDFLUSH:
	case SADB_X_SPDDUMP:
		break;

	case SADB_X_SPDADD:
#if 0
	    {
		struct sadb_x_policy m_policy;

		m_policy.sadb_x_policy_len = PFKEY_UNIT64(sizeof(m_policy));
		m_policy.sadb_x_policy_exttype = SADB_X_EXT_POLICY;
		m_policy.sadb_x_policy_type = SADB_X_PL_IPSEC;
		m_policy.sadb_x_policy_esp_trans = 1;
		m_policy.sadb_x_policy_ah_trans = 2;
		m_policy.sadb_x_policy_esp_network = 3;
		m_policy.sadb_x_policy_ah_network = 4;
		m_policy.sadb_x_policy_reserved = 0;

		memcpy(m_buf + m_len, &m_policy, sizeof(struct sadb_x_policy));
		m_len += sizeof(struct sadb_x_policy);
	    }
#endif

	case SADB_X_SPDDELETE:
		key_setsadbaddr(SADB_EXT_ADDRESS_SRC, AF_INET, "192.168.1.1");
		key_setsadbaddr(SADB_EXT_ADDRESS_DST, AF_INET, "10.0.3.4");
		break;
	}

	((struct sadb_msg *)m_buf)->sadb_msg_len = PFKEY_UNIT64(m_len);

	return;
}

void
key_setsadbsens()
{
	struct sadb_sens m_sens;
	u_char buf[64];
	u_int s, i, slen, ilen, len;

	/* make sens & integ */
	s = htonl(0x01234567);
	i = htonl(0x89abcdef);
	slen = sizeof(s);
	ilen = sizeof(i);
	memcpy(buf, &s, slen);
	memcpy(buf + slen, &i, ilen);

	len = sizeof(m_sens) + PFKEY_ALIGN8(slen) + PFKEY_ALIGN8(ilen);
	m_sens.sadb_sens_len = PFKEY_UNIT64(len);
	m_sens.sadb_sens_exttype = SADB_EXT_SENSITIVITY;
	m_sens.sadb_sens_dpd = 1;
	m_sens.sadb_sens_sens_level = 2;
	m_sens.sadb_sens_sens_len = PFKEY_ALIGN8(slen);
	m_sens.sadb_sens_integ_level = 3;
	m_sens.sadb_sens_integ_len = PFKEY_ALIGN8(ilen);
	m_sens.sadb_sens_reserved = 0;

	key_setsadbextbuf(m_buf, m_len,
			(caddr_t)&m_sens, sizeof(struct sadb_sens),
			buf, slen + ilen);
	m_len += len;

	return;
}

void
key_setsadbprop()
{
	struct sadb_prop m_prop;
	struct sadb_comb *m_comb;
	u_char buf[256];
	u_int len = sizeof(m_prop) + sizeof(m_comb) * 2;

	/* make prop & comb */
	m_prop.sadb_prop_len = PFKEY_UNIT64(len);
	m_prop.sadb_prop_exttype = SADB_EXT_PROPOSAL;
	m_prop.sadb_prop_replay = 0;
	m_prop.sadb_prop_reserved[0] = 0;
	m_prop.sadb_prop_reserved[1] = 0;
	m_prop.sadb_prop_reserved[2] = 0;

	/* the 1st is ESP DES-CBC HMAC-MD5 */
	m_comb = (struct sadb_comb *)buf;
	m_comb->sadb_comb_auth = SADB_AALG_MD5HMAC;
	m_comb->sadb_comb_encrypt = SADB_EALG_DESCBC;
	m_comb->sadb_comb_flags = 0;
	m_comb->sadb_comb_auth_minbits = 8;
	m_comb->sadb_comb_auth_maxbits = 96;
	m_comb->sadb_comb_encrypt_minbits = 64;
	m_comb->sadb_comb_encrypt_maxbits = 64;
	m_comb->sadb_comb_reserved = 0;
	m_comb->sadb_comb_soft_allocations = 0;
	m_comb->sadb_comb_hard_allocations = 0;
	m_comb->sadb_comb_soft_bytes = 0;
	m_comb->sadb_comb_hard_bytes = 0;
	m_comb->sadb_comb_soft_addtime = 0;
	m_comb->sadb_comb_hard_addtime = 0;
	m_comb->sadb_comb_soft_usetime = 0;
	m_comb->sadb_comb_hard_usetime = 0;

	/* the 2st is ESP 3DES-CBC and AH HMAC-SHA1 */
	m_comb = (struct sadb_comb *)(buf + sizeof(*m_comb));
	m_comb->sadb_comb_auth = SADB_AALG_SHA1HMAC;
	m_comb->sadb_comb_encrypt = SADB_EALG_3DESCBC;
	m_comb->sadb_comb_flags = 0;
	m_comb->sadb_comb_auth_minbits = 8;
	m_comb->sadb_comb_auth_maxbits = 96;
	m_comb->sadb_comb_encrypt_minbits = 64;
	m_comb->sadb_comb_encrypt_maxbits = 64;
	m_comb->sadb_comb_reserved = 0;
	m_comb->sadb_comb_soft_allocations = 0;
	m_comb->sadb_comb_hard_allocations = 0;
	m_comb->sadb_comb_soft_bytes = 0;
	m_comb->sadb_comb_hard_bytes = 0;
	m_comb->sadb_comb_soft_addtime = 0;
	m_comb->sadb_comb_hard_addtime = 0;
	m_comb->sadb_comb_soft_usetime = 0;
	m_comb->sadb_comb_hard_usetime = 0;

	key_setsadbextbuf(m_buf, m_len,
			(caddr_t)&m_prop, sizeof(struct sadb_prop),
			buf, sizeof(*m_comb) * 2);
	m_len += len;

	return;
}

void
key_setsadbid(ext, str)
	u_int ext;
	caddr_t str;
{
	struct sadb_ident m_id;
	u_int idlen = strlen(str), len;

	len = sizeof(m_id) + PFKEY_ALIGN8(idlen);
	m_id.sadb_ident_len = PFKEY_UNIT64(len);
	m_id.sadb_ident_exttype = ext;
	m_id.sadb_ident_type = SADB_IDENTTYPE_USERFQDN;
	m_id.sadb_ident_reserved = 0;
	m_id.sadb_ident_id = getpid();

	key_setsadbextbuf(m_buf, m_len,
			(caddr_t)&m_id, sizeof(struct sadb_ident),
			str, idlen);
	m_len += len;

	return;
}

void
key_setsadblft(ext, time)
	u_int ext, time;
{
	struct sadb_lifetime m_lft;

	m_lft.sadb_lifetime_len = PFKEY_UNIT64(sizeof(m_lft));
	m_lft.sadb_lifetime_exttype = ext;
	m_lft.sadb_lifetime_allocations = 0x2;
	m_lft.sadb_lifetime_bytes = 0x1000;
	m_lft.sadb_lifetime_addtime = time;
	m_lft.sadb_lifetime_usetime = 0x0020;

	memcpy(m_buf + m_len, &m_lft, sizeof(struct sadb_lifetime));
	m_len += sizeof(struct sadb_lifetime);

	return;
}

void
key_setspirange()
{
	struct sadb_spirange m_spi;

	m_spi.sadb_spirange_len = PFKEY_UNIT64(sizeof(m_spi));
	m_spi.sadb_spirange_exttype = SADB_EXT_SPIRANGE;
	m_spi.sadb_spirange_min = 0x00001000;
	m_spi.sadb_spirange_max = 0x00002000;
	m_spi.sadb_spirange_reserved = 0;

	memcpy(m_buf + m_len, &m_spi, sizeof(struct sadb_spirange));
	m_len += sizeof(struct sadb_spirange);

	return;
}

void
key_setsadbkey(ext, str)
	u_int ext;
	caddr_t str;
{
	struct sadb_key m_key;
	u_int keylen = strlen(str);
	u_int len;

	len = sizeof(struct sadb_key) + PFKEY_ALIGN8(keylen);
	m_key.sadb_key_len = PFKEY_UNIT64(len);
	m_key.sadb_key_exttype = ext;
	m_key.sadb_key_bits = keylen * 8;
	m_key.sadb_key_reserved = 0;

	key_setsadbextbuf(m_buf, m_len,
			(caddr_t)&m_key, sizeof(struct sadb_key),
			str, keylen);
	m_len += len;

	return;
}

void
key_setsadbsa()
{
	struct sadb_sa m_sa;

	m_sa.sadb_sa_len = PFKEY_UNIT64(sizeof(struct sadb_sa));
	m_sa.sadb_sa_exttype = SADB_EXT_SA;
	m_sa.sadb_sa_spi = htonl(0x12345678);
	m_sa.sadb_sa_replay = 4;
	m_sa.sadb_sa_state = 0;
	m_sa.sadb_sa_auth = SADB_AALG_MD5HMAC;
	m_sa.sadb_sa_encrypt = SADB_EALG_DESCBC;
	m_sa.sadb_sa_flags = 0;

	memcpy(m_buf + m_len, &m_sa, sizeof(struct sadb_sa));
	m_len += sizeof(struct sadb_sa);

	return;
}

void
key_setsadbaddr(ext, af, str)
	u_int ext, af;
	caddr_t str;
{
	struct sadb_address m_addr;
	u_int len;
	struct addrinfo hints, *res;
	const char *serv;
	int plen;

	switch (af) {
	case AF_INET:
		plen = sizeof(struct in_addr) << 3;
		break;
	case AF_INET6:
		plen = sizeof(struct in6_addr) << 3;
		break;
	default:
		/* XXX bark */
		exit(1);
	}

	/* make sockaddr buffer */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	serv = (ext == SADB_EXT_ADDRESS_PROXY ? "0" : "4660");	/*0x1234*/
	if (getaddrinfo(str, serv, &hints, &res) != 0 || res->ai_next) {
		/* XXX bark */
		exit(1);
	}
	
	len = sizeof(struct sadb_address) + PFKEY_ALIGN8(res->ai_addrlen);
	m_addr.sadb_address_len = PFKEY_UNIT64(len);
	m_addr.sadb_address_exttype = ext;
	m_addr.sadb_address_proto =
		(ext == SADB_EXT_ADDRESS_PROXY ? 0 : IPPROTO_TCP);
	m_addr.sadb_address_prefixlen = plen;
	m_addr.sadb_address_reserved = 0;

	key_setsadbextbuf(m_buf, m_len,
			(caddr_t)&m_addr, sizeof(struct sadb_address),
			(caddr_t)res->ai_addr, res->ai_addrlen);
	m_len += len;

	freeaddrinfo(res);

	return;
}

void
key_setsadbextbuf(dst, off, ebuf, elen, vbuf, vlen)
	caddr_t dst, ebuf, vbuf;
	int off, elen, vlen;
{
	memset(dst + off, 0, elen + vlen);
	memcpy(dst + off, (caddr_t)ebuf, elen);
	memcpy(dst + off + elen, vbuf, vlen);

	return;
}

