/*****************************************************************************
* wanrouter.h	Definitions for the WAN Multiprotocol Router Module.
*		This module provides API and common services for WAN Link
*		Drivers and is completely hardware-independent.
*
* Author: 	Nenad Corbic <ncorbic@sangoma.com>
*		Gideon Hack 	
* Additions:	Arnaldo Melo
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jul 21, 2000  Nenad Corbic	Added WAN_FT1_READY State
* Feb 24, 2000  Nenad Corbic    Added support for socket based x25api
* Jan 28, 2000  Nenad Corbic    Added support for the ASYNC protocol.
* Oct 04, 1999  Nenad Corbic 	Updated for 2.1.0 release
* Jun 02, 1999  Gideon Hack	Added support for the S514 adapter.
* May 23, 1999	Arnaldo Melo	Added local_addr to wanif_conf_t
*				WAN_DISCONNECTING state added
* Jul 20, 1998	David Fong	Added Inverse ARP options to 'wanif_conf_t'
* Jun 12, 1998	David Fong	Added Cisco HDLC support.
* Dec 16, 1997	Jaspreet Singh	Moved 'enable_IPX' and 'network_number' to
*				'wanif_conf_t'
* Dec 05, 1997	Jaspreet Singh	Added 'pap', 'chap' to 'wanif_conf_t'
*				Added 'authenticator' to 'wan_ppp_conf_t'
* Nov 06, 1997	Jaspreet Singh	Changed Router Driver version to 1.1 from 1.0
* Oct 20, 1997	Jaspreet Singh	Added 'cir','bc','be' and 'mc' to 'wanif_conf_t'
*				Added 'enable_IPX' and 'network_number' to 
*				'wan_device_t'.  Also added defines for
*				UDP PACKET TYPE, Interrupt test, critical values
*				for RACE conditions.
* Oct 05, 1997	Jaspreet Singh	Added 'dlci_num' and 'dlci[100]' to 
*				'wan_fr_conf_t' to configure a list of dlci(s)
*				for a NODE 
* Jul 07, 1997	Jaspreet Singh	Added 'ttl' to 'wandev_conf_t' & 'wan_device_t'
* May 29, 1997 	Jaspreet Singh	Added 'tx_int_enabled' to 'wan_device_t'
* May 21, 1997	Jaspreet Singh	Added 'udp_port' to 'wan_device_t'
* Apr 25, 1997  Farhan Thawar   Added 'udp_port' to 'wandev_conf_t'
* Jan 16, 1997	Gene Kozin	router_devlist made public
* Jan 02, 1997	Gene Kozin	Initial version (based on wanpipe.h).
*****************************************************************************/

#ifndef	_ROUTER_H
#define	_ROUTER_H

#define	ROUTER_NAME	"wanrouter"	/* in case we ever change it */
#define	ROUTER_VERSION	1		/* version number */
#define	ROUTER_RELEASE	1		/* release (minor version) number */
#define	ROUTER_IOCTL	'W'		/* for IOCTL calls */
#define	ROUTER_MAGIC	0x524D4157L	/* signature: 'WANR' reversed */

/* IOCTL codes for /proc/router/<device> entries (up to 255) */
enum router_ioctls
{
	ROUTER_SETUP	= ROUTER_IOCTL<<8,	/* configure device */
	ROUTER_DOWN,				/* shut down device */
	ROUTER_STAT,				/* get device status */
	ROUTER_IFNEW,				/* add interface */
	ROUTER_IFDEL,				/* delete interface */
	ROUTER_IFSTAT,				/* get interface status */
	ROUTER_USER	= (ROUTER_IOCTL<<8)+16,	/* driver-specific calls */
	ROUTER_USER_MAX	= (ROUTER_IOCTL<<8)+31
};

/* identifiers for displaying proc file data for dual port adapters */
#define PROC_DATA_PORT_0 0x8000	/* the data is for port 0 */
#define PROC_DATA_PORT_1 0x8001	/* the data is for port 1 */

/* NLPID for packet encapsulation (ISO/IEC TR 9577) */
#define	NLPID_IP	0xCC	/* Internet Protocol Datagram */
#define	NLPID_SNAP	0x80	/* IEEE Subnetwork Access Protocol */
#define	NLPID_CLNP	0x81	/* ISO/IEC 8473 */
#define	NLPID_ESIS	0x82	/* ISO/IEC 9542 */
#define	NLPID_ISIS	0x83	/* ISO/IEC ISIS */
#define	NLPID_Q933	0x08	/* CCITT Q.933 */

/* Miscellaneous */
#define	WAN_IFNAME_SZ	15	/* max length of the interface name */
#define	WAN_DRVNAME_SZ	15	/* max length of the link driver name */
#define	WAN_ADDRESS_SZ	31	/* max length of the WAN media address */
#define USED_BY_FIELD	8	/* max length of the used by field */

/* Defines for UDP PACKET TYPE */
#define UDP_PTPIPE_TYPE 	0x01
#define UDP_FPIPE_TYPE		0x02
#define UDP_CPIPE_TYPE		0x03
#define UDP_DRVSTATS_TYPE 	0x04
#define UDP_INVALID_TYPE  	0x05

/* Command return code */
#define CMD_OK		0		/* normal firmware return code */
#define CMD_TIMEOUT	0xFF		/* firmware command timed out */

/* UDP Packet Management */
#define UDP_PKT_FRM_STACK	0x00
#define UDP_PKT_FRM_NETWORK	0x01

/* Maximum interrupt test counter */
#define MAX_INTR_TEST_COUNTER	100

/* Critical Values for RACE conditions*/
#define CRITICAL_IN_ISR		0xA1
#define CRITICAL_INTR_HANDLED	0xB1

/****** Data Types **********************************************************/

/*----------------------------------------------------------------------------
 * X.25-specific link-level configuration.
 */
typedef struct wan_x25_conf
{
	unsigned lo_pvc;	/* lowest permanent circuit number */
	unsigned hi_pvc;	/* highest permanent circuit number */
	unsigned lo_svc;	/* lowest switched circuit number */
	unsigned hi_svc;	/* highest switched circuit number */
	unsigned hdlc_window;	/* HDLC window size (1..7) */
	unsigned pkt_window;	/* X.25 packet window size (1..7) */
	unsigned t1;		/* HDLC timer T1, sec (1..30) */
	unsigned t2;		/* HDLC timer T2, sec (0..29) */
	unsigned t4;		/* HDLC supervisory frame timer = T4 * T1 */
	unsigned n2;		/* HDLC retransmission limit (1..30) */
	unsigned t10_t20;	/* X.25 RESTART timeout, sec (1..255) */
	unsigned t11_t21;	/* X.25 CALL timeout, sec (1..255) */
	unsigned t12_t22;	/* X.25 RESET timeout, sec (1..255) */
	unsigned t13_t23;	/* X.25 CLEAR timeout, sec (1..255) */
	unsigned t16_t26;	/* X.25 INTERRUPT timeout, sec (1..255) */
	unsigned t28;		/* X.25 REGISTRATION timeout, sec (1..255) */
	unsigned r10_r20;	/* RESTART retransmission limit (0..250) */
	unsigned r12_r22;	/* RESET retransmission limit (0..250) */
	unsigned r13_r23;	/* CLEAR retransmission limit (0..250) */
	unsigned ccitt_compat;	/* compatibility mode: 1988/1984/1980 */
	unsigned x25_conf_opt;   /* User defined x25 config optoins */
	unsigned char LAPB_hdlc_only; /* Run in HDLC only mode */
	unsigned char logging;   /* Control connection logging */  
	unsigned char oob_on_modem; /* Whether to send modem status to the user app */
} wan_x25_conf_t;

/*----------------------------------------------------------------------------
 * Frame relay specific link-level configuration.
 */
typedef struct wan_fr_conf
{
	unsigned signalling;	/* local in-channel signalling type */
	unsigned t391;		/* link integrity verification timer */
	unsigned t392;		/* polling verification timer */
	unsigned n391;		/* full status polling cycle counter */
	unsigned n392;		/* error threshold counter */
	unsigned n393;		/* monitored events counter */
	unsigned dlci_num;	/* number of DLCs (access node) */
	unsigned  dlci[100];    /* List of all DLCIs */
} wan_fr_conf_t;

/*----------------------------------------------------------------------------
 * PPP-specific link-level configuration.
 */
typedef struct wan_ppp_conf
{
	unsigned restart_tmr;	/* restart timer */
	unsigned auth_rsrt_tmr;	/* authentication timer */
	unsigned auth_wait_tmr;	/* authentication timer */
	unsigned mdm_fail_tmr;	/* modem failure timer */
	unsigned dtr_drop_tmr;	/* DTR drop timer */
	unsigned connect_tmout;	/* connection timeout */
	unsigned conf_retry;	/* max. retry */
	unsigned term_retry;	/* max. retry */
	unsigned fail_retry;	/* max. retry */
	unsigned auth_retry;	/* max. retry */
	unsigned auth_options;	/* authentication opt. */
	unsigned ip_options;	/* IP options */
	char	authenticator;	/* AUTHENTICATOR or not */
	char	ip_mode;	/* Static/Host/Peer */
} wan_ppp_conf_t;

/*----------------------------------------------------------------------------
 * CHDLC-specific link-level configuration.
 */
typedef struct wan_chdlc_conf
{
	unsigned char ignore_dcd;	/* Protocol options:		*/
	unsigned char ignore_cts;	/*  Ignore these to determine	*/
	unsigned char ignore_keepalive;	/*  link status (Yes or No)	*/
	unsigned char hdlc_streaming;	/*  hdlc_streaming mode (Y/N) */
	unsigned char receive_only;	/*  no transmit buffering (Y/N) */
	unsigned keepalive_tx_tmr;	/* transmit keepalive timer */
	unsigned keepalive_rx_tmr;	/* receive  keepalive timer */
	unsigned keepalive_err_margin;	/* keepalive_error_tolerance */
	unsigned slarp_timer;		/* SLARP request timer */
} wan_chdlc_conf_t;


/*----------------------------------------------------------------------------
 * WAN device configuration. Passed to ROUTER_SETUP IOCTL.
 */
typedef struct wandev_conf
{
	unsigned magic;		/* magic number (for verification) */
	unsigned config_id;	/* configuration structure identifier */
				/****** hardware configuration ******/
	unsigned ioport;	/* adapter I/O port base */
	unsigned long maddr;	/* dual-port memory address */
	unsigned msize;		/* dual-port memory size */
	int irq;		/* interrupt request level */
	int dma;		/* DMA request level */
        char S514_CPU_no[1];	/* S514 PCI adapter CPU number ('A' or 'B') */
        unsigned PCI_slot_no;	/* S514 PCI adapter slot number */
	char auto_pci_cfg;	/* S515 PCI automatic slot detection */
	char comm_port;		/* Communication Port (PRI=0, SEC=1) */ 
	unsigned bps;		/* data transfer rate */
	unsigned mtu;		/* maximum transmit unit size */
        unsigned udp_port;      /* UDP port for management */
	unsigned char ttl;	/* Time To Live for UDP security */
	unsigned char ft1;	/* FT1 Configurator Option */
        char interface;		/* RS-232/V.35, etc. */
	char clocking;		/* external/internal */
	char line_coding;	/* NRZ/NRZI/FM0/FM1, etc. */
	char station;		/* DTE/DCE, primary/secondary, etc. */
	char connection;	/* permanent/switched/on-demand */
	char read_mode;		/* read mode: Polling or interrupt */
	char receive_only;	/* disable tx buffers */
	char tty;		/* Create a fake tty device */
	unsigned tty_major;	/* Major number for wanpipe tty device */
	unsigned tty_minor; 	/* Minor number for wanpipe tty device */
	unsigned tty_mode;	/* TTY operation mode SYNC or ASYNC */
	char backup;		/* Backup Mode */
	unsigned hw_opt[4];	/* other hardware options */
	unsigned reserved[4];
				/****** arbitrary data ***************/
	unsigned data_size;	/* data buffer size */
	void* data;		/* data buffer, e.g. firmware */
	union			/****** protocol-specific ************/
	{
		wan_x25_conf_t x25;	/* X.25 configuration */
		wan_ppp_conf_t ppp;	/* PPP configuration */
		wan_fr_conf_t fr;	/* frame relay configuration */
		wan_chdlc_conf_t chdlc;	/* Cisco HDLC configuration */
	} u;
} wandev_conf_t;

/* 'config_id' definitions */
#define	WANCONFIG_X25	101	/* X.25 link */
#define	WANCONFIG_FR	102	/* frame relay link */
#define	WANCONFIG_PPP	103	/* synchronous PPP link */
#define WANCONFIG_CHDLC	104	/* Cisco HDLC Link */
#define WANCONFIG_BSC	105	/* BiSync Streaming */
#define WANCONFIG_HDLC	106	/* HDLC Support */
#define WANCONFIG_MPPP  107	/* Multi Port PPP over RAW CHDLC */

/*
 * Configuration options defines.
 */
/* general options */
#define	WANOPT_OFF	0
#define	WANOPT_ON	1
#define	WANOPT_NO	0
#define	WANOPT_YES	1

/* intercace options */
#define	WANOPT_RS232	0
#define	WANOPT_V35	1

/* data encoding options */
#define	WANOPT_NRZ	0
#define	WANOPT_NRZI	1
#define	WANOPT_FM0	2
#define	WANOPT_FM1	3

/* link type options */
#define	WANOPT_POINTTOPOINT	0	/* RTS always active */
#define	WANOPT_MULTIDROP	1	/* RTS is active when transmitting */

/* clocking options */
#define	WANOPT_EXTERNAL	0
#define	WANOPT_INTERNAL	1

/* station options */
#define	WANOPT_DTE		0
#define	WANOPT_DCE		1
#define	WANOPT_CPE		0
#define	WANOPT_NODE		1
#define	WANOPT_SECONDARY	0
#define	WANOPT_PRIMARY		1

/* connection options */
#define	WANOPT_PERMANENT	0	/* DTR always active */
#define	WANOPT_SWITCHED		1	/* use DTR to setup link (dial-up) */
#define	WANOPT_ONDEMAND		2	/* activate DTR only before sending */

/* frame relay in-channel signalling */
#define	WANOPT_FR_ANSI		1	/* ANSI T1.617 Annex D */
#define	WANOPT_FR_Q933		2	/* ITU Q.933A */
#define	WANOPT_FR_LMI		3	/* LMI */

/* PPP IP Mode Options */
#define	WANOPT_PPP_STATIC	0
#define	WANOPT_PPP_HOST		1
#define	WANOPT_PPP_PEER		2

/* ASY Mode Options */
#define WANOPT_ONE 		1
#define WANOPT_TWO		2
#define WANOPT_ONE_AND_HALF	3

#define WANOPT_NONE	0
#define WANOPT_ODD      1
#define WANOPT_EVEN	2

/* CHDLC Protocol Options */
/* DF Commmented out for now.

#define WANOPT_CHDLC_NO_DCD		IGNORE_DCD_FOR_LINK_STAT
#define WANOPT_CHDLC_NO_CTS		IGNORE_CTS_FOR_LINK_STAT
#define WANOPT_CHDLC_NO_KEEPALIVE	IGNORE_KPALV_FOR_LINK_STAT
*/

/* Port options */
#define WANOPT_PRI 0
#define WANOPT_SEC 1
/* read mode */
#define	WANOPT_INTR	0
#define WANOPT_POLL	1


#define WANOPT_TTY_SYNC  0
#define WANOPT_TTY_ASYNC 1
/*----------------------------------------------------------------------------
 * WAN Link Status Info (for ROUTER_STAT IOCTL).
 */
typedef struct wandev_stat
{
	unsigned state;		/* link state */
	unsigned ndev;		/* number of configured interfaces */

	/* link/interface configuration */
	unsigned connection;	/* permanent/switched/on-demand */
	unsigned media_type;	/* Frame relay/PPP/X.25/SDLC, etc. */
	unsigned mtu;		/* max. transmit unit for this device */

	/* physical level statistics */
	unsigned modem_status;	/* modem status */
	unsigned rx_frames;	/* received frames count */
	unsigned rx_overruns;	/* receiver overrun error count */
	unsigned rx_crc_err;	/* receive CRC error count */
	unsigned rx_aborts;	/* received aborted frames count */
	unsigned rx_bad_length;	/* unexpetedly long/short frames count */
	unsigned rx_dropped;	/* frames discarded at device level */
	unsigned tx_frames;	/* transmitted frames count */
	unsigned tx_underruns;	/* aborted transmissions (underruns) count */
	unsigned tx_timeouts;	/* transmission timeouts */
	unsigned tx_rejects;	/* other transmit errors */

	/* media level statistics */
	unsigned rx_bad_format;	/* frames with invalid format */
	unsigned rx_bad_addr;	/* frames with invalid media address */
	unsigned tx_retries;	/* frames re-transmitted */
	unsigned reserved[16];	/* reserved for future use */
} wandev_stat_t;

/* 'state' defines */
enum wan_states
{
	WAN_UNCONFIGURED,	/* link/channel is not configured */
	WAN_DISCONNECTED,	/* link/channel is disconnected */
	WAN_CONNECTING,		/* connection is in progress */
	WAN_CONNECTED,		/* link/channel is operational */
	WAN_LIMIT,		/* for verification only */
	WAN_DUALPORT,		/* for Dual Port cards */
	WAN_DISCONNECTING,
	WAN_FT1_READY		/* FT1 Configurator Ready */
};

enum {
	WAN_LOCAL_IP,
	WAN_POINTOPOINT_IP,
	WAN_NETMASK_IP,
	WAN_BROADCAST_IP
};

/* 'modem_status' masks */
#define	WAN_MODEM_CTS	0x0001	/* CTS line active */
#define	WAN_MODEM_DCD	0x0002	/* DCD line active */
#define	WAN_MODEM_DTR	0x0010	/* DTR line active */
#define	WAN_MODEM_RTS	0x0020	/* RTS line active */

/*----------------------------------------------------------------------------
 * WAN interface (logical channel) configuration (for ROUTER_IFNEW IOCTL).
 */
typedef struct wanif_conf
{
	unsigned magic;			/* magic number */
	unsigned config_id;		/* configuration identifier */
	char name[WAN_IFNAME_SZ+1];	/* interface name, ASCIIZ */
	char addr[WAN_ADDRESS_SZ+1];	/* media address, ASCIIZ */
	char usedby[USED_BY_FIELD];	/* used by API or WANPIPE */
	unsigned idle_timeout;		/* sec, before disconnecting */
	unsigned hold_timeout;		/* sec, before re-connecting */
	unsigned cir;			/* Committed Information Rate fwd,bwd*/
	unsigned bc;			/* Committed Burst Size fwd, bwd */
	unsigned be;			/* Excess Burst Size fwd, bwd */ 
	unsigned char enable_IPX;	/* Enable or Disable IPX */
	unsigned char inarp;		/* Send Inverse ARP requests Y/N */
	unsigned inarp_interval;	/* sec, between InARP requests */
	unsigned long network_number;	/* Network Number for IPX */
	char mc;			/* Multicast on or off */
	char local_addr[WAN_ADDRESS_SZ+1];/* local media address, ASCIIZ */
	unsigned char port;		/* board port */
	unsigned char protocol;		/* prococol used in this channel (TCPOX25 or X25) */
	char pap;			/* PAP enabled or disabled */
	char chap;			/* CHAP enabled or disabled */
	unsigned char userid[511];	/* List of User Id */
	unsigned char passwd[511];	/* List of passwords */
	unsigned char sysname[31];	/* Name of the system */
	unsigned char ignore_dcd;	/* Protocol options: */
	unsigned char ignore_cts;	/*  Ignore these to determine */
	unsigned char ignore_keepalive;	/*  link status (Yes or No) */
	unsigned char hdlc_streaming;	/*  Hdlc streaming mode (Y/N) */
	unsigned keepalive_tx_tmr;	/* transmit keepalive timer */
	unsigned keepalive_rx_tmr;	/* receive  keepalive timer */
	unsigned keepalive_err_margin;	/* keepalive_error_tolerance */
	unsigned slarp_timer;		/* SLARP request timer */
	unsigned char ttl;		/* Time To Live for UDP security */
	char interface;			/* RS-232/V.35, etc. */
	char clocking;			/* external/internal */
	unsigned bps;			/* data transfer rate */
	unsigned mtu;			/* maximum transmit unit size */
	unsigned char if_down;		/* brind down interface when disconnected */
	unsigned char gateway;		/* Is this interface a gateway */
	unsigned char true_if_encoding;	/* Set the dev->type to true board protocol */

	unsigned char asy_data_trans;     /* async API options */
        unsigned char rts_hs_for_receive; /* async Protocol options */
        unsigned char xon_xoff_hs_for_receive;
	unsigned char xon_xoff_hs_for_transmit;
	unsigned char dcd_hs_for_transmit;
	unsigned char cts_hs_for_transmit;
	unsigned char async_mode;
	unsigned tx_bits_per_char;
	unsigned rx_bits_per_char;
	unsigned stop_bits;  
	unsigned char parity;
 	unsigned break_timer;
        unsigned inter_char_timer;
	unsigned rx_complete_length;
	unsigned xon_char;
	unsigned xoff_char;
	unsigned char receive_only;	/*  no transmit buffering (Y/N) */
} wanif_conf_t;

#ifdef	__KERNEL__
/****** Kernel Interface ****************************************************/

#include <linux/fs.h>		/* support for device drivers */
#include <linux/proc_fs.h>	/* proc filesystem pragmatics */
#include <linux/netdevice.h>	/* support for network drivers */
#include <linux/spinlock.h>     /* Support for SMP Locking */

/*----------------------------------------------------------------------------
 * WAN device data space.
 */
struct wan_device {
	unsigned magic;			/* magic number */
	char* name;			/* -> WAN device name (ASCIIZ) */
	void* private;			/* -> driver private data */
	unsigned config_id;		/* Configuration ID */
					/****** hardware configuration ******/
	unsigned ioport;		/* adapter I/O port base #1 */
	char S514_cpu_no[1];		/* PCI CPU Number */
	unsigned char S514_slot_no;	/* PCI Slot Number */
	unsigned long maddr;		/* dual-port memory address */
	unsigned msize;			/* dual-port memory size */
	int irq;			/* interrupt request level */
	int dma;			/* DMA request level */
	unsigned bps;			/* data transfer rate */
	unsigned mtu;			/* max physical transmit unit size */
	unsigned udp_port;              /* UDP port for management */
        unsigned char ttl;		/* Time To Live for UDP security */
	unsigned enable_tx_int; 	/* Transmit Interrupt enabled or not */
	char interface;			/* RS-232/V.35, etc. */
	char clocking;			/* external/internal */
	char line_coding;		/* NRZ/NRZI/FM0/FM1, etc. */
	char station;			/* DTE/DCE, primary/secondary, etc. */
	char connection;		/* permanent/switched/on-demand */
	char signalling;		/* Signalling RS232 or V35 */
	char read_mode;			/* read mode: Polling or interrupt */
	char new_if_cnt;                /* Number of interfaces per wanpipe */ 
	char del_if_cnt;		/* Number of times del_if() gets called */
	unsigned char piggyback;        /* Piggibacking a port */
	unsigned hw_opt[4];		/* other hardware options */
					/****** status and statistics *******/
	char state;			/* device state */
	char api_status;		/* device api status */
	struct net_device_stats stats; 	/* interface statistics */
	unsigned reserved[16];		/* reserved for future use */
	unsigned long critical;		/* critical section flag */
	spinlock_t lock;                /* Support for SMP Locking */

					/****** device management methods ***/
	int (*setup) (struct wan_device *wandev, wandev_conf_t *conf);
	int (*shutdown) (struct wan_device *wandev);
	int (*update) (struct wan_device *wandev);
	int (*ioctl) (struct wan_device *wandev, unsigned cmd,
		unsigned long arg);
	int (*new_if)(struct wan_device *wandev, struct net_device *dev,
		      wanif_conf_t *conf);
	int (*del_if)(struct wan_device *wandev, struct net_device *dev);
					/****** maintained by the router ****/
	struct wan_device* next;	/* -> next device */
	struct net_device* dev;		/* list of network interfaces */
	unsigned ndev;			/* number of interfaces */
	struct proc_dir_entry *dent;	/* proc filesystem entry */
};

/* Public functions available for device drivers */
extern int register_wan_device(struct wan_device *wandev);
extern int unregister_wan_device(char *name);

/* Proc interface functions. These must not be called by the drivers! */
extern int wanrouter_proc_init(void);
extern void wanrouter_proc_cleanup(void);
extern int wanrouter_proc_add(struct wan_device *wandev);
extern int wanrouter_proc_delete(struct wan_device *wandev);
extern int wanrouter_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);

/* Public Data */
/* list of registered devices */
extern struct wan_device *wanrouter_router_devlist;

#endif	/* __KERNEL__ */
#endif	/* _ROUTER_H */
