/*
 *	IrNET protocol module : Synchronous PPP over an IrDA socket.
 *
 *		Jean II - HPL `00 - <jt@hpl.hp.com>
 *
 * This file contains definitions and declarations global to the IrNET module,
 * all grouped in one place...
 * This file is a *private* header, so other modules don't want to know
 * what's in there...
 *
 * Note : as most part of the Linux kernel, this module is available
 * under the GNU General Public License (GPL).
 */

#ifndef IRNET_H
#define IRNET_H

/************************** DOCUMENTATION ***************************/
/*
 * What is IrNET
 * -------------
 * IrNET is a protocol allowing to carry TCP/IP traffic between two
 * IrDA peers in an efficient fashion. It is a thin layer, passing PPP
 * packets to IrTTP and vice versa. It uses PPP in synchronous mode,
 * because IrTTP offer a reliable sequenced packet service (as opposed
 * to a byte stream). In fact, you could see IrNET as carrying TCP/IP
 * in a IrDA socket, using PPP to provide the glue.
 *
 * The main difference with traditional PPP over IrCOMM is that we
 * avoid the framing and serial emulation which are a performance
 * bottleneck. It also allows multipoint communications in a sensible
 * fashion.
 *
 * The main difference with IrLAN is that we use PPP for the link
 * management, which is more standard, interoperable and flexible than
 * the IrLAN protocol. For example, PPP adds authentication,
 * encryption, compression, header compression and automated routing
 * setup. And, as IrNET let PPP do the hard work, the implementation
 * is much simpler than IrLAN.
 *
 * The Linux implementation
 * ------------------------
 * IrNET is written on top of the Linux-IrDA stack, and interface with
 * the generic Linux PPP driver. Because IrNET depend on recent
 * changes of the PPP driver interface, IrNET will work only with very
 * recent kernel (2.3.99-pre6 and up).
 *
 * The present implementation offer the following features :
 *	o simple user interface using pppd
 *	o efficient implementation (interface directly to PPP and IrTTP)
 *	o addressing (you can specify the name of the IrNET recipient)
 *	o multipoint operation (limited by IrLAP specification)
 *	o information in /proc/net/irda/irnet
 *	o IrNET events on /dev/irnet (for user space daemon)
 *	o IrNET daemon (irnetd) to automatically handle incoming requests
 *	o Windows 2000 compatibility (tested, but need more work)
 * Currently missing :
 *	o Lot's of testing (that's your job)
 *	o Connection retries (may be too hard to do)
 *	o Check pppd persist mode
 *	o User space daemon (to automatically handle incoming requests)
 *
 * The setup is not currently the most easy, but this should get much
 * better when everything will get integrated...
 *
 * Acknowledgements
 * ----------------
 * This module is based on :
 *	o The PPP driver (ppp_synctty/ppp_generic) by Paul Mackerras
 *	o The IrLAN protocol (irlan_common/XXX) by Dag Brattli
 *	o The IrSock interface (af_irda) by Dag Brattli
 *	o Some other bits from the kernel and my drivers...
 * Infinite thanks to those brave souls for providing the infrastructure
 * upon which IrNET is built.
 *
 * Thanks to all my collegues in HP for helping me. In particular,
 * thanks to Salil Pradhan and Bill Serra for W2k testing...
 * Thanks to Luiz Magalhaes for irnetd and much testing...
 *
 * Thanks to Alan Cox for answering lot's of my stupid questions, and
 * to Paul Mackerras answering my questions on how to best integrate
 * IrNET and pppd.
 *
 * Jean II
 *
 * Note on some implementations choices...
 * ------------------------------------
 *	1) Direct interface vs tty/socket
 * I could have used a tty interface to hook to ppp and use the full
 * socket API to connect to IrDA. The code would have been easier to
 * maintain, and maybe the code would have been smaller...
 * Instead, we hook directly to ppp_generic and to IrTTP, which make
 * things more complicated...
 *
 * The first reason is flexibility : this allow us to create IrNET
 * instances on demand (no /dev/ircommX crap) and to allow linkname
 * specification on pppd command line...
 *
 * Second reason is speed optimisation. If you look closely at the
 * transmit and receive paths, you will notice that they are "super lean"
 * (that's why they look ugly), with no function calls and as little data
 * copy and modification as I could...
 *
 *	2) irnetd in user space
 * irnetd is implemented in user space, which is necessary to call pppd.
 * This also give maximum benefits in term of flexibility and customability,
 * and allow to offer the event channel, useful for other stuff like debug.
 *
 * On the other hand, this require a loose coordination between the
 * present module and irnetd. One critical area is how incoming request
 * are handled.
 * When irnet receive an incoming request, it send an event to irnetd and
 * drop the incoming IrNET socket.
 * irnetd start a pppd instance, which create a new IrNET socket. This new
 * socket is then connected in the originating node to the pppd instance.
 * At this point, in the originating node, the first socket is closed.
 *
 * I admit, this is a bit messy and waste some resources. The alternative
 * is caching incoming socket, and that's also quite messy and waste
 * resources.
 * We also make connection time slower. For example, on a 115 kb/s link it
 * adds 60ms to the connection time (770 ms). However, this is slower than
 * the time it takes to fire up pppd on my P133...
 *
 *
 * History :
 * -------
 *
 * v1 - 15.5.00 - Jean II
 *	o Basic IrNET (hook to ppp_generic & IrTTP - incl. multipoint)
 *	o control channel on /dev/irnet (set name/address)
 *	o event channel on /dev/irnet (for user space daemon)
 *
 * v2 - 5.6.00 - Jean II
 *	o Enable DROP_NOT_READY to avoid PPP timeouts & other weirdness...
 *	o Add DISCONNECT_TO event and rename DISCONNECT_FROM.
 *	o Set official device number alloaction on /dev/irnet
 *
 * v3 - 30.8.00 - Jean II
 *	o Update to latest Linux-IrDA changes :
 *		- queue_t => irda_queue_t
 *	o Update to ppp-2.4.0 :
 *		- move irda_irnet_connect from PPPIOCATTACH to TIOCSETD
 *	o Add EXPIRE event (depend on new IrDA-Linux patch)
 *	o Switch from `hashbin_remove' to `hashbin_remove_this' to fix
 *	  a multilink bug... (depend on new IrDA-Linux patch)
 *	o fix a self->daddr to self->raddr in irda_irnet_connect to fix
 *	  another multilink bug (darn !)
 *	o Remove LINKNAME_IOCTL cruft
 *
 * v3b - 31.8.00 - Jean II
 *	o Dump discovery log at event channel startup
 *
 * v4 - 28.9.00 - Jean II
 *	o Fix interaction between poll/select and dump discovery log
 *	o Add IRNET_BLOCKED_LINK event (depend on new IrDA-Linux patch)
 *	o Add IRNET_NOANSWER_FROM event (mostly to help support)
 *	o Release flow control in disconnect_indication
 *	o Block packets while connecting (speed up connections)
 *
 * v5 - 11.01.01 - Jean II
 *	o Init self->max_header_size, just in case...
 *	o Set up ap->chan.hdrlen, to get zero copy on tx side working.
 *	o avoid tx->ttp->flow->ppp->tx->... loop, by checking flow state
 *		Thanks to Christian Gennerat for finding this bug !
 *	---
 *	o Declare the proper MTU/MRU that we can support
 *		(but PPP doesn't read the MTU value :-()
 *	o Declare hashbin HB_NOLOCK instead of HB_LOCAL to avoid
 *		disabling and enabling irq twice
 *
 * v6 - 31.05.01 - Jean II
 *	o Print source address in Found, Discovery, Expiry & Request events
 *	o Print requested source address in /proc/net/irnet
 *	o Change control channel input. Allow multiple commands in one line.
 *	o Add saddr command to change ap->rsaddr (and use that in IrDA)
 *	---
 *	o Make the IrDA connection procedure totally asynchronous.
 *	  Heavy rewrite of the IAS query code and the whole connection
 *	  procedure. Now, irnet_connect() no longer need to be called from
 *	  a process context...
 *	o Enable IrDA connect retries in ppp_irnet_send(). The good thing
 *	  is that IrDA connect retries are directly driven by PPP LCP
 *	  retries (we retry for each LCP packet), so that everything
 *	  is transparently controlled from pppd lcp-max-configure.
 *	o Add ttp_connect flag to prevent rentry on the connect procedure
 *	o Test and fixups to eliminate side effects of retries
 *
 * v7 - 22.08.01 - Jean II
 *	o Cleanup : Change "saddr = 0x0" to "saddr = DEV_ADDR_ANY"
 *	o Fix bug in BLOCK_WHEN_CONNECT introduced in v6 : due to the
 *	  asynchronous IAS query, self->tsap is NULL when PPP send the
 *	  first packet.  This was preventing "connect-delay 0" to work.
 *	  Change the test in ppp_irnet_send() to self->ttp_connect.
 *
 * v8 - 1.11.01 - Jean II
 *	o Tighten the use of self->ttp_connect and self->ttp_open to
 *	  prevent various race conditions.
 *	o Avoid leaking discovery log and skb
 *	o Replace "self" with "server" in irnet_connect_indication() to
 *	  better detect cut'n'paste error ;-)
 *
 * v9 - 29.11.01 - Jean II
 *	o Fix event generation in disconnect indication that I broke in v8
 *	  It was always generation "No-Answer" because I was testing ttp_open
 *	  just after clearing it. *blush*.
 *	o Use newly created irttp_listen() to fix potential crash when LAP
 *	  destroyed before irnet module removed.
 *
 * v10 - 4.3.2 - Jean II
 *	o When receiving a disconnect indication, don't reenable the
 *	  PPP Tx queue, this will trigger a reconnect. Instead, close
 *	  the channel, which will kill pppd...
 *
 * v11 - 20.3.02 - Jean II
 *	o Oops ! v10 fix disabled IrNET retries and passive behaviour.
 *	  Better fix in irnet_disconnect_indication() :
 *	  - if connected, kill pppd via hangup.
 *	  - if not connected, reenable ppp Tx, which trigger IrNET retry.
 *
 * v12 - 10.4.02 - Jean II
 *	o Fix race condition in irnet_connect_indication().
 *	  If the socket was already trying to connect, drop old connection
 *	  and use new one only if acting as primary. See comments.
 *
 * v13 - 30.5.02 - Jean II
 *	o Update module init code
 *
 * v14 - 20.2.03 - Jean II
 *	o Add discovery hint bits in the control channel.
 *	o Remove obsolete MOD_INC/DEC_USE_COUNT in favor of .owner
 *
 * v15 - 7.4.03 - Jean II
 *	o Replace spin_lock_irqsave() with spin_lock_bh() so that we can
 *	  use ppp_unit_number(). It's probably also better overall...
 *	o Disable call to ppp_unregister_channel(), because we can't do it.
 */

/***************************** INCLUDES *****************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/capability.h>
#include <linux/ctype.h>	/* isspace() */
#include <asm/uaccess.h>
#include <linux/init.h>

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ppp_channel.h>

#include <net/irda/irda.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>
#include <net/irda/discovery.h>

/***************************** OPTIONS *****************************/
/*
 * Define or undefine to compile or not some optional part of the
 * IrNET driver...
 * Note : the present defaults make sense, play with that at your
 * own risk...
 */
/* IrDA side of the business... */
#define DISCOVERY_NOMASK	/* To enable W2k compatibility... */
#define ADVERTISE_HINT		/* Advertise IrLAN hint bit */
#define ALLOW_SIMULT_CONNECT	/* This seem to work, cross fingers... */
#define DISCOVERY_EVENTS	/* Query the discovery log to post events */
#define INITIAL_DISCOVERY	/* Dump current discovery log as events */
#undef STREAM_COMPAT		/* Not needed - potentially messy */
#undef CONNECT_INDIC_KICK	/* Might mess IrDA, not needed */
#undef FAIL_SEND_DISCONNECT	/* Might mess IrDA, not needed */
#undef PASS_CONNECT_PACKETS	/* Not needed ? Safe */
#undef MISSING_PPP_API		/* Stuff I wish I could do */

/* PPP side of the business */
#define BLOCK_WHEN_CONNECT	/* Block packets when connecting */
#define CONNECT_IN_SEND		/* Retry IrDA connection procedure */
#undef FLUSH_TO_PPP		/* Not sure about this one, let's play safe */
#undef SECURE_DEVIRNET		/* Bah... */

/****************************** DEBUG ******************************/

/*
 * This set of flags enable and disable all the various warning,
 * error and debug message of this driver.
 * Each section can be enabled and disabled independently
 */
/* In the PPP part */
#define DEBUG_CTRL_TRACE	0	/* Control channel */
#define DEBUG_CTRL_INFO		0	/* various info */
#define DEBUG_CTRL_ERROR	1	/* problems */
#define DEBUG_FS_TRACE		0	/* filesystem callbacks */
#define DEBUG_FS_INFO		0	/* various info */
#define DEBUG_FS_ERROR		1	/* problems */
#define DEBUG_PPP_TRACE		0	/* PPP related functions */
#define DEBUG_PPP_INFO		0	/* various info */
#define DEBUG_PPP_ERROR		1	/* problems */
#define DEBUG_MODULE_TRACE	0	/* module insertion/removal */
#define DEBUG_MODULE_ERROR	1	/* problems */

/* In the IrDA part */
#define DEBUG_IRDA_SR_TRACE	0	/* IRDA subroutines */
#define DEBUG_IRDA_SR_INFO	0	/* various info */
#define DEBUG_IRDA_SR_ERROR	1	/* problems */
#define DEBUG_IRDA_SOCK_TRACE	0	/* IRDA main socket functions */
#define DEBUG_IRDA_SOCK_INFO	0	/* various info */
#define DEBUG_IRDA_SOCK_ERROR	1	/* problems */
#define DEBUG_IRDA_SERV_TRACE	0	/* The IrNET server */
#define DEBUG_IRDA_SERV_INFO	0	/* various info */
#define DEBUG_IRDA_SERV_ERROR	1	/* problems */
#define DEBUG_IRDA_TCB_TRACE	0	/* IRDA IrTTP callbacks */
#define DEBUG_IRDA_CB_INFO	0	/* various info */
#define DEBUG_IRDA_CB_ERROR	1	/* problems */
#define DEBUG_IRDA_OCB_TRACE	0	/* IRDA other callbacks */
#define DEBUG_IRDA_OCB_INFO	0	/* various info */
#define DEBUG_IRDA_OCB_ERROR	1	/* problems */

#define DEBUG_ASSERT		0	/* Verify all assertions */

/*
 * These are the macros we are using to actually print the debug
 * statements. Don't look at it, it's ugly...
 *
 * One of the trick is that, as the DEBUG_XXX are constant, the
 * compiler will optimise away the if() in all cases.
 */
/* All error messages (will show up in the normal logs) */
#define DERROR(dbg, format, args...) \
	{if(DEBUG_##dbg) \
		printk(KERN_INFO "irnet: %s(): " format, __func__ , ##args);}

/* Normal debug message (will show up in /var/log/debug) */
#define DEBUG(dbg, format, args...) \
	{if(DEBUG_##dbg) \
		printk(KERN_DEBUG "irnet: %s(): " format, __func__ , ##args);}

/* Entering a function (trace) */
#define DENTER(dbg, format, args...) \
	{if(DEBUG_##dbg) \
		printk(KERN_DEBUG "irnet: -> %s" format, __func__ , ##args);}

/* Entering and exiting a function in one go (trace) */
#define DPASS(dbg, format, args...) \
	{if(DEBUG_##dbg) \
		printk(KERN_DEBUG "irnet: <>%s" format, __func__ , ##args);}

/* Exiting a function (trace) */
#define DEXIT(dbg, format, args...) \
	{if(DEBUG_##dbg) \
		printk(KERN_DEBUG "irnet: <-%s()" format, __func__ , ##args);}

/* Exit a function with debug */
#define DRETURN(ret, dbg, args...) \
	{DEXIT(dbg, ": " args);\
	return ret; }

/* Exit a function on failed condition */
#define DABORT(cond, ret, dbg, args...) \
	{if(cond) {\
		DERROR(dbg, args);\
		return ret; }}

/* Invalid assertion, print out an error and exit... */
#define DASSERT(cond, ret, dbg, args...) \
	{if((DEBUG_ASSERT) && !(cond)) {\
		DERROR(dbg, "Invalid assertion: " args);\
		return ret; }}

/************************ CONSTANTS & MACROS ************************/

/* Paranoia */
#define IRNET_MAGIC	0xB00754

/* Number of control events in the control channel buffer... */
#define IRNET_MAX_EVENTS	8	/* Should be more than enough... */

/****************************** TYPES ******************************/

/*
 * This is the main structure where we store all the data pertaining to
 * one instance of irnet.
 * Note : in irnet functions, a pointer this structure is usually called
 * "ap" or "self". If the code is borrowed from the IrDA stack, it tend
 * to be called "self", and if it is borrowed from the PPP driver it is
 * "ap". Apart from that, it's exactly the same structure ;-)
 */
typedef struct irnet_socket
{
  /* ------------------- Instance management ------------------- */
  /* We manage a linked list of IrNET socket instances */
  irda_queue_t		q;		/* Must be first - for hasbin */
  int			magic;		/* Paranoia */

  /* --------------------- FileSystem part --------------------- */
  /* "pppd" interact directly with us on a /dev/ file */
  struct file *		file;		/* File descriptor of this instance */
  /* TTY stuff - to keep "pppd" happy */
  struct ktermios	termios;	/* Various tty flags */
  /* Stuff for the control channel */
  int			event_index;	/* Last read in the event log */

  /* ------------------------- PPP part ------------------------- */
  /* We interface directly to the ppp_generic driver in the kernel */
  int			ppp_open;	/* registered with ppp_generic */
  struct ppp_channel	chan;		/* Interface to generic ppp layer */

  int			mru;		/* Max size of PPP payload */
  u32			xaccm[8];	/* Asynchronous character map (just */
  u32			raccm;		/* to please pppd - dummy) */
  unsigned int		flags;		/* PPP flags (compression, ...) */
  unsigned int		rbits;		/* Unused receive flags ??? */
  struct work_struct disconnect_work;   /* Process context disconnection */
  /* ------------------------ IrTTP part ------------------------ */
  /* We create a pseudo "socket" over the IrDA tranport */
  unsigned long		ttp_open;	/* Set when IrTTP is ready */
  unsigned long		ttp_connect;	/* Set when IrTTP is connecting */
  struct tsap_cb *	tsap;		/* IrTTP instance (the connection) */

  char			rname[NICKNAME_MAX_LEN + 1];
					/* IrDA nickname of destination */
  __u32			rdaddr;		/* Requested peer IrDA address */
  __u32			rsaddr;		/* Requested local IrDA address */
  __u32			daddr;		/* actual peer IrDA address */
  __u32			saddr;		/* my local IrDA address */
  __u8			dtsap_sel;	/* Remote TSAP selector */
  __u8			stsap_sel;	/* Local TSAP selector */

  __u32			max_sdu_size_rx;/* Socket parameters used for IrTTP */
  __u32			max_sdu_size_tx;
  __u32			max_data_size;
  __u8			max_header_size;
  LOCAL_FLOW		tx_flow;	/* State of the Tx path in IrTTP */

  /* ------------------- IrLMP and IrIAS part ------------------- */
  /* Used for IrDA Discovery and socket name resolution */
  void *		ckey;		/* IrLMP client handle */
  __u16			mask;		/* Hint bits mask (filter discov.)*/
  int			nslots;		/* Number of slots for discovery */

  struct iriap_cb *	iriap;		/* Used to query remote IAS */
  int			errno;		/* status of the IAS query */

  /* -------------------- Discovery log part -------------------- */
  /* Used by initial discovery on the control channel
   * and by irnet_discover_daddr_and_lsap_sel() */
  struct irda_device_info *discoveries;	/* Copy of the discovery log */
  int			disco_index;	/* Last read in the discovery log */
  int			disco_number;	/* Size of the discovery log */

} irnet_socket;

/*
 * This is the various event that we will generate on the control channel
 */
typedef enum irnet_event
{
  IRNET_DISCOVER,		/* New IrNET node discovered */
  IRNET_EXPIRE,			/* IrNET node expired */
  IRNET_CONNECT_TO,		/* IrNET socket has connected to other node */
  IRNET_CONNECT_FROM,		/* Other node has connected to IrNET socket */
  IRNET_REQUEST_FROM,		/* Non satisfied connection request */
  IRNET_NOANSWER_FROM,		/* Failed connection request */
  IRNET_BLOCKED_LINK,		/* Link (IrLAP) is blocked for > 3s */
  IRNET_DISCONNECT_FROM,	/* IrNET socket has disconnected */
  IRNET_DISCONNECT_TO		/* Closing IrNET socket */
} irnet_event;

/*
 * This is the storage for an event and its arguments
 */
typedef struct irnet_log
{
  irnet_event	event;
  int		unit;
  __u32		saddr;
  __u32		daddr;
  char		name[NICKNAME_MAX_LEN + 1];	/* 21 + 1 */
  __u16_host_order hints;			/* Discovery hint bits */
} irnet_log;

/*
 * This is the storage for all events and related stuff...
 */
typedef struct irnet_ctrl_channel
{
  irnet_log	log[IRNET_MAX_EVENTS];	/* Event log */
  int		index;		/* Current index in log */
  spinlock_t	spinlock;	/* Serialize access to the event log */
  wait_queue_head_t	rwait;	/* processes blocked on read (or poll) */
} irnet_ctrl_channel;

/**************************** PROTOTYPES ****************************/
/*
 * Global functions of the IrNET module
 * Note : we list here also functions called from one file to the other.
 */

/* -------------------------- IRDA PART -------------------------- */
extern int
	irda_irnet_create(irnet_socket *);	/* Initialise a IrNET socket */
extern int
	irda_irnet_connect(irnet_socket *);	/* Try to connect over IrDA */
extern void
	irda_irnet_destroy(irnet_socket *);	/* Teardown  a IrNET socket */
extern int
	irda_irnet_init(void);		/* Initialise IrDA part of IrNET */
extern void
	irda_irnet_cleanup(void);	/* Teardown IrDA part of IrNET */

/**************************** VARIABLES ****************************/

/* Control channel stuff - allocated in irnet_irda.h */
extern struct irnet_ctrl_channel	irnet_events;

#endif /* IRNET_H */
