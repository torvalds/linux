/* $OpenBSD: lldpctl.h,v 1.1 2025/05/02 06:12:53 dlg Exp $ */

/*
 * Copyright (c) 2024 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define LLDP_CTL_PATH		"/var/run/lldp.sock"

enum lldp_ctl_msg {
	LLDP_CTL_MSG_PING,
	LLDP_CTL_MSG_PONG,

	LLDP_CTL_MSG_MSAP_REQ,		/* ctl -> daemon */
	LLDP_CTL_MSG_MSAP,		/* daemon -> ctl */
	LLDP_CTL_MSG_MSAP_END,		/* daemon -> ctl */

	LLDP_CTL_MSG_ACTR_REQ,		/* ctl -> daemon */
	LLDP_CTL_MSG_ACTR,		/* daemon -> ctl */
	LLDP_CTL_MSG_ACTR_END,		/* daemon -> ctl */
};

enum agent_counter {
	statsAgeoutsTotal,
	statsFramesDiscardedTotal,
	statsFramesInErrorsTotal,
	statsFramesInTotal,
	statsFramesOutTotal,
	statsTLVsDiscardedTotal,
	statsTLVsUnrecognisedTotal,
	lldpduLengthErrors,

	AGENT_COUNTER_NCOUNTERS
};

struct lldp_ctl_msg_msap_req {
	char			ifname[IFNAMSIZ];
	unsigned int		gen;
};

struct lldp_ctl_msg_msap {
	char			ifname[IFNAMSIZ];
	unsigned int		gen;
	struct ether_addr	saddr;
	struct ether_addr	daddr;

	struct timespec		created;
	struct timespec		updated;

	uint64_t		packets;
	uint64_t		updates;

	/* followed by the pdu */
};

struct lldp_ctl_msg_actrs_req {
	unsigned int		which;
#define LLDP_ACTRS_ALL			0
#define LLDP_ACTRS_DAEMON		1
#define LLDP_ACTRS_IFACE		2
	char			ifname[IFNAMSIZ];
};

struct lldp_ctl_msg_actrs {
	char			ifname[IFNAMSIZ];
	uint64_t		ctrs[AGENT_COUNTER_NCOUNTERS];
};
