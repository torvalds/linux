/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef IF_CAIF_H_
#define IF_CAIF_H_
#include <linux/sockios.h>
#include <linux/types.h>
#include <linux/socket.h>

/**
 * enum ifla_caif - CAIF NetlinkRT parameters.
 * @IFLA_CAIF_IPV4_CONNID:  Connection ID for IPv4 PDP Context.
 *			    The type of attribute is NLA_U32.
 * @IFLA_CAIF_IPV6_CONNID:  Connection ID for IPv6 PDP Context.
 *			    The type of attribute is NLA_U32.
 * @IFLA_CAIF_LOOPBACK:	    If different from zero, device is doing loopback
 *			    The type of attribute is NLA_U8.
 *
 * When using RT Netlink to create, destroy or configure a CAIF IP interface,
 * enum ifla_caif is used to specify the configuration attributes.
 */
enum ifla_caif {
	__IFLA_CAIF_UNSPEC,
	IFLA_CAIF_IPV4_CONNID,
	IFLA_CAIF_IPV6_CONNID,
	IFLA_CAIF_LOOPBACK,
	__IFLA_CAIF_MAX
};
#define	IFLA_CAIF_MAX (__IFLA_CAIF_MAX-1)

#endif /*IF_CAIF_H_*/
