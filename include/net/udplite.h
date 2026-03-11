/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Definitions for the UDP-Lite (RFC 3828) code.
 */
#ifndef _UDPLITE_H
#define _UDPLITE_H

#include <net/ip6_checksum.h>
#include <net/udp.h>

/* UDP-Lite socket options */
#define UDPLITE_SEND_CSCOV   10 /* sender partial coverage (as sent)      */
#define UDPLITE_RECV_CSCOV   11 /* receiver partial coverage (threshold ) */

#endif	/* _UDPLITE_H */
