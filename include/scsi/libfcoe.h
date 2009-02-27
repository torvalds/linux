/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _LIBFCOE_H
#define _LIBFCOE_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>

/*
 * this percpu struct for fcoe
 */
struct fcoe_percpu_s {
	int		cpu;
	struct task_struct *thread;
	struct sk_buff_head fcoe_rx_list;
	struct page *crc_eof_page;
	int crc_eof_offset;
};

/*
 * the fcoe sw transport private data
 */
struct fcoe_softc {
	struct list_head list;
	struct fc_lport *lp;
	struct net_device *real_dev;
	struct net_device *phys_dev;		/* device with ethtool_ops */
	struct packet_type  fcoe_packet_type;
	struct sk_buff_head fcoe_pending_queue;
	u8	fcoe_pending_queue_active;

	u8 dest_addr[ETH_ALEN];
	u8 ctl_src_addr[ETH_ALEN];
	u8 data_src_addr[ETH_ALEN];
	/*
	 * fcoe protocol address learning related stuff
	 */
	u16 flogi_oxid;
	u8 flogi_progress;
	u8 address_mode;
};

static inline struct net_device *fcoe_netdev(
	const struct fc_lport *lp)
{
	return ((struct fcoe_softc *)lport_priv(lp))->real_dev;
}

static inline struct fcoe_hdr *skb_fcoe_header(const struct sk_buff *skb)
{
	return (struct fcoe_hdr *)skb_network_header(skb);
}

static inline int skb_fcoe_offset(const struct sk_buff *skb)
{
	return skb_network_offset(skb);
}

static inline struct fc_frame_header *skb_fc_header(const struct sk_buff *skb)
{
	return (struct fc_frame_header *)skb_transport_header(skb);
}

static inline int skb_fc_offset(const struct sk_buff *skb)
{
	return skb_transport_offset(skb);
}

static inline void skb_reset_fc_header(struct sk_buff *skb)
{
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, skb_network_offset(skb) +
				 sizeof(struct fcoe_hdr));
}

static inline bool skb_fc_is_data(const struct sk_buff *skb)
{
	return skb_fc_header(skb)->fh_r_ctl == FC_RCTL_DD_SOL_DATA;
}

static inline bool skb_fc_is_cmd(const struct sk_buff *skb)
{
	return skb_fc_header(skb)->fh_r_ctl == FC_RCTL_DD_UNSOL_CMD;
}

static inline bool skb_fc_has_exthdr(const struct sk_buff *skb)
{
	return (skb_fc_header(skb)->fh_r_ctl == FC_RCTL_VFTH) ||
	    (skb_fc_header(skb)->fh_r_ctl == FC_RCTL_IFRH) ||
	    (skb_fc_header(skb)->fh_r_ctl == FC_RCTL_ENCH);
}

static inline bool skb_fc_is_roff(const struct sk_buff *skb)
{
	return skb_fc_header(skb)->fh_f_ctl[2] & FC_FC_REL_OFF;
}

static inline u16 skb_fc_oxid(const struct sk_buff *skb)
{
	return be16_to_cpu(skb_fc_header(skb)->fh_ox_id);
}

static inline u16 skb_fc_rxid(const struct sk_buff *skb)
{
	return be16_to_cpu(skb_fc_header(skb)->fh_rx_id);
}

/* FIXME - DMA_BIDIRECTIONAL ? */
#define skb_cb(skb)	((struct fcoe_rcv_info *)&((skb)->cb[0]))
#define skb_cmd(skb)	(skb_cb(skb)->fr_cmd)
#define skb_dir(skb)	(skb_cmd(skb)->sc_data_direction)
static inline bool skb_fc_is_read(const struct sk_buff *skb)
{
	if (skb_fc_is_cmd(skb) && skb_cmd(skb))
		return skb_dir(skb) == DMA_FROM_DEVICE;
	return false;
}

static inline bool skb_fc_is_write(const struct sk_buff *skb)
{
	if (skb_fc_is_cmd(skb) && skb_cmd(skb))
		return skb_dir(skb) == DMA_TO_DEVICE;
	return false;
}

/* libfcoe funcs */
int fcoe_reset(struct Scsi_Host *shost);
u64 fcoe_wwn_from_mac(unsigned char mac[MAX_ADDR_LEN],
		      unsigned int scheme, unsigned int port);

u32 fcoe_fc_crc(struct fc_frame *fp);
int fcoe_xmit(struct fc_lport *, struct fc_frame *);
int fcoe_rcv(struct sk_buff *, struct net_device *,
	     struct packet_type *, struct net_device *);

int fcoe_percpu_receive_thread(void *arg);
void fcoe_clean_pending_queue(struct fc_lport *lp);
void fcoe_percpu_clean(struct fc_lport *lp);
void fcoe_watchdog(ulong vp);
int fcoe_link_ok(struct fc_lport *lp);

struct fc_lport *fcoe_hostlist_lookup(const struct net_device *);
int fcoe_hostlist_add(const struct fc_lport *);
int fcoe_hostlist_remove(const struct fc_lport *);

struct Scsi_Host *fcoe_host_alloc(struct scsi_host_template *, int);
int fcoe_libfc_config(struct fc_lport *, struct libfc_function_template *);

/* fcoe sw hba */
int __init fcoe_sw_init(void);
int __exit fcoe_sw_exit(void);
#endif /* _LIBFCOE_H */
