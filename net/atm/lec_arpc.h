/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Lec arp cache
 *
 * Marko Kiiskila <mkiiskila@yahoo.com>
 */
#ifndef _LEC_ARP_H_
#define _LEC_ARP_H_
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/if_ether.h>
#include <linux/atmlec.h>

struct lec_arp_table {
	struct hlist_node next;		/* Linked entry list */
	unsigned char atm_addr[ATM_ESA_LEN];	/* Atm address */
	unsigned char mac_addr[ETH_ALEN];	/* Mac address */
	int is_rdesc;			/* Mac address is a route descriptor */
	struct atm_vcc *vcc;		/* Vcc this entry is attached */
	struct atm_vcc *recv_vcc;	/* Vcc we receive data from */

	void (*old_push) (struct atm_vcc *vcc, struct sk_buff *skb);
					/* Push that leads to daemon */

	void (*old_recv_push) (struct atm_vcc *vcc, struct sk_buff *skb);
					/* Push that leads to daemon */

	unsigned long last_used;	/* For expiry */
	unsigned long timestamp;	/* Used for various timestamping things:
					 * 1. FLUSH started
					 *    (status=ESI_FLUSH_PENDING)
					 * 2. Counting to
					 *    max_unknown_frame_time
					 *    (status=ESI_ARP_PENDING||
					 *     status=ESI_VC_PENDING)
					 */
	unsigned char no_tries;		/* No of times arp retry has been tried */
	unsigned char status;		/* Status of this entry */
	unsigned short flags;		/* Flags for this entry */
	unsigned short packets_flooded;	/* Data packets flooded */
	unsigned long flush_tran_id;	/* Transaction id in flush protocol */
	struct timer_list timer;	/* Arping timer */
	struct lec_priv *priv;		/* Pointer back */
	u8 *tlvs;
	u32 sizeoftlvs;			/*
					 * LANE2: Each MAC address can have TLVs
					 * associated with it.  sizeoftlvs tells the
					 * the length of the tlvs array
					 */
	struct sk_buff_head tx_wait;	/* wait queue for outgoing packets */
	refcount_t usage;		/* usage count */
};

/*
 * LANE2: Template tlv struct for accessing
 * the tlvs in the lec_arp_table->tlvs array
 */
struct tlv {
	u32 type;
	u8 length;
	u8 value[255];
};

/* Status fields */
#define ESI_UNKNOWN 0		/*
				 * Next packet sent to this mac address
				 * causes ARP-request to be sent
				 */
#define ESI_ARP_PENDING 1	/*
				 * There is no ATM address associated with this
				 * 48-bit address.  The LE-ARP protocol is in
				 * progress.
				 */
#define ESI_VC_PENDING 2	/*
				 * There is a valid ATM address associated with
				 * this 48-bit address but there is no VC set
				 * up to that ATM address.  The signaling
				 * protocol is in process.
				 */
#define ESI_FLUSH_PENDING 4	/*
				 * The LEC has been notified of the FLUSH_START
				 * status and it is assumed that the flush
				 * protocol is in process.
				 */
#define ESI_FORWARD_DIRECT 5	/*
				 * Either the Path Switching Delay (C22) has
				 * elapsed or the LEC has notified the Mapping
				 * that the flush protocol has completed.  In
				 * either case, it is safe to forward packets
				 * to this address via the data direct VC.
				 */

/* Flag values */
#define LEC_REMOTE_FLAG      0x0001
#define LEC_PERMANENT_FLAG   0x0002

#endif /* _LEC_ARP_H_ */
