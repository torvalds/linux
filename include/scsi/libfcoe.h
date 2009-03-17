/*
 * Copyright (c) 2008-2009 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007-2008 Intel Corporation.  All rights reserved.
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

#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>

/*
 * FIP tunable parameters.
 */
#define FCOE_CTLR_START_DELAY	2000	/* mS after first adv. to choose FCF */
#define FCOE_CTRL_SOL_TOV	2000	/* min. solicitation interval (mS) */
#define FCOE_CTLR_FCF_LIMIT	20	/* max. number of FCF entries */

/**
 * enum fip_state - internal state of FCoE controller.
 * @FIP_ST_DISABLED: 	controller has been disabled or not yet enabled.
 * @FIP_ST_LINK_WAIT:	the physical link is down or unusable.
 * @FIP_ST_AUTO:	determining whether to use FIP or non-FIP mode.
 * @FIP_ST_NON_FIP:	non-FIP mode selected.
 * @FIP_ST_ENABLED:	FIP mode selected.
 */
enum fip_state {
	FIP_ST_DISABLED,
	FIP_ST_LINK_WAIT,
	FIP_ST_AUTO,
	FIP_ST_NON_FIP,
	FIP_ST_ENABLED,
};

/**
 * struct fcoe_ctlr - FCoE Controller and FIP state.
 * @state:	internal FIP state for network link and FIP or non-FIP mode.
 * @lp:		&fc_lport: libfc local port.
 * @sel_fcf:	currently selected FCF, or NULL.
 * @fcfs:	list of discovered FCFs.
 * @fcf_count:	number of discovered FCF entries.
 * @sol_time:	time when a multicast solicitation was last sent.
 * @sel_time:	time after which to select an FCF.
 * @port_ka_time: time of next port keep-alive.
 * @ctlr_ka_time: time of next controller keep-alive.
 * @timer:	timer struct used for all delayed events.
 * @link_work:	&work_struct for doing FCF selection.
 * @recv_work:	&work_struct for receiving FIP frames.
 * @fip_recv_list: list of received FIP frames.
 * @user_mfs:	configured maximum FC frame size, including FC header.
 * @flogi_oxid: exchange ID of most recent fabric login.
 * @flogi_count: number of FLOGI attempts in AUTO mode.
 * @link:	current link status for libfc.
 * @last_link:	last link state reported to libfc.
 * @map_dest:	use the FC_MAP mode for destination MAC addresses.
 * @dest_addr:	MAC address of the selected FC forwarder.
 * @ctl_src_addr: the native MAC address of our local port.
 * @data_src_addr: the assigned MAC address for the local port after FLOGI.
 * @send:	LLD-supplied function to handle sending of FIP Ethernet frames.
 * @update_mac: LLD-supplied function to handle changes to MAC addresses.
 * @lock:	lock protecting this structure.
 *
 * This structure is used by all FCoE drivers.  It contains information
 * needed by all FCoE low-level drivers (LLDs) as well as internal state
 * for FIP, and fields shared with the LLDS.
 */
struct fcoe_ctlr {
	enum fip_state state;
	struct fc_lport *lp;
	struct fcoe_fcf *sel_fcf;
	struct list_head fcfs;
	u16 fcf_count;
	unsigned long sol_time;
	unsigned long sel_time;
	unsigned long port_ka_time;
	unsigned long ctlr_ka_time;
	struct timer_list timer;
	struct work_struct link_work;
	struct work_struct recv_work;
	struct sk_buff_head fip_recv_list;
	u16 user_mfs;
	u16 flogi_oxid;
	u8 flogi_count;
	u8 link;
	u8 last_link;
	u8 map_dest;
	u8 dest_addr[ETH_ALEN];
	u8 ctl_src_addr[ETH_ALEN];
	u8 data_src_addr[ETH_ALEN];

	void (*send)(struct fcoe_ctlr *, struct sk_buff *);
	void (*update_mac)(struct fcoe_ctlr *, u8 *old, u8 *new);
	spinlock_t lock;
};

/*
 * struct fcoe_fcf - Fibre-Channel Forwarder.
 * @list:	list linkage.
 * @time:	system time (jiffies) when an advertisement was last received.
 * @switch_name: WWN of switch from advertisement.
 * @fabric_name: WWN of fabric from advertisement.
 * @fc_map:	FC_MAP value from advertisement.
 * @fcf_mac:	Ethernet address of the FCF.
 * @vfid:	virtual fabric ID.
 * @pri:	seletion priority, smaller values are better.
 * @flags:	flags received from advertisement.
 * @fka_period:	keep-alive period, in jiffies.
 *
 * A Fibre-Channel Forwarder (FCF) is the entity on the Ethernet that
 * passes FCoE frames on to an FC fabric.  This structure represents
 * one FCF from which advertisements have been received.
 *
 * When looking up an FCF, @switch_name, @fabric_name, @fc_map, @vfid, and
 * @fcf_mac together form the lookup key.
 */
struct fcoe_fcf {
	struct list_head list;
	unsigned long time;

	u64 switch_name;
	u64 fabric_name;
	u32 fc_map;
	u16 vfid;
	u8 fcf_mac[ETH_ALEN];

	u8 pri;
	u16 flags;
	u32 fka_period;
};

/* FIP API functions */
void fcoe_ctlr_init(struct fcoe_ctlr *);
void fcoe_ctlr_destroy(struct fcoe_ctlr *);
void fcoe_ctlr_link_up(struct fcoe_ctlr *);
int fcoe_ctlr_link_down(struct fcoe_ctlr *);
int fcoe_ctlr_els_send(struct fcoe_ctlr *, struct sk_buff *);
void fcoe_ctlr_recv(struct fcoe_ctlr *, struct sk_buff *);
int fcoe_ctlr_recv_flogi(struct fcoe_ctlr *, struct fc_frame *fp, u8 *sa);

/* libfcoe funcs */
u64 fcoe_wwn_from_mac(unsigned char mac[], unsigned int, unsigned int);
int fcoe_libfc_config(struct fc_lport *, struct libfc_function_template *);

#endif /* _LIBFCOE_H */
