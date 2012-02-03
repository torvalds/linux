/*
 * Dongle BUS interface
 * USB Linux Implementation
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: dbus_usb_linux.c,v 1.43.2.13 2011/01/20 02:54:21 Exp $
 */

#include <typedefs.h>
#include <osl.h>

/* DBUS_LINUX_RXDPC setting is in wlconfig file.
 *
 * If DBUS_LINUX_RXDPC is off, spin_lock_bh() for CTFPOOL in
 * linux_osl.c has to be changed to spin_lock_irqsave() because
 * PKTGET/PKTFREE are no longer in bottom half.
 *
 * Right now we have another queue rpcq in wl_linux.c. Maybe we
 * can eliminate that one to reduce the overhead.
 *
 * Enabling 2nd EP and DBUS_LINUX_RXDPC causing traffic form
 * both EP's to be queued in the same rx queue. If we want
 * RXDPC to work with 2nd EP. The EP for RPC call return
 * should bypass the dpc and go directly up.
 */
/* #define DBUS_LINUX_RXDPC */

/* Dbus histrogram for ntxq, nrxq, dpc parameter tuning */
/* #define DBUS_LINUX_HIST */

#include <usbrdl.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <dbus.h>
#include <bcmutils.h>
#include <bcmdevs.h>
#include <usbstd.h>
#include <usbrdl.h>
#ifdef DBUS_LINUX_RXDPC
#include <linux/sched.h>


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
#define RESCHED()	_cond_resched()
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define RESCHED()	cond_resched()
#else
#define RESCHED()	__cond_resched()
#endif /* LINUX_VERSION_CODE  */
#endif	/* DBUS_LINUX_RXDPC */

#ifdef USBOS_THREAD
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/hardirq.h>
#include <linux/list.h>
#include <linux_osl.h>
#endif /* USBOS_THREAD */
#ifdef USBSHIM
#include <bcm_usbshim.h>
const struct s_usburb *usburb;

#define USB_ALLOC_URB()         (usburb->bcm_alloc_urb != NULL ? usburb->bcm_alloc_urb(0) : 0)
#define USB_SUBMIT_URB(urb)     (usburb->bcm_submit_urb != NULL ? usburb->bcm_submit_urb(urb) : 0)
#define USB_UNLINK_URB(urb)     (usburb->bcm_kill_urb != NULL ? usburb->bcm_kill_urb(urb) : 0)
#define USB_FREE_URB(urb)       (usburb->bcm_free_urb != NULL ? usburb->bcm_free_urb(urb) : 0)
#define USB_REGISTER()          (usburb->bcm_register != NULL ? \
					usburb->bcm_register(&dbus_usbdev) : DBUS_ERR)
#define USB_DEREGISTER()        (usburb->bcm_deregister != NULL ? \
					usburb->bcm_deregister(&dbus_usbdev) : 0)

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
#define USB_AUTOPM_SET_INTERFACE(intf)	(usburb->bcm_autopm_set_interface != NULL ? \
					usburb->bcm_autopm_set_interface(intf) : 0)
#endif /* ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))

#define USB_ALLOC_URB()		usb_alloc_urb(0, GFP_ATOMIC)
#define USB_SUBMIT_URB(urb)	usb_submit_urb(urb, GFP_ATOMIC)
#define USB_UNLINK_URB(urb)     (usb_kill_urb(urb))
#define USB_FREE_URB(urb)       (usb_free_urb(urb))
#define USB_REGISTER()          usb_register(&dbus_usbdev)
#define USB_DEREGISTER()        usb_deregister(&dbus_usbdev)

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
#define USB_AUTOPM_SET_INTERFACE(intf)	 usb_autopm_set_interface(intf)
#endif /* ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

#else /* 2.4 */

#define USB_ALLOC_URB()		usb_alloc_urb(0)
#define USB_SUBMIT_URB(urb)	usb_submit_urb(urb)
#define USB_UNLINK_URB(urb)	usb_unlink_urb(urb)
#define USB_FREE_URB(urb)       (usb_free_urb(urb))
#define USB_REGISTER()          usb_register(&dbus_usbdev)
#define USB_DEREGISTER()        usb_deregister(&dbus_usbdev)

#endif /* 2.4 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define KERNEL26

#define USB_BUFFER_ALLOC(dev, size, mem, dma) \
				usb_buffer_alloc(dev, size, mem, dma)
#define USB_BUFFER_FREE(dev, size, data, dma) \
				usb_buffer_free(dev, size, data, dma)
#define URB_QUEUE_BULK		URB_ZERO_PACKET
#define CALLBACK_ARGS		struct urb *urb, struct pt_regs *regs
#define CONFIGDESC(usb)		(&((usb)->actconfig)->desc)

#define IFPTR(usb, idx)		((usb)->actconfig->interface[idx])
#define IFALTS(usb, idx)	(IFPTR((usb), (idx))->altsetting[0])
#define IFDESC(usb, idx)	IFALTS((usb), (idx)).desc
#define IFEPDESC(usb, idx, ep)	(IFALTS((usb), (idx)).endpoint[ep]).desc
#define DAEMONIZE(a)		daemonize(a); allow_signal(SIGKILL); allow_signal(SIGTERM);
#define SET_NICE(n)		set_user_nice(current, n)

#else /* 2.4 */

#define URB_QUEUE_BULK		0
#define USB_ALLOC_URB()		usb_alloc_urb(0)
#define USB_SUBMIT_URB(urb)	usb_submit_urb(urb)
#define USB_UNLINK_URB(urb)	usb_unlink_urb(urb)
#define USB_BUFFER_ALLOC(dev, size, mem, dma) \
				kmalloc(size, mem)
#define USB_BUFFER_FREE(dev, size, data, dma) \
				kfree(data)
#define CALLBACK_ARGS		struct urb *urb
#define CONFIGDESC(usb)		((usb)->actconfig)
#define IFPTR(usb, idx)		(&(usb)->actconfig->interface[idx])
#define IFALTS(usb, idx)	((usb)->actconfig->interface[idx].altsetting[0])
#define IFDESC(usb, idx)	IFALTS((usb), (idx))
#define IFEPDESC(usb, idx, ep)	(IFALTS((usb), (idx)).endpoint[ep])

#ifdef DBUS_LINUX_RXDPC
#define DAEMONIZE(a)		daemonize();
#define SET_NICE(n)		do {current->nice = (n);} while (0)
#endif	/* DBUS_LINUX_RXDPC */
#endif /* 2.4 */

#define CONTROL_IF		0
#define BULK_IF			0

#define USB_SYNC_WAIT_TIMEOUT	300	/* ms */

/* Private data kept in skb */
#define SKB_PRIV(skb, idx)	(&((void **)skb->cb)[idx])
#define SKB_PRIV_URB(skb)	(*(struct urb **)SKB_PRIV(skb, 0))

#define WD_MS 50 /* ms watchdong interval */
#define DHD_IDLETIME 2

enum usbos_suspend_state {
	USBOS_SUSPEND_STATE_DEVICE_ACTIVE = 0, /* Device is busy, won't allow suspend */
	USBOS_SUSPEND_STATE_SUSPEND_PENDING,	/* Device is idle, can be suspended.
						* Wating PM to suspend the device
						*/
	USBOS_SUSPEND_STATE_SUSPENDED	/* Device suspended */
};

typedef struct {
	uint32 notification;
	uint32 reserved;
} intr_t;

typedef struct {
	dbus_pub_t *pub;

	void *cbarg;
	dbus_intf_callbacks_t *cbs;

	/* Imported */
	struct usb_device *usb;	/* USB device pointer from OS */
	struct urb *intr_urb; /* URB for interrupt endpoint */
	struct list_head req_freeq;
	struct list_head req_rxpostedq;	/* Posted down to USB driver for RX */
	struct list_head req_txpostedq;	/* Posted down to USB driver for TX */
	spinlock_t free_lock; /* Lock for free list */
	spinlock_t rxposted_lock; /* Lock for rx posted list */
	spinlock_t txposted_lock; /* Lock for tx posted list */
	uint rx_pipe, tx_pipe, intr_pipe, rx_pipe2; /* Pipe numbers for USB I/O */
	uint rxbuf_len;

	struct list_head req_rxpendingq; /* RXDPC: Pending for dpc to send up */
	spinlock_t rxpending_lock;	/* RXDPC: Lock for rx pending list */
	long dpc_pid;
	struct semaphore dpc_sem;
	struct completion dpc_exited;
	int rxpending;
#if defined(DBUS_LINUX_HIST)
	int	dpc_cnt, dpc_pktcnt, dpc_maxpktcnt;
#endif

	struct urb *ctl_urb;
	int ctl_in_pipe, ctl_out_pipe;
	struct usb_ctrlrequest ctl_write;
	struct usb_ctrlrequest ctl_read;

	spinlock_t rxlock;      /* Lock for rxq management */
	spinlock_t txlock;      /* Lock for txq management */

	int intr_size;          /* Size of interrupt message */
	int interval;           /* Interrupt polling interval */
	intr_t intr;            /* Data buffer for interrupt endpoint */

	int maxps;
	int txposted;
	int rxposted;
	bool rxctl_deferrespok;	/* Get a response for setup from dongle */

	wait_queue_head_t wait;
	bool waitdone;
	int sync_urb_status;

	struct urb *blk_urb; /* Used for downloading embedded image */

#if defined(DBUS_LINUX_HIST)
	int *txposted_hist;
	int *rxposted_hist;
#endif
#ifdef USBOS_THREAD
	spinlock_t              usbos_list_lock;
	struct list_head        usbos_list;
	struct list_head        usbos_free_list;
	atomic_t                usbos_list_cnt;
	wait_queue_head_t       usbos_queue_head;
	struct task_struct      *usbos_kt;
#endif /* USBOS_THREAD */
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	int idletime;
	int idlecount;
	bool activity;
	struct timer_list wdtimer;
	bool wd_timer_valid;
	long wdpid;
    struct semaphore wdsem;
    struct completion wd_exited;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
} usbos_info_t;

typedef struct urb_req {
	void *pkt;
	int buf_len;
	struct urb *urb;
	void *arg;
	usbos_info_t *usbinfo;
	struct list_head urb_list;
} urb_req_t;
#ifdef USBOS_THREAD
typedef struct usbos_list_entry {
	struct list_head    list;   /* must be first */
	void               *urb_context;
	int                 urb_length;
	int                 urb_status;
} usbos_list_entry_t;

void* dbus_usbos_thread_init(usbos_info_t *usbos_info);
void  dbus_usbos_thread_deinit(usbos_info_t *usbos_info);
void  dbus_usbos_dispatch_schedule(CALLBACK_ARGS);
int   dbus_usbos_thread_func(void *data);
void  dbus_usbos_recv_complete_handle(urb_req_t *req, int len, int status);
#endif /* USBOS_THREAD */

/* Shared Function prototypes */
bool dbus_usbos_dl_cmd(usbos_info_t *usbinfo, uint8 cmd, void *buffer, int buflen);
int dbus_usbos_wait(usbos_info_t *usbinfo, uint16 ms);
bool dbus_usbos_dl_send_bulk(usbos_info_t *usbinfo, void *buffer, int len);
int dbus_write_membytes(usbos_info_t *usbinfo, bool set, uint32 address, uint8 *data, uint size);


/* Local function prototypes */
static void dbus_usbos_send_complete(CALLBACK_ARGS);
#ifdef DBUS_LINUX_RXDPC
static void dbus_usbos_recv_dpc(usbos_info_t *usbos_info);
static int dbus_usbos_dpc_thread(void *data);
#endif /* DBUS_LINUX_RXDPC */
static void dbus_usbos_recv_complete(CALLBACK_ARGS);
static int  dbus_usbos_errhandler(void *bus, int err);
static int  dbus_usbos_state_change(void *bus, int state);
static void dbusos_stop(usbos_info_t *usbos_info);

#ifdef KERNEL26
static int dbus_usbos_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void dbus_usbos_disconnect(struct usb_interface *intf);
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
static int dbus_usbos_resume(struct usb_interface *intf);
static int dbus_usbos_suspend(struct usb_interface *intf, pm_message_t message);
static void dbus_usbos_sleep(usbos_info_t *usbos_info);
static void dbus_usb_wd_init(usbos_info_t *usbos_info);
static void dbus_usb_wdtimer_init(usbos_info_t *usbos_info);
static void dbus_usb_wd_remove(usbos_info_t *usbos_info);
static void dbus_usb_wdtimer_remove(usbos_info_t *usbos_info);
static void dbus_usbos_wdtimer(ulong data);
static int dhd_usbos_watchdog_thread(void *data);
#endif
#else
static void *dbus_usbos_probe(struct usb_device *usb, unsigned int ifnum,
	const struct usb_device_id *id);
static void dbus_usbos_disconnect(struct usb_device *usb, void *ptr);
#endif /* KERNEL26 */

#ifdef USB_TRIGGER_DEBUG
static bool dbus_usbos_ctl_send_debugtrig(usbos_info_t *usbinfo);
#endif /* USB_TRIGGER_DEBUG */
static struct usb_device_id devid_table[] = {
	{ USB_DEVICE(BCM_DNGL_VID, 0x0000) }, /* Configurable via register() */
#if defined(BCM_DNGL_EMBEDIMAGE)
	{ USB_DEVICE(BCM_DNGL_VID, BCM_DNGL_BL_PID_4328) },
	{ USB_DEVICE(BCM_DNGL_VID, BCM_DNGL_BL_PID_4322) },
	{ USB_DEVICE(BCM_DNGL_VID, BCM_DNGL_BL_PID_4319) },
	{ USB_DEVICE(BCM_DNGL_VID, BCM_DNGL_BL_PID_43236) },
#endif
#ifdef EXTENDED_VID_PID
	EXTENDED_VID_PID,
#endif /* EXTENDED_VID_PID */
	{ USB_DEVICE(BCM_DNGL_VID, BCM_DNGL_BDC_PID) }, /* Default BDC */
	{ }
};

MODULE_DEVICE_TABLE(usb, devid_table);

static struct usb_driver dbus_usbdev = {
	name:           "dbus_usbdev",
	probe:          dbus_usbos_probe,
	disconnect:     dbus_usbos_disconnect,
	id_table:       devid_table,
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	suspend: 	dbus_usbos_suspend,
	resume:  	dbus_usbos_resume,
	supports_autosuspend: 1
#endif
};

/* This stores USB info during Linux probe callback
 * since attach() is not called yet at this point
 */
typedef struct {
	void *usbos_info;
	struct usb_device *usb; /* USB device pointer from OS */
	uint rx_pipe, tx_pipe, intr_pipe, rx_pipe2; /* Pipe numbers for USB I/O */
	int intr_size; /* Size of interrupt message */
	int interval;  /* Interrupt polling interval */
	bool dldone;
	int vid;
	int pid;
	bool dereged;
	bool disc_cb_done;
	DEVICE_SPEED device_speed;
	enum usbos_suspend_state suspend_state;
	struct usb_interface *intf;
} probe_info_t;

static probe_info_t g_probe_info;

/*
 * USB Linux dbus_intf_t
 */
static void *dbus_usbos_intf_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs);
static void dbus_usbos_intf_detach(dbus_pub_t *pub, void *info);
static int  dbus_usbos_intf_send_irb(void *bus, dbus_irb_tx_t *txirb);
static int  dbus_usbos_intf_recv_irb(void *bus, dbus_irb_rx_t *rxirb);
static int  dbus_usbos_intf_recv_irb_from_ep(void *bus, dbus_irb_rx_t *rxirb, uint32 ep_idx);
static int  dbus_usbos_intf_cancel_irb(void *bus, dbus_irb_tx_t *txirb);
static int  dbus_usbos_intf_send_ctl(void *bus, uint8 *buf, int len);
static int  dbus_usbos_intf_recv_ctl(void *bus, uint8 *buf, int len);
static int  dbus_usbos_intf_get_attrib(void *bus, dbus_attrib_t *attrib);
static int  dbus_usbos_intf_up(void *bus);
static int  dbus_usbos_intf_down(void *bus);
static int  dbus_usbos_intf_stop(void *bus);

#if defined(DBUS_LINUX_HIST)
static void dbus_usbos_intf_dump(void *bus, struct bcmstrbuf *b);
#endif 
static int  dbus_usbos_intf_set_config(void *bus, dbus_config_t *config);
static bool dbus_usbos_intf_recv_needed(void *bus);
static void *dbus_usbos_intf_exec_rxlock(void *bus, exec_cb_t cb, struct exec_parms *args);
static void *dbus_usbos_intf_exec_txlock(void *bus, exec_cb_t cb, struct exec_parms *args);
int dbus_usbos_intf_pnp(void *bus, int event);

static dbus_intf_t dbus_usbos_intf = {
	dbus_usbos_intf_attach,
	dbus_usbos_intf_detach,
	dbus_usbos_intf_up,
	dbus_usbos_intf_down,
	dbus_usbos_intf_send_irb,
	dbus_usbos_intf_recv_irb,
	dbus_usbos_intf_cancel_irb,
	dbus_usbos_intf_send_ctl,
	dbus_usbos_intf_recv_ctl,
	NULL, /* get_stats */
	dbus_usbos_intf_get_attrib,
	dbus_usbos_intf_pnp, /* pnp */
	NULL, /* remove */
	NULL, /* resume */
	NULL, /* suspend */
	dbus_usbos_intf_stop,
	NULL, /* reset */
	NULL, /* pktget */
	NULL, /* pktfree */
	NULL, /* iovar_op */
#if defined(DBUS_LINUX_HIST)
	dbus_usbos_intf_dump, /* dump */
#else
	NULL, /* dump */
#endif 
	dbus_usbos_intf_set_config, /* set_config */
	NULL, /* get_config */
	NULL, /* device_exists */
	NULL, /* dlneeded */
	NULL, /* dlstart */
	NULL, /* dlrun */
	dbus_usbos_intf_recv_needed,
	dbus_usbos_intf_exec_rxlock,
	dbus_usbos_intf_exec_txlock,
	NULL, /* set_revinfo */
	NULL, /* get_revinfo */

	NULL, /* tx_timer_init */
	NULL, /* tx_timer_start */
	NULL, /* tx_timer_stop */

	NULL, /* sched_dpc */
	NULL, /* lock */
	NULL, /* unlock */
	NULL, /* sched_probe_cb */

	NULL, /* shutdown */

	NULL, /* recv_stop */
	NULL, /* recv_resume */

	dbus_usbos_intf_recv_irb_from_ep
};

static probe_cb_t probe_cb = NULL;
static disconnect_cb_t disconnect_cb = NULL;
static void *probe_arg = NULL;
static void *disc_arg = NULL;

static urb_req_t * BCMFASTPATH
dbus_usbos_qdeq(struct list_head *urbreq_q, spinlock_t *lock)
{
	unsigned long flags;
	urb_req_t *req;

	ASSERT(urbreq_q != NULL);

	spin_lock_irqsave(lock, flags);

	if (list_empty(urbreq_q)) {
		req = NULL;
	} else {
		ASSERT(urbreq_q->next != NULL);
		ASSERT(urbreq_q->next != urbreq_q);

		req = list_entry(urbreq_q->next, urb_req_t, urb_list);
		list_del_init(&req->urb_list);
	}

	spin_unlock_irqrestore(lock, flags);

	return req;
}

static void BCMFASTPATH
dbus_usbos_qenq(struct list_head *urbreq_q, urb_req_t *req, spinlock_t *lock)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);

	list_add_tail(&req->urb_list, urbreq_q);

	spin_unlock_irqrestore(lock, flags);

}

static void
dbus_usbos_req_del(urb_req_t *req, spinlock_t *lock)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);

	list_del_init(&req->urb_list);

	spin_unlock_irqrestore(lock, flags);
}


static int
dbus_usbos_urbreqs_alloc(usbos_info_t *usbos_info)
{
	int i;

	DBUSTRACE(("%s: allocating URBs. ntxq %d nrxq %d\n", __FUNCTION__,
		usbos_info->pub->ntxq, usbos_info->pub->nrxq));
	/* FIX: should probably have independent queues for tx and rx */
	for (i = 0; i < (usbos_info->pub->nrxq + usbos_info->pub->ntxq); i++) {
		urb_req_t *req;

		req = MALLOC(usbos_info->pub->osh, sizeof(urb_req_t));
		if (req == NULL) {
			/* dbus_usbos_urbreqs_free() takes care of partial
			 * allocation
			 */
			DBUSERR(("%s: usb_alloc_urb failed\n", __FUNCTION__));
			return DBUS_ERR_NOMEM;
		}
		bzero(req, sizeof(urb_req_t));

		req->urb = USB_ALLOC_URB();
		if (req->urb == NULL) {
			DBUSERR(("%s: usb_alloc_urb failed\n", __FUNCTION__));
			return DBUS_ERR_NOMEM;
		}

		INIT_LIST_HEAD(&req->urb_list);

#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
		/* don't allocate now. Do it on demand */
		req->pkt = NULL;
#else
		/* pre-allocate  buffers never to be released */
		req->pkt = MALLOC(usbos_info->pub->osh, usbos_info->rxbuf_len);
		if (req->pkt == NULL) {
			DBUSERR(("%s: usb_alloc_urb failed\n", __FUNCTION__));
			return DBUS_ERR_NOMEM;
		}
#endif
		req->buf_len = usbos_info->rxbuf_len;
		dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
	}

	return DBUS_OK;
}

/* Don't call until all URBs unlinked */
static void
dbus_usbos_urbreqs_free(usbos_info_t *usbos_info)
{
	urb_req_t *req;

	while ((req = dbus_usbos_qdeq(&usbos_info->req_freeq,
		&usbos_info->free_lock)) != NULL) {

		if (req->pkt) {
			/* We do MFREE instead of PKTFREE because the pkt has been
			 * converted to native already
			 */
			MFREE(usbos_info->pub->osh, req->pkt, req->buf_len);
			req->pkt = NULL;
			req->buf_len = 0;
		}

		if (req->urb) {
			USB_FREE_URB(req->urb);
			req->urb = NULL;
		}
		MFREE(usbos_info->pub->osh, req, sizeof(urb_req_t));
	}
}

void
dbus_usbos_send_complete(CALLBACK_ARGS)
{
	urb_req_t *req = urb->context;
	dbus_irb_tx_t *txirb = req->arg;
	usbos_info_t *usbos_info = req->usbinfo;
	unsigned long flags;
	int status = DBUS_OK;

	spin_lock_irqsave(&usbos_info->txlock, flags);
	dbus_usbos_req_del(req, &usbos_info->txposted_lock);
	usbos_info->txposted--;
#if defined(DBUS_LINUX_HIST)
	usbos_info->txposted_hist[usbos_info->txposted]++;
#endif 
	if (unlikely (usbos_info->txposted < 0)) {
		DBUSERR(("%s ERROR: txposted is negative!!\n", __FUNCTION__));
	}
	spin_unlock_irqrestore(&usbos_info->txlock, flags);

	if (unlikely (urb->status)) {
		status = DBUS_ERR_TXFAIL;
		DBUSTRACE(("txfail status %d\n", urb->status));
	}

#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
	/* detach the packet from the req */
	req->pkt = NULL;
#endif
	dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
	if (txirb->send_buf) {
		kfree(txirb->send_buf);
	}
	if (likely (usbos_info->cbarg && usbos_info->cbs)) {
		if (likely (usbos_info->cbs->send_irb_complete != NULL))
			usbos_info->cbs->send_irb_complete(usbos_info->cbarg, txirb, status);
	}
}

static int BCMFASTPATH
dbus_usbos_recv_urb_submit(usbos_info_t *usbos_info, dbus_irb_rx_t *rxirb, uint32 ep_idx)
{
	urb_req_t *req;
	int ret = DBUS_OK;
	unsigned long flags;
	void *p;
	uint rx_pipe;

	if (!(req = dbus_usbos_qdeq(&usbos_info->req_freeq, &usbos_info->free_lock))) {
		DBUSERR(("%s No free URB!\n", __FUNCTION__));
		return DBUS_ERR_RXDROP;
	}

	spin_lock_irqsave(&usbos_info->rxlock, flags);

#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
	req->pkt = rxirb->pkt = PKTGET(usbos_info->pub->osh, req->buf_len, FALSE);
	if (!rxirb->pkt) {
		DBUSERR(("%s: PKTGET failed\n", __FUNCTION__));
		dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
		ret = DBUS_ERR_RXDROP;
		goto fail;
	}
	/* consider the packet "native" so we don't count it as MALLOCED in the osl */
	PKTTONATIVE(usbos_info->pub->osh, req->pkt);
	rxirb->buf = NULL;
	p = PKTDATA(usbos_info->pub->osh, req->pkt);
#else
	rxirb->buf = req->pkt;
	p = rxirb->buf;
#endif /* defined(BCM_RPC_NOCOPY) */
	rxirb->buf_len = req->buf_len;
	req->usbinfo = usbos_info;
	req->arg = rxirb;
	if (ep_idx == 0) {
		rx_pipe = usbos_info->rx_pipe;
	} else {
		rx_pipe = usbos_info->rx_pipe2;
		ASSERT(usbos_info->rx_pipe2);
	}
	/* Prepare the URB */
	usb_fill_bulk_urb(req->urb, usbos_info->usb, rx_pipe,
		p,
		rxirb->buf_len,
		(usb_complete_t)dbus_usbos_recv_complete, req);
		req->urb->transfer_flags |= URB_QUEUE_BULK;

	if ((ret = USB_SUBMIT_URB(req->urb))) {
		DBUSERR(("%s USB_SUBMIT_URB failed. status %d\n", __FUNCTION__, ret));
		dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
		ret = DBUS_ERR_RXFAIL;
		goto fail;
	}
	usbos_info->rxposted++;
#if defined(DBUS_LINUX_HIST)
	usbos_info->rxposted_hist[usbos_info->rxposted]++;
#endif 

	dbus_usbos_qenq(&usbos_info->req_rxpostedq, req, &usbos_info->rxposted_lock);
fail:
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);
	return ret;
}

#ifdef DBUS_LINUX_RXDPC
static void BCMFASTPATH
dbus_usbos_recv_dpc(usbos_info_t *usbos_info)
{
	urb_req_t *req = NULL;
	dbus_irb_rx_t *rxirb = NULL;
	int dbus_status = DBUS_OK;
	bool killed = (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPEND_PENDING) ? 1 : 0;

#if defined(DBUS_LINUX_HIST)
	int cnt = 0;

	usbos_info->dpc_cnt++;
#endif 

	while ((req = dbus_usbos_qdeq(&usbos_info->req_rxpendingq,
		&usbos_info->rxpending_lock)) != NULL) {
		struct urb *urb = req->urb;
		rxirb = req->arg;

		/* Handle errors */
		if (urb->status) {
			/*
			 * Linux 2.4 disconnect: -ENOENT or -EILSEQ for CRC error; rmmod: -ENOENT
			 * Linux 2.6 disconnect: -EPROTO, rmmod: -ESHUTDOWN
			 */
			if ((urb->status == -ENOENT && (!killed)) || urb->status == -ESHUTDOWN) {
				/* NOTE: unlink() can not be called from URB callback().
				 * Do not call dbusos_stop() here.
				 */
				dbus_usbos_state_change(usbos_info, DBUS_STATE_DOWN);
			} else if (urb->status == -EPROTO) {
			} else {
				DBUSERR(("%s rx error %d\n", __FUNCTION__, urb->status));
				dbus_usbos_errhandler(usbos_info, DBUS_ERR_RXFAIL);
			}

			/* On error, don't submit more URBs yet */
			DBUSERR(("%s %d rx error %d\n", __FUNCTION__, __LINE__, urb->status));
			rxirb->buf = NULL;
			rxirb->actual_len = 0;
			dbus_status = DBUS_ERR_RXFAIL;
			goto fail;
		}

		/* Make the skb represent the received urb */
		rxirb->actual_len = urb->actual_length;

fail:
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
		PKTFRMNATIVE(usbos_info->pub->osh, rxirb->pkt);
		/* detach the packet from the req */
		req->pkt = NULL;
#endif

		usbos_info->rxpending--;
#if defined(DBUS_LINUX_HIST)
		cnt++;
#endif 
		dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
		if (usbos_info->cbarg && usbos_info->cbs &&
			usbos_info->cbs->recv_irb_complete) {
			usbos_info->cbs->recv_irb_complete(usbos_info->cbarg, rxirb, dbus_status);
		}
	}

#if defined(DBUS_LINUX_HIST)
	usbos_info->dpc_pktcnt += cnt;
	usbos_info->dpc_maxpktcnt = MAX(cnt, usbos_info->dpc_maxpktcnt);
#endif 
#ifdef DBUS_LINUX_HIST
	{
		static unsigned long last_dump = 0;

		/* dump every 20 sec */
		if (jiffies > (last_dump + 20*HZ)) {
			dbus_usbos_intf_dump(usbos_info, NULL);
			last_dump = jiffies;
		}
	}
#endif /* DBUS_LINUX_HIST */
}

static int BCMFASTPATH
dbus_usbos_dpc_thread(void *data)
{
	usbos_info_t *usbos_info = (usbos_info_t*)data;

	DAEMONIZE("dbus_rx_dpc");
	/* High priority for short response time. We will yield by ourselves. */
	/* SET_NICE(-10); */

	/* Run until signal received */
	while (1) {
		if (down_interruptible(&usbos_info->dpc_sem) == 0) {
			dbus_usbos_recv_dpc(usbos_info);
			RESCHED();
		} else
			break;
	}

	complete_and_exit(&usbos_info->dpc_exited, 0);
	return 0;
}
#endif /* DBUS_LINUX_RXDPC */

#ifdef USBOS_THREAD
void
dbus_usbos_recv_complete(CALLBACK_ARGS)
{
	dbus_usbos_dispatch_schedule(urb, regs);
}

void BCMFASTPATH
dbus_usbos_recv_complete_handle(urb_req_t *req, int len, int status)
{
#ifdef DBUS_LINUX_RXDPC
	usbos_info_t *usbos_info = req->usbinfo;
	unsigned long flags;

	spin_lock_irqsave(&usbos_info->rxlock, flags);
	/* detach the packet from the queue */
	dbus_usbos_req_del(req, &usbos_info->rxposted_lock);
	usbos_info->rxposted--;

	/* Enqueue to rxpending queue */
	usbos_info->rxpending++;
	dbus_usbos_qenq(&usbos_info->req_rxpendingq, req, &usbos_info->rxpending_lock);
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);

	/* Wake up dpc for further processing */
	ASSERT(usbos_info->dpc_pid >= 0);
	up(&usbos_info->dpc_sem);
#else
	dbus_irb_rx_t *rxirb = req->arg;
	usbos_info_t *usbos_info = req->usbinfo;
	unsigned long flags;
	int dbus_status = DBUS_OK;
	bool killed = (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPEND_PENDING) ? 1 : 0;

	spin_lock_irqsave(&usbos_info->rxlock, flags);
	dbus_usbos_req_del(req, &usbos_info->rxposted_lock);
	usbos_info->rxposted--;
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);

	/* Handle errors */
	if (status) {
		/*
		 * Linux 2.4 disconnect: -ENOENT or -EILSEQ for CRC error; rmmod: -ENOENT
		 * Linux 2.6 disconnect: -EPROTO, rmmod: -ESHUTDOWN
		 */
		if ((status == -ENOENT && (!killed))|| status == -ESHUTDOWN) {
			/* NOTE: unlink() can not be called from URB callback().
			 * Do not call dbusos_stop() here.
			 */
			dbus_usbos_state_change(usbos_info, DBUS_STATE_DOWN);
		} else if (status == -EPROTO) {
		} else {
			DBUSTRACE(("%s rx error %d\n", __FUNCTION__, status));
			dbus_usbos_errhandler(usbos_info, DBUS_ERR_RXFAIL);
		}

		/* On error, don't submit more URBs yet */
		rxirb->buf = NULL;
		rxirb->actual_len = 0;
		dbus_status = DBUS_ERR_RXFAIL;
		goto fail;
	}

	/* Make the skb represent the received urb */
	rxirb->actual_len = len;
fail:
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
	PKTFRMNATIVE(usbos_info->pub->osh, rxirb->pkt);
	/* detach the packet from the queue */
	req->pkt = NULL;
#endif /* BCM_RPC_NOCOPY || BCM_RPC_RXNOCOPY */

	dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
	if (usbos_info->cbarg && usbos_info->cbs) {
		if (usbos_info->cbs->recv_irb_complete) {
			usbos_info->cbs->recv_irb_complete(usbos_info->cbarg, rxirb, dbus_status);
		}
	}
#endif /* DBUS_LINUX_RXDPC */
}


#else /*  !USBOS_THREAD */
void BCMFASTPATH
dbus_usbos_recv_complete(CALLBACK_ARGS)
{
#ifdef DBUS_LINUX_RXDPC
	urb_req_t *req = urb->context;
	usbos_info_t *usbos_info = req->usbinfo;
	unsigned long flags;

	spin_lock_irqsave(&usbos_info->rxlock, flags);
	/* detach the packet from the queue */
	dbus_usbos_req_del(req, &usbos_info->rxposted_lock);
	usbos_info->rxposted--;

	/* Enqueue to rxpending queue */
	usbos_info->rxpending++;
	dbus_usbos_qenq(&usbos_info->req_rxpendingq, req, &usbos_info->rxpending_lock);
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);

	/* Wake up dpc for further processing */
	ASSERT(usbos_info->dpc_pid >= 0);
	up(&usbos_info->dpc_sem);
#else
	urb_req_t *req = urb->context;
	dbus_irb_rx_t *rxirb = req->arg;
	usbos_info_t *usbos_info = req->usbinfo;
	unsigned long flags;
	int dbus_status = DBUS_OK;
	bool killed = (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPEND_PENDING) ? 1 : 0;

	spin_lock_irqsave(&usbos_info->rxlock, flags);
	dbus_usbos_req_del(req, &usbos_info->rxposted_lock);
	usbos_info->rxposted--;
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);

	/* Handle errors */
	if (urb->status) {
		/*
		 * Linux 2.4 disconnect: -ENOENT or -EILSEQ for CRC error; rmmod: -ENOENT
		 * Linux 2.6 disconnect: -EPROTO, rmmod: -ESHUTDOWN
		 */
		if ((urb->status == -ENOENT && (!killed))|| urb->status == -ESHUTDOWN) {
			/* NOTE: unlink() can not be called from URB callback().
			 * Do not call dbusos_stop() here.
			 */
			dbus_usbos_state_change(usbos_info, DBUS_STATE_DOWN);
		} else if (urb->status == -EPROTO) {
		} else {
			DBUSTRACE(("%s rx error %d\n", __FUNCTION__, urb->status));
			dbus_usbos_errhandler(usbos_info, DBUS_ERR_RXFAIL);
		}

		/* On error, don't submit more URBs yet */
		rxirb->buf = NULL;
		rxirb->actual_len = 0;
		dbus_status = DBUS_ERR_RXFAIL;
		goto fail;
	}

	/* Make the skb represent the received urb */
	rxirb->actual_len = urb->actual_length;
fail:
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
	/* detach the packet from the queue */
	req->pkt = NULL;
#endif /* BCM_RPC_NOCOPY || BCM_RPC_RXNOCOPY */

	dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
	if (usbos_info->cbarg && usbos_info->cbs) {
		if (usbos_info->cbs->recv_irb_complete) {
			usbos_info->cbs->recv_irb_complete(usbos_info->cbarg, rxirb, dbus_status);
		}
	}
#endif /* DBUS_LINUX_RXDPC */
}
#endif /*  USBOS_THREAD */


static void
dbus_usbos_ctl_complete(usbos_info_t *usbos_info, int type, int urbstatus)
{
	int status = DBUS_ERR;

	if (usbos_info == NULL)
		return;

	switch (urbstatus) {
		case 0:
			status = DBUS_OK;
		break;
		case -EINPROGRESS:
		case -ENOENT:
		default:
			DBUSERR(("%s: failed with status %d\n", __FUNCTION__, urbstatus));
			status = DBUS_ERR;
		break;
	}

	if (usbos_info->cbarg && usbos_info->cbs) {
		if (usbos_info->cbs->ctl_complete)
			usbos_info->cbs->ctl_complete(usbos_info->cbarg, type, status);
	}
}

static void
dbus_usbos_ctlread_complete(CALLBACK_ARGS)
{
	usbos_info_t *usbos_info = (usbos_info_t *)urb->context;

	ASSERT(urb);
	usbos_info = (usbos_info_t *)urb->context;

	dbus_usbos_ctl_complete(usbos_info, DBUS_CBCTL_READ, urb->status);
}

static void
dbus_usbos_ctlwrite_complete(CALLBACK_ARGS)
{
	usbos_info_t *usbos_info = (usbos_info_t *)urb->context;

	ASSERT(urb);
	usbos_info = (usbos_info_t *)urb->context;

	dbus_usbos_ctl_complete(usbos_info, DBUS_CBCTL_WRITE, urb->status);
}

static void
dbus_usbos_unlink(struct list_head *urbreq_q, spinlock_t *lock)
{
	urb_req_t *req;

	/* dbus_usbos_recv_complete() adds req back to req_freeq */
	while ((req = dbus_usbos_qdeq(urbreq_q, lock)) != NULL) {
		ASSERT(req->urb != NULL);
		USB_UNLINK_URB(req->urb);
	}
}

static void
dbusos_stop(usbos_info_t *usbos_info)
{
	urb_req_t *req;
	req = NULL;

	ASSERT(usbos_info);

#ifdef USB_TRIGGER_DEBUG
	dbus_usbos_ctl_send_debugtrig(usbos_info);
#endif /* USB_TRIGGER_DEBUG */
	dbus_usbos_state_change(usbos_info, DBUS_STATE_DOWN);
	DBUSTRACE(("%s: unlink all URBs\n", __FUNCTION__));
	if (usbos_info->intr_urb)
		USB_UNLINK_URB(usbos_info->intr_urb);

	if (usbos_info->ctl_urb)
		USB_UNLINK_URB(usbos_info->ctl_urb);

	if (usbos_info->blk_urb)
		USB_UNLINK_URB(usbos_info->blk_urb);

	dbus_usbos_unlink(&usbos_info->req_txpostedq, &usbos_info->txposted_lock);
	if (usbos_info->txposted > 0) {
		DBUSERR(("%s ERROR: tx REQs posted=%d in stop!\n", __FUNCTION__,
			usbos_info->txposted));
	}
	dbus_usbos_unlink(&usbos_info->req_rxpostedq, &usbos_info->rxposted_lock);
	if (usbos_info->rxposted > 0) {
		DBUSERR(("%s ERROR: rx REQs posted=%d in stop!\n", __FUNCTION__,
			usbos_info->rxposted));
	}

	/* Make sure all the urb are completed, usb_unlink_urb doesn't gaurantee
	 * that. Wait for 9000us since max irq interval for EHCI is 8ms.
	 */
	SPINWAIT(usbos_info->txposted != 0 || usbos_info->rxposted != 0, 9000);
	ASSERT(usbos_info->txposted == 0 && usbos_info->rxposted == 0);

#ifdef DBUS_LINUX_RXDPC
	/* Stop the dpc thread */
	if (usbos_info->dpc_pid >= 0) {
		KILL_PROC(usbos_info->dpc_pid, SIGTERM);
		wait_for_completion(&usbos_info->dpc_exited);
	}

	/* Move pending reqs to free queue so they can be freed */
	while ((req = dbus_usbos_qdeq(&usbos_info->req_rxpendingq,
		&usbos_info->rxpending_lock)) != NULL) {
		dbus_usbos_qenq(&usbos_info->req_freeq, req,
			&usbos_info->free_lock);
	}
#endif /* DBUS_LINUX_RXDPC */
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	dbus_usb_wd_remove(usbos_info);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
}

#ifdef KERNEL26
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
static int
dbus_usbos_suspend(struct usb_interface *intf,
            pm_message_t message)
{
	DBUSERR(("%s suspend state: %d\n", __FUNCTION__, g_probe_info.suspend_state));
	if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPEND_PENDING) {
		g_probe_info.suspend_state = USBOS_SUSPEND_STATE_SUSPENDED;
		return 0;
	}
	else {
		return -EBUSY;
	}
}

static int dbus_usbos_resume(struct usb_interface *intf)
{
	DBUSERR(("%s Device resumed\n", __FUNCTION__));
	/* For device initiated resume, set the usage count */
	g_probe_info.suspend_state = USBOS_SUSPEND_STATE_DEVICE_ACTIVE;
	if (g_probe_info.intf->pm_usage_cnt == 0) {
		g_probe_info.intf->pm_usage_cnt = 1;
		dbus_usbos_intf_up((void *)g_probe_info.usbos_info);
	}
	return 0;
}
#endif /* ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

static int
dbus_usbos_probe(struct usb_interface *intf, const struct usb_device_id *id)
#else
static void *
dbus_usbos_probe(struct usb_device *usb, unsigned int ifnum, const struct usb_device_id *id)
#endif /* KERNEL26 */
{
	int ep;
	struct usb_endpoint_descriptor *endpoint;
	int ret = 0;
#ifdef KERNEL26
	struct usb_device *usb = interface_to_usbdev(intf);
#else
	int claimed = 0;
#endif
	int num_of_eps;

	g_probe_info.usb = usb;
	g_probe_info.dldone = TRUE;
#ifdef KERNEL26
	g_probe_info.intf = intf;
#endif /* KERNEL26 */


	if (id != NULL) {
		g_probe_info.vid = id->idVendor;
		g_probe_info.pid = id->idProduct;
	}

#ifdef KERNEL26
	usb_set_intfdata(intf, &g_probe_info);
#endif

	/* Check that the device supports only one configuration */
	if (usb->descriptor.bNumConfigurations != 1) {
		ret = -1;
		goto fail;
	}

	if (usb->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) {
		ret = -1;
		goto fail;
	}

	/*
	 * Only the BDC interface configuration is supported:
	 *	Device class: USB_CLASS_VENDOR_SPEC
	 *	if0 class: USB_CLASS_VENDOR_SPEC
	 *	if0/ep0: control
	 *	if0/ep1: bulk in
	 *	if0/ep2: bulk out (ok if swapped with bulk in)
	 */
	if (CONFIGDESC(usb)->bNumInterfaces != 1) {
		ret = -1;
		goto fail;
	}

	/* Check interface */
#ifndef KERNEL26
	if (usb_interface_claimed(IFPTR(usb, CONTROL_IF))) {
		ret = -1;
		goto fail;
	}
#endif

	if (IFDESC(usb, CONTROL_IF).bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
	    IFDESC(usb, CONTROL_IF).bInterfaceSubClass != 2 ||
	    IFDESC(usb, CONTROL_IF).bInterfaceProtocol != 0xff) {
		DBUSERR(("%s: invalid control interface: class %d, subclass %d, proto %d\n",
		           __FUNCTION__,
		           IFDESC(usb, CONTROL_IF).bInterfaceClass,
		           IFDESC(usb, CONTROL_IF).bInterfaceSubClass,
		           IFDESC(usb, CONTROL_IF).bInterfaceProtocol));
		ret = -1;
		goto fail;
	}

	/* Check control endpoint */
	endpoint = &IFEPDESC(usb, CONTROL_IF, 0);
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT) {
		DBUSERR(("%s: invalid control endpoint %d\n",
		           __FUNCTION__, endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK));
		ret = -1;
		goto fail;
	}

	g_probe_info.intr_pipe =
		usb_rcvintpipe(usb, endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

#ifndef KERNEL26
	/* Claim interface */
	usb_driver_claim_interface(&dbus_usbdev, IFPTR(usb, CONTROL_IF), &g_probe_info);
	claimed = 1;
#endif
	g_probe_info.rx_pipe = 0;
	g_probe_info.rx_pipe2 = 0;
	g_probe_info.tx_pipe = 0;
	num_of_eps = IFDESC(usb, BULK_IF).bNumEndpoints - 1;
	if ((num_of_eps != 2) && (num_of_eps != 3)) {
		ASSERT(0);
	}
	/* Check data endpoints and get pipes */
	for (ep = 1; ep <= num_of_eps; ep++) {
		endpoint = &IFEPDESC(usb, BULK_IF, ep);
		if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
		    USB_ENDPOINT_XFER_BULK) {
			DBUSERR(("%s: invalid data endpoint %d\n",
			           __FUNCTION__, ep));
			ret = -1;
			goto fail;
		}

		if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) {
			if (!g_probe_info.rx_pipe) {
				g_probe_info.rx_pipe = usb_rcvbulkpipe(usb,
					(endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK));
			} else {
				g_probe_info.rx_pipe2 = usb_rcvbulkpipe(usb,
					(endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK));
			}

		} else
			g_probe_info.tx_pipe = usb_sndbulkpipe(usb, (endpoint->bEndpointAddress &
			                                     USB_ENDPOINT_NUMBER_MASK));
	}

	/* Allocate interrupt URB and data buffer */
	/* RNDIS says 8-byte intr, our old drivers used 4-byte */
	g_probe_info.intr_size = (IFEPDESC(usb, CONTROL_IF, 0).wMaxPacketSize == 16) ? 8 : 4;

	g_probe_info.interval = IFEPDESC(usb, CONTROL_IF, 0).bInterval;

#ifndef KERNEL26
	/* usb_fill_int_urb does the interval decoding in 2.6 */
	if (usb->speed == USB_SPEED_HIGH)
		g_probe_info.interval = 1 << (g_probe_info.interval - 1);
#endif
	if (usb->speed == USB_SPEED_HIGH) {
		g_probe_info.device_speed = HIGH_SPEED;
		DBUSERR(("high speed device detected\n"));
	} else {
		g_probe_info.device_speed = FULL_SPEED;
		DBUSERR(("full speed device detected\n"));
	}
	if (probe_cb) {
		disc_arg = probe_cb(probe_arg, "", USB_BUS, 0);
	}

	g_probe_info.disc_cb_done = FALSE;

	/* Success */
#ifdef KERNEL26
	return DBUS_OK;
#else
	usb_inc_dev_use(usb);
	return &g_probe_info;
#endif

fail:
	DBUSERR(("%s: failed with errno %d\n", __FUNCTION__, ret));
#ifndef KERNEL26
	if (claimed)
		usb_driver_release_interface(&dbus_usbdev, IFPTR(usb, CONTROL_IF));
#endif
#ifdef KERNEL26
	usb_set_intfdata(intf, NULL);
#endif

#ifdef KERNEL26
	return ret;
#else
	return NULL;
#endif
}

#ifdef KERNEL26
static void
dbus_usbos_disconnect(struct usb_interface *intf)
#else
static void
dbus_usbos_disconnect(struct usb_device *usb, void *ptr)
#endif
{
#ifdef KERNEL26
	struct usb_device *usb = interface_to_usbdev(intf);
	probe_info_t *probe_usb_init_data = usb_get_intfdata(intf);
#else
	probe_info_t *probe_usb_init_data = (probe_info_t *) ptr;
#endif
	usbos_info_t *usbos_info;

	if ((probe_usb_init_data == NULL) || (usb == NULL)) {
		/* Should never happen */
		ASSERT(0);
		return;
	}

	usbos_info = (usbos_info_t *) probe_usb_init_data->usbos_info;
	if (usbos_info) {
		if ((probe_usb_init_data->dereged == FALSE) && disconnect_cb) {
			disconnect_cb(disc_arg);
			probe_usb_init_data->disc_cb_done = TRUE;
		}
	}

#ifndef KERNEL26
	usb_driver_release_interface(&dbus_usbdev, IFPTR(usb, CONTROL_IF));
	usb_dec_dev_use(usb);
#endif
}

static int
dbus_usbos_intf_send_irb(void *bus, dbus_irb_tx_t *txirb)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	urb_req_t *req;
	int ret = DBUS_OK;
	unsigned long flags;
	void *pkt;

	if (usbos_info == NULL)
		return DBUS_ERR;

	if (!(req = dbus_usbos_qdeq(&usbos_info->req_freeq, &usbos_info->free_lock))) {
		DBUSERR(("%s No free URB!\n", __FUNCTION__));
		return DBUS_ERR_TXDROP;
	}
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPENDED) {
		if (dbus_usbos_intf_pnp(usbos_info, DBUS_PNP_RESUME) != BCME_OK) {
			DBUSERR(("%s Could not Resume the bus!\n", __FUNCTION__));
			return DBUS_ERR_TXDROP;
		}
	}
	usbos_info->activity = TRUE;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

	spin_lock_irqsave(&usbos_info->txlock, flags);
	req->arg = txirb;
	req->usbinfo = usbos_info;

	/* Prepare the URB */
	if (txirb->buf) {
		usb_fill_bulk_urb(req->urb, usbos_info->usb, usbos_info->tx_pipe, txirb->buf,
			txirb->len, (usb_complete_t)dbus_usbos_send_complete, req);
	} else if (txirb->pkt) {
		uint32 len = 0, pktlen = 0;
		void *transfer_buf;
		/* check the length and change if not 4 bytes aligned. */
		if (PKTNEXT(osbos_info->pub->osh, txirb->pkt)) {
			transfer_buf = kmalloc(pkttotlen(usbos_info->pub->osh, txirb->pkt),
				GFP_ATOMIC);
			if (!transfer_buf) {
				ret = DBUS_ERR_TXDROP;
				DBUSERR(("fail to alloc to usb buffer\n"));
				goto fail;
			}
			pkt = txirb->pkt;
			txirb->send_buf = transfer_buf;
			while (pkt) {
				pktlen = PKTLEN(usbos_info->pub->osh, pkt);
				bcopy(PKTDATA(usbos_info->pub->osh, pkt), transfer_buf, pktlen);
				transfer_buf += pktlen;
				len += pktlen;
				pkt = PKTNEXT(usbos_info->pub->osh, pkt);
			}

			len = ROUNDUP(len, sizeof(uint32));
			usb_fill_bulk_urb(req->urb, usbos_info->usb, usbos_info->tx_pipe,
				txirb->send_buf,
				len,
				(usb_complete_t)dbus_usbos_send_complete, req);

		} else {
			txirb->send_buf = NULL;
			len = PKTLEN(usbos_info->pub->osh, txirb->pkt);
			len = ROUNDUP(len, sizeof(uint32));
			usb_fill_bulk_urb(req->urb, usbos_info->usb, usbos_info->tx_pipe,
				PKTDATA(usbos_info->pub->osh, txirb->pkt),
				len,
				(usb_complete_t)dbus_usbos_send_complete, req);
		}
	} else {
		ASSERT(0);
	}

	req->urb->transfer_flags |= URB_QUEUE_BULK;

	if ((ret = USB_SUBMIT_URB(req->urb))) {
		dbus_usbos_qenq(&usbos_info->req_freeq, req, &usbos_info->free_lock);
		ret = DBUS_ERR_TXDROP;
		goto fail;
	}

	usbos_info->txposted++;

	dbus_usbos_qenq(&usbos_info->req_txpostedq, req, &usbos_info->txposted_lock);
fail:
	spin_unlock_irqrestore(&usbos_info->txlock, flags);
	return ret;
}

static int
dbus_usbos_intf_recv_irb(void *bus, dbus_irb_rx_t *rxirb)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	int ret = DBUS_OK;

	if (usbos_info == NULL)
		return DBUS_ERR;

	ret = dbus_usbos_recv_urb_submit(usbos_info, rxirb, 0);
	return ret;
}

static int
dbus_usbos_intf_recv_irb_from_ep(void *bus, dbus_irb_rx_t *rxirb, uint32 ep_idx)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	int ret = DBUS_OK;

	if (usbos_info == NULL)
		return DBUS_ERR;

	ret = dbus_usbos_recv_urb_submit(usbos_info, rxirb, ep_idx);
	return ret;
}
static int
dbus_usbos_intf_cancel_irb(void *bus, dbus_irb_tx_t *txirb)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if (usbos_info == NULL)
		return DBUS_ERR;

	/* FIX: Need to implement */
	return DBUS_ERR;
}

static int
dbus_usbos_intf_send_ctl(void *bus, uint8 *buf, int len)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	int ret = DBUS_OK;
	uint16 size;

	if ((usbos_info == NULL) || (buf == NULL) || (len == 0))
		return DBUS_ERR;

	if (usbos_info->ctl_urb == NULL)
		return DBUS_ERR;

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	/* If the USB/HSIC bus in sleep state, wake it up */
	if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPENDED) {
		if (dbus_usbos_intf_pnp(usbos_info, DBUS_PNP_RESUME) != BCME_OK) {
			DBUSERR(("%s Could not Resume the bus!\n", __FUNCTION__));
			return DBUS_ERR_TXDROP;
		}
	}
	usbos_info->activity = TRUE;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
	size = len;
	if (usbos_info->pub->attrib.devid == 0x4330 && usbos_info->pub->attrib.chiprev < 3) {
		size = ROUNDUP(len, 64);
	}
	usbos_info->ctl_write.wLength = cpu_to_le16p(&size);
	usbos_info->ctl_urb->transfer_buffer_length = size;

	usb_fill_control_urb(usbos_info->ctl_urb,
		usbos_info->usb,
		usbos_info->ctl_out_pipe,
		(unsigned char *) &usbos_info->ctl_write,
		buf, size, (usb_complete_t)dbus_usbos_ctlwrite_complete, usbos_info);

	ret = USB_SUBMIT_URB(usbos_info->ctl_urb);
	if (ret < 0) {
		DBUSERR(("%s: usb_submit_urb failed %d\n", __FUNCTION__, ret));
		return DBUS_ERR_TXCTLFAIL;
	}

	return DBUS_OK;
}

static int
dbus_usbos_intf_recv_ctl(void *bus, uint8 *buf, int len)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	int ret = DBUS_OK;
	uint16 size;

	if ((usbos_info == NULL) || (buf == NULL) || (len == 0))
		return DBUS_ERR;

	if (usbos_info->ctl_urb == NULL)
		return DBUS_ERR;

	size = len;
	usbos_info->ctl_read.wLength = cpu_to_le16p(&size);
	usbos_info->ctl_urb->transfer_buffer_length = size;

	if (usbos_info->rxctl_deferrespok) {
		/* BMAC model */
		usbos_info->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_VENDOR |
			USB_RECIP_INTERFACE;
		usbos_info->ctl_read.bRequest = DL_DEFER_RESP_OK;
	} else {
		/* full dongle model */
		usbos_info->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_CLASS |
			USB_RECIP_INTERFACE;
		usbos_info->ctl_read.bRequest = 1;
	}

	usb_fill_control_urb(usbos_info->ctl_urb,
		usbos_info->usb,
		usbos_info->ctl_in_pipe,
		(unsigned char *) &usbos_info->ctl_read,
		buf, size, (usb_complete_t)dbus_usbos_ctlread_complete, usbos_info);

	ret = USB_SUBMIT_URB(usbos_info->ctl_urb);
	if (ret < 0) {
		DBUSERR(("%s: usb_submit_urb failed %d\n", __FUNCTION__, ret));
		return DBUS_ERR_RXCTLFAIL;
	}

	return ret;
}

static int
dbus_usbos_intf_get_attrib(void *bus, dbus_attrib_t *attrib)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if ((usbos_info == NULL) || (attrib == NULL))
		return DBUS_ERR;

	attrib->bustype = DBUS_USB;
	attrib->vid = g_probe_info.vid;
	attrib->pid = g_probe_info.pid;
	attrib->devid = 0x4322;

	/* FIX: Need nchan for both TX and RX?;
	 * BDC uses one RX pipe and one TX pipe
	 * RPC may use two RX pipes and one TX pipe?
	 */
	attrib->nchan = 1;

	/* MaxPacketSize for USB hi-speed bulk out is 512 pipes
	 * and 64-bytes for full-speed.
	 * When sending pkt > MaxPacketSize, Host SW breaks it
	 * up into multiple packets.
	 */
	attrib->mtu = usbos_info->maxps;

	return DBUS_OK;
}

static int
dbus_usbos_intf_up(void *bus)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	uint16 ifnum;

	if (usbos_info == NULL)
		return DBUS_ERR;

	if (usbos_info->usb == NULL)
		return DBUS_ERR;

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	/* If the USB/HSIC bus in sleep state, wake it up */
	if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPENDED) {
		if (dbus_usbos_intf_pnp(usbos_info, DBUS_PNP_RESUME) != BCME_OK) {
			DBUSERR(("%s Could not Resume the bus!\n", __FUNCTION__));
			return DBUS_ERR_TXDROP;
		}
	}
	usbos_info->activity = TRUE;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

	if (usbos_info->ctl_urb) {
		usbos_info->ctl_in_pipe = usb_rcvctrlpipe(usbos_info->usb, 0);
		usbos_info->ctl_out_pipe = usb_sndctrlpipe(usbos_info->usb, 0);

		ifnum = cpu_to_le16(IFDESC(usbos_info->usb, CONTROL_IF).bInterfaceNumber);
		/* CTL Write */
		usbos_info->ctl_write.bRequestType =
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
		usbos_info->ctl_write.bRequest = 0;
		usbos_info->ctl_write.wValue = cpu_to_le16(0);
		usbos_info->ctl_write.wIndex = cpu_to_le16p(&ifnum);

		/* CTL Read */
		usbos_info->ctl_read.bRequestType =
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
		usbos_info->ctl_read.bRequest = 1;
		usbos_info->ctl_read.wValue = cpu_to_le16(0);
		usbos_info->ctl_read.wIndex = cpu_to_le16p(&ifnum);
	}

	/* Success, indicate usbos_info is fully up */
	dbus_usbos_state_change(usbos_info, DBUS_STATE_UP);
	return DBUS_OK;
}

static int
dbus_usbos_intf_down(void *bus)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if (usbos_info == NULL)
		return DBUS_ERR;

	dbusos_stop(usbos_info);
	return DBUS_OK;
}

static int
dbus_usbos_intf_stop(void *bus)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if (usbos_info == NULL)
		return DBUS_ERR;

	dbusos_stop(usbos_info);
	return DBUS_OK;
}

#if defined(DBUS_LINUX_HIST)
static void
dbus_usbos_intf_dump(void *bus, struct bcmstrbuf *b)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	int i = 0, j = 0;

	if (b) {
		bcm_bprintf(b, "\ndbus linux dump\n");
		bcm_bprintf(b, "txposted %d rxposted %d\n",
			usbos_info->txposted, usbos_info->rxposted);

		bcm_bprintf(b, "RXDPC: dpc_cnt %d dpc_pktcnt %d dpc_maxpktcnt %d avg_dpc_pktcnt\n",
			usbos_info->dpc_cnt, usbos_info->dpc_pktcnt,
			usbos_info->dpc_maxpktcnt, usbos_info->dpc_cnt ?
			(usbos_info->dpc_pktcnt/usbos_info->dpc_cnt):1);

		/* Histrogram */
		bcm_bprintf(b, "txposted\n");
	} else {
		printf("\ndbus linux dump\n");
		printf("txposted %d rxposted %d\n",
			usbos_info->txposted, usbos_info->rxposted);
		printf("RXDPC: dpc_cnt %d dpc_pktcnt %d dpc_maxpktcnt %d avg_dpc_pktcnt %d\n",
			usbos_info->dpc_cnt, usbos_info->dpc_pktcnt,
			usbos_info->dpc_maxpktcnt, usbos_info->dpc_cnt ?
			(usbos_info->dpc_pktcnt/usbos_info->dpc_cnt):1);

		/* Histrogram */
		printf("txposted\n");
	}

	for (i = 0; i < usbos_info->pub->ntxq; i++) {
		if (usbos_info->txposted_hist[i]) {
			if (b)
				bcm_bprintf(b, "%d: %d ", i, usbos_info->txposted_hist[i]);
			else
				printf("%d: %d ", i, usbos_info->txposted_hist[i]);
			j++;
			if (j % 10 == 0) {
				if (b)
					bcm_bprintf(b, "\n");
				else
					printf("\n");
			}
		}
	}

	j = 0;
	if (b)
		bcm_bprintf(b, "\nrxposted\n");
	else
		printf("\nrxposted\n");
	for (i = 0; i < usbos_info->pub->nrxq; i++) {
		if (usbos_info->rxposted_hist[i]) {
			if (b)
				bcm_bprintf(b, "%d: %d ", i, usbos_info->rxposted_hist[i]);
			else
				printf("%d: %d ", i, usbos_info->rxposted_hist[i]);
			j++;
			if (j % 10 == 0) {
				if (b)
					bcm_bprintf(b, "\n");
				else
					printf("\n");
			}
		}
	}
	if (b)
		bcm_bprintf(b, "\n");
	else
		printf("\n");

	return;
}
#endif 

static int
dbus_usbos_intf_set_config(void *bus, dbus_config_t *config)
{
	usbos_info_t* usbos_info = bus;

	usbos_info->rxctl_deferrespok = config->rxctl_deferrespok;
	return DBUS_OK;
}

static int
dbus_usbos_sync_wait(usbos_info_t *usbinfo, uint16 time)
{
	int ret;
	int err = DBUS_OK;
	int ms = time;

	ret = wait_event_interruptible_timeout(usbinfo->wait,
		usbinfo->waitdone == TRUE, (ms * HZ / 1000));

	if ((usbinfo->waitdone == FALSE) || (usbinfo->sync_urb_status)) {
		DBUSERR(("%s: timeout(%d) or urb err=0x%x\n",
			__FUNCTION__, ret, usbinfo->sync_urb_status));
		err = DBUS_ERR;
	}
	usbinfo->waitdone = FALSE;
	return err;
}

static void
dbus_usbos_sync_complete(CALLBACK_ARGS)
{
	usbos_info_t *usbos_info = (usbos_info_t *)urb->context;

	usbos_info->waitdone = TRUE;
	wake_up_interruptible(&usbos_info->wait);

	usbos_info->sync_urb_status = urb->status;

	if (urb->status) {
		DBUSERR(("%s: sync urb error %d\n", __FUNCTION__, urb->status));
	}
}


bool
dbus_usbos_dl_cmd(usbos_info_t *usbinfo, uint8 cmd, void *buffer, int buflen)
{
	int ret = DBUS_OK;
	char *tmpbuf;
	uint16 size;

	if ((usbinfo == NULL) || (buffer == NULL) || (buflen == 0))
		return FALSE;

	if (usbinfo->ctl_urb == NULL)
		return FALSE;

	tmpbuf = (char *) MALLOC(usbinfo->pub->osh, buflen);
	if (!tmpbuf) {
		DBUSERR(("%s: Unable to allocate memory \n", __FUNCTION__));
		return FALSE;
	}

	size = buflen;
	usbinfo->ctl_urb->transfer_buffer_length = size;

	usbinfo->ctl_read.wLength = cpu_to_le16p(&size);
	usbinfo->ctl_read.bRequestType = USB_DIR_IN | USB_TYPE_VENDOR |
		USB_RECIP_INTERFACE;
	usbinfo->ctl_read.bRequest = cmd;

	usb_fill_control_urb(usbinfo->ctl_urb,
		usbinfo->usb,
		usb_rcvctrlpipe(usbinfo->usb, 0),
		(unsigned char *) &usbinfo->ctl_read,
		(void *) tmpbuf, size, (usb_complete_t)dbus_usbos_sync_complete, usbinfo);

	ret = USB_SUBMIT_URB(usbinfo->ctl_urb);
	if (ret < 0) {
		DBUSERR(("%s: usb_submit_urb failed %d\n", __FUNCTION__, ret));
		MFREE(usbinfo->pub->osh, tmpbuf, buflen);
		return FALSE;
	}

	ret = dbus_usbos_sync_wait(usbinfo, USB_SYNC_WAIT_TIMEOUT);
	memcpy(buffer, tmpbuf, buflen);
	MFREE(usbinfo->pub->osh, tmpbuf, buflen);

	return (ret == DBUS_OK);
}
int
dbus_write_membytes(usbos_info_t* usbinfo, bool set, uint32 address, uint8 *data, uint size)
{
	hwacc_t hwacc;
	int write_bytes = 4;
	int status;
	int retval = 0;

	DBUSTRACE(("Enter:%s\n", __FUNCTION__));


	/* Read is not supported */
	if (0 == set) {
		DBUSERR(("Currently read is not supported!!\n"));
		return -1;
	}

	hwacc.cmd = DL_CMD_WRHW;
	hwacc.addr = address;

	DBUSTRACE(("Address:%x size:%d", hwacc.addr, size));
	do {
		if (size >= 4) {
			write_bytes = 4;
		} else if (size >= 2) {
			write_bytes = 2;
		} else {
			write_bytes = 1;
		}

		hwacc.len = write_bytes;

		while (size >= write_bytes) {
			hwacc.data = *((unsigned int*)data);

			status = usb_control_msg(usbinfo->usb, usb_sndctrlpipe(usbinfo->usb, 0),
				DL_WRHW, UT_WRITE_VENDOR_INTERFACE, 1, 0, (char *)&hwacc,
				sizeof(hwacc_t), USB_CTRL_EP_TIMEOUT);

			if (status < 0) {
				retval = -1;
				DBUSERR((" Ctrl write hwacc failed w/status %d @ address:%x \n",
					status, hwacc.addr));
				goto err;
			}

			hwacc.addr += write_bytes;
			data += write_bytes;
			size -= write_bytes;
		}
	} while (size > 0);

err:
	return retval;
}


int
dbus_usbos_wait(usbos_info_t *usbinfo, uint16 ms)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
	/* FIX: Need to test under 2.6 kernel */
	mdelay(ms);
#else
	wait_ms(ms);
#endif
	return DBUS_OK;
}

bool
dbus_usbos_dl_send_bulk(usbos_info_t *usbinfo, void *buffer, int len)
{
	int ret = DBUS_OK;

	if (usbinfo == NULL)
		goto fail;

	if (usbinfo->blk_urb == NULL)
		goto fail;

	/* Prepare the URB */
	usb_fill_bulk_urb(usbinfo->blk_urb, usbinfo->usb, usbinfo->tx_pipe, buffer,
		len, (usb_complete_t)dbus_usbos_sync_complete, usbinfo);

	usbinfo->blk_urb->transfer_flags |= URB_QUEUE_BULK;

	if ((ret = USB_SUBMIT_URB(usbinfo->blk_urb))) {
		DBUSERR(("%s: usb_submit_urb failed %d\n", __FUNCTION__, ret));
		goto fail;
	}
	ret = dbus_usbos_sync_wait(usbinfo, USB_SYNC_WAIT_TIMEOUT);

	return (ret == DBUS_OK);
fail:
	return FALSE;
}

static bool
dbus_usbos_intf_recv_needed(void *bus)
{
	return FALSE;
}

static void*
dbus_usbos_intf_exec_rxlock(void *bus, exec_cb_t cb, struct exec_parms *args)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	void *ret;
	unsigned long flags;

	if (usbos_info == NULL)
		return NULL;

	spin_lock_irqsave(&usbos_info->rxlock, flags);
	ret = cb(args);
	spin_unlock_irqrestore(&usbos_info->rxlock, flags);

	return ret;
}

static void*
dbus_usbos_intf_exec_txlock(void *bus, exec_cb_t cb, struct exec_parms *args)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	void *ret;
	unsigned long flags;

	if (usbos_info == NULL)
		return NULL;

	spin_lock_irqsave(&usbos_info->txlock, flags);
	ret = cb(args);
	spin_unlock_irqrestore(&usbos_info->txlock, flags);

	return ret;
}

int
dbus_usbos_errhandler(void *bus, int err)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if (usbos_info == NULL)
		return DBUS_ERR;

	if (usbos_info->cbarg && usbos_info->cbs) {
		if (usbos_info->cbs->errhandler)
			usbos_info->cbs->errhandler(usbos_info->cbarg, err);
	}

	return DBUS_OK;
}

int
dbus_usbos_state_change(void *bus, int state)
{
	usbos_info_t *usbos_info = (usbos_info_t *) bus;

	if (usbos_info == NULL)
		return DBUS_ERR;

	if (usbos_info->cbarg && usbos_info->cbs) {
		if (usbos_info->cbs->state_change)
			usbos_info->cbs->state_change(usbos_info->cbarg, state);
	}

	usbos_info->pub->busstate = state;
	return DBUS_OK;
}

int
dbus_bus_osl_register(int vid, int pid, probe_cb_t prcb,
	disconnect_cb_t discb, void *prarg, dbus_intf_t **intf, void *param1, void *param2)
{
#ifdef USBSHIM
	usburb = &bcm_usburb;
#endif

	bzero(&g_probe_info, sizeof(probe_info_t));

	probe_cb = prcb;
	disconnect_cb = discb;
	probe_arg = prarg;

	devid_table[0].idVendor = vid;
	devid_table[0].idProduct = pid;

	*intf = &dbus_usbos_intf;

	USB_REGISTER();

	return DBUS_ERR_NODEVICE;
}

int
dbus_bus_osl_deregister()
{
	g_probe_info.dereged = TRUE;

	if (disconnect_cb && (g_probe_info.disc_cb_done == FALSE))
		disconnect_cb(disc_arg);

	USB_DEREGISTER();

	return DBUS_OK;
}

void *
dbus_usbos_intf_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs)
{
	usbos_info_t *usbos_info;

	if (g_probe_info.dldone == FALSE) {
		DBUSERR(("%s: err device not downloaded!\n", __FUNCTION__));
		return NULL;
	}

#ifdef USBSHIM
	usburb = &bcm_usburb;
#endif

	/* Sanity check for BUS_INFO() */
	ASSERT(OFFSETOF(usbos_info_t, pub) == 0);

	usbos_info = MALLOC(pub->osh, sizeof(usbos_info_t));
	if (usbos_info == NULL)
		return NULL;

	bzero(usbos_info, sizeof(usbos_info_t));

	usbos_info->pub = pub;
	usbos_info->cbarg = cbarg;
	usbos_info->cbs = cbs;

	/* Needed for disconnect() */
	g_probe_info.usbos_info = usbos_info;

	/* Update USB Info */
	usbos_info->usb = g_probe_info.usb;
	usbos_info->rx_pipe = g_probe_info.rx_pipe;
	usbos_info->rx_pipe2 = g_probe_info.rx_pipe2;
	usbos_info->tx_pipe = g_probe_info.tx_pipe;
	usbos_info->intr_pipe = g_probe_info.intr_pipe;
	usbos_info->intr_size = g_probe_info.intr_size;
	usbos_info->interval = g_probe_info.interval;
	usbos_info->pub->device_speed = g_probe_info.device_speed;
	if (usbos_info->rx_pipe2) {
		usbos_info->pub->attrib.has_2nd_bulk_in_ep = 1;
	} else {
		usbos_info->pub->attrib.has_2nd_bulk_in_ep = 0;
	}

	if (usbos_info->tx_pipe)
		usbos_info->maxps = usb_maxpacket(usbos_info->usb,
			usbos_info->tx_pipe, usb_pipeout(usbos_info->tx_pipe));

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	usbos_info->idletime = DHD_IDLETIME;
	usbos_info->idlecount = 0;
	usbos_info->activity = TRUE;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

	INIT_LIST_HEAD(&usbos_info->req_freeq);
	INIT_LIST_HEAD(&usbos_info->req_rxpostedq);
	INIT_LIST_HEAD(&usbos_info->req_txpostedq);
	spin_lock_init(&usbos_info->free_lock);
	spin_lock_init(&usbos_info->rxposted_lock);
	spin_lock_init(&usbos_info->txposted_lock);
	spin_lock_init(&usbos_info->rxlock);
	spin_lock_init(&usbos_info->txlock);

#ifdef DBUS_LINUX_RXDPC
	INIT_LIST_HEAD(&usbos_info->req_rxpendingq);
	spin_lock_init(&usbos_info->rxpending_lock);
#endif /* DBUS_LINUX_RXDPC */

#if defined(DBUS_LINUX_HIST)
	usbos_info->txposted_hist = MALLOC(pub->osh, (usbos_info->pub->ntxq+1) * sizeof(int));
	bzero(usbos_info->txposted_hist, (usbos_info->pub->ntxq+1) * sizeof(int));
	usbos_info->rxposted_hist = MALLOC(pub->osh, (usbos_info->pub->nrxq+1) * sizeof(int));
	bzero(usbos_info->rxposted_hist, (usbos_info->pub->nrxq+1) * sizeof(int));
#endif
#ifdef USB_DISABLE_INT_EP
	usbos_info->intr_urb = NULL;
#else
	if (!(usbos_info->intr_urb = USB_ALLOC_URB())) {
		DBUSERR(("%s: usb_alloc_urb (tx) failed\n", __FUNCTION__));
		goto fail;
	}
#endif

	if (!(usbos_info->ctl_urb = USB_ALLOC_URB())) {
		DBUSERR(("%s: usb_alloc_urb (tx) failed\n", __FUNCTION__));
		goto fail;
	}

	init_waitqueue_head(&usbos_info->wait);

	if (!(usbos_info->blk_urb = USB_ALLOC_URB())) {	/* for embedded image downloading */
		DBUSERR(("%s: usb_alloc_urb (tx) failed\n", __FUNCTION__));
		goto fail;
	}

	usbos_info->rxbuf_len = (uint)usbos_info->pub->rxsize;


#ifdef DBUS_LINUX_RXDPC		    /* Initialize DPC thread */
	sema_init(&usbos_info->dpc_sem, 0);
	init_completion(&usbos_info->dpc_exited);
	usbos_info->dpc_pid = kernel_thread(dbus_usbos_dpc_thread, usbos_info, 0);
	if (usbos_info->dpc_pid < 0) {
		DBUSERR(("%s: failed to create dpc thread\n", __FUNCTION__));
		goto fail;
	}
#endif /* DBUS_LINUX_RXDPC */

	if (dbus_usbos_urbreqs_alloc(usbos_info) != DBUS_OK) {
		goto fail;
	}

#ifdef USBOS_THREAD
	if (dbus_usbos_thread_init(usbos_info) == NULL)
		goto fail;
#endif /* USBOS_THREAD */
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	usbos_info->wd_timer_valid = FALSE;
	init_timer(&usbos_info->wdtimer);
	usbos_info->wdtimer.data = (ulong)usbos_info;
	usbos_info->wdtimer.function = dbus_usbos_wdtimer;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	/* Initialize watchdog thread */
	sema_init(&usbos_info->wdsem, 0);
	usbos_info->wdpid = -1;
	init_completion(&usbos_info->wd_exited);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

	return (void *) usbos_info;
fail:
#ifdef DBUS_LINUX_RXDPC
	if (usbos_info->dpc_pid >= 0) {
		KILL_PROC(usbos_info->dpc_pid, SIGTERM);
		wait_for_completion(&usbos_info->dpc_exited);
	}
#endif /* DBUS_LINUX_RXDPC */
	if (usbos_info->intr_urb) {
		USB_FREE_URB(usbos_info->intr_urb);
		usbos_info->intr_urb = NULL;
	}

	if (usbos_info->ctl_urb) {
		USB_FREE_URB(usbos_info->ctl_urb);
		usbos_info->ctl_urb = NULL;
	}

#ifdef BCM_DNGL_EMBEDIMAGE
	if (usbos_info->blk_urb) {
		USB_FREE_URB(usbos_info->blk_urb);
		usbos_info->blk_urb = NULL;
	}
#endif

	dbus_usbos_urbreqs_free(usbos_info);

	g_probe_info.usbos_info = NULL;

	MFREE(pub->osh, usbos_info, sizeof(usbos_info_t));
	return NULL;

}

void
dbus_usbos_intf_detach(dbus_pub_t *pub, void *info)
{
	usbos_info_t *usbos_info = (usbos_info_t *) info;
	osl_t *osh = pub->osh;

	if (usbos_info == NULL) {
		return;
	}

	/* Must unlink all URBs prior to driver unload;
	 * otherwise an URB callback can occur after driver
	 * has been de-allocated and rmmod'd
	 */
	dbusos_stop(usbos_info);

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	dbus_usb_wd_remove(usbos_info);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */

	if (usbos_info->intr_urb) {
		USB_FREE_URB(usbos_info->intr_urb);
		usbos_info->intr_urb = NULL;
	}

	if (usbos_info->ctl_urb) {
		USB_FREE_URB(usbos_info->ctl_urb);
		usbos_info->ctl_urb = NULL;
	}

	if (usbos_info->blk_urb) {
		USB_FREE_URB(usbos_info->blk_urb);
		usbos_info->blk_urb = NULL;
	}

	dbus_usbos_urbreqs_free(usbos_info);

#if defined(DBUS_LINUX_HIST)
	MFREE(osh, usbos_info->txposted_hist, (usbos_info->pub->ntxq+1) * sizeof(int));
	MFREE(osh, usbos_info->rxposted_hist, (usbos_info->pub->nrxq+1) * sizeof(int));
#endif 
#ifdef USBOS_THREAD
	dbus_usbos_thread_deinit(usbos_info);
#endif /* USBOS_THREAD */

	g_probe_info.usbos_info = NULL;
	MFREE(osh, usbos_info, sizeof(usbos_info_t));
}

/*
 *	Kernel need have CONFIG_PM and CONFIG_USB_SUSPEND enabled
 * 	autosuspend also has to be enabled. if not enable by default,
 *	set /sys/bus/usb/devices/.../power/level to auto, where ... is the device'S ID
 *
 *	wl_down ->
 *      bcm_rpc_sleep -> bcm_rpc_tp_sleep -> dbus_pnp_sleep -> dbus_usbos_intf_pnp ->
 *
 *      wl_up ->
 *      bcm_rpc_resume -> bcm_rpc_tp_resume -> dbus_pnp_resume -> dbus_usbos_intf_pnp ->
 */
int
dbus_usbos_intf_pnp(void *bus, int event)
{

#if !((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
	DBUSERR(("PNP: %s not supported", __FUNCTION__));
	return DBUS_OK;
#else
	usbos_info_t *usbos_info = (usbos_info_t *) bus;
	DBUSERR(("PNP: %s event %d\n", __FUNCTION__, event));
	if (usbos_info == NULL)
		return DBUS_ERR;
	if (event == DBUS_PNP_RESUME) {
		DBUSTRACE(("intf pnp RESUME\n"));

		g_probe_info.intf->pm_usage_cnt = 1;
		USB_AUTOPM_SET_INTERFACE(g_probe_info.intf);

		if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPEND_PENDING) {
			/* SUSPEND not happend yet! */
			g_probe_info.suspend_state = USBOS_SUSPEND_STATE_DEVICE_ACTIVE;
		}

		if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPENDED) {
		SPINWAIT((g_probe_info.suspend_state ==
			USBOS_SUSPEND_STATE_DEVICE_ACTIVE), 3000);
		}

		if (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_DEVICE_ACTIVE) {
			DBUSTRACE(("resume USB device OK\n"));
			if (!dbus_usbos_intf_up((void *)g_probe_info.usbos_info))
				return DBUS_OK;
		}
		DBUSERR(("resume failed\n"));
		return DBUS_ERR;
	} else if (event == DBUS_PNP_SLEEP) {
		DBUSTRACE(("PNP SLEEP\n"));
		dbus_usbos_sleep(bus);
	} else if (event == DBUS_PNP_HSIC_SATE) {
		DBUSTRACE(("PNP STATE\n"));
		return (g_probe_info.suspend_state == USBOS_SUSPEND_STATE_SUSPENDED ? 1 : 0);
	} else if (event == DBUS_PNP_HSIC_AUTOSLEEP_ENABLE) {
		DBUSTRACE(("HSIC autosleep Enable\n"));
		dbus_usb_wd_init(usbos_info);
	} else if (event == DBUS_PNP_HSIC_AUTOSLEEP_DISABLE) {
		DBUSTRACE(("HSIC autosleep Disable\n"));
		dbus_usb_wd_remove(usbos_info);
	} else if (event == DBUS_PNP_HSIC_AUTOSLEEP_STATE) {
		DBUSTRACE(("HSIC autosleep State\n"));
		return (usbos_info->wd_timer_valid ? 1 : 0);
	}
	return DBUS_OK;
#endif /* ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
static void
dbus_usbos_sleep(usbos_info_t *usbos_info)
{
	ASSERT(usbos_info);

	g_probe_info.suspend_state = USBOS_SUSPEND_STATE_SUSPEND_PENDING;
	dbus_usbos_state_change(usbos_info, DBUS_STATE_SLEEP);
	DBUSTRACE(("%s: unlink all URBs\n", __FUNCTION__));
	if (usbos_info->intr_urb)
		USB_UNLINK_URB(usbos_info->intr_urb);
	if (usbos_info->ctl_urb)
		USB_UNLINK_URB(usbos_info->ctl_urb);
	if (usbos_info->blk_urb)
		USB_UNLINK_URB(usbos_info->blk_urb);

	dbus_usbos_unlink(&usbos_info->req_txpostedq, &usbos_info->txposted_lock);
	if (usbos_info->txposted > 0) {
		DBUSERR(("%s ERROR: tx REQs posted=%d in stop!\n", __FUNCTION__,
			usbos_info->txposted));
	}
	dbus_usbos_unlink(&usbos_info->req_rxpostedq, &usbos_info->rxposted_lock);
	if (usbos_info->rxposted > 0) {
		DBUSERR(("%s ERROR: rx REQs posted=%d in stop!\n", __FUNCTION__,
			usbos_info->rxposted));
	}

	/* Make sure all the urb are completed, usb_unlink_urb doesn't gaurantee
	* that. Wait for 9000us since max irq interval for EHCI is 8ms.
	*/
	SPINWAIT(usbos_info->txposted != 0 || usbos_info->rxposted != 0, 9000);
	if (!(usbos_info->txposted == 0 && usbos_info->rxposted == 0)) {
		printf("fail to  cancel irbs in 9000us\n");
	}
	g_probe_info.intf->pm_usage_cnt = 0;
	USB_AUTOPM_SET_INTERFACE(g_probe_info.intf);
}
#endif /* ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
#ifdef USBOS_THREAD
void*
dbus_usbos_thread_init(usbos_info_t *usbos_info)
{
	usbos_list_entry_t  *entry;
	unsigned long       flags, ii;

	spin_lock_init(&usbos_info->usbos_list_lock);
	INIT_LIST_HEAD(&usbos_info->usbos_list);
	INIT_LIST_HEAD(&usbos_info->usbos_free_list);
	init_waitqueue_head(&usbos_info->usbos_queue_head);
	atomic_set(&usbos_info->usbos_list_cnt, 0);


	for (ii = 0; ii < (usbos_info->pub->nrxq + usbos_info->pub->ntxq); ii++) {
		entry = MALLOC(usbos_info->pub->osh, sizeof(usbos_list_entry_t));
		if (entry) {
			spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);
			list_add_tail((struct list_head*) entry, &usbos_info->usbos_free_list);
			spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);
		} else {
			DBUSERR(("Failed to create list\n"));
		}
	}

	usbos_info->usbos_kt = kthread_create(dbus_usbos_thread_func,
		usbos_info, "usb-thread");

	if (IS_ERR(usbos_info->usbos_kt)) {
		DBUSERR(("Thread Creation failed\n"));
		return (NULL);
	}

	wake_up_process(usbos_info->usbos_kt);

	return (usbos_info->usbos_kt);
}


void
dbus_usbos_thread_deinit(usbos_info_t *usbos_info)
{
	struct list_head    *cur, *next;
	usbos_list_entry_t  *entry;
	unsigned long       flags;

	if (usbos_info->usbos_kt) {
		kthread_stop(usbos_info->usbos_kt);
	}

	list_for_each_safe(cur, next, &usbos_info->usbos_list)
	{
		entry = list_entry(cur, struct usbos_list_entry, list);
		/* detach this entry from the list and then free the entry */
		spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);
		list_del(cur);
		MFREE(usbos_info->pub->osh, entry, sizeof(usbos_list_entry_t));
		spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);
	}

	list_for_each_safe(cur, next, &usbos_info->usbos_free_list)
	{
		entry = list_entry(cur, struct usbos_list_entry, list);
		/* detach this entry from the list and then free the entry */
		spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);
		list_del(cur);
		MFREE(usbos_info->pub->osh, entry, sizeof(usbos_list_entry_t));
		spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);
	}
}

int
dbus_usbos_thread_func(void *data)
{
	usbos_info_t        *usbos_info = (usbos_info_t *)data;
	usbos_list_entry_t  *entry;
	struct list_head    *cur, *next;
	unsigned long       flags;

	while (1) {
		/* If the list is empty, then go to sleep */
		wait_event_interruptible_timeout
		(usbos_info->usbos_queue_head,
			atomic_read(&usbos_info->usbos_list_cnt) > 0,
			1);

		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);

		/* For each entry on the list, process it.  Remove the entry from
		* the list when done.
		*/
		list_for_each_safe(cur, next, &usbos_info->usbos_list)
		{
			urb_req_t           *req;
			int                 len;
			int                 stat;
			usbos_info_t        *usbos_info;

			entry = list_entry(cur, struct usbos_list_entry, list);
			if (entry == NULL)
				break;

			req = entry->urb_context;
			len = entry->urb_length;
			stat = entry->urb_status;
			usbos_info = req->usbinfo;

			/* detach this entry from the list and attach it to the free list */
			list_del_init(cur);
			spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);

			dbus_usbos_recv_complete_handle(req, len, stat);

			spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);

			list_add_tail(cur, &usbos_info->usbos_free_list);

			atomic_dec(&usbos_info->usbos_list_cnt);
		}

		spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);

	}

	return 0;
}

void
dbus_usbos_dispatch_schedule(CALLBACK_ARGS)
{
	urb_req_t           *req = urb->context;
	usbos_info_t        *usbos_info = req->usbinfo;
	usbos_list_entry_t  *entry;
	unsigned long       flags;
	struct list_head    *cur;

	spin_lock_irqsave(&usbos_info->usbos_list_lock, flags);

	cur   = usbos_info->usbos_free_list.next;
	entry = list_entry(cur, struct usbos_list_entry, list);

	/* detach this entry from the free list and prepare it insert it to use list */
	list_del_init(cur);

	if (entry) {
		entry->urb_context = urb->context;
		entry->urb_length  = urb->actual_length;
		entry->urb_status  = urb->status;

		atomic_inc(&usbos_info->usbos_list_cnt);
		list_add_tail(cur, &usbos_info->usbos_list);
	}
	else {
		DBUSERR(("!!!!!!OUT OF MEMORY!!!!!!!\n"));
	}

	spin_unlock_irqrestore(&usbos_info->usbos_list_lock, flags);

	/* thread */
	wake_up_interruptible(&usbos_info->usbos_queue_head);

}

#endif /* USBOS_THREAD */

#ifdef USB_TRIGGER_DEBUG
static bool
dbus_usbos_ctl_send_debugtrig(usbos_info_t* usbinfo)
{
	bootrom_id_t id;

	if (usbinfo == NULL)
		return FALSE;

	id.chip = 0xDEAD;

	dbus_usbos_dl_cmd(usbinfo, DL_DBGTRIG, &id, sizeof(bootrom_id_t));

	/* ignore the result for now */
	return TRUE;
}
#endif /* USB_TRIGGER_DEBUG */

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND))
static void
dbus_usbos_wdtimer(ulong data)
{
	usbos_info_t* usbos_info = (usbos_info_t *)data;

	if (usbos_info == NULL || usbos_info->pub == NULL)
		return;

	/* On idle timeout clear activity flag and/or turn off clock */
	if ((usbos_info->idletime > 0) &&
		(g_probe_info.suspend_state != USBOS_SUSPEND_STATE_SUSPENDED)) {
		if (++usbos_info->idlecount >= usbos_info->idletime) {
			usbos_info->idlecount = 0;
			if (usbos_info->activity) {
				usbos_info->activity = FALSE;
			} else {
				if (usbos_info->wdpid >= 0) {
					up(&usbos_info->wdsem);
				}
			}
		}
	}

	if (usbos_info->wd_timer_valid) {
		mod_timer(&usbos_info->wdtimer, jiffies + WD_MS*HZ/100);
	}
}

static void
dbus_usb_wd_init(usbos_info_t *usbos_info)
{
	if (usbos_info->wdpid >= 0)
		return;
	usbos_info->wdpid = kernel_thread(dhd_usbos_watchdog_thread, usbos_info, 0);
	if (usbos_info->wdpid < 0) {
		DBUSERR(("%s: failed to create watchdog thread\n", __FUNCTION__));
		return;
	}
	dbus_usb_wdtimer_init(usbos_info);
}

static void
dbus_usb_wdtimer_init(usbos_info_t *usbos_info)
{
	/* Stop timer and restart at new value */
	if (usbos_info->wd_timer_valid == TRUE) {
		del_timer(&usbos_info->wdtimer);
		usbos_info->wd_timer_valid = FALSE;
	}

	usbos_info->wdtimer.expires = jiffies + WD_MS*HZ/1000;
	add_timer(&usbos_info->wdtimer);

	usbos_info->wd_timer_valid = TRUE;
}

static void
dbus_usb_wd_remove(usbos_info_t *usbos_info)
{
	dbus_usb_wdtimer_remove(usbos_info);
	if (usbos_info->wdpid >= 0) {
		KILL_PROC(usbos_info->wdpid, SIGTERM);
		wait_for_completion(&usbos_info->wd_exited);
		usbos_info->wdpid = -1;
	}
}

static void
dbus_usb_wdtimer_remove(usbos_info_t *usbos_info)
{
	/* Stop timer */
	if (usbos_info->wd_timer_valid == TRUE) {
		del_timer(&usbos_info->wdtimer);
		usbos_info->wd_timer_valid = FALSE;
	}
}

static int
dhd_usbos_watchdog_thread(void *data)
{
	usbos_info_t* usbos_info = (usbos_info_t *)data;

	/* This thread doesn't need any user-level access,
	* so get rid of all our resources
	*/

	DAEMONIZE("dbus_usb_watchdog");

	/* Run until signal received */
	while (1) {
		if (down_interruptible (&usbos_info->wdsem) == 0) {
			if (usbos_info->pub->busstate != DBUS_STATE_DOWN) {
				dbus_usbos_intf_pnp(usbos_info, DBUS_PNP_SLEEP);
			}
		} else
			break;
	}
	complete_and_exit(&usbos_info->wd_exited, 0);
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)) && defined(CONFIG_USB_SUSPEND)) */
