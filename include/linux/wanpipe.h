/*****************************************************************************
* wanpipe.h	WANPIPE(tm) Multiprotocol WAN Link Driver.
*		User-level API definitions.
*
* Author: 	Nenad Corbic <ncorbic@sangoma.com>
*		Gideon Hack  	
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Nov 3,  2000  Nenad Corbic    Added config_id to sdla_t structure.
*                               Used to determine the protocol running.
* Jul 13, 2000  Nenad Corbic	Added SyncPPP Support
* Feb 24, 2000  Nenad Corbic    Added support for x25api driver
* Oct 04, 1999  Nenad Corbic    New CHDLC and FRAME RELAY code, SMP support
* Jun 02, 1999  Gideon Hack	Added 'update_call_count' for Cisco HDLC 
*				support
* Jun 26, 1998	David Fong	Added 'ip_mode' in sdla_t.u.p for dynamic IP
*				routing mode configuration
* Jun 12, 1998	David Fong	Added Cisco HDLC union member in sdla_t
* Dec 08, 1997	Jaspreet Singh  Added 'authenticator' in union of 'sdla_t' 
* Nov 26, 1997	Jaspreet Singh	Added 'load_sharing' structure.  Also added 
*				'devs_struct','dev_to_devtint_next' to 'sdla_t'	
* Nov 24, 1997	Jaspreet Singh	Added 'irq_dis_if_send_count', 
*				'irq_dis_poll_count' to 'sdla_t'.
* Nov 06, 1997	Jaspreet Singh	Added a define called 'INTR_TEST_MODE'
* Oct 20, 1997	Jaspreet Singh	Added 'buff_intr_mode_unbusy' and 
*				'dlci_intr_mode_unbusy' to 'sdla_t'
* Oct 18, 1997	Jaspreet Singh	Added structure to maintain global driver
*				statistics.
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o added UDP management stuff
* Jan 02, 1997	Gene Kozin	Version 3.0.0
*****************************************************************************/
#ifndef	_WANPIPE_H
#define	_WANPIPE_H

#include <linux/wanrouter.h>

/* Defines */

#ifndef	PACKED
#define	PACKED	__attribute__((packed))
#endif

#define	WANPIPE_MAGIC	0x414C4453L	/* signature: 'SDLA' reversed */

/* IOCTL numbers (up to 16) */
#define	WANPIPE_DUMP	(ROUTER_USER+0)	/* dump adapter's memory */
#define	WANPIPE_EXEC	(ROUTER_USER+1)	/* execute firmware command */

#define TRACE_ALL                       0x00
#define TRACE_PROT			0x01
#define TRACE_DATA			0x02

/* values for request/reply byte */
#define UDPMGMT_REQUEST	0x01
#define UDPMGMT_REPLY	0x02
#define UDP_OFFSET	12

#define MAX_CMD_BUFF 	10
#define MAX_X25_LCN 	255	/* Maximum number of x25 channels */
#define MAX_LCN_NUM	4095	/* Maximum lcn number */
#define MAX_FT1_RETRY 	100

#ifndef AF_WANPIPE
	#define AF_WANPIPE 25
	#ifndef PF_WANPIPE
		#define PF_WANPIPE AF_WANPIPE
	#endif
#endif


#define TX_TIMEOUT 5*HZ

/* General Critical Flags */
#define SEND_CRIT	0x00
#define PERI_CRIT	0x01

/* Chdlc and PPP polling critical flag */
#define POLL_CRIT	0x03

/* Frame Relay Tx IRQ send critical flag */
#define SEND_TXIRQ_CRIT 0x02

/* Frame Relay ARP critical flag */
#define ARP_CRIT	0x03

/* Bit maps for dynamic interface configuration
 * DYN_OPT_ON : turns this option on/off 
 * DEV_DOWN   : device was shutdown by the driver not
 *              by user 
 */
#define DYN_OPT_ON	0x00
#define DEV_DOWN	0x01

/*
 * Data structures for IOCTL calls.
 */

typedef struct sdla_dump	/* WANPIPE_DUMP */
{
	unsigned long magic;	/* for verification */
	unsigned long offset;	/* absolute adapter memory address */
	unsigned long length;	/* block length */
	void* ptr;		/* -> buffer */
} sdla_dump_t;

typedef struct sdla_exec	/* WANPIPE_EXEC */
{
	unsigned long magic;	/* for verification */
	void* cmd;		/* -> command structure */
	void* data;		/* -> data buffer */
} sdla_exec_t;

/* UDP management stuff */

typedef struct wum_header
{
	unsigned char signature[8];	/* 00h: signature */
	unsigned char type;		/* 08h: request/reply */
	unsigned char command;		/* 09h: commnand */
	unsigned char reserved[6];	/* 0Ah: reserved */
} wum_header_t;

/*************************************************************************
 Data Structure for global statistics
*************************************************************************/

typedef struct global_stats
{
	unsigned long isr_entry;
	unsigned long isr_already_critical;		
	unsigned long isr_rx;
	unsigned long isr_tx;
	unsigned long isr_intr_test;
	unsigned long isr_spurious;
	unsigned long isr_enable_tx_int;
	unsigned long rx_intr_corrupt_rx_bfr;
	unsigned long rx_intr_on_orphaned_DLCI;
	unsigned long rx_intr_dev_not_started;
	unsigned long tx_intr_dev_not_started;
	unsigned long poll_entry;
	unsigned long poll_already_critical;
	unsigned long poll_processed;
	unsigned long poll_tbusy_bad_status;
	unsigned long poll_host_disable_irq;
	unsigned long poll_host_enable_irq;

} global_stats_t;


typedef struct{
	unsigned short	udp_src_port		PACKED;
	unsigned short	udp_dst_port		PACKED;
	unsigned short	udp_length		PACKED;
	unsigned short	udp_checksum		PACKED;
} udp_pkt_t;


typedef struct {
	unsigned char	ver_inet_hdr_length	PACKED;
	unsigned char	service_type		PACKED;
	unsigned short	total_length		PACKED;
	unsigned short	identifier		PACKED;
	unsigned short	flags_frag_offset	PACKED;
	unsigned char	ttl			PACKED;
	unsigned char	protocol		PACKED;
	unsigned short	hdr_checksum		PACKED;
	unsigned long	ip_src_address		PACKED;
	unsigned long	ip_dst_address		PACKED;
} ip_pkt_t;


typedef struct {
        unsigned char           signature[8]    PACKED;
        unsigned char           request_reply   PACKED;
        unsigned char           id              PACKED;
        unsigned char           reserved[6]     PACKED;
} wp_mgmt_t;

/*************************************************************************
 Data Structure for if_send  statistics
*************************************************************************/  
typedef struct if_send_stat{
	unsigned long if_send_entry;
	unsigned long if_send_skb_null;
	unsigned long if_send_broadcast;
	unsigned long if_send_multicast;
	unsigned long if_send_critical_ISR;
	unsigned long if_send_critical_non_ISR;
	unsigned long if_send_tbusy;
	unsigned long if_send_tbusy_timeout;
	unsigned long if_send_PIPE_request;
	unsigned long if_send_wan_disconnected;
	unsigned long if_send_dlci_disconnected;
	unsigned long if_send_no_bfrs;
	unsigned long if_send_adptr_bfrs_full;
	unsigned long if_send_bfr_passed_to_adptr;
	unsigned long if_send_protocol_error;
       	unsigned long if_send_bfr_not_passed_to_adptr;
       	unsigned long if_send_tx_int_enabled;
        unsigned long if_send_consec_send_fail; 
} if_send_stat_t;

typedef struct rx_intr_stat{
	unsigned long rx_intr_no_socket;
	unsigned long rx_intr_dev_not_started;
	unsigned long rx_intr_PIPE_request;
	unsigned long rx_intr_bfr_not_passed_to_stack;
	unsigned long rx_intr_bfr_passed_to_stack;
} rx_intr_stat_t;	

typedef struct pipe_mgmt_stat{
	unsigned long UDP_PIPE_mgmt_kmalloc_err;
	unsigned long UDP_PIPE_mgmt_direction_err;
	unsigned long UDP_PIPE_mgmt_adptr_type_err;
	unsigned long UDP_PIPE_mgmt_adptr_cmnd_OK;
	unsigned long UDP_PIPE_mgmt_adptr_cmnd_timeout;
	unsigned long UDP_PIPE_mgmt_adptr_send_passed;
	unsigned long UDP_PIPE_mgmt_adptr_send_failed;
	unsigned long UDP_PIPE_mgmt_not_passed_to_stack;
	unsigned long UDP_PIPE_mgmt_passed_to_stack;
	unsigned long UDP_PIPE_mgmt_no_socket;
        unsigned long UDP_PIPE_mgmt_passed_to_adptr;
} pipe_mgmt_stat_t;


typedef struct {
	struct sk_buff *skb;
} bh_data_t, cmd_data_t;

#define MAX_LGTH_UDP_MGNT_PKT 2000
 

/* This is used for interrupt testing */
#define INTR_TEST_MODE	0x02

#define	WUM_SIGNATURE_L	0x50495046
#define	WUM_SIGNATURE_H	0x444E3845

#define	WUM_KILL	0x50
#define	WUM_EXEC	0x51

#define WANPIPE		0x00
#define API		0x01
#define BRIDGE		0x02
#define BRIDGE_NODE	0x03

#ifdef	__KERNEL__
/****** Kernel Interface ****************************************************/

#include <linux/sdladrv.h>	/* SDLA support module API definitions */
#include <linux/sdlasfm.h>	/* SDLA firmware module definitions */
#include <linux/workqueue.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

/****** Data Structures *****************************************************/

/* Adapter Data Space.
 * This structure is needed because we handle multiple cards, otherwise
 * static data would do it.
 */
typedef struct sdla
{
	char devname[WAN_DRVNAME_SZ+1];	/* card name */
	sdlahw_t hw;			/* hardware configuration */
	struct wan_device wandev;	/* WAN device data space */
	
	unsigned open_cnt;		/* number of open interfaces */
	unsigned long state_tick;	/* link state timestamp */
	unsigned intr_mode;		/* Type of Interrupt Mode */
	char in_isr;			/* interrupt-in-service flag */
	char buff_int_mode_unbusy;	/* flag for carrying out dev_tint */  
	char dlci_int_mode_unbusy;	/* flag for carrying out dev_tint */
	long configured;		/* flag for previous configurations */
	
	unsigned short irq_dis_if_send_count; /* Disabling irqs in if_send*/
	unsigned short irq_dis_poll_count;   /* Disabling irqs in poll routine*/
	unsigned short force_enable_irq;
	char TracingEnabled;		/* flag for enabling trace */
	global_stats_t statistics;	/* global statistics */
	void* mbox;			/* -> mailbox */
	void* rxmb;			/* -> receive mailbox */
	void* flags;			/* -> adapter status flags */
	void (*isr)(struct sdla* card);	/* interrupt service routine */
	void (*poll)(struct sdla* card); /* polling routine */
	int (*exec)(struct sdla* card, void* u_cmd, void* u_data);
					/* Used by the listen() system call */		
	/* Wanpipe Socket Interface */
	int   (*func) (struct sk_buff *, struct sock *);
	struct sock *sk;

	/* Shutdown function */
	void (*disable_comm) (struct sdla *card);

	/* Secondary Port Device: Piggibacking */
	struct sdla *next;

	/* TTY driver variables */
	unsigned char tty_opt;
	struct tty_struct *tty;
	unsigned int tty_minor;
	unsigned int tty_open;
	unsigned char *tty_buf;
	unsigned char *tty_rx;
	struct work_struct tty_work;
	
	union
	{
		struct
		{			/****** X.25 specific data **********/
			u32 lo_pvc;
			u32 hi_pvc;
			u32 lo_svc;
			u32 hi_svc;
			struct net_device *svc_to_dev_map[MAX_X25_LCN];
			struct net_device *pvc_to_dev_map[MAX_X25_LCN];
			struct net_device *tx_dev;
			struct net_device *cmd_dev;
			u32 no_dev;
			volatile u8 *hdlc_buf_status;
			u32 tx_interrupts_pending;
                        u16 timer_int_enabled;
			struct net_device *poll_device;
			atomic_t command_busy;

			u16 udp_pkt_lgth;
                        u32 udp_type;
                        u8  udp_pkt_src;
			u32 udp_lcn;
                        struct net_device *udp_dev;
                        s8 udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];

		 	u8 LAPB_hdlc;		/* Option to turn off X25 and run only LAPB */
			u8 logging;		/* Option to log call messages */
			u8 oob_on_modem;	/* Option to send modem status to the api */
			u16 num_of_ch;		/* Number of channels configured by the user */

			struct work_struct x25_poll_work;
			struct timer_list x25_timer;
		} x;
		struct
		{			/****** frame relay specific data ***/
			void* rxmb_base;	/* -> first Rx buffer */
			void* rxmb_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
			unsigned short node_dlci[100];
			unsigned short dlci_num;
                        struct net_device *dlci_to_dev_map[991 + 1];
                        unsigned tx_interrupts_pending;
                        unsigned short timer_int_enabled;
                        unsigned short udp_pkt_lgth;
                        int udp_type;
                        char udp_pkt_src;
                        unsigned udp_dlci;
                        char udp_pkt_data[MAX_LGTH_UDP_MGNT_PKT];
                        void* trc_el_base;      		/* first trace element */
                        void* trc_el_last;      		/* last trace element */
                        void *curr_trc_el;      		/* current trace element */
                        unsigned short trc_bfr_space; 		/* trace buffer space */
			unsigned char  update_comms_stats;
			struct net_device *arp_dev;
			spinlock_t if_send_lock;
		} f;
		struct			/****** PPP-specific data ***********/
		{
			char if_name[WAN_IFNAME_SZ+1];	/* interface name */
			void* txbuf;		/* -> current Tx buffer */
			void* txbuf_base;	/* -> first Tx buffer */
			void* txbuf_last;	/* -> last Tx buffer */
			void* rxbuf_base;	/* -> first Rx buffer */
			void* rxbuf_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
			char ip_mode;		/* STATIC/HOST/PEER IP Mode */
			char authenticator;	/* Authenticator for PAP/CHAP */
			unsigned char comm_enabled; /* Is comm enabled or not */
			unsigned char peer_route;   /* Process Peer Route */	
			unsigned long *txbuf_next;  /* Next Tx buffer to use */ 
			unsigned long *rxbuf_next;  /* Next Rx buffer to use */
		} p;
		struct			/* Cisco HDLC-specific data */
		{
			char if_name[WAN_IFNAME_SZ+1];	/* interface name */
			unsigned char comm_port;/* Communication Port O or 1 */
			unsigned char usedby;  /* Used by WANPIPE or API */
			void* rxmb;		/* Receive mail box */
			void* flags;		/* flags */
			void* tx_status;	/* Tx status element */
			void* rx_status;	/* Rx status element */
			void* txbuf;		/* -> current Tx buffer */
			void* txbuf_base;	/* -> first Tx buffer */
			void* txbuf_last;	/* -> last Tx buffer */
			void* rxbuf_base;	/* -> first Rx buffer */
			void* rxbuf_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
			unsigned char receive_only; /* high speed receivers */
			unsigned short protocol_options;
			unsigned short kpalv_tx;	/* Tx kpalv timer */
			unsigned short kpalv_rx;	/* Rx kpalv timer */
			unsigned short kpalv_err;	/* Error tolerance */
			unsigned short slarp_timer;	/* SLARP req timer */
			unsigned state;			/* state of the link */
			unsigned char api_status;
			unsigned char update_call_count;
			unsigned short api_options;	/* for async config */
			unsigned char  async_mode;
		        unsigned short tx_bits_per_char;
		        unsigned short rx_bits_per_char;
		        unsigned short stop_bits;
		        unsigned short parity;
			unsigned short break_timer;
		        unsigned short inter_char_timer;
		        unsigned short rx_complete_length;
		        unsigned short xon_char;
		        unsigned short xoff_char;
			unsigned char comm_enabled; /* Is comm enabled or not */
			unsigned char backup;
		} c;
		struct
		{
			void* tx_status;	/* Tx status element */
			void* rx_status;	/* Rx status element */
			void* trace_status;	/* Trace status element */
			void* txbuf;		/* -> current Tx buffer */
			void* txbuf_base;	/* -> first Tx buffer */
			void* txbuf_last;	/* -> last Tx buffer */
			void* rxbuf_base;	/* -> first Rx buffer */
			void* rxbuf_last;	/* -> last Rx buffer */
			void* tracebuf;		/* -> current Trace buffer */
			void* tracebuf_base;	/* -> current Trace buffer */
			void* tracebuf_last;	/* -> current Trace buffer */
			unsigned rx_base;	/* receive buffer base */
			unsigned rx_end;	/* receive buffer end */
			unsigned trace_base;	/* trace buffer base */
			unsigned trace_end;	/* trace buffer end */

		} h;
	} u;
} sdla_t;

/****** Public Functions ****************************************************/

void wanpipe_open      (sdla_t* card);			/* wpmain.c */
void wanpipe_close     (sdla_t* card);			/* wpmain.c */
void wanpipe_set_state (sdla_t* card, int state);	/* wpmain.c */

int wpx_init (sdla_t* card, wandev_conf_t* conf);	/* wpx.c */
int wpf_init (sdla_t* card, wandev_conf_t* conf);	/* wpf.c */
int wpp_init (sdla_t* card, wandev_conf_t* conf);	/* wpp.c */
int wpc_init (sdla_t* card, wandev_conf_t* conf); /* Cisco HDLC */
int bsc_init (sdla_t* card, wandev_conf_t* conf);	/* BSC streaming */
int hdlc_init(sdla_t* card, wandev_conf_t* conf);	/* HDLC support */
int wpft1_init (sdla_t* card, wandev_conf_t* conf);     /* FT1 Config support */
int wsppp_init (sdla_t* card, wandev_conf_t* conf);	/* Sync PPP on top of RAW CHDLC */

extern sdla_t * wanpipe_find_card(char *);
extern sdla_t * wanpipe_find_card_num (int);

extern void wanpipe_queue_work (struct work_struct *);
extern void wanpipe_mark_bh (void);
extern void wakeup_sk_bh(struct net_device *dev);
extern int change_dev_flags(struct net_device *dev, unsigned flags);
extern unsigned long get_ip_address(struct net_device *dev, int option);
extern void add_gateway(sdla_t *card, struct net_device *dev);


#endif	/* __KERNEL__ */
#endif	/* _WANPIPE_H */

