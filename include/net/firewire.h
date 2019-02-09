/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_FIREWIRE_H
#define _NET_FIREWIRE_H

/* Pseudo L2 address */
#define FWNET_ALEN	16
union fwnet_hwaddr {
	u8 u[FWNET_ALEN];
	/* "Hardware address" defined in RFC2734/RF3146 */
	struct {
		__be64 uniq_id;		/* EUI-64			*/
		u8 max_rec;		/* max packet size		*/
		u8 sspd;		/* max speed			*/
		__be16 fifo_hi;		/* hi 16bits of FIFO addr	*/
		__be32 fifo_lo;		/* lo 32bits of FIFO addr	*/
	} __packed uc;
};

/* Pseudo L2 Header */
#define FWNET_HLEN	18
struct fwnet_header {
	u8 h_dest[FWNET_ALEN];	/* destination address */
	__be16 h_proto;		/* packet type ID field */
} __packed;

#endif
