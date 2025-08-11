/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBIE_PCTYPE_H
#define __LIBIE_PCTYPE_H

/* Packet Classifier Type indexes, used to set the xxQF_HENA registers. Also
 * communicated over the virtchnl API as part of struct virtchnl_rss_hashena.
 */
enum libie_filter_pctype {
	/* Note: Values 0-28 are reserved for future use.
	 * Value 29, 30, 32 are not supported on XL710 and X710.
	 */
	LIBIE_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP	= 29,
	LIBIE_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP	= 30,
	LIBIE_FILTER_PCTYPE_NONF_IPV4_UDP		= 31,
	LIBIE_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK	= 32,
	LIBIE_FILTER_PCTYPE_NONF_IPV4_TCP		= 33,
	LIBIE_FILTER_PCTYPE_NONF_IPV4_SCTP		= 34,
	LIBIE_FILTER_PCTYPE_NONF_IPV4_OTHER		= 35,
	LIBIE_FILTER_PCTYPE_FRAG_IPV4			= 36,
	/* Note: Values 37-38 are reserved for future use.
	 * Value 39, 40, 42 are not supported on XL710 and X710.
	 */
	LIBIE_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP	= 39,
	LIBIE_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP	= 40,
	LIBIE_FILTER_PCTYPE_NONF_IPV6_UDP		= 41,
	LIBIE_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK	= 42,
	LIBIE_FILTER_PCTYPE_NONF_IPV6_TCP		= 43,
	LIBIE_FILTER_PCTYPE_NONF_IPV6_SCTP		= 44,
	LIBIE_FILTER_PCTYPE_NONF_IPV6_OTHER		= 45,
	LIBIE_FILTER_PCTYPE_FRAG_IPV6			= 46,
	/* Note: Value 47 is reserved for future use */
	LIBIE_FILTER_PCTYPE_FCOE_OX			= 48,
	LIBIE_FILTER_PCTYPE_FCOE_RX			= 49,
	LIBIE_FILTER_PCTYPE_FCOE_OTHER			= 50,
	/* Note: Values 51-62 are reserved for future use */
	LIBIE_FILTER_PCTYPE_L2_PAYLOAD			= 63
};

#endif /* __LIBIE_PCTYPE_H */
