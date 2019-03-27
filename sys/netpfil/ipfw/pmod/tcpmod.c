/*-
 * Copyright (c) 2017 Yandex LLC
 * Copyright (c) 2017 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/pfil.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/ip_fw.h>
#include <netinet/ip6.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/pmod/pmod.h>

#include <machine/in_cksum.h>

VNET_DEFINE_STATIC(uint16_t, tcpmod_setmss_eid) = 0;
#define	V_tcpmod_setmss_eid	VNET(tcpmod_setmss_eid)

static int
tcpmod_setmss(struct mbuf **mp, struct tcphdr *tcp, int tlen, uint16_t mss)
{
	struct mbuf *m;
	u_char *cp;
	int optlen, ret;
	uint16_t oldmss, csum;

	m = *mp;
	ret = IP_FW_DENY;
	if (m->m_len < m->m_pkthdr.len) {
		/*
		 * We shouldn't have any data, IP packet contains only
		 * TCP header with options.
		 */
		*mp = m = m_pullup(m, m->m_pkthdr.len);
		if (m == NULL)
			return (ret);
	}
	/* Parse TCP options. */
	for (tlen -= sizeof(struct tcphdr), cp = (u_char *)(tcp + 1);
	    tlen > 0; tlen -= optlen, cp += optlen) {
		if (cp[0] == TCPOPT_EOL)
			break;
		if (cp[0] == TCPOPT_NOP) {
			optlen = 1;
			continue;
		}
		if (tlen < 2)
			break;
		optlen = cp[1];
		if (optlen < 2 || optlen > tlen)
			break;
		if (cp[0] == TCPOPT_MAXSEG) {
			if (optlen != TCPOLEN_MAXSEG)
				break;
			ret = 0; /* report success */
			bcopy(cp + 2, &oldmss, sizeof(oldmss));
			/* Do not update lower MSS value */
			if (ntohs(oldmss) <= ntohs(mss))
				break;
			bcopy(&mss, cp + 2, sizeof(mss));
			/* Update checksum if it is not delayed. */
			if ((m->m_pkthdr.csum_flags &
			    (CSUM_TCP | CSUM_TCP_IPV6)) == 0) {
				bcopy(&tcp->th_sum, &csum, sizeof(csum));
				csum = cksum_adjust(csum, oldmss, mss);
				bcopy(&csum, &tcp->th_sum, sizeof(csum));
			}
			break;
		}
	}

	return (ret);
}

#ifdef INET6
static int
tcpmod_ipv6_setmss(struct mbuf **mp, uint16_t mss)
{
	struct ip6_hdr *ip6;
	struct ip6_hbh *hbh;
	struct tcphdr *tcp;
	int hlen, plen, proto;

	ip6 = mtod(*mp, struct ip6_hdr *);
	hlen = sizeof(*ip6);
	proto = ip6->ip6_nxt;
	/*
	 * Skip IPv6 extension headers and get the TCP header.
	 * ipfw_chk() has already done this work. So we are sure that
	 * we will not do an access to the out of bounds. For this
	 * reason we skip some checks here.
	 */
	while (proto == IPPROTO_HOPOPTS || proto == IPPROTO_ROUTING ||
	    proto == IPPROTO_DSTOPTS) {
		hbh = mtodo(*mp, hlen);
		proto = hbh->ip6h_nxt;
		hlen += (hbh->ip6h_len + 1) << 3;
	}
	tcp = mtodo(*mp, hlen);
	plen = (*mp)->m_pkthdr.len - hlen;
	hlen = tcp->th_off << 2;
	/* We must have TCP options and enough data in a packet. */
	if (hlen <= sizeof(struct tcphdr) || hlen > plen)
		return (IP_FW_DENY);
	return (tcpmod_setmss(mp, tcp, hlen, mss));
}
#endif /* INET6 */

#ifdef INET
static int
tcpmod_ipv4_setmss(struct mbuf **mp, uint16_t mss)
{
	struct tcphdr *tcp;
	struct ip *ip;
	int hlen, plen;

	ip = mtod(*mp, struct ip *);
	hlen = ip->ip_hl << 2;
	tcp = mtodo(*mp, hlen);
	plen = (*mp)->m_pkthdr.len - hlen;
	hlen = tcp->th_off << 2;
	/* We must have TCP options and enough data in a packet. */
	if (hlen <= sizeof(struct tcphdr) || hlen > plen)
		return (IP_FW_DENY);
	return (tcpmod_setmss(mp, tcp, hlen, mss));
}
#endif /* INET */

/*
 * ipfw external action handler.
 */
static int
ipfw_tcpmod(struct ip_fw_chain *chain, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done)
{
	ipfw_insn *icmd;
	int ret;

	*done = 0; /* try next rule if not matched */
	ret = IP_FW_DENY;
	icmd = cmd + 1;
	if (cmd->opcode != O_EXTERNAL_ACTION ||
	    cmd->arg1 != V_tcpmod_setmss_eid ||
	    icmd->opcode != O_EXTERNAL_DATA ||
	    icmd->len != F_INSN_SIZE(ipfw_insn))
		return (ret);

	/*
	 * NOTE: ipfw_chk() can set f_id.proto from IPv6 fragment header,
	 * but f_id._flags can be filled only from real TCP header.
	 *
	 * NOTE: ipfw_chk() drops very short packets in the PULLUP_TO()
	 * macro. But we need to check that mbuf is contiguous more than
	 * IP+IP_options/IP_extensions+tcphdr length, because TCP header
	 * must have TCP options, and ipfw_chk() does PULLUP_TO() size of
	 * struct tcphdr.
	 *
	 * NOTE: we require only the presence of SYN flag. User should
	 * properly configure the rule to select the direction of packets,
	 * that should be modified.
	 */
	if (args->f_id.proto != IPPROTO_TCP ||
	    (args->f_id._flags & TH_SYN) == 0)
		return (ret);

	switch (args->f_id.addr_type) {
#ifdef INET
		case 4:
			ret = tcpmod_ipv4_setmss(&args->m, htons(icmd->arg1));
			break;
#endif
#ifdef INET6
		case 6:
			ret = tcpmod_ipv6_setmss(&args->m, htons(icmd->arg1));
			break;
#endif
	}
	/*
	 * We return zero in both @ret and @done on success, and ipfw_chk()
	 * will update rule counters. Otherwise a packet will not be matched
	 * by rule.
	 */
	return (ret);
}

int
tcpmod_init(struct ip_fw_chain *ch, int first)
{

	V_tcpmod_setmss_eid = ipfw_add_eaction(ch, ipfw_tcpmod, "tcp-setmss");
	if (V_tcpmod_setmss_eid == 0)
		return (ENXIO);
	return (0);
}

void
tcpmod_uninit(struct ip_fw_chain *ch, int last)
{

	ipfw_del_eaction(ch, V_tcpmod_setmss_eid);
	V_tcpmod_setmss_eid = 0;
}

