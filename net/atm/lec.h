/*
 * Lan Emulation client header file
 *
 * Marko Kiiskila <mkiiskila@yahoo.com>
 */

#ifndef _LEC_H_
#define _LEC_H_

#include <linux/atmdev.h>
#include <linux/netdevice.h>
#include <linux/atmlec.h>

#define LEC_HEADER_LEN 16

struct lecdatahdr_8023 {
	__be16 le_header;
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
	__be16 h_type;
};

struct lecdatahdr_8025 {
	__be16 le_header;
	unsigned char ac_pad;
	unsigned char fc;
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
};

#define LEC_MINIMUM_8023_SIZE   62
#define LEC_MINIMUM_8025_SIZE   16

/*
 * Operations that LANE2 capable device can do. Two first functions
 * are used to make the device do things. See spec 3.1.3 and 3.1.4.
 *
 * The third function is intended for the MPOA component sitting on
 * top of the LANE device. The MPOA component assigns it's own function
 * to (*associate_indicator)() and the LANE device will use that
 * function to tell about TLVs it sees floating through.
 *
 */
struct lane2_ops {
	int (*resolve) (struct net_device *dev, const u8 *dst_mac, int force,
			u8 **tlvs, u32 *sizeoftlvs);
	int (*associate_req) (struct net_device *dev, const u8 *lan_dst,
			      const u8 *tlvs, u32 sizeoftlvs);
	void (*associate_indicator) (struct net_device *dev, const u8 *mac_addr,
				     const u8 *tlvs, u32 sizeoftlvs);
};

/*
 * ATM LAN Emulation supports both LLC & Dix Ethernet EtherType
 * frames.
 *
 * 1. Dix Ethernet EtherType frames encoded by placing EtherType
 *    field in h_type field. Data follows immediately after header.
 * 2. LLC Data frames whose total length, including LLC field and data,
 *    but not padding required to meet the minimum data frame length,
 *    is less than ETH_P_802_3_MIN MUST be encoded by placing that length
 *    in the h_type field. The LLC field follows header immediately.
 * 3. LLC data frames longer than this maximum MUST be encoded by placing
 *    the value 0 in the h_type field.
 *
 */

/* Hash table size */
#define LEC_ARP_TABLE_SIZE 16

struct lec_priv {
	unsigned short lecid;			/* Lecid of this client */
	struct hlist_head lec_arp_empty_ones;
						/* Used for storing VCC's that don't have a MAC address attached yet */
	struct hlist_head lec_arp_tables[LEC_ARP_TABLE_SIZE];
						/* Actual LE ARP table */
	struct hlist_head lec_no_forward;
						/*
						 * Used for storing VCC's (and forward packets from) which are to
						 * age out by not using them to forward packets.
						 * This is because to some LE clients there will be 2 VCCs. Only
						 * one of them gets used.
						 */
	struct hlist_head mcast_fwds;
						/*
						 * With LANEv2 it is possible that BUS (or a special multicast server)
						 * establishes multiple Multicast Forward VCCs to us. This list
						 * collects all those VCCs. LANEv1 client has only one item in this
						 * list. These entries are not aged out.
						 */
	spinlock_t lec_arp_lock;
	struct atm_vcc *mcast_vcc;		/* Default Multicast Send VCC */
	struct atm_vcc *lecd;
	struct delayed_work lec_arp_work;	/* C10 */
	unsigned int maximum_unknown_frame_count;
						/*
						 * Within the period of time defined by this variable, the client will send
						 * no more than C10 frames to BUS for a given unicast destination. (C11)
						 */
	unsigned long max_unknown_frame_time;
						/*
						 * If no traffic has been sent in this vcc for this period of time,
						 * vcc will be torn down (C12)
						 */
	unsigned long vcc_timeout_period;
						/*
						 * An LE Client MUST not retry an LE_ARP_REQUEST for a
						 * given frame's LAN Destination more than maximum retry count times,
						 * after the first LEC_ARP_REQUEST (C13)
						 */
	unsigned short max_retry_count;
						/*
						 * Max time the client will maintain an entry in its arp cache in
						 * absence of a verification of that relationship (C17)
						 */
	unsigned long aging_time;
						/*
						 * Max time the client will maintain an entry in cache when
						 * topology change flag is true (C18)
						 */
	unsigned long forward_delay_time;	/* Topology change flag (C19) */
	int topology_change;
						/*
						 * Max time the client expects an LE_ARP_REQUEST/LE_ARP_RESPONSE
						 * cycle to take (C20)
						 */
	unsigned long arp_response_time;
						/*
						 * Time limit ot wait to receive an LE_FLUSH_RESPONSE after the
						 * LE_FLUSH_REQUEST has been sent before taking recover action. (C21)
						 */
	unsigned long flush_timeout;
						/* The time since sending a frame to the bus after which the
						 * LE Client may assume that the frame has been either discarded or
						 * delivered to the recipient (C22)
						 */
	unsigned long path_switching_delay;

	u8 *tlvs;				/* LANE2: TLVs are new */
	u32 sizeoftlvs;				/* The size of the tlv array in bytes */
	int lane_version;			/* LANE2 */
	int itfnum;				/* e.g. 2 for lec2, 5 for lec5 */
	struct lane2_ops *lane2_ops;		/* can be NULL for LANE v1 */
	int is_proxy;				/* bridge between ATM and Ethernet */
};

struct lec_vcc_priv {
	void (*old_pop) (struct atm_vcc *vcc, struct sk_buff *skb);
	int xoff;
};

#define LEC_VCC_PRIV(vcc)	((struct lec_vcc_priv *)((vcc)->user_back))

#endif				/* _LEC_H_ */
