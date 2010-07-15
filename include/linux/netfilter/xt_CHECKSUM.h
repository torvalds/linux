/* Header file for iptables ipt_CHECKSUM target
 *
 * (C) 2002 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 Red Hat Inc
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This software is distributed under GNU GPL v2, 1991
*/
#ifndef _IPT_CHECKSUM_TARGET_H
#define _IPT_CHECKSUM_TARGET_H

#define XT_CHECKSUM_OP_FILL	0x01	/* fill in checksum in IP header */

struct xt_CHECKSUM_info {
	__u8 operation;	/* bitset of operations */
};

#endif /* _IPT_CHECKSUM_TARGET_H */
