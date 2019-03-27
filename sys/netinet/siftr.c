/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009
 * 	Swinburne University of Technology, Melbourne, Australia.
 * Copyright (c) 2009-2010, The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by Lawrence Stewart under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/******************************************************
 * Statistical Information For TCP Research (SIFTR)
 *
 * A FreeBSD kernel module that adds very basic intrumentation to the
 * TCP stack, allowing internal stats to be recorded to a log file
 * for experimental, debugging and performance analysis purposes.
 *
 * SIFTR was first released in 2007 by James Healy and Lawrence Stewart whilst
 * working on the NewTCP research project at Swinburne University of
 * Technology's Centre for Advanced Internet Architectures, Melbourne,
 * Australia, which was made possible in part by a grant from the Cisco
 * University Research Program Fund at Community Foundation Silicon Valley.
 * More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 *
 * Work on SIFTR v1.2.x was sponsored by the FreeBSD Foundation as part of
 * the "Enhancing the FreeBSD TCP Implementation" project 2008-2009.
 * More details are available at:
 *   http://www.freebsdfoundation.org/
 *   http://caia.swin.edu.au/freebsd/etcp09/
 *
 * Lawrence Stewart is the current maintainer, and all contact regarding
 * SIFTR should be directed to him via email: lastewart@swin.edu.au
 *
 * Initial release date: June 2007
 * Most recent update: September 2010
 ******************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/alq.h>
#include <sys/errno.h>
#include <sys/eventhandler.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/pfil.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>

#ifdef SIFTR_IPV6
#include <netinet/ip6.h>
#include <netinet/ip6_var.h>
#include <netinet6/in6_pcb.h>
#endif /* SIFTR_IPV6 */

#include <machine/in_cksum.h>

/*
 * Three digit version number refers to X.Y.Z where:
 * X is the major version number
 * Y is bumped to mark backwards incompatible changes
 * Z is bumped to mark backwards compatible changes
 */
#define V_MAJOR		1
#define V_BACKBREAK	2
#define V_BACKCOMPAT	4
#define MODVERSION	__CONCAT(V_MAJOR, __CONCAT(V_BACKBREAK, V_BACKCOMPAT))
#define MODVERSION_STR	__XSTRING(V_MAJOR) "." __XSTRING(V_BACKBREAK) "." \
    __XSTRING(V_BACKCOMPAT)

#define HOOK 0
#define UNHOOK 1
#define SIFTR_EXPECTED_MAX_TCP_FLOWS 65536
#define SYS_NAME "FreeBSD"
#define PACKET_TAG_SIFTR 100
#define PACKET_COOKIE_SIFTR 21749576
#define SIFTR_LOG_FILE_MODE 0644
#define SIFTR_DISABLE 0
#define SIFTR_ENABLE 1

/*
 * Hard upper limit on the length of log messages. Bump this up if you add new
 * data fields such that the line length could exceed the below value.
 */
#define MAX_LOG_MSG_LEN 200
/* XXX: Make this a sysctl tunable. */
#define SIFTR_ALQ_BUFLEN (1000*MAX_LOG_MSG_LEN)

/*
 * 1 byte for IP version
 * IPv4: src/dst IP (4+4) + src/dst port (2+2) = 12 bytes
 * IPv6: src/dst IP (16+16) + src/dst port (2+2) = 36 bytes
 */
#ifdef SIFTR_IPV6
#define FLOW_KEY_LEN 37
#else
#define FLOW_KEY_LEN 13
#endif

#ifdef SIFTR_IPV6
#define SIFTR_IPMODE 6
#else
#define SIFTR_IPMODE 4
#endif

/* useful macros */
#define UPPER_SHORT(X)	(((X) & 0xFFFF0000) >> 16)
#define LOWER_SHORT(X)	((X) & 0x0000FFFF)

#define FIRST_OCTET(X)	(((X) & 0xFF000000) >> 24)
#define SECOND_OCTET(X)	(((X) & 0x00FF0000) >> 16)
#define THIRD_OCTET(X)	(((X) & 0x0000FF00) >> 8)
#define FOURTH_OCTET(X)	((X) & 0x000000FF)

static MALLOC_DEFINE(M_SIFTR, "siftr", "dynamic memory used by SIFTR");
static MALLOC_DEFINE(M_SIFTR_PKTNODE, "siftr_pktnode",
    "SIFTR pkt_node struct");
static MALLOC_DEFINE(M_SIFTR_HASHNODE, "siftr_hashnode",
    "SIFTR flow_hash_node struct");

/* Used as links in the pkt manager queue. */
struct pkt_node {
	/* Timestamp of pkt as noted in the pfil hook. */
	struct timeval		tval;
	/* Direction pkt is travelling. */
	enum {
		DIR_IN = 0,
		DIR_OUT = 1,
	}			direction;
	/* IP version pkt_node relates to; either INP_IPV4 or INP_IPV6. */
	uint8_t			ipver;
	/* Hash of the pkt which triggered the log message. */
	uint32_t		hash;
	/* Local/foreign IP address. */
#ifdef SIFTR_IPV6
	uint32_t		ip_laddr[4];
	uint32_t		ip_faddr[4];
#else
	uint8_t			ip_laddr[4];
	uint8_t			ip_faddr[4];
#endif
	/* Local TCP port. */
	uint16_t		tcp_localport;
	/* Foreign TCP port. */
	uint16_t		tcp_foreignport;
	/* Congestion Window (bytes). */
	u_long			snd_cwnd;
	/* Sending Window (bytes). */
	u_long			snd_wnd;
	/* Receive Window (bytes). */
	u_long			rcv_wnd;
	/* Unused (was: Bandwidth Controlled Window (bytes)). */
	u_long			snd_bwnd;
	/* Slow Start Threshold (bytes). */
	u_long			snd_ssthresh;
	/* Current state of the TCP FSM. */
	int			conn_state;
	/* Max Segment Size (bytes). */
	u_int			max_seg_size;
	/*
	 * Smoothed RTT stored as found in the TCP control block
	 * in units of (TCP_RTT_SCALE*hz).
	 */
	int			smoothed_rtt;
	/* Is SACK enabled? */
	u_char			sack_enabled;
	/* Window scaling for snd window. */
	u_char			snd_scale;
	/* Window scaling for recv window. */
	u_char			rcv_scale;
	/* TCP control block flags. */
	u_int			flags;
	/* Retransmit timeout length. */
	int			rxt_length;
	/* Size of the TCP send buffer in bytes. */
	u_int			snd_buf_hiwater;
	/* Current num bytes in the send socket buffer. */
	u_int			snd_buf_cc;
	/* Size of the TCP receive buffer in bytes. */
	u_int			rcv_buf_hiwater;
	/* Current num bytes in the receive socket buffer. */
	u_int			rcv_buf_cc;
	/* Number of bytes inflight that we are waiting on ACKs for. */
	u_int			sent_inflight_bytes;
	/* Number of segments currently in the reassembly queue. */
	int			t_segqlen;
	/* Flowid for the connection. */
	u_int			flowid;	
	/* Flow type for the connection. */
	u_int			flowtype;	
	/* Link to next pkt_node in the list. */
	STAILQ_ENTRY(pkt_node)	nodes;
};

struct flow_hash_node
{
	uint16_t counter;
	uint8_t key[FLOW_KEY_LEN];
	LIST_ENTRY(flow_hash_node) nodes;
};

struct siftr_stats
{
	/* # TCP pkts seen by the SIFTR PFIL hooks, including any skipped. */
	uint64_t n_in;
	uint64_t n_out;
	/* # pkts skipped due to failed malloc calls. */
	uint32_t nskip_in_malloc;
	uint32_t nskip_out_malloc;
	/* # pkts skipped due to failed mtx acquisition. */
	uint32_t nskip_in_mtx;
	uint32_t nskip_out_mtx;
	/* # pkts skipped due to failed inpcb lookups. */
	uint32_t nskip_in_inpcb;
	uint32_t nskip_out_inpcb;
	/* # pkts skipped due to failed tcpcb lookups. */
	uint32_t nskip_in_tcpcb;
	uint32_t nskip_out_tcpcb;
	/* # pkts skipped due to stack reinjection. */
	uint32_t nskip_in_dejavu;
	uint32_t nskip_out_dejavu;
};

DPCPU_DEFINE_STATIC(struct siftr_stats, ss);

static volatile unsigned int siftr_exit_pkt_manager_thread = 0;
static unsigned int siftr_enabled = 0;
static unsigned int siftr_pkts_per_log = 1;
static unsigned int siftr_generate_hashes = 0;
static uint16_t     siftr_port_filter = 0;
/* static unsigned int siftr_binary_log = 0; */
static char siftr_logfile[PATH_MAX] = "/var/log/siftr.log";
static char siftr_logfile_shadow[PATH_MAX] = "/var/log/siftr.log";
static u_long siftr_hashmask;
STAILQ_HEAD(pkthead, pkt_node) pkt_queue = STAILQ_HEAD_INITIALIZER(pkt_queue);
LIST_HEAD(listhead, flow_hash_node) *counter_hash;
static int wait_for_pkt;
static struct alq *siftr_alq = NULL;
static struct mtx siftr_pkt_queue_mtx;
static struct mtx siftr_pkt_mgr_mtx;
static struct thread *siftr_pkt_manager_thr = NULL;
static char direction[2] = {'i','o'};

/* Required function prototypes. */
static int siftr_sysctl_enabled_handler(SYSCTL_HANDLER_ARGS);
static int siftr_sysctl_logfile_name_handler(SYSCTL_HANDLER_ARGS);


/* Declare the net.inet.siftr sysctl tree and populate it. */

SYSCTL_DECL(_net_inet_siftr);

SYSCTL_NODE(_net_inet, OID_AUTO, siftr, CTLFLAG_RW, NULL,
    "siftr related settings");

SYSCTL_PROC(_net_inet_siftr, OID_AUTO, enabled, CTLTYPE_UINT|CTLFLAG_RW,
    &siftr_enabled, 0, &siftr_sysctl_enabled_handler, "IU",
    "switch siftr module operations on/off");

SYSCTL_PROC(_net_inet_siftr, OID_AUTO, logfile, CTLTYPE_STRING|CTLFLAG_RW,
    &siftr_logfile_shadow, sizeof(siftr_logfile_shadow), &siftr_sysctl_logfile_name_handler,
    "A", "file to save siftr log messages to");

SYSCTL_UINT(_net_inet_siftr, OID_AUTO, ppl, CTLFLAG_RW,
    &siftr_pkts_per_log, 1,
    "number of packets between generating a log message");

SYSCTL_UINT(_net_inet_siftr, OID_AUTO, genhashes, CTLFLAG_RW,
    &siftr_generate_hashes, 0,
    "enable packet hash generation");

SYSCTL_U16(_net_inet_siftr, OID_AUTO, port_filter, CTLFLAG_RW,
    &siftr_port_filter, 0,
    "enable packet filter on a TCP port");

/* XXX: TODO
SYSCTL_UINT(_net_inet_siftr, OID_AUTO, binary, CTLFLAG_RW,
    &siftr_binary_log, 0,
    "write log files in binary instead of ascii");
*/


/* Begin functions. */

static void
siftr_process_pkt(struct pkt_node * pkt_node)
{
	struct flow_hash_node *hash_node;
	struct listhead *counter_list;
	struct siftr_stats *ss;
	struct ale *log_buf;
	uint8_t key[FLOW_KEY_LEN];
	uint8_t found_match, key_offset;

	hash_node = NULL;
	ss = DPCPU_PTR(ss);
	found_match = 0;
	key_offset = 1;

	/*
	 * Create the key that will be used to create a hash index
	 * into our hash table. Our key consists of:
	 * ipversion, localip, localport, foreignip, foreignport
	 */
	key[0] = pkt_node->ipver;
	memcpy(key + key_offset, &pkt_node->ip_laddr,
	    sizeof(pkt_node->ip_laddr));
	key_offset += sizeof(pkt_node->ip_laddr);
	memcpy(key + key_offset, &pkt_node->tcp_localport,
	    sizeof(pkt_node->tcp_localport));
	key_offset += sizeof(pkt_node->tcp_localport);
	memcpy(key + key_offset, &pkt_node->ip_faddr,
	    sizeof(pkt_node->ip_faddr));
	key_offset += sizeof(pkt_node->ip_faddr);
	memcpy(key + key_offset, &pkt_node->tcp_foreignport,
	    sizeof(pkt_node->tcp_foreignport));

	counter_list = counter_hash +
	    (hash32_buf(key, sizeof(key), 0) & siftr_hashmask);

	/*
	 * If the list is not empty i.e. the hash index has
	 * been used by another flow previously.
	 */
	if (LIST_FIRST(counter_list) != NULL) {
		/*
		 * Loop through the hash nodes in the list.
		 * There should normally only be 1 hash node in the list,
		 * except if there have been collisions at the hash index
		 * computed by hash32_buf().
		 */
		LIST_FOREACH(hash_node, counter_list, nodes) {
			/*
			 * Check if the key for the pkt we are currently
			 * processing is the same as the key stored in the
			 * hash node we are currently processing.
			 * If they are the same, then we've found the
			 * hash node that stores the counter for the flow
			 * the pkt belongs to.
			 */
			if (memcmp(hash_node->key, key, sizeof(key)) == 0) {
				found_match = 1;
				break;
			}
		}
	}

	/* If this flow hash hasn't been seen before or we have a collision. */
	if (hash_node == NULL || !found_match) {
		/* Create a new hash node to store the flow's counter. */
		hash_node = malloc(sizeof(struct flow_hash_node),
		    M_SIFTR_HASHNODE, M_WAITOK);

		if (hash_node != NULL) {
			/* Initialise our new hash node list entry. */
			hash_node->counter = 0;
			memcpy(hash_node->key, key, sizeof(key));
			LIST_INSERT_HEAD(counter_list, hash_node, nodes);
		} else {
			/* Malloc failed. */
			if (pkt_node->direction == DIR_IN)
				ss->nskip_in_malloc++;
			else
				ss->nskip_out_malloc++;

			return;
		}
	} else if (siftr_pkts_per_log > 1) {
		/*
		 * Taking the remainder of the counter divided
		 * by the current value of siftr_pkts_per_log
		 * and storing that in counter provides a neat
		 * way to modulate the frequency of log
		 * messages being written to the log file.
		 */
		hash_node->counter = (hash_node->counter + 1) %
		    siftr_pkts_per_log;

		/*
		 * If we have not seen enough packets since the last time
		 * we wrote a log message for this connection, return.
		 */
		if (hash_node->counter > 0)
			return;
	}

	log_buf = alq_getn(siftr_alq, MAX_LOG_MSG_LEN, ALQ_WAITOK);

	if (log_buf == NULL)
		return; /* Should only happen if the ALQ is shutting down. */

#ifdef SIFTR_IPV6
	pkt_node->ip_laddr[3] = ntohl(pkt_node->ip_laddr[3]);
	pkt_node->ip_faddr[3] = ntohl(pkt_node->ip_faddr[3]);

	if (pkt_node->ipver == INP_IPV6) { /* IPv6 packet */
		pkt_node->ip_laddr[0] = ntohl(pkt_node->ip_laddr[0]);
		pkt_node->ip_laddr[1] = ntohl(pkt_node->ip_laddr[1]);
		pkt_node->ip_laddr[2] = ntohl(pkt_node->ip_laddr[2]);
		pkt_node->ip_faddr[0] = ntohl(pkt_node->ip_faddr[0]);
		pkt_node->ip_faddr[1] = ntohl(pkt_node->ip_faddr[1]);
		pkt_node->ip_faddr[2] = ntohl(pkt_node->ip_faddr[2]);

		/* Construct an IPv6 log message. */
		log_buf->ae_bytesused = snprintf(log_buf->ae_data,
		    MAX_LOG_MSG_LEN,
		    "%c,0x%08x,%zd.%06ld,%x:%x:%x:%x:%x:%x:%x:%x,%u,%x:%x:%x:"
		    "%x:%x:%x:%x:%x,%u,%ld,%ld,%ld,%ld,%ld,%u,%u,%u,%u,%u,%u,"
		    "%u,%d,%u,%u,%u,%u,%u,%u,%u,%u\n",
		    direction[pkt_node->direction],
		    pkt_node->hash,
		    pkt_node->tval.tv_sec,
		    pkt_node->tval.tv_usec,
		    UPPER_SHORT(pkt_node->ip_laddr[0]),
		    LOWER_SHORT(pkt_node->ip_laddr[0]),
		    UPPER_SHORT(pkt_node->ip_laddr[1]),
		    LOWER_SHORT(pkt_node->ip_laddr[1]),
		    UPPER_SHORT(pkt_node->ip_laddr[2]),
		    LOWER_SHORT(pkt_node->ip_laddr[2]),
		    UPPER_SHORT(pkt_node->ip_laddr[3]),
		    LOWER_SHORT(pkt_node->ip_laddr[3]),
		    ntohs(pkt_node->tcp_localport),
		    UPPER_SHORT(pkt_node->ip_faddr[0]),
		    LOWER_SHORT(pkt_node->ip_faddr[0]),
		    UPPER_SHORT(pkt_node->ip_faddr[1]),
		    LOWER_SHORT(pkt_node->ip_faddr[1]),
		    UPPER_SHORT(pkt_node->ip_faddr[2]),
		    LOWER_SHORT(pkt_node->ip_faddr[2]),
		    UPPER_SHORT(pkt_node->ip_faddr[3]),
		    LOWER_SHORT(pkt_node->ip_faddr[3]),
		    ntohs(pkt_node->tcp_foreignport),
		    pkt_node->snd_ssthresh,
		    pkt_node->snd_cwnd,
		    pkt_node->snd_bwnd,
		    pkt_node->snd_wnd,
		    pkt_node->rcv_wnd,
		    pkt_node->snd_scale,
		    pkt_node->rcv_scale,
		    pkt_node->conn_state,
		    pkt_node->max_seg_size,
		    pkt_node->smoothed_rtt,
		    pkt_node->sack_enabled,
		    pkt_node->flags,
		    pkt_node->rxt_length,
		    pkt_node->snd_buf_hiwater,
		    pkt_node->snd_buf_cc,
		    pkt_node->rcv_buf_hiwater,
		    pkt_node->rcv_buf_cc,
		    pkt_node->sent_inflight_bytes,
		    pkt_node->t_segqlen,
		    pkt_node->flowid,
		    pkt_node->flowtype);
	} else { /* IPv4 packet */
		pkt_node->ip_laddr[0] = FIRST_OCTET(pkt_node->ip_laddr[3]);
		pkt_node->ip_laddr[1] = SECOND_OCTET(pkt_node->ip_laddr[3]);
		pkt_node->ip_laddr[2] = THIRD_OCTET(pkt_node->ip_laddr[3]);
		pkt_node->ip_laddr[3] = FOURTH_OCTET(pkt_node->ip_laddr[3]);
		pkt_node->ip_faddr[0] = FIRST_OCTET(pkt_node->ip_faddr[3]);
		pkt_node->ip_faddr[1] = SECOND_OCTET(pkt_node->ip_faddr[3]);
		pkt_node->ip_faddr[2] = THIRD_OCTET(pkt_node->ip_faddr[3]);
		pkt_node->ip_faddr[3] = FOURTH_OCTET(pkt_node->ip_faddr[3]);
#endif /* SIFTR_IPV6 */

		/* Construct an IPv4 log message. */
		log_buf->ae_bytesused = snprintf(log_buf->ae_data,
		    MAX_LOG_MSG_LEN,
		    "%c,0x%08x,%jd.%06ld,%u.%u.%u.%u,%u,%u.%u.%u.%u,%u,%ld,%ld,"
		    "%ld,%ld,%ld,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u\n",
		    direction[pkt_node->direction],
		    pkt_node->hash,
		    (intmax_t)pkt_node->tval.tv_sec,
		    pkt_node->tval.tv_usec,
		    pkt_node->ip_laddr[0],
		    pkt_node->ip_laddr[1],
		    pkt_node->ip_laddr[2],
		    pkt_node->ip_laddr[3],
		    ntohs(pkt_node->tcp_localport),
		    pkt_node->ip_faddr[0],
		    pkt_node->ip_faddr[1],
		    pkt_node->ip_faddr[2],
		    pkt_node->ip_faddr[3],
		    ntohs(pkt_node->tcp_foreignport),
		    pkt_node->snd_ssthresh,
		    pkt_node->snd_cwnd,
		    pkt_node->snd_bwnd,
		    pkt_node->snd_wnd,
		    pkt_node->rcv_wnd,
		    pkt_node->snd_scale,
		    pkt_node->rcv_scale,
		    pkt_node->conn_state,
		    pkt_node->max_seg_size,
		    pkt_node->smoothed_rtt,
		    pkt_node->sack_enabled,
		    pkt_node->flags,
		    pkt_node->rxt_length,
		    pkt_node->snd_buf_hiwater,
		    pkt_node->snd_buf_cc,
		    pkt_node->rcv_buf_hiwater,
		    pkt_node->rcv_buf_cc,
		    pkt_node->sent_inflight_bytes,
		    pkt_node->t_segqlen,
		    pkt_node->flowid,
		    pkt_node->flowtype);
#ifdef SIFTR_IPV6
	}
#endif

	alq_post_flags(siftr_alq, log_buf, 0);
}


static void
siftr_pkt_manager_thread(void *arg)
{
	STAILQ_HEAD(pkthead, pkt_node) tmp_pkt_queue =
	    STAILQ_HEAD_INITIALIZER(tmp_pkt_queue);
	struct pkt_node *pkt_node, *pkt_node_temp;
	uint8_t draining;

	draining = 2;

	mtx_lock(&siftr_pkt_mgr_mtx);

	/* draining == 0 when queue has been flushed and it's safe to exit. */
	while (draining) {
		/*
		 * Sleep until we are signalled to wake because thread has
		 * been told to exit or until 1 tick has passed.
		 */
		mtx_sleep(&wait_for_pkt, &siftr_pkt_mgr_mtx, PWAIT, "pktwait",
		    1);

		/* Gain exclusive access to the pkt_node queue. */
		mtx_lock(&siftr_pkt_queue_mtx);

		/*
		 * Move pkt_queue to tmp_pkt_queue, which leaves
		 * pkt_queue empty and ready to receive more pkt_nodes.
		 */
		STAILQ_CONCAT(&tmp_pkt_queue, &pkt_queue);

		/*
		 * We've finished making changes to the list. Unlock it
		 * so the pfil hooks can continue queuing pkt_nodes.
		 */
		mtx_unlock(&siftr_pkt_queue_mtx);

		/*
		 * We can't hold a mutex whilst calling siftr_process_pkt
		 * because ALQ might sleep waiting for buffer space.
		 */
		mtx_unlock(&siftr_pkt_mgr_mtx);

		/* Flush all pkt_nodes to the log file. */
		STAILQ_FOREACH_SAFE(pkt_node, &tmp_pkt_queue, nodes,
		    pkt_node_temp) {
			siftr_process_pkt(pkt_node);
			STAILQ_REMOVE_HEAD(&tmp_pkt_queue, nodes);
			free(pkt_node, M_SIFTR_PKTNODE);
		}

		KASSERT(STAILQ_EMPTY(&tmp_pkt_queue),
		    ("SIFTR tmp_pkt_queue not empty after flush"));

		mtx_lock(&siftr_pkt_mgr_mtx);

		/*
		 * If siftr_exit_pkt_manager_thread gets set during the window
		 * where we are draining the tmp_pkt_queue above, there might
		 * still be pkts in pkt_queue that need to be drained.
		 * Allow one further iteration to occur after
		 * siftr_exit_pkt_manager_thread has been set to ensure
		 * pkt_queue is completely empty before we kill the thread.
		 *
		 * siftr_exit_pkt_manager_thread is set only after the pfil
		 * hooks have been removed, so only 1 extra iteration
		 * is needed to drain the queue.
		 */
		if (siftr_exit_pkt_manager_thread)
			draining--;
	}

	mtx_unlock(&siftr_pkt_mgr_mtx);

	/* Calls wakeup on this thread's struct thread ptr. */
	kthread_exit();
}


static uint32_t
hash_pkt(struct mbuf *m, uint32_t offset)
{
	uint32_t hash;

	hash = 0;

	while (m != NULL && offset > m->m_len) {
		/*
		 * The IP packet payload does not start in this mbuf, so
		 * need to figure out which mbuf it starts in and what offset
		 * into the mbuf's data region the payload starts at.
		 */
		offset -= m->m_len;
		m = m->m_next;
	}

	while (m != NULL) {
		/* Ensure there is data in the mbuf */
		if ((m->m_len - offset) > 0)
			hash = hash32_buf(m->m_data + offset,
			    m->m_len - offset, hash);

		m = m->m_next;
		offset = 0;
        }

	return (hash);
}


/*
 * Check if a given mbuf has the SIFTR mbuf tag. If it does, log the fact that
 * it's a reinjected packet and return. If it doesn't, tag the mbuf and return.
 * Return value >0 means the caller should skip processing this mbuf.
 */
static inline int
siftr_chkreinject(struct mbuf *m, int dir, struct siftr_stats *ss)
{
	if (m_tag_locate(m, PACKET_COOKIE_SIFTR, PACKET_TAG_SIFTR, NULL)
	    != NULL) {
		if (dir == PFIL_IN)
			ss->nskip_in_dejavu++;
		else
			ss->nskip_out_dejavu++;

		return (1);
	} else {
		struct m_tag *tag = m_tag_alloc(PACKET_COOKIE_SIFTR,
		    PACKET_TAG_SIFTR, 0, M_NOWAIT);
		if (tag == NULL) {
			if (dir == PFIL_IN)
				ss->nskip_in_malloc++;
			else
				ss->nskip_out_malloc++;

			return (1);
		}

		m_tag_prepend(m, tag);
	}

	return (0);
}


/*
 * Look up an inpcb for a packet. Return the inpcb pointer if found, or NULL
 * otherwise.
 */
static inline struct inpcb *
siftr_findinpcb(int ipver, struct ip *ip, struct mbuf *m, uint16_t sport,
    uint16_t dport, int dir, struct siftr_stats *ss)
{
	struct inpcb *inp;

	/* We need the tcbinfo lock. */
	INP_INFO_WUNLOCK_ASSERT(&V_tcbinfo);

	if (dir == PFIL_IN)
		inp = (ipver == INP_IPV4 ?
		    in_pcblookup(&V_tcbinfo, ip->ip_src, sport, ip->ip_dst,
		    dport, INPLOOKUP_RLOCKPCB, m->m_pkthdr.rcvif)
		    :
#ifdef SIFTR_IPV6
		    in6_pcblookup(&V_tcbinfo,
		    &((struct ip6_hdr *)ip)->ip6_src, sport,
		    &((struct ip6_hdr *)ip)->ip6_dst, dport, INPLOOKUP_RLOCKPCB,
		    m->m_pkthdr.rcvif)
#else
		    NULL
#endif
		    );

	else
		inp = (ipver == INP_IPV4 ?
		    in_pcblookup(&V_tcbinfo, ip->ip_dst, dport, ip->ip_src,
		    sport, INPLOOKUP_RLOCKPCB, m->m_pkthdr.rcvif)
		    :
#ifdef SIFTR_IPV6
		    in6_pcblookup(&V_tcbinfo,
		    &((struct ip6_hdr *)ip)->ip6_dst, dport,
		    &((struct ip6_hdr *)ip)->ip6_src, sport, INPLOOKUP_RLOCKPCB,
		    m->m_pkthdr.rcvif)
#else
		    NULL
#endif
		    );

	/* If we can't find the inpcb, bail. */
	if (inp == NULL) {
		if (dir == PFIL_IN)
			ss->nskip_in_inpcb++;
		else
			ss->nskip_out_inpcb++;
	}

	return (inp);
}


static inline void
siftr_siftdata(struct pkt_node *pn, struct inpcb *inp, struct tcpcb *tp,
    int ipver, int dir, int inp_locally_locked)
{
#ifdef SIFTR_IPV6
	if (ipver == INP_IPV4) {
		pn->ip_laddr[3] = inp->inp_laddr.s_addr;
		pn->ip_faddr[3] = inp->inp_faddr.s_addr;
#else
		*((uint32_t *)pn->ip_laddr) = inp->inp_laddr.s_addr;
		*((uint32_t *)pn->ip_faddr) = inp->inp_faddr.s_addr;
#endif
#ifdef SIFTR_IPV6
	} else {
		pn->ip_laddr[0] = inp->in6p_laddr.s6_addr32[0];
		pn->ip_laddr[1] = inp->in6p_laddr.s6_addr32[1];
		pn->ip_laddr[2] = inp->in6p_laddr.s6_addr32[2];
		pn->ip_laddr[3] = inp->in6p_laddr.s6_addr32[3];
		pn->ip_faddr[0] = inp->in6p_faddr.s6_addr32[0];
		pn->ip_faddr[1] = inp->in6p_faddr.s6_addr32[1];
		pn->ip_faddr[2] = inp->in6p_faddr.s6_addr32[2];
		pn->ip_faddr[3] = inp->in6p_faddr.s6_addr32[3];
	}
#endif
	pn->tcp_localport = inp->inp_lport;
	pn->tcp_foreignport = inp->inp_fport;
	pn->snd_cwnd = tp->snd_cwnd;
	pn->snd_wnd = tp->snd_wnd;
	pn->rcv_wnd = tp->rcv_wnd;
	pn->snd_bwnd = 0;		/* Unused, kept for compat. */
	pn->snd_ssthresh = tp->snd_ssthresh;
	pn->snd_scale = tp->snd_scale;
	pn->rcv_scale = tp->rcv_scale;
	pn->conn_state = tp->t_state;
	pn->max_seg_size = tp->t_maxseg;
	pn->smoothed_rtt = tp->t_srtt;
	pn->sack_enabled = (tp->t_flags & TF_SACK_PERMIT) != 0;
	pn->flags = tp->t_flags;
	pn->rxt_length = tp->t_rxtcur;
	pn->snd_buf_hiwater = inp->inp_socket->so_snd.sb_hiwat;
	pn->snd_buf_cc = sbused(&inp->inp_socket->so_snd);
	pn->rcv_buf_hiwater = inp->inp_socket->so_rcv.sb_hiwat;
	pn->rcv_buf_cc = sbused(&inp->inp_socket->so_rcv);
	pn->sent_inflight_bytes = tp->snd_max - tp->snd_una;
	pn->t_segqlen = tp->t_segqlen;
	pn->flowid = inp->inp_flowid;
	pn->flowtype = inp->inp_flowtype;

	/* We've finished accessing the tcb so release the lock. */
	if (inp_locally_locked)
		INP_RUNLOCK(inp);

	pn->ipver = ipver;
	pn->direction = (dir == PFIL_IN ? DIR_IN : DIR_OUT);

	/*
	 * Significantly more accurate than using getmicrotime(), but slower!
	 * Gives true microsecond resolution at the expense of a hit to
	 * maximum pps throughput processing when SIFTR is loaded and enabled.
	 */
	microtime(&pn->tval);
	TCP_PROBE1(siftr, &pn);

}


/*
 * pfil hook that is called for each IPv4 packet making its way through the
 * stack in either direction.
 * The pfil subsystem holds a non-sleepable mutex somewhere when
 * calling our hook function, so we can't sleep at all.
 * It's very important to use the M_NOWAIT flag with all function calls
 * that support it so that they won't sleep, otherwise you get a panic.
 */
static pfil_return_t
siftr_chkpkt(struct mbuf **m, struct ifnet *ifp, int flags,
    void *ruleset __unused, struct inpcb *inp)
{
	struct pkt_node *pn;
	struct ip *ip;
	struct tcphdr *th;
	struct tcpcb *tp;
	struct siftr_stats *ss;
	unsigned int ip_hl;
	int inp_locally_locked, dir;

	inp_locally_locked = 0;
	dir = PFIL_DIR(flags);
	ss = DPCPU_PTR(ss);

	/*
	 * m_pullup is not required here because ip_{input|output}
	 * already do the heavy lifting for us.
	 */

	ip = mtod(*m, struct ip *);

	/* Only continue processing if the packet is TCP. */
	if (ip->ip_p != IPPROTO_TCP)
		goto ret;

	/*
	 * If a kernel subsystem reinjects packets into the stack, our pfil
	 * hook will be called multiple times for the same packet.
	 * Make sure we only process unique packets.
	 */
	if (siftr_chkreinject(*m, dir, ss))
		goto ret;

	if (dir == PFIL_IN)
		ss->n_in++;
	else
		ss->n_out++;

	/*
	 * Create a tcphdr struct starting at the correct offset
	 * in the IP packet. ip->ip_hl gives the ip header length
	 * in 4-byte words, so multiply it to get the size in bytes.
	 */
	ip_hl = (ip->ip_hl << 2);
	th = (struct tcphdr *)((caddr_t)ip + ip_hl);

	/*
	 * If the pfil hooks don't provide a pointer to the
	 * inpcb, we need to find it ourselves and lock it.
	 */
	if (!inp) {
		/* Find the corresponding inpcb for this pkt. */
		inp = siftr_findinpcb(INP_IPV4, ip, *m, th->th_sport,
		    th->th_dport, dir, ss);

		if (inp == NULL)
			goto ret;
		else
			inp_locally_locked = 1;
	}

	INP_LOCK_ASSERT(inp);

	/* Find the TCP control block that corresponds with this packet */
	tp = intotcpcb(inp);

	/*
	 * If we can't find the TCP control block (happens occasionaly for a
	 * packet sent during the shutdown phase of a TCP connection),
	 * or we're in the timewait state, bail
	 */
	if (tp == NULL || inp->inp_flags & INP_TIMEWAIT) {
		if (dir == PFIL_IN)
			ss->nskip_in_tcpcb++;
		else
			ss->nskip_out_tcpcb++;

		goto inp_unlock;
	}

	/*
	 * Only pkts selected by the tcp port filter
	 * can be inserted into the pkt_queue
	 */
	if ((siftr_port_filter != 0) && 
	    (siftr_port_filter != ntohs(inp->inp_lport)) &&
	    (siftr_port_filter != ntohs(inp->inp_fport))) {
		goto inp_unlock;
	}

	pn = malloc(sizeof(struct pkt_node), M_SIFTR_PKTNODE, M_NOWAIT|M_ZERO);

	if (pn == NULL) {
		if (dir == PFIL_IN)
			ss->nskip_in_malloc++;
		else
			ss->nskip_out_malloc++;

		goto inp_unlock;
	}

	siftr_siftdata(pn, inp, tp, INP_IPV4, dir, inp_locally_locked);

	if (siftr_generate_hashes) {
		if ((*m)->m_pkthdr.csum_flags & CSUM_TCP) {
			/*
			 * For outbound packets, the TCP checksum isn't
			 * calculated yet. This is a problem for our packet
			 * hashing as the receiver will calc a different hash
			 * to ours if we don't include the correct TCP checksum
			 * in the bytes being hashed. To work around this
			 * problem, we manually calc the TCP checksum here in
			 * software. We unset the CSUM_TCP flag so the lower
			 * layers don't recalc it.
			 */
			(*m)->m_pkthdr.csum_flags &= ~CSUM_TCP;

			/*
			 * Calculate the TCP checksum in software and assign
			 * to correct TCP header field, which will follow the
			 * packet mbuf down the stack. The trick here is that
			 * tcp_output() sets th->th_sum to the checksum of the
			 * pseudo header for us already. Because of the nature
			 * of the checksumming algorithm, we can sum over the
			 * entire IP payload (i.e. TCP header and data), which
			 * will include the already calculated pseduo header
			 * checksum, thus giving us the complete TCP checksum.
			 *
			 * To put it in simple terms, if checksum(1,2,3,4)=10,
			 * then checksum(1,2,3,4,5) == checksum(10,5).
			 * This property is what allows us to "cheat" and
			 * checksum only the IP payload which has the TCP
			 * th_sum field populated with the pseudo header's
			 * checksum, and not need to futz around checksumming
			 * pseudo header bytes and TCP header/data in one hit.
			 * Refer to RFC 1071 for more info.
			 *
			 * NB: in_cksum_skip(struct mbuf *m, int len, int skip)
			 * in_cksum_skip 2nd argument is NOT the number of
			 * bytes to read from the mbuf at "skip" bytes offset
			 * from the start of the mbuf (very counter intuitive!).
			 * The number of bytes to read is calculated internally
			 * by the function as len-skip i.e. to sum over the IP
			 * payload (TCP header + data) bytes, it is INCORRECT
			 * to call the function like this:
			 * in_cksum_skip(at, ip->ip_len - offset, offset)
			 * Rather, it should be called like this:
			 * in_cksum_skip(at, ip->ip_len, offset)
			 * which means read "ip->ip_len - offset" bytes from
			 * the mbuf cluster "at" at offset "offset" bytes from
			 * the beginning of the "at" mbuf's data pointer.
			 */
			th->th_sum = in_cksum_skip(*m, ntohs(ip->ip_len),
			    ip_hl);
		}

		/*
		 * XXX: Having to calculate the checksum in software and then
		 * hash over all bytes is really inefficient. Would be nice to
		 * find a way to create the hash and checksum in the same pass
		 * over the bytes.
		 */
		pn->hash = hash_pkt(*m, ip_hl);
	}

	mtx_lock(&siftr_pkt_queue_mtx);
	STAILQ_INSERT_TAIL(&pkt_queue, pn, nodes);
	mtx_unlock(&siftr_pkt_queue_mtx);
	goto ret;

inp_unlock:
	if (inp_locally_locked)
		INP_RUNLOCK(inp);

ret:
	return (PFIL_PASS);
}


#ifdef SIFTR_IPV6
static int
siftr_chkpkt6(struct mbuf **m, struct ifnet *ifp, int flags, struct inpcb *inp)
{
	struct pkt_node *pn;
	struct ip6_hdr *ip6;
	struct tcphdr *th;
	struct tcpcb *tp;
	struct siftr_stats *ss;
	unsigned int ip6_hl;
	int inp_locally_locked, dir;

	inp_locally_locked = 0;
	dir = PFIL_DIR(flags);
	ss = DPCPU_PTR(ss);

	/*
	 * m_pullup is not required here because ip6_{input|output}
	 * already do the heavy lifting for us.
	 */

	ip6 = mtod(*m, struct ip6_hdr *);

	/*
	 * Only continue processing if the packet is TCP
	 * XXX: We should follow the next header fields
	 * as shown on Pg 6 RFC 2460, but right now we'll
	 * only check pkts that have no extension headers.
	 */
	if (ip6->ip6_nxt != IPPROTO_TCP)
		goto ret6;

	/*
	 * If a kernel subsystem reinjects packets into the stack, our pfil
	 * hook will be called multiple times for the same packet.
	 * Make sure we only process unique packets.
	 */
	if (siftr_chkreinject(*m, dir, ss))
		goto ret6;

	if (dir == PFIL_IN)
		ss->n_in++;
	else
		ss->n_out++;

	ip6_hl = sizeof(struct ip6_hdr);

	/*
	 * Create a tcphdr struct starting at the correct offset
	 * in the ipv6 packet. ip->ip_hl gives the ip header length
	 * in 4-byte words, so multiply it to get the size in bytes.
	 */
	th = (struct tcphdr *)((caddr_t)ip6 + ip6_hl);

	/*
	 * For inbound packets, the pfil hooks don't provide a pointer to the
	 * inpcb, so we need to find it ourselves and lock it.
	 */
	if (!inp) {
		/* Find the corresponding inpcb for this pkt. */
		inp = siftr_findinpcb(INP_IPV6, (struct ip *)ip6, *m,
		    th->th_sport, th->th_dport, dir, ss);

		if (inp == NULL)
			goto ret6;
		else
			inp_locally_locked = 1;
	}

	/* Find the TCP control block that corresponds with this packet. */
	tp = intotcpcb(inp);

	/*
	 * If we can't find the TCP control block (happens occasionaly for a
	 * packet sent during the shutdown phase of a TCP connection),
	 * or we're in the timewait state, bail.
	 */
	if (tp == NULL || inp->inp_flags & INP_TIMEWAIT) {
		if (dir == PFIL_IN)
			ss->nskip_in_tcpcb++;
		else
			ss->nskip_out_tcpcb++;

		goto inp_unlock6;
	}

	/*
	 * Only pkts selected by the tcp port filter
	 * can be inserted into the pkt_queue
	 */
	if ((siftr_port_filter != 0) && 
	    (siftr_port_filter != ntohs(inp->inp_lport)) &&
	    (siftr_port_filter != ntohs(inp->inp_fport))) {
		goto inp_unlock6;
	}

	pn = malloc(sizeof(struct pkt_node), M_SIFTR_PKTNODE, M_NOWAIT|M_ZERO);

	if (pn == NULL) {
		if (dir == PFIL_IN)
			ss->nskip_in_malloc++;
		else
			ss->nskip_out_malloc++;

		goto inp_unlock6;
	}

	siftr_siftdata(pn, inp, tp, INP_IPV6, dir, inp_locally_locked);

	/* XXX: Figure out how to generate hashes for IPv6 packets. */

	mtx_lock(&siftr_pkt_queue_mtx);
	STAILQ_INSERT_TAIL(&pkt_queue, pn, nodes);
	mtx_unlock(&siftr_pkt_queue_mtx);
	goto ret6;

inp_unlock6:
	if (inp_locally_locked)
		INP_RUNLOCK(inp);

ret6:
	/* Returning 0 ensures pfil will not discard the pkt. */
	return (0);
}
#endif /* #ifdef SIFTR_IPV6 */

VNET_DEFINE_STATIC(pfil_hook_t, siftr_inet_hook);
#define	V_siftr_inet_hook	VNET(siftr_inet_hook)
#ifdef INET6
VNET_DEFINE_STATIC(pfil_hook_t, siftr_inet6_hook);
#define	V_siftr_inet6_hook	VNET(siftr_inet6_hook)
#endif
static int
siftr_pfil(int action)
{
	struct pfil_hook_args pha;
	struct pfil_link_args pla;

	pha.pa_version = PFIL_VERSION;
	pha.pa_flags = PFIL_IN | PFIL_OUT;
	pha.pa_modname = "siftr";
	pha.pa_ruleset = NULL;
	pha.pa_rulname = "default";

	pla.pa_version = PFIL_VERSION;
	pla.pa_flags = PFIL_IN | PFIL_OUT |
	    PFIL_HEADPTR | PFIL_HOOKPTR;

	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);

		if (action == HOOK) {
			pha.pa_func = siftr_chkpkt;
			pha.pa_type = PFIL_TYPE_IP4;
			V_siftr_inet_hook = pfil_add_hook(&pha);
			pla.pa_hook = V_siftr_inet_hook;
			pla.pa_head = V_inet_pfil_head;
			(void)pfil_link(&pla);
#ifdef SIFTR_IPV6
			pha.pa_func = siftr_chkpkt6;
			pha.pa_type = PFIL_TYPE_IP6;
			V_siftr_inet6_hook = pfil_add_hook(&pha);
			pla.pa_hook = V_siftr_inet6_hook;
			pla.pa_head = V_inet6_pfil_head;
			(void)pfil_link(&pla);
#endif
		} else if (action == UNHOOK) {
			pfil_remove_hook(V_siftr_inet_hook);
#ifdef SIFTR_IPV6
			pfil_remove_hook(V_siftr_inet6_hook);
#endif
		}
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK();

	return (0);
}


static int
siftr_sysctl_logfile_name_handler(SYSCTL_HANDLER_ARGS)
{
	struct alq *new_alq;
	int error;

	error = sysctl_handle_string(oidp, arg1, arg2, req);

	/* Check for error or same filename */
	if (error != 0 || req->newptr == NULL ||
	    strncmp(siftr_logfile, arg1, arg2) == 0)
		goto done;

	/* Filname changed */
	error = alq_open(&new_alq, arg1, curthread->td_ucred,
	    SIFTR_LOG_FILE_MODE, SIFTR_ALQ_BUFLEN, 0);
	if (error != 0)
		goto done;

	/*
	 * If disabled, siftr_alq == NULL so we simply close
	 * the alq as we've proved it can be opened.
	 * If enabled, close the existing alq and switch the old
	 * for the new.
	 */
	if (siftr_alq == NULL) {
		alq_close(new_alq);
	} else {
		alq_close(siftr_alq);
		siftr_alq = new_alq;
	}

	/* Update filename upon success */
	strlcpy(siftr_logfile, arg1, arg2);
done:
	return (error);
}

static int
siftr_manage_ops(uint8_t action)
{
	struct siftr_stats totalss;
	struct timeval tval;
	struct flow_hash_node *counter, *tmp_counter;
	struct sbuf *s;
	int i, key_index, error;
	uint32_t bytes_to_write, total_skipped_pkts;
	uint16_t lport, fport;
	uint8_t *key, ipver __unused;

#ifdef SIFTR_IPV6
	uint32_t laddr[4];
	uint32_t faddr[4];
#else
	uint8_t laddr[4];
	uint8_t faddr[4];
#endif

	error = 0;
	total_skipped_pkts = 0;

	/* Init an autosizing sbuf that initially holds 200 chars. */
	if ((s = sbuf_new(NULL, NULL, 200, SBUF_AUTOEXTEND)) == NULL)
		return (-1);

	if (action == SIFTR_ENABLE && siftr_pkt_manager_thr == NULL) {
		/*
		 * Create our alq
		 * XXX: We should abort if alq_open fails!
		 */
		alq_open(&siftr_alq, siftr_logfile, curthread->td_ucred,
		    SIFTR_LOG_FILE_MODE, SIFTR_ALQ_BUFLEN, 0);

		STAILQ_INIT(&pkt_queue);

		DPCPU_ZERO(ss);

		siftr_exit_pkt_manager_thread = 0;

		kthread_add(&siftr_pkt_manager_thread, NULL, NULL,
		    &siftr_pkt_manager_thr, RFNOWAIT, 0,
		    "siftr_pkt_manager_thr");

		siftr_pfil(HOOK);

		microtime(&tval);

		sbuf_printf(s,
		    "enable_time_secs=%jd\tenable_time_usecs=%06ld\t"
		    "siftrver=%s\thz=%u\ttcp_rtt_scale=%u\tsysname=%s\t"
		    "sysver=%u\tipmode=%u\n",
		    (intmax_t)tval.tv_sec, tval.tv_usec, MODVERSION_STR, hz,
		    TCP_RTT_SCALE, SYS_NAME, __FreeBSD_version, SIFTR_IPMODE);

		sbuf_finish(s);
		alq_writen(siftr_alq, sbuf_data(s), sbuf_len(s), ALQ_WAITOK);

	} else if (action == SIFTR_DISABLE && siftr_pkt_manager_thr != NULL) {
		/*
		 * Remove the pfil hook functions. All threads currently in
		 * the hook functions are allowed to exit before siftr_pfil()
		 * returns.
		 */
		siftr_pfil(UNHOOK);

		/* This will block until the pkt manager thread unlocks it. */
		mtx_lock(&siftr_pkt_mgr_mtx);

		/* Tell the pkt manager thread that it should exit now. */
		siftr_exit_pkt_manager_thread = 1;

		/*
		 * Wake the pkt_manager thread so it realises that
		 * siftr_exit_pkt_manager_thread == 1 and exits gracefully.
		 * The wakeup won't be delivered until we unlock
		 * siftr_pkt_mgr_mtx so this isn't racy.
		 */
		wakeup(&wait_for_pkt);

		/* Wait for the pkt_manager thread to exit. */
		mtx_sleep(siftr_pkt_manager_thr, &siftr_pkt_mgr_mtx, PWAIT,
		    "thrwait", 0);

		siftr_pkt_manager_thr = NULL;
		mtx_unlock(&siftr_pkt_mgr_mtx);

		totalss.n_in = DPCPU_VARSUM(ss, n_in);
		totalss.n_out = DPCPU_VARSUM(ss, n_out);
		totalss.nskip_in_malloc = DPCPU_VARSUM(ss, nskip_in_malloc);
		totalss.nskip_out_malloc = DPCPU_VARSUM(ss, nskip_out_malloc);
		totalss.nskip_in_mtx = DPCPU_VARSUM(ss, nskip_in_mtx);
		totalss.nskip_out_mtx = DPCPU_VARSUM(ss, nskip_out_mtx);
		totalss.nskip_in_tcpcb = DPCPU_VARSUM(ss, nskip_in_tcpcb);
		totalss.nskip_out_tcpcb = DPCPU_VARSUM(ss, nskip_out_tcpcb);
		totalss.nskip_in_inpcb = DPCPU_VARSUM(ss, nskip_in_inpcb);
		totalss.nskip_out_inpcb = DPCPU_VARSUM(ss, nskip_out_inpcb);

		total_skipped_pkts = totalss.nskip_in_malloc +
		    totalss.nskip_out_malloc + totalss.nskip_in_mtx +
		    totalss.nskip_out_mtx + totalss.nskip_in_tcpcb +
		    totalss.nskip_out_tcpcb + totalss.nskip_in_inpcb +
		    totalss.nskip_out_inpcb;

		microtime(&tval);

		sbuf_printf(s,
		    "disable_time_secs=%jd\tdisable_time_usecs=%06ld\t"
		    "num_inbound_tcp_pkts=%ju\tnum_outbound_tcp_pkts=%ju\t"
		    "total_tcp_pkts=%ju\tnum_inbound_skipped_pkts_malloc=%u\t"
		    "num_outbound_skipped_pkts_malloc=%u\t"
		    "num_inbound_skipped_pkts_mtx=%u\t"
		    "num_outbound_skipped_pkts_mtx=%u\t"
		    "num_inbound_skipped_pkts_tcpcb=%u\t"
		    "num_outbound_skipped_pkts_tcpcb=%u\t"
		    "num_inbound_skipped_pkts_inpcb=%u\t"
		    "num_outbound_skipped_pkts_inpcb=%u\t"
		    "total_skipped_tcp_pkts=%u\tflow_list=",
		    (intmax_t)tval.tv_sec,
		    tval.tv_usec,
		    (uintmax_t)totalss.n_in,
		    (uintmax_t)totalss.n_out,
		    (uintmax_t)(totalss.n_in + totalss.n_out),
		    totalss.nskip_in_malloc,
		    totalss.nskip_out_malloc,
		    totalss.nskip_in_mtx,
		    totalss.nskip_out_mtx,
		    totalss.nskip_in_tcpcb,
		    totalss.nskip_out_tcpcb,
		    totalss.nskip_in_inpcb,
		    totalss.nskip_out_inpcb,
		    total_skipped_pkts);

		/*
		 * Iterate over the flow hash, printing a summary of each
		 * flow seen and freeing any malloc'd memory.
		 * The hash consists of an array of LISTs (man 3 queue).
		 */
		for (i = 0; i <= siftr_hashmask; i++) {
			LIST_FOREACH_SAFE(counter, counter_hash + i, nodes,
			    tmp_counter) {
				key = counter->key;
				key_index = 1;

				ipver = key[0];

				memcpy(laddr, key + key_index, sizeof(laddr));
				key_index += sizeof(laddr);
				memcpy(&lport, key + key_index, sizeof(lport));
				key_index += sizeof(lport);
				memcpy(faddr, key + key_index, sizeof(faddr));
				key_index += sizeof(faddr);
				memcpy(&fport, key + key_index, sizeof(fport));

#ifdef SIFTR_IPV6
				laddr[3] = ntohl(laddr[3]);
				faddr[3] = ntohl(faddr[3]);

				if (ipver == INP_IPV6) {
					laddr[0] = ntohl(laddr[0]);
					laddr[1] = ntohl(laddr[1]);
					laddr[2] = ntohl(laddr[2]);
					faddr[0] = ntohl(faddr[0]);
					faddr[1] = ntohl(faddr[1]);
					faddr[2] = ntohl(faddr[2]);

					sbuf_printf(s,
					    "%x:%x:%x:%x:%x:%x:%x:%x;%u-"
					    "%x:%x:%x:%x:%x:%x:%x:%x;%u,",
					    UPPER_SHORT(laddr[0]),
					    LOWER_SHORT(laddr[0]),
					    UPPER_SHORT(laddr[1]),
					    LOWER_SHORT(laddr[1]),
					    UPPER_SHORT(laddr[2]),
					    LOWER_SHORT(laddr[2]),
					    UPPER_SHORT(laddr[3]),
					    LOWER_SHORT(laddr[3]),
					    ntohs(lport),
					    UPPER_SHORT(faddr[0]),
					    LOWER_SHORT(faddr[0]),
					    UPPER_SHORT(faddr[1]),
					    LOWER_SHORT(faddr[1]),
					    UPPER_SHORT(faddr[2]),
					    LOWER_SHORT(faddr[2]),
					    UPPER_SHORT(faddr[3]),
					    LOWER_SHORT(faddr[3]),
					    ntohs(fport));
				} else {
					laddr[0] = FIRST_OCTET(laddr[3]);
					laddr[1] = SECOND_OCTET(laddr[3]);
					laddr[2] = THIRD_OCTET(laddr[3]);
					laddr[3] = FOURTH_OCTET(laddr[3]);
					faddr[0] = FIRST_OCTET(faddr[3]);
					faddr[1] = SECOND_OCTET(faddr[3]);
					faddr[2] = THIRD_OCTET(faddr[3]);
					faddr[3] = FOURTH_OCTET(faddr[3]);
#endif
					sbuf_printf(s,
					    "%u.%u.%u.%u;%u-%u.%u.%u.%u;%u,",
					    laddr[0],
					    laddr[1],
					    laddr[2],
					    laddr[3],
					    ntohs(lport),
					    faddr[0],
					    faddr[1],
					    faddr[2],
					    faddr[3],
					    ntohs(fport));
#ifdef SIFTR_IPV6
				}
#endif

				free(counter, M_SIFTR_HASHNODE);
			}

			LIST_INIT(counter_hash + i);
		}

		sbuf_printf(s, "\n");
		sbuf_finish(s);

		i = 0;
		do {
			bytes_to_write = min(SIFTR_ALQ_BUFLEN, sbuf_len(s)-i);
			alq_writen(siftr_alq, sbuf_data(s)+i, bytes_to_write, ALQ_WAITOK);
			i += bytes_to_write;
		} while (i < sbuf_len(s));

		alq_close(siftr_alq);
		siftr_alq = NULL;
	} else
		error = EINVAL;

	sbuf_delete(s);

	/*
	 * XXX: Should be using ret to check if any functions fail
	 * and set error appropriately
	 */

	return (error);
}


static int
siftr_sysctl_enabled_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = siftr_enabled;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new > 1)
			return (EINVAL);
		else if (new != siftr_enabled) {
			if ((error = siftr_manage_ops(new)) == 0) {
				siftr_enabled = new;
			} else {
				siftr_manage_ops(SIFTR_DISABLE);
			}
		}
	}

	return (error);
}


static void
siftr_shutdown_handler(void *arg)
{
	if (siftr_enabled == 1) {
		siftr_manage_ops(SIFTR_DISABLE);
	}
}


/*
 * Module is being unloaded or machine is shutting down. Take care of cleanup.
 */
static int
deinit_siftr(void)
{
	/* Cleanup. */
	siftr_manage_ops(SIFTR_DISABLE);
	hashdestroy(counter_hash, M_SIFTR, siftr_hashmask);
	mtx_destroy(&siftr_pkt_queue_mtx);
	mtx_destroy(&siftr_pkt_mgr_mtx);

	return (0);
}


/*
 * Module has just been loaded into the kernel.
 */
static int
init_siftr(void)
{
	EVENTHANDLER_REGISTER(shutdown_pre_sync, siftr_shutdown_handler, NULL,
	    SHUTDOWN_PRI_FIRST);

	/* Initialise our flow counter hash table. */
	counter_hash = hashinit(SIFTR_EXPECTED_MAX_TCP_FLOWS, M_SIFTR,
	    &siftr_hashmask);

	mtx_init(&siftr_pkt_queue_mtx, "siftr_pkt_queue_mtx", NULL, MTX_DEF);
	mtx_init(&siftr_pkt_mgr_mtx, "siftr_pkt_mgr_mtx", NULL, MTX_DEF);

	/* Print message to the user's current terminal. */
	uprintf("\nStatistical Information For TCP Research (SIFTR) %s\n"
	    "          http://caia.swin.edu.au/urp/newtcp\n\n",
	    MODVERSION_STR);

	return (0);
}


/*
 * This is the function that is called to load and unload the module.
 * When the module is loaded, this function is called once with
 * "what" == MOD_LOAD
 * When the module is unloaded, this function is called twice with
 * "what" = MOD_QUIESCE first, followed by "what" = MOD_UNLOAD second
 * When the system is shut down e.g. CTRL-ALT-DEL or using the shutdown command,
 * this function is called once with "what" = MOD_SHUTDOWN
 * When the system is shut down, the handler isn't called until the very end
 * of the shutdown sequence i.e. after the disks have been synced.
 */
static int
siftr_load_handler(module_t mod, int what, void *arg)
{
	int ret;

	switch (what) {
	case MOD_LOAD:
		ret = init_siftr();
		break;

	case MOD_QUIESCE:
	case MOD_SHUTDOWN:
		ret = deinit_siftr();
		break;

	case MOD_UNLOAD:
		ret = 0;
		break;

	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}


static moduledata_t siftr_mod = {
	.name = "siftr",
	.evhand = siftr_load_handler,
};

/*
 * Param 1: name of the kernel module
 * Param 2: moduledata_t struct containing info about the kernel module
 *          and the execution entry point for the module
 * Param 3: From sysinit_sub_id enumeration in /usr/include/sys/kernel.h
 *          Defines the module initialisation order
 * Param 4: From sysinit_elem_order enumeration in /usr/include/sys/kernel.h
 *          Defines the initialisation order of this kld relative to others
 *          within the same subsystem as defined by param 3
 */
DECLARE_MODULE(siftr, siftr_mod, SI_SUB_LAST, SI_ORDER_ANY);
MODULE_DEPEND(siftr, alq, 1, 1, 1);
MODULE_VERSION(siftr, MODVERSION);
