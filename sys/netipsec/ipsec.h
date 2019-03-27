/*	$FreeBSD$	*/
/*	$KAME: ipsec.h,v 1.53 2001/11/20 08:32:38 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

/*
 * IPsec controller part.
 */

#ifndef _NETIPSEC_IPSEC_H_
#define _NETIPSEC_IPSEC_H_

#if defined(_KERNEL) && !defined(_LKM) && !defined(KLD_MODULE)
#include "opt_inet.h"
#include "opt_ipsec.h"
#endif

#include <net/pfkeyv2.h>
#include <netipsec/keydb.h>

#ifdef _KERNEL

#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_rwlock.h>

#define	IPSEC_ASSERT(_c,_m) KASSERT(_c, _m)

/*
 * Security Policy Index
 * Ensure that both address families in the "src" and "dst" are same.
 * When the value of the ul_proto is ICMPv6, the port field in "src"
 * specifies ICMPv6 type, and the port field in "dst" specifies ICMPv6 code.
 */
struct secpolicyindex {
	union sockaddr_union src;	/* IP src address for SP */
	union sockaddr_union dst;	/* IP dst address for SP */
	uint8_t ul_proto;		/* upper layer Protocol */
	uint8_t dir;			/* direction of packet flow */
	uint8_t prefs;			/* prefix length in bits for src */
	uint8_t prefd;			/* prefix length in bits for dst */
};

/* Request for IPsec */
struct ipsecrequest {
	struct secasindex saidx;/* hint for search proper SA */
				/* if __ss_len == 0 then no address specified.*/
	u_int level;		/* IPsec level defined below. */
};

/* Security Policy Data Base */
struct secpolicy {
	TAILQ_ENTRY(secpolicy) chain;
	LIST_ENTRY(secpolicy) idhash;
	LIST_ENTRY(secpolicy) drainq;

	struct secpolicyindex spidx;	/* selector */
#define	IPSEC_MAXREQ		4
	struct ipsecrequest *req[IPSEC_MAXREQ];
	u_int tcount;			/* IPsec transforms count */
	volatile u_int refcnt;		/* reference count */
	u_int policy;			/* policy_type per pfkeyv2.h */
	u_int state;
#define	IPSEC_SPSTATE_DEAD	0
#define	IPSEC_SPSTATE_LARVAL	1
#define	IPSEC_SPSTATE_ALIVE	2
#define	IPSEC_SPSTATE_PCB	3
#define	IPSEC_SPSTATE_IFNET	4
	uint32_t priority;		/* priority of this policy */
	uint32_t id;			/* It's unique number on the system. */
	/*
	 * lifetime handler.
	 * the policy can be used without limitiation if both lifetime and
	 * validtime are zero.
	 * "lifetime" is passed by sadb_lifetime.sadb_lifetime_addtime.
	 * "validtime" is passed by sadb_lifetime.sadb_lifetime_usetime.
	 */
	time_t created;		/* time created the policy */
	time_t lastused;	/* updated every when kernel sends a packet */
	long lifetime;		/* duration of the lifetime of this policy */
	long validtime;		/* duration this policy is valid without use */
};

/*
 * PCB security policies.
 * Application can setup private security policies for socket.
 * Such policies can have IPSEC, BYPASS and ENTRUST type.
 * By default, policies are set to NULL. This means that they have ENTRUST type.
 * When application sets BYPASS or IPSEC type policy, the flags field
 * is also updated. When flags is not set, the system could store
 * used security policy into the sp_in/sp_out pointer to speed up further
 * lookups.
 */
struct inpcbpolicy {
	struct secpolicy	*sp_in;
	struct secpolicy	*sp_out;

	uint32_t		genid;
	uint16_t		flags;
#define	INP_INBOUND_POLICY	0x0001
#define	INP_OUTBOUND_POLICY	0x0002
	uint16_t		hdrsz;
};

/* SP acquiring list table. */
struct secspacq {
	LIST_ENTRY(secspacq) chain;

	struct secpolicyindex spidx;

	time_t created;		/* for lifetime */
	int count;		/* for lifetime */
	/* XXX: here is mbuf place holder to be sent ? */
};
#endif /* _KERNEL */

/* buffer size for formatted output of ipsec address */
#define	IPSEC_ADDRSTRLEN	(INET6_ADDRSTRLEN + 11)

/* according to IANA assignment, port 0x0000 and proto 0xff are reserved. */
#define IPSEC_PORT_ANY		0
#define IPSEC_ULPROTO_ANY	255
#define IPSEC_PROTO_ANY		255

/* mode of security protocol */
/* NOTE: DON'T use IPSEC_MODE_ANY at SPD.  It's only use in SAD */
#define	IPSEC_MODE_ANY		0	/* i.e. wildcard. */
#define	IPSEC_MODE_TRANSPORT	1
#define	IPSEC_MODE_TUNNEL	2
#define	IPSEC_MODE_TCPMD5	3	/* TCP MD5 mode */

/*
 * Direction of security policy.
 * NOTE: Since INVALID is used just as flag.
 * The other are used for loop counter too.
 */
#define IPSEC_DIR_ANY		0
#define IPSEC_DIR_INBOUND	1
#define IPSEC_DIR_OUTBOUND	2
#define IPSEC_DIR_MAX		3
#define IPSEC_DIR_INVALID	4

/* Policy level */
/*
 * IPSEC, ENTRUST and BYPASS are allowed for setsockopt() in PCB,
 * DISCARD, IPSEC and NONE are allowed for setkey() in SPD.
 * DISCARD and NONE are allowed for system default.
 */
#define IPSEC_POLICY_DISCARD	0	/* discarding packet */
#define IPSEC_POLICY_NONE	1	/* through IPsec engine */
#define IPSEC_POLICY_IPSEC	2	/* do IPsec */
#define IPSEC_POLICY_ENTRUST	3	/* consulting SPD if present. */
#define IPSEC_POLICY_BYPASS	4	/* only for privileged socket. */

/* Policy scope */
#define	IPSEC_POLICYSCOPE_ANY		0x00	/* unspecified */
#define	IPSEC_POLICYSCOPE_GLOBAL	0x01	/* global scope */
#define	IPSEC_POLICYSCOPE_IFNET		0x02	/* if_ipsec(4) scope */
#define	IPSEC_POLICYSCOPE_PCB		0x04	/* PCB scope */

/* Security protocol level */
#define	IPSEC_LEVEL_DEFAULT	0	/* reference to system default */
#define	IPSEC_LEVEL_USE		1	/* use SA if present. */
#define	IPSEC_LEVEL_REQUIRE	2	/* require SA. */
#define	IPSEC_LEVEL_UNIQUE	3	/* unique SA. */

#define IPSEC_MANUAL_REQID_MAX	0x3fff
				/*
				 * if security policy level == unique, this id
				 * indicate to a relative SA for use, else is
				 * zero.
				 * 1 - 0x3fff are reserved for manual keying.
				 * 0 are reserved for above reason.  Others is
				 * for kernel use.
				 * Note that this id doesn't identify SA
				 * by only itself.
				 */
#define IPSEC_REPLAYWSIZE  32

/* statistics for ipsec processing */
struct ipsecstat {
	uint64_t ips_in_polvio;		/* input: sec policy violation */
	uint64_t ips_in_nomem;		/* input: no memory available */
	uint64_t ips_in_inval;		/* input: generic error */

	uint64_t ips_out_polvio;	/* output: sec policy violation */
	uint64_t ips_out_nosa;		/* output: SA unavailable  */
	uint64_t ips_out_nomem;		/* output: no memory available */
	uint64_t ips_out_noroute;	/* output: no route available */
	uint64_t ips_out_inval;		/* output: generic error */
	uint64_t ips_out_bundlesa;	/* output: bundled SA processed */

	uint64_t ips_spdcache_hits;	/* SPD cache hits */
	uint64_t ips_spdcache_misses;	/* SPD cache misses */

	uint64_t ips_clcopied;		/* clusters copied during clone */
	uint64_t ips_mbinserted;	/* mbufs inserted during makespace */
	/* 
	 * Temporary statistics for performance analysis.
	 */
	/* See where ESP/AH/IPCOMP header land in mbuf on input */
	uint64_t ips_input_front;
	uint64_t ips_input_middle;
	uint64_t ips_input_end;
};

/*
 * Definitions for IPsec & Key sysctl operations.
 */
#define IPSECCTL_STATS			1	/* stats */
#define IPSECCTL_DEF_POLICY		2
#define IPSECCTL_DEF_ESP_TRANSLEV	3	/* int; ESP transport mode */
#define IPSECCTL_DEF_ESP_NETLEV		4	/* int; ESP tunnel mode */
#define IPSECCTL_DEF_AH_TRANSLEV	5	/* int; AH transport mode */
#define IPSECCTL_DEF_AH_NETLEV		6	/* int; AH tunnel mode */
#if 0	/* obsolete, do not reuse */
#define IPSECCTL_INBOUND_CALL_IKE	7
#endif
#define	IPSECCTL_AH_CLEARTOS		8
#define	IPSECCTL_AH_OFFSETMASK		9
#define	IPSECCTL_DFBIT			10
#define	IPSECCTL_ECN			11
#define	IPSECCTL_DEBUG			12
#define	IPSECCTL_ESP_RANDPAD		13

#ifdef _KERNEL
#include <sys/counter.h>

struct ipsec_ctx_data;
#define	IPSEC_INIT_CTX(_ctx, _mp, _inp, _sav, _af, _enc) do {	\
	(_ctx)->mp = (_mp);				\
	(_ctx)->inp = (_inp);				\
	(_ctx)->sav = (_sav);				\
	(_ctx)->af = (_af);				\
	(_ctx)->enc = (_enc);				\
} while(0)
int	ipsec_run_hhooks(struct ipsec_ctx_data *ctx, int direction);

VNET_DECLARE(int, ipsec_debug);
#define	V_ipsec_debug		VNET(ipsec_debug)

#ifdef REGRESSION
VNET_DECLARE(int, ipsec_replay);
VNET_DECLARE(int, ipsec_integrity);

#define	V_ipsec_replay		VNET(ipsec_replay)
#define	V_ipsec_integrity	VNET(ipsec_integrity)
#endif

VNET_PCPUSTAT_DECLARE(struct ipsecstat, ipsec4stat);
VNET_DECLARE(int, ip4_esp_trans_deflev);
VNET_DECLARE(int, ip4_esp_net_deflev);
VNET_DECLARE(int, ip4_ah_trans_deflev);
VNET_DECLARE(int, ip4_ah_net_deflev);
VNET_DECLARE(int, ip4_ipsec_dfbit);
VNET_DECLARE(int, ip4_ipsec_ecn);
VNET_DECLARE(int, crypto_support);
VNET_DECLARE(int, async_crypto);
VNET_DECLARE(int, natt_cksum_policy);

#define	IPSECSTAT_INC(name)	\
    VNET_PCPUSTAT_ADD(struct ipsecstat, ipsec4stat, name, 1)
#define	V_ip4_esp_trans_deflev	VNET(ip4_esp_trans_deflev)
#define	V_ip4_esp_net_deflev	VNET(ip4_esp_net_deflev)
#define	V_ip4_ah_trans_deflev	VNET(ip4_ah_trans_deflev)
#define	V_ip4_ah_net_deflev	VNET(ip4_ah_net_deflev)
#define	V_ip4_ipsec_dfbit	VNET(ip4_ipsec_dfbit)
#define	V_ip4_ipsec_ecn		VNET(ip4_ipsec_ecn)
#define	V_crypto_support	VNET(crypto_support)
#define	V_async_crypto		VNET(async_crypto)
#define	V_natt_cksum_policy	VNET(natt_cksum_policy)

#define ipseclog(x)	do { if (V_ipsec_debug) log x; } while (0)
/* for openbsd compatibility */
#ifdef IPSEC_DEBUG
#define	IPSEC_DEBUG_DECLARE(x)	x
#define	DPRINTF(x)	do { if (V_ipsec_debug) printf x; } while (0)
#else
#define	IPSEC_DEBUG_DECLARE(x)
#define	DPRINTF(x)
#endif

struct inpcb;
struct m_tag;
struct secasvar;
struct sockopt;
struct tcphdr;
union sockaddr_union;

int ipsec_if_input(struct mbuf *, struct secasvar *, uint32_t);

struct ipsecrequest *ipsec_newisr(void);
void ipsec_delisr(struct ipsecrequest *);
struct secpolicy *ipsec4_checkpolicy(const struct mbuf *, struct inpcb *,
    int *, int);

u_int ipsec_get_reqlevel(struct secpolicy *, u_int);

void udp_ipsec_adjust_cksum(struct mbuf *, struct secasvar *, int, int);
int udp_ipsec_output(struct mbuf *, struct secasvar *);
int udp_ipsec_input(struct mbuf *, int, int);
int udp_ipsec_pcbctl(struct inpcb *, struct sockopt *);

int ipsec_chkreplay(uint32_t, struct secasvar *);
int ipsec_updatereplay(uint32_t, struct secasvar *);
int ipsec_updateid(struct secasvar *, crypto_session_t *, crypto_session_t *);
int ipsec_initialized(void);

void ipsec_setspidx_inpcb(struct inpcb *, struct secpolicyindex *, u_int);

void ipsec4_setsockaddrs(const struct mbuf *, union sockaddr_union *,
    union sockaddr_union *);
int ipsec4_in_reject(const struct mbuf *, struct inpcb *);
int ipsec4_input(struct mbuf *, int, int);
int ipsec4_forward(struct mbuf *);
int ipsec4_pcbctl(struct inpcb *, struct sockopt *);
int ipsec4_output(struct mbuf *, struct inpcb *);
int ipsec4_capability(struct mbuf *, u_int);
int ipsec4_common_input_cb(struct mbuf *, struct secasvar *, int, int);
int ipsec4_process_packet(struct mbuf *, struct secpolicy *, struct inpcb *);
int ipsec_process_done(struct mbuf *, struct secpolicy *, struct secasvar *,
    u_int);

extern	void m_checkalignment(const char* where, struct mbuf *m0,
		int off, int len);
extern	struct mbuf *m_makespace(struct mbuf *m0, int skip, int hlen, int *off);
extern	caddr_t m_pad(struct mbuf *m, int n);
extern	int m_striphdr(struct mbuf *m, int skip, int hlen);

#endif /* _KERNEL */

#ifndef _KERNEL
extern caddr_t ipsec_set_policy(char *, int);
extern int ipsec_get_policylen(caddr_t);
extern char *ipsec_dump_policy(caddr_t, char *);
extern const char *ipsec_strerror(void);

#endif /* ! KERNEL */

#endif /* _NETIPSEC_IPSEC_H_ */
