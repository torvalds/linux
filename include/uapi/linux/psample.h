/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UAPI_PSAMPLE_H
#define __UAPI_PSAMPLE_H

enum {
	PSAMPLE_ATTR_IIFINDEX,
	PSAMPLE_ATTR_OIFINDEX,
	PSAMPLE_ATTR_ORIGSIZE,
	PSAMPLE_ATTR_SAMPLE_GROUP,
	PSAMPLE_ATTR_GROUP_SEQ,
	PSAMPLE_ATTR_SAMPLE_RATE,
	PSAMPLE_ATTR_DATA,
	PSAMPLE_ATTR_GROUP_REFCOUNT,
	PSAMPLE_ATTR_TUNNEL,

	__PSAMPLE_ATTR_MAX
};

enum psample_command {
	PSAMPLE_CMD_SAMPLE,
	PSAMPLE_CMD_GET_GROUP,
	PSAMPLE_CMD_NEW_GROUP,
	PSAMPLE_CMD_DEL_GROUP,
};

enum psample_tunnel_key_attr {
	PSAMPLE_TUNNEL_KEY_ATTR_ID,                 /* be64 Tunnel ID */
	PSAMPLE_TUNNEL_KEY_ATTR_IPV4_SRC,           /* be32 src IP address. */
	PSAMPLE_TUNNEL_KEY_ATTR_IPV4_DST,           /* be32 dst IP address. */
	PSAMPLE_TUNNEL_KEY_ATTR_TOS,                /* u8 Tunnel IP ToS. */
	PSAMPLE_TUNNEL_KEY_ATTR_TTL,                /* u8 Tunnel IP TTL. */
	PSAMPLE_TUNNEL_KEY_ATTR_DONT_FRAGMENT,      /* No argument, set DF. */
	PSAMPLE_TUNNEL_KEY_ATTR_CSUM,               /* No argument. CSUM packet. */
	PSAMPLE_TUNNEL_KEY_ATTR_OAM,                /* No argument. OAM frame.  */
	PSAMPLE_TUNNEL_KEY_ATTR_GENEVE_OPTS,        /* Array of Geneve options. */
	PSAMPLE_TUNNEL_KEY_ATTR_TP_SRC,	            /* be16 src Transport Port. */
	PSAMPLE_TUNNEL_KEY_ATTR_TP_DST,		    /* be16 dst Transport Port. */
	PSAMPLE_TUNNEL_KEY_ATTR_VXLAN_OPTS,	    /* Nested VXLAN opts* */
	PSAMPLE_TUNNEL_KEY_ATTR_IPV6_SRC,           /* struct in6_addr src IPv6 address. */
	PSAMPLE_TUNNEL_KEY_ATTR_IPV6_DST,           /* struct in6_addr dst IPv6 address. */
	PSAMPLE_TUNNEL_KEY_ATTR_PAD,
	PSAMPLE_TUNNEL_KEY_ATTR_ERSPAN_OPTS,        /* struct erspan_metadata */
	PSAMPLE_TUNNEL_KEY_ATTR_IPV4_INFO_BRIDGE,   /* No argument. IPV4_INFO_BRIDGE mode.*/
	__PSAMPLE_TUNNEL_KEY_ATTR_MAX
};

/* Can be overridden at runtime by module option */
#define PSAMPLE_ATTR_MAX (__PSAMPLE_ATTR_MAX - 1)

#define PSAMPLE_NL_MCGRP_CONFIG_NAME "config"
#define PSAMPLE_NL_MCGRP_SAMPLE_NAME "packets"
#define PSAMPLE_GENL_NAME "psample"
#define PSAMPLE_GENL_VERSION 1

#endif
