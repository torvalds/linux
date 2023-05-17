/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008-2009 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007-2008 Intel Corporation.  All rights reserved.
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
#include <linux/local_lock.h>
#include <linux/random.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>
#include <scsi/fcoe_sysfs.h>

#define FCOE_MAX_CMD_LEN	16	/* Supported CDB length */

/*
 * Max MTU for FCoE: 14 (FCoE header) + 24 (FC header) + 2112 (max FC payload)
 * + 4 (FC CRC) + 4 (FCoE trailer) =  2158 bytes
 */
#define FCOE_MTU	2158

/*
 * FIP tunable parameters.
 */
#define FCOE_CTLR_START_DELAY	2000	/* mS after first adv. to choose FCF */
#define FCOE_CTLR_SOL_TOV	2000	/* min. solicitation interval (mS) */
#define FCOE_CTLR_FCF_LIMIT	20	/* max. number of FCF entries */
#define FCOE_CTLR_VN2VN_LOGIN_LIMIT 3	/* max. VN2VN rport login retries */

/**
 * enum fip_state - internal state of FCoE controller.
 * @FIP_ST_DISABLED: 	controller has been disabled or not yet enabled.
 * @FIP_ST_LINK_WAIT:	the physical link is down or unusable.
 * @FIP_ST_AUTO:	determining whether to use FIP or non-FIP mode.
 * @FIP_ST_NON_FIP:	non-FIP mode selected.
 * @FIP_ST_ENABLED:	FIP mode selected.
 * @FIP_ST_VNMP_START:	VN2VN multipath mode start, wait
 * @FIP_ST_VNMP_PROBE1:	VN2VN sent first probe, listening
 * @FIP_ST_VNMP_PROBE2:	VN2VN sent second probe, listening
 * @FIP_ST_VNMP_CLAIM:	VN2VN sent claim, waiting for responses
 * @FIP_ST_VNMP_UP:	VN2VN multipath mode operation
 */
enum fip_state {
	FIP_ST_DISABLED,
	FIP_ST_LINK_WAIT,
	FIP_ST_AUTO,
	FIP_ST_NON_FIP,
	FIP_ST_ENABLED,
	FIP_ST_VNMP_START,
	FIP_ST_VNMP_PROBE1,
	FIP_ST_VNMP_PROBE2,
	FIP_ST_VNMP_CLAIM,
	FIP_ST_VNMP_UP,
};

/*
 * Modes:
 * The mode is the state that is to be entered after link up.
 * It must not change after fcoe_ctlr_init() sets it.
 */
enum fip_mode {
	FIP_MODE_AUTO,
	FIP_MODE_NON_FIP,
	FIP_MODE_FABRIC,
	FIP_MODE_VN2VN,
};

/**
 * struct fcoe_ctlr - FCoE Controller and FIP state
 * @state:	   internal FIP state for network link and FIP or non-FIP mode.
 * @mode:	   LLD-selected mode.
 * @lp:		   &fc_lport: libfc local port.
 * @sel_fcf:	   currently selected FCF, or NULL.
 * @fcfs:	   list of discovered FCFs.
 * @cdev:          (Optional) pointer to sysfs fcoe_ctlr_device.
 * @fcf_count:	   number of discovered FCF entries.
 * @sol_time:	   time when a multicast solicitation was last sent.
 * @sel_time:	   time after which to select an FCF.
 * @port_ka_time:  time of next port keep-alive.
 * @ctlr_ka_time:  time of next controller keep-alive.
 * @timer:	   timer struct used for all delayed events.
 * @timer_work:	   &work_struct for doing keep-alives and resets.
 * @recv_work:	   &work_struct for receiving FIP frames.
 * @fip_recv_list: list of received FIP frames.
 * @flogi_req:	   clone of FLOGI request sent
 * @rnd_state:	   state for pseudo-random number generator.
 * @port_id:	   proposed or selected local-port ID.
 * @user_mfs:	   configured maximum FC frame size, including FC header.
 * @flogi_oxid:    exchange ID of most recent fabric login.
 * @flogi_req_send: send of FLOGI requested
 * @flogi_count:   number of FLOGI attempts in AUTO mode.
 * @map_dest:	   use the FC_MAP mode for destination MAC addresses.
 * @fip_resp:	   start FIP VLAN discovery responder
 * @spma:	   supports SPMA server-provided MACs mode
 * @probe_tries:   number of FC_IDs probed
 * @priority:      DCBx FCoE APP priority
 * @dest_addr:	   MAC address of the selected FC forwarder.
 * @ctl_src_addr:  the native MAC address of our local port.
 * @send:	   LLD-supplied function to handle sending FIP Ethernet frames
 * @update_mac:    LLD-supplied function to handle changes to MAC addresses.
 * @get_src_addr:  LLD-supplied function to supply a source MAC address.
 * @ctlr_mutex:	   lock protecting this structure.
 * @ctlr_lock:     spinlock covering flogi_req
 *
 * This structure is used by all FCoE drivers.  It contains information
 * needed by all FCoE low-level drivers (LLDs) as well as internal state
 * for FIP, and fields shared with the LLDS.
 */
struct fcoe_ctlr {
	enum fip_state state;
	enum fip_mode mode;
	struct fc_lport *lp;
	struct fcoe_fcf *sel_fcf;
	struct list_head fcfs;
	struct fcoe_ctlr_device *cdev;
	u16 fcf_count;
	unsigned long sol_time;
	unsigned long sel_time;
	unsigned long port_ka_time;
	unsigned long ctlr_ka_time;
	struct timer_list timer;
	struct work_struct timer_work;
	struct work_struct recv_work;
	struct sk_buff_head fip_recv_list;
	struct sk_buff *flogi_req;

	struct rnd_state rnd_state;
	u32 port_id;

	u16 user_mfs;
	u16 flogi_oxid;
	u8 flogi_req_send;
	u8 flogi_count;
	bool map_dest;
	bool fip_resp;
	u8 spma;
	u8 probe_tries;
	u8 priority;
	u8 dest_addr[ETH_ALEN];
	u8 ctl_src_addr[ETH_ALEN];

	void (*send)(struct fcoe_ctlr *, struct sk_buff *);
	void (*update_mac)(struct fc_lport *, u8 *addr);
	u8 * (*get_src_addr)(struct fc_lport *);
	struct mutex ctlr_mutex;
	spinlock_t ctlr_lock;
};

/**
 * fcoe_ctlr_priv() - Return the private data from a fcoe_ctlr
 * @cltr: The fcoe_ctlr whose private data will be returned
 */
static inline void *fcoe_ctlr_priv(const struct fcoe_ctlr *ctlr)
{
	return (void *)(ctlr + 1);
}

/*
 * This assumes that the fcoe_ctlr (x) is allocated with the fcoe_ctlr_device.
 */
#define fcoe_ctlr_to_ctlr_dev(x)					\
	(x)->cdev

/**
 * struct fcoe_fcf - Fibre-Channel Forwarder
 * @list:	 list linkage
 * @event_work:  Work for FC Transport actions queue
 * @event:       The event to be processed
 * @fip:         The controller that the FCF was discovered on
 * @fcf_dev:     The associated fcoe_fcf_device instance
 * @time:	 system time (jiffies) when an advertisement was last received
 * @switch_name: WWN of switch from advertisement
 * @fabric_name: WWN of fabric from advertisement
 * @fc_map:	 FC_MAP value from advertisement
 * @fcf_mac:	 Ethernet address of the FCF for FIP traffic
 * @fcoe_mac:	 Ethernet address of the FCF for FCoE traffic
 * @vfid:	 virtual fabric ID
 * @pri:	 selection priority, smaller values are better
 * @flogi_sent:	 current FLOGI sent to this FCF
 * @flags:	 flags received from advertisement
 * @fka_period:	 keep-alive period, in jiffies
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
	struct work_struct event_work;
	struct fcoe_ctlr *fip;
	struct fcoe_fcf_device *fcf_dev;
	unsigned long time;

	u64 switch_name;
	u64 fabric_name;
	u32 fc_map;
	u16 vfid;
	u8 fcf_mac[ETH_ALEN];
	u8 fcoe_mac[ETH_ALEN];

	u8 pri;
	u8 flogi_sent;
	u16 flags;
	u32 fka_period;
	u8 fd_flags:1;
};

#define fcoe_fcf_to_fcf_dev(x)			\
	((x)->fcf_dev)

/**
 * struct fcoe_rport - VN2VN remote port
 * @time:	time of create or last beacon packet received from node
 * @fcoe_len:	max FCoE frame size, not including VLAN or Ethernet headers
 * @flags:	flags from probe or claim
 * @login_count: number of unsuccessful rport logins to this port
 * @enode_mac:	E_Node control MAC address
 * @vn_mac:	VN_Node assigned MAC address for data
 */
struct fcoe_rport {
	struct fc_rport_priv rdata;
	unsigned long time;
	u16 fcoe_len;
	u16 flags;
	u8 login_count;
	u8 enode_mac[ETH_ALEN];
	u8 vn_mac[ETH_ALEN];
};

/* FIP API functions */
void fcoe_ctlr_init(struct fcoe_ctlr *, enum fip_mode);
void fcoe_ctlr_destroy(struct fcoe_ctlr *);
void fcoe_ctlr_link_up(struct fcoe_ctlr *);
int fcoe_ctlr_link_down(struct fcoe_ctlr *);
int fcoe_ctlr_els_send(struct fcoe_ctlr *, struct fc_lport *, struct sk_buff *);
void fcoe_ctlr_recv(struct fcoe_ctlr *, struct sk_buff *);
int fcoe_ctlr_recv_flogi(struct fcoe_ctlr *, struct fc_lport *,
			 struct fc_frame *);

/* libfcoe funcs */
u64 fcoe_wwn_from_mac(unsigned char mac[ETH_ALEN], unsigned int scheme,
		      unsigned int port);
int fcoe_libfc_config(struct fc_lport *, struct fcoe_ctlr *,
		      const struct libfc_function_template *, int init_fcp);
u32 fcoe_fc_crc(struct fc_frame *fp);
int fcoe_start_io(struct sk_buff *skb);
int fcoe_get_wwn(struct net_device *netdev, u64 *wwn, int type);
void __fcoe_get_lesb(struct fc_lport *lport, struct fc_els_lesb *fc_lesb,
		     struct net_device *netdev);
void fcoe_wwn_to_str(u64 wwn, char *buf, int len);
int fcoe_validate_vport_create(struct fc_vport *vport);
int fcoe_link_speed_update(struct fc_lport *);
void fcoe_get_lesb(struct fc_lport *, struct fc_els_lesb *);
void fcoe_ctlr_get_lesb(struct fcoe_ctlr_device *ctlr_dev);

/**
 * is_fip_mode() - returns true if FIP mode selected.
 * @fip:	FCoE controller.
 */
static inline bool is_fip_mode(struct fcoe_ctlr *fip)
{
	return fip->state == FIP_ST_ENABLED;
}

/* helper for FCoE SW HBA drivers, can include subven and subdev if needed. The
 * modpost would use pci_device_id table to auto-generate formatted module alias
 * into the corresponding .mod.c file, but there may or may not be a pci device
 * id table for FCoE drivers so we use the following helper for build the fcoe
 * driver module alias.
 */
#define MODULE_ALIAS_FCOE_PCI(ven, dev) \
	MODULE_ALIAS("fcoe-pci:"	\
		"v" __stringify(ven)	\
		"d" __stringify(dev) "sv*sd*bc*sc*i*")

/* the name of the default FCoE transport driver fcoe.ko */
#define FCOE_TRANSPORT_DEFAULT	"fcoe"

/* struct fcoe_transport - The FCoE transport interface
 * @name:	a vendor specific name for their FCoE transport driver
 * @attached:	whether this transport is already attached
 * @list:	list linkage to all attached transports
 * @match:	handler to allow the transport driver to match up a given netdev
 * @alloc:      handler to allocate per-instance FCoE structures
 *		(no discovery or login)
 * @create:	handler to sysfs entry of create for FCoE instances
 * @destroy:    handler to delete per-instance FCoE structures
 *		(frees all memory)
 * @enable:	handler to sysfs entry of enable for FCoE instances
 * @disable:	handler to sysfs entry of disable for FCoE instances
 */
struct fcoe_transport {
	char name[IFNAMSIZ];
	bool attached;
	struct list_head list;
	bool (*match) (struct net_device *device);
	int (*alloc) (struct net_device *device);
	int (*create) (struct net_device *device, enum fip_mode fip_mode);
	int (*destroy) (struct net_device *device);
	int (*enable) (struct net_device *device);
	int (*disable) (struct net_device *device);
};

/**
 * struct fcoe_percpu_s - The context for FCoE receive thread(s)
 * @kthread:	    The thread context (used by bnx2fc)
 * @work:	    The work item (used by fcoe)
 * @fcoe_rx_list:   The queue of pending packets to process
 * @page:	    The memory page for calculating frame trailer CRCs
 * @crc_eof_offset: The offset into the CRC page pointing to available
 *		    memory for a new trailer
 */
struct fcoe_percpu_s {
	struct task_struct *kthread;
	struct work_struct work;
	struct sk_buff_head fcoe_rx_list;
	struct page *crc_eof_page;
	int crc_eof_offset;
	local_lock_t lock;
};

/**
 * struct fcoe_port - The FCoE private structure
 * @priv:		       The associated fcoe interface. The structure is
 *			       defined by the low level driver
 * @lport:		       The associated local port
 * @fcoe_pending_queue:	       The pending Rx queue of skbs
 * @fcoe_pending_queue_active: Indicates if the pending queue is active
 * @max_queue_depth:	       Max queue depth of pending queue
 * @min_queue_depth:	       Min queue depth of pending queue
 * @timer:		       The queue timer
 * @destroy_work:	       Handle for work context
 *			       (to prevent RTNL deadlocks)
 * @data_srt_addr:	       Source address for data
 *
 * An instance of this structure is to be allocated along with the
 * Scsi_Host and libfc fc_lport structures.
 */
struct fcoe_port {
	void		      *priv;
	struct fc_lport	      *lport;
	struct sk_buff_head   fcoe_pending_queue;
	u8		      fcoe_pending_queue_active;
	u32		      max_queue_depth;
	u32		      min_queue_depth;
	struct timer_list     timer;
	struct work_struct    destroy_work;
	u8		      data_src_addr[ETH_ALEN];
	struct net_device * (*get_netdev)(const struct fc_lport *lport);
};

/**
 * fcoe_get_netdev() - Return the net device associated with a local port
 * @lport: The local port to get the net device from
 */
static inline struct net_device *fcoe_get_netdev(const struct fc_lport *lport)
{
	struct fcoe_port *port = ((struct fcoe_port *)lport_priv(lport));

	return (port->get_netdev) ? port->get_netdev(lport) : NULL;
}

void fcoe_clean_pending_queue(struct fc_lport *);
void fcoe_check_wait_queue(struct fc_lport *lport, struct sk_buff *skb);
void fcoe_queue_timer(struct timer_list *t);
int fcoe_get_paged_crc_eof(struct sk_buff *skb, int tlen,
			   struct fcoe_percpu_s *fps);

/* FCoE Sysfs helpers */
void fcoe_fcf_get_selected(struct fcoe_fcf_device *);
void fcoe_ctlr_set_fip_mode(struct fcoe_ctlr_device *);

/**
 * struct netdev_list
 * A mapping from netdevice to fcoe_transport
 */
struct fcoe_netdev_mapping {
	struct list_head list;
	struct net_device *netdev;
	struct fcoe_transport *ft;
};

/* fcoe transports registration and deregistration */
int fcoe_transport_attach(struct fcoe_transport *ft);
int fcoe_transport_detach(struct fcoe_transport *ft);

/* sysfs store handler for ctrl_control interface */
ssize_t fcoe_ctlr_create_store(const char *buf, size_t count);
ssize_t fcoe_ctlr_destroy_store(const char *buf, size_t count);

#endif /* _LIBFCOE_H */


