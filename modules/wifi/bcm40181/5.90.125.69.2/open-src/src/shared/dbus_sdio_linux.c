/*
 * Dongle BUS interface
 * SDIO Linux Implementation
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: dbus_sdio_linux.c 275693 2011-08-04 19:59:34Z $
 */

#include <linuxver.h>
#include <linux/module.h>
#include <typedefs.h>
#include <osl.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#define CDEV_IOC_IF
#ifdef CDEV_IOC_IF
#include <asm/uaccess.h>
#include <linux/poll.h>
#endif

#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdefs.h>
#include "dbus.h"

typedef struct {
	dbus_pub_t *pub; /* Must be first */

	void *cbarg;
	dbus_intf_callbacks_t *cbs;

	spinlock_t rxlock;      /* Lock for rxq management */
	spinlock_t txlock;      /* Lock for txq management */

	int maxps;

	/* Thread based operation */
	bool threads_only;
	struct semaphore sdsem;
	long watchdog_pid;
	struct semaphore watchdog_sem;
	struct completion watchdog_exited;
	long dpc_pid;
	struct semaphore dpc_sem;
	struct completion dpc_exited;
	long txq_pid;
	struct semaphore txq_sem;
	struct completion txq_exited;

	uint tickcnt;
	struct timer_list timer;
	bool wd_timer_valid;
	struct tasklet_struct tasklet;
	spinlock_t      sdlock;
	spinlock_t      txqlock;
} sdos_info_t;

/* Local function prototypes */
static void dbus_sdos_send_complete(void *arg);
static void dbus_sdos_recv_complete(void *arg, dbus_irb_rx_t *rxirb);
static void dbus_sdos_ctl_complete(sdos_info_t *sdos_info, int type);
static int dbus_sdos_errhandler(void *bus, int err);
static int dbus_sdos_state_change(void *bus, int state);
static void dbus_sdos_disconnect_cb(void);
static void dbus_sdos_probe_dpc(ulong data);
static int dhd_probe_thread(void *data);

/* Functions shared between dbus_sdio.c/dbus_sdio_os.c */
extern int dbus_sdio_txq_sched(void *bus);
extern int dbus_sdio_txq_stop(void *bus);
extern int dbus_sdio_txq_process(void *bus);
extern int probe_dlstart(void);
extern int probe_dlstop(void);
extern int probe_dlwrite(uint8 *buf, int count, bool isvars);
extern int probe_iovar(const char *name, void *params, int plen, void *arg, int len, bool set,
	void **val, int *val_len);

/* This stores SDIO info during Linux probe callback
 * since attach() is not called yet at this point
 */
typedef struct {
	void *sdos_info;

	struct tasklet_struct probe_tasklet;
	long dpc_pid;
	struct semaphore sem;
	struct semaphore dlsem;
	struct completion dpc_exited;
} probe_info_t;

typedef struct {
	struct work_struct work; /* sleepable: Must be at top */
	void *context;
} work_tcb_t;

static work_tcb_t probe_work;
static probe_info_t g_probe_info;
extern bcmsdh_driver_t sdh_driver;

/* FIX: Can this stuff be moved to linuxver.h?
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif /* LINUX_VERSION_CODE  */

#define MOD_PARAM_PATHLEN       2048
extern char firmware_path[MOD_PARAM_PATHLEN];
extern char nvram_path[MOD_PARAM_PATHLEN];
/* load firmware and/or nvram values from the filesystem */
module_param_string(firmware_path, firmware_path, MOD_PARAM_PATHLEN, 0);
module_param_string(nvram_path, nvram_path, MOD_PARAM_PATHLEN, 0);

/* Watchdog frequency */
uint dhd_watchdog_ms = 10;
module_param(dhd_watchdog_ms, uint, 0);

/* Watchdog thread priority, -1 to use kernel timer */
int dhd_watchdog_prio = 97;
module_param(dhd_watchdog_prio, int, 0);

/* DPC thread priority, -1 to use tasklet */
int dhd_dpc_prio = 98;
module_param(dhd_dpc_prio, int, 0);

/* DPC thread priority, -1 to use tasklet */
extern int dhd_dongle_memsize;
module_param(dhd_dongle_memsize, int, 0);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif /* LINUX_VERSION_CODE  */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt())
#endif

#define DHD_IDLETIME_TICKS 1

/* Idle timeout for backplane clock */
int dhd_idletime = DHD_IDLETIME_TICKS;
module_param(dhd_idletime, int, 0);

/* Use polling */
uint dhd_poll = FALSE;
module_param(dhd_poll, uint, 0);

/* Use interrupts */
uint dhd_intr = TRUE;
module_param(dhd_intr, uint, 0);

/* SDIO Drive Strength (in milliamps) */
uint dhd_sdiod_drive_strength = 6;
module_param(dhd_sdiod_drive_strength, uint, 0);

/* Tx/Rx bounds */
extern uint dhd_txbound;
extern uint dhd_rxbound;
module_param(dhd_txbound, uint, 0);
module_param(dhd_rxbound, uint, 0);



#ifdef SDTEST
/* Echo packet generator (pkts/s) */
uint dhd_pktgen = 0;
module_param(dhd_pktgen, uint, 0);

/* Echo packet len (0 => sawtooth, max 2040) */
uint dhd_pktgen_len = 0;
module_param(dhd_pktgen_len, uint, 0);
#endif

/* Same as DHD_GET_VAR/DHD_SET_VAR */
#define	DBUS_GET_VAR	2
#define	DBUS_SET_VAR	3
#define	MAX_BLKSZ	8192
#define DL_BLKSZ	2048
#define TSTVARSZ	200
/*
 * Delay bringing up eth1 until image is downloaded.
 * Use char interface for download:
 *   - mknod /dev/sddl0 c 248 0
 *   - chmod 777 /dev/sddl0
 *   - cat rtecdc.bin nvram.txt > /dev/sddl0
 *
 * Default is to bring up eth1 immediately.
 */
extern uint delay_eth;
module_param(delay_eth, uint, 0);

/*
 * SDIO Linux dbus_intf_t
 */
static void * dbus_sdos_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs);
static void dbus_sdos_detach(dbus_pub_t *pub, void *info);
static int dbus_sdos_send_irb(void *bus, dbus_irb_tx_t *txirb);
static int dbus_sdos_recv_irb(void *bus, dbus_irb_rx_t *rxirb);
static int dbus_sdos_cancel_irb(void *bus, dbus_irb_tx_t *txirb);
static int dbus_sdos_send_ctl(void *bus, uint8 *buf, int len);
static int dbus_sdos_recv_ctl(void *bus, uint8 *buf, int len);
static int dbus_sdos_get_attrib(void *bus, dbus_attrib_t *attrib);
static int dbus_sdos_up(void *bus);
static int dbus_sdos_down(void *bus);
static int dbus_sdos_stop(void *bus);
static bool dbus_sdos_device_exists(void *bus);
static bool dbus_sdos_dlneeded(void *bus);
static int dbus_sdos_dlstart(void *bus, uint8 *fw, int len);
static int dbus_sdos_dlrun(void *bus);
static bool dbus_sdos_recv_needed(void *bus);
static void *dbus_sdos_exec_rxlock(void *bus, exec_cb_t cb, struct exec_parms *args);
static void *dbus_sdos_exec_txlock(void *bus, exec_cb_t cb, struct exec_parms *args);
static int dbus_sdos_sched_dpc(void *bus);
static int dbus_sdos_lock(void *bus);
static int dbus_sdos_unlock(void *bus);
static int dbus_sdos_sched_probe_cb(void *bus);

static dbus_intf_t dbus_sdos_intf = {
	dbus_sdos_attach,
	dbus_sdos_detach,
	dbus_sdos_up,
	dbus_sdos_down,
	dbus_sdos_send_irb,
	dbus_sdos_recv_irb,
	dbus_sdos_cancel_irb,
	dbus_sdos_send_ctl,
	dbus_sdos_recv_ctl,
	NULL, /* get_stats */
	dbus_sdos_get_attrib,
	NULL, /* pnp */
	NULL, /* remove */
	NULL, /* resume */
	NULL, /* suspend */
	dbus_sdos_stop,
	NULL, /* reset */
	NULL, /* pktget */
	NULL, /* pktfree */
	NULL, /* iovar_op */
	NULL, /* dump */
	NULL, /* set_config */
	NULL, /* get_config */
	dbus_sdos_device_exists,
	dbus_sdos_dlneeded,
	dbus_sdos_dlstart,
	dbus_sdos_dlrun,
	dbus_sdos_recv_needed,
	dbus_sdos_exec_rxlock,
	dbus_sdos_exec_txlock,
	NULL, /* set_revinfo */
	NULL, /* get_revinfo */
	NULL, /* tx_timer_init */
	NULL, /* tx_timer_start */
	NULL, /* tx_timer_stop */
	dbus_sdos_sched_dpc,
	dbus_sdos_lock,
	dbus_sdos_unlock,
	dbus_sdos_sched_probe_cb

	/* shutdown */

	/* recv_stop */
	/* recv_resume */
};

static probe_cb_t probe_cb = NULL;
static disconnect_cb_t disconnect_cb = NULL;
static void *probe_arg = NULL;
static void *disc_arg = NULL;

static void
dbus_sdos_disconnect_cb()
{
	if (disconnect_cb)
		disconnect_cb(disc_arg);
}

static void
dbus_sdos_send_complete(void *arg)
{
	sdos_info_t *sdos_info = arg;
	dbus_irb_tx_t *txirb = NULL;
	int status = DBUS_OK;

	if (sdos_info->cbarg && sdos_info->cbs) {
		if (sdos_info->cbs->send_irb_complete)
			sdos_info->cbs->send_irb_complete(sdos_info->cbarg, txirb, status);
	}
}

static void
dbus_sdos_recv_complete(void *arg, dbus_irb_rx_t *rxirb)
{
	int status = DBUS_OK;
	sdos_info_t *sdos_info = arg;

	if (sdos_info->cbarg && sdos_info->cbs) {
		if (sdos_info->cbs->recv_irb_complete)
			sdos_info->cbs->recv_irb_complete(sdos_info->cbarg, rxirb, status);
	}
}

static void
dbus_sdos_ctl_complete(sdos_info_t *sdos_info, int type)
{
	int status = DBUS_ERR;

	if (sdos_info == NULL)
		return;

	if (sdos_info->cbarg && sdos_info->cbs) {
		if (sdos_info->cbs->ctl_complete)
			sdos_info->cbs->ctl_complete(sdos_info->cbarg, type, status);
	}
}

static void
dbusos_stop(sdos_info_t *sdos_info)
{
	dbus_sdos_state_change(sdos_info, DBUS_STATE_DOWN);

	/* Clear the watchdog timer */
	del_timer(&sdos_info->timer);
	sdos_info->wd_timer_valid = FALSE;
}

static bool
dbus_sdos_device_exists(void *bus)
{
	return TRUE;
}

static bool
dbus_sdos_dlneeded(void *bus)
{
	return FALSE;
}

static int
dbus_sdos_dlstart(void *bus, uint8 *fw, int len)
{
	return DBUS_ERR;
}

static int
dbus_sdos_dlrun(void *bus)
{
	return DBUS_ERR;
}

static bool
dbus_sdos_recv_needed(void *bus)
{
	return FALSE;
}

static void*
dbus_sdos_exec_rxlock(void *bus, exec_cb_t cb, struct exec_parms *args)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;
	void *ret;
	unsigned long flags;

	if (sdos_info == NULL)
		return NULL;

	spin_lock_irqsave(&sdos_info->rxlock, flags);
	ret = cb(args);
	spin_unlock_irqrestore(&sdos_info->rxlock, flags);

	return ret;
}

static void*
dbus_sdos_exec_txlock(void *bus, exec_cb_t cb, struct exec_parms *args)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;
	void *ret;
	unsigned long flags;

	if (sdos_info == NULL)
		return NULL;

	spin_lock_irqsave(&sdos_info->txlock, flags);
	ret = cb(args);
	spin_unlock_irqrestore(&sdos_info->txlock, flags);

	return ret;
}

static int
dbus_sdos_sched_dpc(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->dpc_pid >= 0) {
		up(&sdos_info->dpc_sem);
		return DBUS_OK;
	}

	tasklet_schedule(&sdos_info->tasklet);
	return DBUS_OK;
}

static int
dbus_sdos_lock(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->threads_only)
		down(&sdos_info->sdsem);
	else
		spin_lock_bh(&sdos_info->sdlock);

	return DBUS_OK;
}

static int
dbus_sdos_unlock(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->threads_only)
		up(&sdos_info->sdsem);
	else
		spin_unlock_bh(&sdos_info->sdlock);

	return DBUS_OK;
}

static int
dbus_sdos_sched_probe_cb(void *bus)
{
	if (delay_eth != 0) {
		if (g_probe_info.dpc_pid >= 0)
			up(&g_probe_info.sem);
	}
	return DBUS_OK;
}

static void
dbus_wd_timer_init(sdos_info_t *sdos_info, uint wdtick)
{
	/* Stop timer and restart at new value */
	if (sdos_info->wd_timer_valid == TRUE) {
		del_timer(&sdos_info->timer);
		sdos_info->wd_timer_valid = FALSE;
	}

	dhd_watchdog_ms = (uint)wdtick;
	sdos_info->timer.expires = jiffies + dhd_watchdog_ms*HZ/1000;
	add_timer(&sdos_info->timer);

	sdos_info->wd_timer_valid = TRUE;
}

static int
dhd_watchdog_thread(void *data)
{
	sdos_info_t *sdos_info = (sdos_info_t *)data;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */

	DAEMONIZE("dbus_sdio_watchdog");

	/* Run until signal received */
	while (1) {
		if (down_interruptible (&sdos_info->watchdog_sem) == 0) {
			if (sdos_info->pub->busstate != DBUS_STATE_DOWN) {
				if (sdos_info->cbarg && sdos_info->cbs) {
					if (sdos_info->cbs->watchdog)
						sdos_info->cbs->watchdog(sdos_info->cbarg);
				}
			}

			/* Count the tick for reference */
			sdos_info->tickcnt++;

			/* Reschedule the watchdog */
			if (sdos_info->wd_timer_valid) {
				mod_timer(&sdos_info->timer, jiffies + dhd_watchdog_ms*HZ/1000);
			}
		} else
			break;
	}

	complete_and_exit(&sdos_info->watchdog_exited, 0);
}

static void
dbus_wd_timer(ulong data)
{
	sdos_info_t *sdos_info = (sdos_info_t *)data;

	if (sdos_info->watchdog_pid >= 0) {
		up(&sdos_info->watchdog_sem);
	}
}

static int
dbus_txq_thread(void *data)
{
	sdos_info_t *sdos_info = (sdos_info_t *)data;

	DAEMONIZE("dbus_sdio_txq");

	/* Run until signal received */
	while (1) {
		if (down_interruptible(&sdos_info->txq_sem) == 0)
			dbus_sdio_txq_process(sdos_info->cbarg);
		else
			break;
	}

	complete_and_exit(&sdos_info->txq_exited, 0);
}

static int
dhd_probe_thread(void *data)
{
	probe_info_t *pinfo = (probe_info_t *) data;

	DAEMONIZE("dbus_probe_thread");

	if (probe_cb) {
		if (down_interruptible(&pinfo->sem) == 0)
			disc_arg = probe_cb(probe_arg, "", 0, 0);
	}

	pinfo->dpc_pid = -1;
	complete_and_exit(&pinfo->dpc_exited, 0);
}

static int
dhd_dpc_thread(void *data)
{
	sdos_info_t *sdos_info = (sdos_info_t *)data;

	/* This thread doesn't need any user-level access,
	 * so get rid of all our resources
	 */

	DAEMONIZE("dbus_sdio_dpc");

	/* Run until signal received */
	while (1) {
		if (down_interruptible(&sdos_info->dpc_sem) == 0) {
			/* Call bus dpc unless it indicated down (then clean stop) */
			if (sdos_info->pub->busstate != DBUS_STATE_DOWN) {
				if (sdos_info->cbarg && sdos_info->cbs) {
					if (sdos_info->cbs->dpc)
						if (sdos_info->cbs->dpc(sdos_info->cbarg, FALSE)) {
							up(&sdos_info->dpc_sem);
						}
				}
			}
		} else
			break;
	}

	complete_and_exit(&sdos_info->dpc_exited, 0);
}

/* FIX: Need to tight this into dbus_sdio.c */
static void *
dbus_sdos_open_image(char * filename)
{
	struct file *fp;

	fp = filp_open(filename, O_RDONLY, 0);
	/*
	 * 2.6.11 (FC4) supports filp_open() but later revs don't?
	 * Alternative:
	 * fp = open_namei(AT_FDCWD, filename, O_RD, 0);
	 * ???
	 */
	 if (IS_ERR(fp))
		 fp = NULL;

	 return fp;
}

/* FIX: Need to tight this into dbus_sdio.c */
static int
dbus_sdos_get_image_block(char * buf, int len, void * image)
{
	struct file *fp = (struct file *) image;
	int rdlen;

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

/* FIX: Need to tight this into dbus_sdio.c */
static void
dbus_sdos_close_image(void * image)
{
	if (image)
		filp_close((struct file *) image, NULL);
}

static void
dbus_sdos_probe_dpc(ulong data)
{
	probe_info_t *pinfo;

	pinfo = (probe_info_t *) data;
	if (probe_cb) {
		disc_arg = probe_cb(probe_arg, "", 0, 0);
	}
}

static int
dbus_sdos_send_ctl(void *bus, uint8 *buf, int len)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if ((sdos_info == NULL) || (buf == NULL) || (len == 0))
		return DBUS_ERR;

	return DBUS_OK;
}

static int
dbus_sdos_recv_ctl(void *bus, uint8 *buf, int len)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if ((sdos_info == NULL) || (buf == NULL) || (len == 0))
		return DBUS_ERR;

	return DBUS_OK;
}

static int
dbus_sdos_get_attrib(void *bus, dbus_attrib_t *attrib)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if ((sdos_info == NULL) || (attrib == NULL))
		return DBUS_ERR;

	attrib->bustype = DBUS_SDIO;
	attrib->vid = 0;
	attrib->pid = 0;
	attrib->devid = 0x4322;

	/* FIX: Need nchan for both TX and RX?;
	 * BDC uses one RX pipe and one TX pipe
	 * RPC may use two RX pipes and one TX pipe?
	 */
	attrib->nchan = 1;
	attrib->mtu = 0;

	return DBUS_OK;
}

static int
dbus_sdos_up(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	dbus_wd_timer_init(sdos_info, dhd_watchdog_ms);
	return DBUS_OK;
}

static int
dbus_sdos_down(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	dbus_sdos_state_change(sdos_info, DBUS_STATE_DOWN);
	return DBUS_OK;
}

static int
dbus_sdos_send_irb(void *bus, dbus_irb_tx_t *txirb)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;
	int ret = DBUS_ERR;

	if (sdos_info == NULL)
		return DBUS_ERR;

	return ret;
}

static int
dbus_sdos_recv_irb(void *bus, dbus_irb_rx_t *rxirb)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;
	int ret = DBUS_ERR;

	if (sdos_info == NULL)
		return DBUS_ERR;

	return ret;
}

static int
dbus_sdos_cancel_irb(void *bus, dbus_irb_tx_t *txirb)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	/* FIX: Need to implement */
	return DBUS_ERR;
}

static int
dbus_sdos_stop(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	dbusos_stop(sdos_info);
	return DBUS_OK;
}

int
dbus_sdos_errhandler(void *bus, int err)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->cbarg && sdos_info->cbs) {
		if (sdos_info->cbs->errhandler)
			sdos_info->cbs->errhandler(sdos_info->cbarg, err);
	}

	return DBUS_OK;
}

int
dbus_sdos_state_change(void *bus, int state)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->cbarg && sdos_info->cbs) {
		if (sdos_info->cbs->state_change)
			sdos_info->cbs->state_change(sdos_info->cbarg, state);
	}

	return DBUS_OK;
}

#ifdef CDEV_IOC_IF
#define CHARDEV_MAJOR                              248
#define CHARDEV_NAME                               "hnd"
#define CHARDEV_SUFFIX                             " dev"

/*
 * Linux SD downloader interface prior to attach()
 * Once download succeeds, probe callback is initiated so attach()
 * can take place.
 */
static int dbus_dldr_open(struct inode *inode, struct file *file);
static int dbus_dldr_close(struct inode * inode, struct file * file);
static unsigned int dbus_dldr_poll(struct file *, poll_table *);
static ssize_t dbus_dldr_read(struct file *filp,
	char *buf, size_t count, loff_t *off);
static ssize_t dbus_dldr_write(struct file *filp,
	const char *buf, size_t count, loff_t *off);
static int dbus_dldr_ioctl(struct inode *inode, struct file * file,
	unsigned int cmd, unsigned long arg);
static int dbus_dldr_mmap(struct file * file, struct vm_area_struct * vma);

static int
dbus_dldr_open(struct inode *inode, struct file *filp)
{
	return probe_dlstart();
}

static int
dbus_dldr_close(struct inode *inode, struct file *filp)
{
	return probe_dlstop();
}

static unsigned int
dbus_dldr_poll(struct file *filp, poll_table *wait)
{
	return (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);
}

static ssize_t
dbus_dldr_read(struct file *filp, char *buf, size_t count, loff_t *off)
{
	return 0;
}

static uint8 g_wrblk[MAX_BLKSZ];
static uint8 g_vars[MAX_BLKSZ];

static bool
is_vars(char *buf, int count)
{
	int n;
	char c;
	bool b = TRUE;

	n = count <= TSTVARSZ ? count : TSTVARSZ;
	while (n--) {
		c = buf[n];
		if (!((c >= 0x20 && c <= 0x7E) || (c == 0xA) || (c == 0xD))) {
			b = FALSE;
			break;
		}
	}

	return b;
}

/*
 * Ported from dhdu app
 */
static int
parse_vars(char *inbuf, int incnt, char *obuf, int ocnt)
{
	int buf_len, slen;
	char *line, *s, *e, *f;
	char *buf;
	int buf_maxlen = ocnt;

	if (inbuf == NULL)
		return -1;

	buf_len = 0;
	line = inbuf;
	buf = obuf;

	while ((line - inbuf) < incnt) {
		bool found_eq = FALSE;

		/* Skip any initial white space */
		for (s = line; *s == ' ' || *s == '\t'; s++)
			;
		/* Determine end of string */
		for (e = s; *e != 0 && *e != '#' && *e != '\r' && *e != '\n'; e++)
			if (*e == '=')
				found_eq = TRUE;

		for (f = e; *f != '\n'; f++)
			;

		if (*f == '\n') {
			f++;
			line = f;
		} else {
			printf("Invalid vars file: unexpected eof.\n");
			return -1;
		}

		/* Strip any white space from end of string */
		while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
			e--;

		slen = e - s;

		/* Skip lines that end up blank */
		if (slen == 0)
			continue;

		if (!found_eq) {
			printf("Invalid line in NVRAM file \n");
			return -1;
		}

		if (buf_len + slen + 1 > buf_maxlen) {
			printf("NVRAM file too long\n");
			return -1;
		}

		memcpy(buf + buf_len, s, slen);
		buf_len += slen;
		buf[buf_len++] = 0;
	}

	return buf_len;
}

static ssize_t
dbus_dldr_write(struct file *filp, const char *buf, size_t count, loff_t *off)
{
	int n, k;
	bool isvars;
	char *bp = (char *) buf;

	down(&g_probe_info.dlsem);

	n = count >= DL_BLKSZ ? DL_BLKSZ : count;
	if (copy_from_user(g_wrblk, bp, n)) {
		n = -EFAULT;
		goto exit;
	}

	isvars = is_vars(g_wrblk, n);

	if (isvars == TRUE) {
		k = parse_vars(bp, n, g_vars, sizeof(g_vars));
		probe_dlwrite((uint8 *)g_vars, k, TRUE);
		goto exit;
	}

	n = 0;
	bp = (char *) buf;
	while (count > 0) {
		k = count >= DL_BLKSZ ? DL_BLKSZ : count;
		if (copy_from_user(g_wrblk, bp, k)) {
			n = -EFAULT;
			break;
		}

		n += k;
		bp += k;
		count -= k;
		probe_dlwrite((uint8 *)g_wrblk, k, isvars);
	}

exit:
	up(&g_probe_info.dlsem);
	return n;
}

static int
dbus_dldr_ioctl(struct inode *inode, struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	int access_ok = 1, err = 0;
	unsigned int size;
	void *val = NULL;
	uint8 *buf = (uint8 *) arg;
	void *parms;
	char *name;
	int len = 0;

	size = _IOC_SIZE(cmd);
	if (_IOC_DIR(cmd) & _IOC_READ)
		access_ok = access_ok(VERIFY_WRITE, (void*) arg, size);
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		access_ok = access_ok(VERIFY_READ, (void*) arg, size);

	if (!access_ok)
		return -EFAULT;

	down(&g_probe_info.dlsem);

	switch (cmd) {
	case DBUS_GET_VAR:
		name = (char *)arg;
		parms = buf + strlen(buf) + 1;
		err = probe_iovar((const char *)name,
			parms, 0, (void *) arg, 0 /* len */, FALSE, &val, &len);

		if (val != NULL) {
			if (copy_to_user((void *)arg, val, len))
				err = -EFAULT;
		}
	break;

	case DBUS_SET_VAR:
		name = (char *)arg;
		parms = buf + strlen(buf) + 1;
		err = probe_iovar((const char *)name,
			parms, 0, (void *) arg, 0 /* len */, TRUE, NULL, NULL);
	break;

	default:
		DBUSERR(("Unhandled char ioctl: %d\n", cmd));
		err = -EINVAL;
	break;
	}

	if (err)
		err = -EFAULT;

	up(&g_probe_info.dlsem);

	return err;
}

static int
dbus_dldr_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return 0;
}

static struct file_operations dbus_dldr_fops = {
	owner:      THIS_MODULE,
	poll:       dbus_dldr_poll,
	read:       dbus_dldr_read,
	write:      dbus_dldr_write,
	ioctl:      dbus_dldr_ioctl,
	mmap:       dbus_dldr_mmap,
	open:       dbus_dldr_open,
	release:    dbus_dldr_close
};
#endif /* CDEV_IOC_IF */

int
dbus_bus_osl_register(int vid, int pid, probe_cb_t prcb,
	disconnect_cb_t discb, void *prarg, dbus_intf_t **intf, void *param1, void *param2)
{
	probe_info_t *pinfo = &g_probe_info;
	int err;

	probe_cb = prcb;
	disconnect_cb = discb;
	probe_arg = prarg;

	*intf = &dbus_sdos_intf;

	bzero(pinfo, sizeof(probe_info_t));
#ifdef CDEV_IOC_IF
	/* To support async probe callback when an image is downloadeda,
	 * we need access to the driver before net interface is brought up.
	 * Under Linux, we can create a download channel using char dev node interface.
	 */
	if (register_chrdev(CHARDEV_MAJOR, CHARDEV_NAME CHARDEV_SUFFIX, &dbus_dldr_fops))
		DBUSERR(("register_chrdev failed\n"));
	else
		init_MUTEX(&g_probe_info.dlsem);
#endif

	err = bcmsdh_register(&sdh_driver);
	if (err == 0)
		err = DBUS_OK;
	else
		err = DBUS_ERR;

	if (delay_eth != 0) {
		sema_init(&g_probe_info.sem, 0);
		init_completion(&g_probe_info.dpc_exited);
		g_probe_info.dpc_pid = kernel_thread(dhd_probe_thread, &g_probe_info, 0);
	} else {
		g_probe_info.dpc_pid = -1;
	}

	return err;
}

int
dbus_bus_osl_deregister()
{
	probe_info_t *pinfo;

	pinfo = &g_probe_info;
	flush_scheduled_work();
#ifdef CDEV_IOC_IF
	unregister_chrdev(CHARDEV_MAJOR, CHARDEV_NAME CHARDEV_SUFFIX);
#endif

	bcmsdh_unregister();
	return DBUS_OK;
}

static void *
dbus_sdos_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs)
{
	sdos_info_t *sdos_info;

	sdos_info = MALLOC(pub->osh, sizeof(sdos_info_t));
	if (sdos_info == NULL)
		return NULL;

	/* Sanity check for BUS_INFO() */
	ASSERT(OFFSETOF(sdos_info_t, pub) == 0);

	bzero(sdos_info, sizeof(sdos_info_t));

	sdos_info->pub = pub;
	sdos_info->cbarg = cbarg;
	sdos_info->cbs = cbs;

	spin_lock_init(&sdos_info->sdlock);
	spin_lock_init(&sdos_info->txqlock);
	spin_lock_init(&sdos_info->rxlock);
	spin_lock_init(&sdos_info->txlock);

	/* Set up the watchdog timer */
	init_timer(&sdos_info->timer);
	sdos_info->timer.data = (ulong)sdos_info;
	sdos_info->timer.function = dbus_wd_timer;

	/* Set up txq thread */
	sema_init(&sdos_info->txq_sem, 0);
	init_completion(&sdos_info->txq_exited);
	sdos_info->txq_pid = kernel_thread(dbus_txq_thread, sdos_info, 0);

	/* Initialize thread based operation and lock */
	init_MUTEX(&sdos_info->sdsem);
	if ((dhd_watchdog_prio >= 0) && (dhd_dpc_prio >= 0)) {
		sdos_info->threads_only = TRUE;
	} else {
		sdos_info->threads_only = FALSE;
	}

	if (dhd_dpc_prio >= 0) {
		/* Initialize watchdog thread */
		sema_init(&sdos_info->watchdog_sem, 0);
		init_completion(&sdos_info->watchdog_exited);
		sdos_info->watchdog_pid = kernel_thread(dhd_watchdog_thread, sdos_info, 0);
	} else {
		sdos_info->watchdog_pid = -1;
	}

	/* Set up the bottom half handler */
	if (dhd_dpc_prio >= 0) {
		/* Initialize DPC thread */
		sema_init(&sdos_info->dpc_sem, 0);
		init_completion(&sdos_info->dpc_exited);
		sdos_info->dpc_pid = kernel_thread(dhd_dpc_thread, sdos_info, 0);
	}

	/* Needed for disconnect() */
	g_probe_info.sdos_info = sdos_info;

	return (void *) sdos_info;
}

static void
dbus_sdos_detach(dbus_pub_t *pub, void *info)
{
	sdos_info_t *sdos_info = (sdos_info_t *) info;
	osl_t *osh = pub->osh;

	if (sdos_info == NULL) {
		return;
	}

	dbusos_stop(sdos_info);

	if (sdos_info->watchdog_pid >= 0) {
		KILL_PROC(sdos_info->watchdog_pid, SIGTERM);
		wait_for_completion(&sdos_info->watchdog_exited);
	}

	if (sdos_info->txq_pid >= 0) {
		KILL_PROC(sdos_info->txq_pid, SIGTERM);
		wait_for_completion(&sdos_info->txq_exited);
	}

	if (sdos_info->dpc_pid >= 0) {
		KILL_PROC(sdos_info->dpc_pid, SIGTERM);
		wait_for_completion(&sdos_info->dpc_exited);
	} else
		tasklet_kill(&sdos_info->tasklet);

	if (g_probe_info.dpc_pid >= 0) {
		KILL_PROC(g_probe_info.dpc_pid, SIGTERM);
		wait_for_completion(&g_probe_info.dpc_exited);
	}

	g_probe_info.sdos_info = NULL;
	MFREE(osh, sdos_info, sizeof(sdos_info_t));
}

int
dbus_sdio_txq_sched(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->txq_pid >= 0)
		up(&sdos_info->txq_sem);
	else
		ASSERT(0);

	return DBUS_OK;
}

int
dbus_sdio_txq_stop(void *bus)
{
	sdos_info_t *sdos_info = (sdos_info_t *) bus;

	if (sdos_info == NULL)
		return DBUS_ERR;

	if (sdos_info->txq_pid >= 0) {
		KILL_PROC(sdos_info->txq_pid, SIGTERM);
		wait_for_completion(&sdos_info->txq_exited);
		sdos_info->txq_pid = -1;
	}
	return DBUS_OK;
}

/* FIX: */
extern void test(void);
void test(void)
{
	dbus_sdos_send_complete(NULL);
	dbus_sdos_recv_complete(NULL, NULL);
	dbus_sdos_ctl_complete(NULL, 0);
	dbus_sdos_errhandler(NULL, 0);
	dbus_sdos_state_change(NULL, 0);
	dbus_sdos_disconnect_cb();
	dbus_sdos_close_image(NULL);
	dbus_sdos_get_image_block(NULL, 0, NULL);
	dbus_sdos_open_image(NULL);
	dbus_sdos_probe_dpc(0);
	bzero(&probe_work, sizeof(probe_work));
}
