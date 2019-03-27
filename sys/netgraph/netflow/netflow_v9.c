/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander V. Chernikov <melifaro@ipfw.ru>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 	$FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"
#include "opt_route.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <vm/uma.h>

#include <net/if.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>

#include <netgraph/netflow/netflow.h>
#include <netgraph/netflow/ng_netflow.h>
#include <netgraph/netflow/netflow_v9.h>

MALLOC_DECLARE(M_NETFLOW_GENERAL);
MALLOC_DEFINE(M_NETFLOW_GENERAL, "netflow_general", "plog, V9 templates data");

/*
 * Base V9 templates for L4+ IPv4/IPv6 protocols
 */
struct netflow_v9_template _netflow_v9_record_ipv4_tcp[] =
{
	{ NETFLOW_V9_FIELD_IPV4_SRC_ADDR, 4},
	{ NETFLOW_V9_FIELD_IPV4_DST_ADDR, 4},
	{ NETFLOW_V9_FIELD_IPV4_NEXT_HOP, 4},
	{ NETFLOW_V9_FIELD_INPUT_SNMP, 2},
	{ NETFLOW_V9_FIELD_OUTPUT_SNMP, 2},
	{ NETFLOW_V9_FIELD_IN_PKTS, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_IN_BYTES, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_OUT_PKTS, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_OUT_BYTES, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_FIRST_SWITCHED, 4},
	{ NETFLOW_V9_FIELD_LAST_SWITCHED, 4},
	{ NETFLOW_V9_FIELD_L4_SRC_PORT, 2},
	{ NETFLOW_V9_FIELD_L4_DST_PORT, 2},
	{ NETFLOW_V9_FIELD_TCP_FLAGS, 1},
	{ NETFLOW_V9_FIELD_PROTOCOL, 1},
	{ NETFLOW_V9_FIELD_TOS, 1},
	{ NETFLOW_V9_FIELD_SRC_AS, 4},
	{ NETFLOW_V9_FIELD_DST_AS, 4},
	{ NETFLOW_V9_FIELD_SRC_MASK, 1},
	{ NETFLOW_V9_FIELD_DST_MASK, 1},
	{0, 0}
};

struct netflow_v9_template _netflow_v9_record_ipv6_tcp[] =
{
	{ NETFLOW_V9_FIELD_IPV6_SRC_ADDR, 16},
	{ NETFLOW_V9_FIELD_IPV6_DST_ADDR, 16},
	{ NETFLOW_V9_FIELD_IPV6_NEXT_HOP, 16},
	{ NETFLOW_V9_FIELD_INPUT_SNMP, 2},
	{ NETFLOW_V9_FIELD_OUTPUT_SNMP, 2},
	{ NETFLOW_V9_FIELD_IN_PKTS, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_IN_BYTES, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_OUT_PKTS, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_OUT_BYTES, sizeof(CNTR)},
	{ NETFLOW_V9_FIELD_FIRST_SWITCHED, 4},
	{ NETFLOW_V9_FIELD_LAST_SWITCHED, 4},
	{ NETFLOW_V9_FIELD_L4_SRC_PORT, 2},
	{ NETFLOW_V9_FIELD_L4_DST_PORT, 2},
	{ NETFLOW_V9_FIELD_TCP_FLAGS, 1},
	{ NETFLOW_V9_FIELD_PROTOCOL, 1},
	{ NETFLOW_V9_FIELD_TOS, 1},
	{ NETFLOW_V9_FIELD_SRC_AS, 4},
	{ NETFLOW_V9_FIELD_DST_AS, 4},
	{ NETFLOW_V9_FIELD_SRC_MASK, 1},
	{ NETFLOW_V9_FIELD_DST_MASK, 1},
	{0, 0}
};

/*
 * Pre-compiles flow exporter for all possible FlowSets
 * so we can add flowset to packet via simple memcpy()
 */
static void
generate_v9_templates(priv_p priv)
{
	uint16_t *p, *template_fields_cnt;
	int cnt;

	int flowset_size = sizeof(struct netflow_v9_flowset_header) +
		_NETFLOW_V9_TEMPLATE_SIZE(_netflow_v9_record_ipv4_tcp) + /* netflow_v9_record_ipv4_tcp */
		_NETFLOW_V9_TEMPLATE_SIZE(_netflow_v9_record_ipv6_tcp); /* netflow_v9_record_ipv6_tcp */

	priv->v9_flowsets[0] = malloc(flowset_size, M_NETFLOW_GENERAL, M_WAITOK | M_ZERO);

	if (flowset_size % 4)
		flowset_size += 4 - (flowset_size % 4); /* Padding to 4-byte boundary */

	priv->flowsets_count = 1;
	p = (uint16_t *)priv->v9_flowsets[0];
	*p++ = 0; /* Flowset ID, 0 is reserved for Template FlowSets  */
	*p++ = htons(flowset_size); /* Total FlowSet length */

	/*
	 * Most common TCP/UDP IPv4 template, ID = 256
	 */
	*p++ = htons(NETFLOW_V9_MAX_RESERVED_FLOWSET + NETFLOW_V9_FLOW_V4_L4);
	template_fields_cnt = p++;
	for (cnt = 0; _netflow_v9_record_ipv4_tcp[cnt].field_id != 0; cnt++) {
		*p++ = htons(_netflow_v9_record_ipv4_tcp[cnt].field_id);
		*p++ = htons(_netflow_v9_record_ipv4_tcp[cnt].field_length);
	}
	*template_fields_cnt = htons(cnt);

	/*
	 * TCP/UDP IPv6 template, ID = 257
	 */
	*p++ = htons(NETFLOW_V9_MAX_RESERVED_FLOWSET + NETFLOW_V9_FLOW_V6_L4);
	template_fields_cnt = p++;
	for (cnt = 0; _netflow_v9_record_ipv6_tcp[cnt].field_id != 0; cnt++) {
		*p++ = htons(_netflow_v9_record_ipv6_tcp[cnt].field_id);
		*p++ = htons(_netflow_v9_record_ipv6_tcp[cnt].field_length);
	}
	*template_fields_cnt = htons(cnt);

	priv->flowset_records[0] = 2;
}

/* Closes current data flowset */
static void inline
close_flowset(struct mbuf *m, struct netflow_v9_packet_opt *t)
{
	struct mbuf *m_old;
	uint32_t zero = 0;
	int offset = 0;
	uint16_t *flowset_length, len;

	/* Hack to ensure we are not crossing mbuf boundary, length is uint16_t  */
	m_old = m_getptr(m, t->flow_header + offsetof(struct netflow_v9_flowset_header, length), &offset);
	flowset_length = (uint16_t *)(mtod(m_old, char *) + offset);

	len = (uint16_t)(m_pktlen(m) - t->flow_header);
	/* Align on 4-byte boundary (RFC 3954, Clause 5.3) */
	if (len % 4) {
		if (m_append(m, 4 - (len % 4), (void *)&zero) != 1)
			panic("ng_netflow: m_append() failed!");

		len += 4 - (len % 4);
	}

	*flowset_length = htons(len);
}

/*
 * Non-static functions called from ng_netflow.c
 */

/* We have full datagram in fib data. Send it to export hook. */
int
export9_send(priv_p priv, fib_export_p fe, item_p item, struct netflow_v9_packet_opt *t, int flags)
{
	struct mbuf *m = NGI_M(item);
	struct netflow_v9_export_dgram *dgram = mtod(m,
					struct netflow_v9_export_dgram *);
	struct netflow_v9_header *header = &dgram->header;
	struct timespec ts;
	int error = 0;

	if (t == NULL) {
		CTR0(KTR_NET, "export9_send(): V9 export packet without tag");
		NG_FREE_ITEM(item);
		return (0);
	}

	/* Close flowset if not closed already */
	if (m_pktlen(m) != t->flow_header)
		close_flowset(m, t);

	/* Fill export header. */
	header->count = t->count;
	header->sys_uptime = htonl(MILLIUPTIME(time_uptime));
	getnanotime(&ts);
	header->unix_secs  = htonl(ts.tv_sec);
	header->seq_num = htonl(atomic_fetchadd_32(&fe->flow9_seq, 1));
	header->count = htons(t->count);
	header->source_id = htonl(fe->domain_id);

	if (priv->export9 != NULL)
		NG_FWD_ITEM_HOOK_FLAGS(error, item, priv->export9, flags);
	else
		NG_FREE_ITEM(item);

	free(t, M_NETFLOW_GENERAL);

	return (error);
}



/* Add V9 record to dgram. */
int
export9_add(item_p item, struct netflow_v9_packet_opt *t, struct flow_entry *fle)
{
	size_t len = 0;
	struct netflow_v9_flowset_header fsh;
	struct netflow_v9_record_general rg;
	struct mbuf *m = NGI_M(item);
	uint16_t flow_type;
	struct flow_entry_data *fed;
#ifdef INET6	
	struct flow6_entry_data *fed6;
#endif
	if (t == NULL) {
		CTR0(KTR_NET, "ng_netflow: V9 export packet without tag!");
		return (0);
	}

	/* Prepare flow record */
	fed = (struct flow_entry_data *)&fle->f;
#ifdef INET6
	fed6 = (struct flow6_entry_data *)&fle->f;
#endif
	/* We can use flow_type field since fle6 offset is equal to fle */
	flow_type = fed->r.flow_type;

	switch (flow_type) {
	case NETFLOW_V9_FLOW_V4_L4:
	{
		/* IPv4 TCP/UDP/[SCTP] */
		struct netflow_v9_record_ipv4_tcp *rec = &rg.rec.v4_tcp;
		
		rec->src_addr = fed->r.r_src.s_addr;
		rec->dst_addr = fed->r.r_dst.s_addr;
		rec->next_hop = fed->next_hop.s_addr;
		rec->i_ifx    = htons(fed->fle_i_ifx);
		rec->o_ifx    = htons(fed->fle_o_ifx);
		rec->i_packets  = htonl(fed->packets);
		rec->i_octets   = htonl(fed->bytes);
		rec->o_packets  = htonl(0);
		rec->o_octets   = htonl(0);
		rec->first    = htonl(MILLIUPTIME(fed->first));
		rec->last     = htonl(MILLIUPTIME(fed->last));
		rec->s_port   = fed->r.r_sport;
		rec->d_port   = fed->r.r_dport;
		rec->flags    = fed->tcp_flags;
		rec->prot     = fed->r.r_ip_p;
		rec->tos      = fed->r.r_tos;
		rec->dst_mask = fed->dst_mask;
		rec->src_mask = fed->src_mask;

		/* Not supported fields. */
		rec->src_as = rec->dst_as = 0;

		len = sizeof(struct netflow_v9_record_ipv4_tcp);
		break;
	}
#ifdef INET6	
	case NETFLOW_V9_FLOW_V6_L4:
	{
		/* IPv6 TCP/UDP/[SCTP] */
		struct netflow_v9_record_ipv6_tcp *rec = &rg.rec.v6_tcp;

		rec->src_addr = fed6->r.src.r_src6;
		rec->dst_addr = fed6->r.dst.r_dst6;
		rec->next_hop = fed6->n.next_hop6;
		rec->i_ifx    = htons(fed6->fle_i_ifx);
		rec->o_ifx    = htons(fed6->fle_o_ifx);
		rec->i_packets  = htonl(fed6->packets);
		rec->i_octets   = htonl(fed6->bytes);
		rec->o_packets  = htonl(0);
		rec->o_octets   = htonl(0);
		rec->first    = htonl(MILLIUPTIME(fed6->first));
		rec->last     = htonl(MILLIUPTIME(fed6->last));
		rec->s_port   = fed6->r.r_sport;
		rec->d_port   = fed6->r.r_dport;
		rec->flags    = fed6->tcp_flags;
		rec->prot     = fed6->r.r_ip_p;
		rec->tos      = fed6->r.r_tos;
		rec->dst_mask = fed6->dst_mask;
		rec->src_mask = fed6->src_mask;

		/* Not supported fields. */
		rec->src_as = rec->dst_as = 0;

		len = sizeof(struct netflow_v9_record_ipv6_tcp);
		break;
	}
#endif	
	default:
	{
		CTR1(KTR_NET, "export9_add(): Don't know what to do with %d flow type!", flow_type);
		return (0);
	}
	}

	/* Check if new records has the same template */
	if (flow_type != t->flow_type) {
		/* close old flowset */
		if (t->flow_type != 0)
			close_flowset(m, t);

		t->flow_type = flow_type;
		t->flow_header = m_pktlen(m);

		/* Generate data flowset ID */
		fsh.id = htons(NETFLOW_V9_MAX_RESERVED_FLOWSET + flow_type);
		fsh.length = 0;

		/* m_append should not fail since all data is already allocated */
		if (m_append(m, sizeof(fsh), (void *)&fsh) != 1)
			panic("ng_netflow: m_append() failed");
		
	}

	if (m_append(m, len, (void *)&rg.rec) != 1)
		panic("ng_netflow: m_append() failed");

	t->count++;

	if (m_pktlen(m) + sizeof(struct netflow_v9_record_general) + sizeof(struct netflow_v9_flowset_header) >= _NETFLOW_V9_MAX_SIZE(t->mtu))
		return (1); /* end of datagram */
	return (0);
}

/*
 * Detach export datagram from fib instance, if there is any.
 * If there is no, allocate a new one.
 */
item_p
get_export9_dgram(priv_p priv, fib_export_p fe, struct netflow_v9_packet_opt **tt)
{
	item_p	item = NULL;
	struct netflow_v9_packet_opt *t = NULL;

	mtx_lock(&fe->export9_mtx);
	if (fe->exp.item9 != NULL) {
		item = fe->exp.item9;
		fe->exp.item9 = NULL;
		t = fe->exp.item9_opt;
		fe->exp.item9_opt = NULL;
	}
	mtx_unlock(&fe->export9_mtx);

	if (item == NULL) {
		struct netflow_v9_export_dgram *dgram;
		struct mbuf *m;
		uint16_t mtu = priv->mtu;

		/* Allocate entire packet at once, allowing easy m_append() calls */
		m = m_getm(NULL, mtu, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return (NULL);

		t = malloc(sizeof(struct netflow_v9_packet_opt), M_NETFLOW_GENERAL, M_NOWAIT | M_ZERO);
		if (t == NULL) {
			m_free(m);
			return (NULL);
		}

		item = ng_package_data(m, NG_NOFLAGS);
		if (item == NULL) {
			free(t, M_NETFLOW_GENERAL);
			return (NULL);
		}

		dgram = mtod(m, struct netflow_v9_export_dgram *);
		dgram->header.count = 0;
		dgram->header.version = htons(NETFLOW_V9);
		/* Set mbuf current data length */
		m->m_len = m->m_pkthdr.len = sizeof(struct netflow_v9_header);

		t->count = 0;
		t->mtu = mtu;
		t->flow_header = m->m_len;
	
		/*
		 * Check if we need to insert templates into packet
		 */
		
		struct netflow_v9_flowset_header	*fl;
	
		if ((time_uptime >= priv->templ_time + fe->templ_last_ts) ||
				(fe->sent_packets >= priv->templ_packets + fe->templ_last_pkt)) {

			fe->templ_last_ts = time_uptime;
			fe->templ_last_pkt = fe->sent_packets;

			fl = priv->v9_flowsets[0];
			m_append(m, ntohs(fl->length), (void *)fl);
			t->flow_header = m->m_len;
			t->count += priv->flowset_records[0];
		}

	}

	*tt = t;
	return (item);
}

/*
 * Re-attach incomplete datagram back to fib instance.
 * If there is already another one, then send incomplete.
 */
void
return_export9_dgram(priv_p priv, fib_export_p fe, item_p item, struct netflow_v9_packet_opt *t, int flags)
{
	/*
	 * It may happen on SMP, that some thread has already
	 * put its item there, in this case we bail out and
	 * send what we have to collector.
	 */
	mtx_lock(&fe->export9_mtx);
	if (fe->exp.item9 == NULL) {
		fe->exp.item9 = item;
		fe->exp.item9_opt = t;
		mtx_unlock(&fe->export9_mtx);
	} else {
		mtx_unlock(&fe->export9_mtx);
		export9_send(priv, fe, item, t, flags);
	}
}

/* Allocate memory and set up flow cache */
void
ng_netflow_v9_cache_init(priv_p priv)
{
	generate_v9_templates(priv);

	priv->templ_time = NETFLOW_V9_MAX_TIME_TEMPL;
	priv->templ_packets = NETFLOW_V9_MAX_PACKETS_TEMPL;
	priv->mtu = BASE_MTU;
}

/* Free all flow cache memory. Called from ng_netflow_cache_flush() */
void
ng_netflow_v9_cache_flush(priv_p priv)
{
	int i;

	/* Free flowsets*/
	for (i = 0; i < priv->flowsets_count; i++)
		free(priv->v9_flowsets[i], M_NETFLOW_GENERAL);
}

/* Get a snapshot of NetFlow v9 settings */
void
ng_netflow_copyv9info(priv_p priv, struct ng_netflow_v9info *i)
{

	i->templ_time = priv->templ_time;
	i->templ_packets = priv->templ_packets;
	i->mtu = priv->mtu;
}

