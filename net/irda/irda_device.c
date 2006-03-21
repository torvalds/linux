/*********************************************************************
 *
 * Filename:      irda_device.c
 * Version:       0.9
 * Description:   Utility functions used by the device drivers
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Oct  9 09:22:27 1999
 * Modified at:   Sun Jan 23 17:41:24 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#include <linux/config.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/capability.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/spinlock.h>

#include <asm/ioctls.h>
#include <asm/uaccess.h>
#include <asm/dma.h>
#include <asm/io.h>

#include <net/irda/irda_device.h>
#include <net/irda/irlap.h>
#include <net/irda/timer.h>
#include <net/irda/wrapper.h>

static void __irda_task_delete(struct irda_task *task);

static hashbin_t *dongles = NULL;
static hashbin_t *tasks = NULL;

#ifdef CONFIG_IRDA_DEBUG
static const char *task_state[] = {
	"IRDA_TASK_INIT",
	"IRDA_TASK_DONE",
	"IRDA_TASK_WAIT",
	"IRDA_TASK_WAIT1",
	"IRDA_TASK_WAIT2",
	"IRDA_TASK_WAIT3",
	"IRDA_TASK_CHILD_INIT",
	"IRDA_TASK_CHILD_WAIT",
	"IRDA_TASK_CHILD_DONE",
};
#endif	/* CONFIG_IRDA_DEBUG */

static void irda_task_timer_expired(void *data);

int __init irda_device_init( void)
{
	dongles = hashbin_new(HB_NOLOCK);
	if (dongles == NULL) {
		IRDA_WARNING("IrDA: Can't allocate dongles hashbin!\n");
		return -ENOMEM;
	}
	spin_lock_init(&dongles->hb_spinlock);

	tasks = hashbin_new(HB_LOCK);
	if (tasks == NULL) {
		IRDA_WARNING("IrDA: Can't allocate tasks hashbin!\n");
		hashbin_delete(dongles, NULL);
		return -ENOMEM;
	}

	/* We no longer initialise the driver ourselves here, we let
	 * the system do it for us... - Jean II */

	return 0;
}

static void __exit leftover_dongle(void *arg)
{
	struct dongle_reg *reg = arg;
	IRDA_WARNING("IrDA: Dongle type %x not unregistered\n",
		     reg->type);
}

void __exit irda_device_cleanup(void)
{
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	hashbin_delete(tasks, (FREE_FUNC) __irda_task_delete);

	hashbin_delete(dongles, leftover_dongle);
}

/*
 * Function irda_device_set_media_busy (self, status)
 *
 *    Called when we have detected that another station is transmitting
 *    in contention mode.
 */
void irda_device_set_media_busy(struct net_device *dev, int status)
{
	struct irlap_cb *self;

	IRDA_DEBUG(4, "%s(%s)\n", __FUNCTION__, status ? "TRUE" : "FALSE");

	self = (struct irlap_cb *) dev->atalk_ptr;

	/* Some drivers may enable the receive interrupt before calling
	 * irlap_open(), or they may disable the receive interrupt
	 * after calling irlap_close().
	 * The IrDA stack is protected from this in irlap_driver_rcv().
	 * However, the driver calls directly the wrapper, that calls
	 * us directly. Make sure we protect ourselves.
	 * Jean II */
	if (!self || self->magic != LAP_MAGIC)
		return;

	if (status) {
		self->media_busy = TRUE;
		if (status == SMALL)
			irlap_start_mbusy_timer(self, SMALLBUSY_TIMEOUT);
		else
			irlap_start_mbusy_timer(self, MEDIABUSY_TIMEOUT);
		IRDA_DEBUG( 4, "Media busy!\n");
	} else {
		self->media_busy = FALSE;
		irlap_stop_mbusy_timer(self);
	}
}
EXPORT_SYMBOL(irda_device_set_media_busy);


/*
 * Function irda_device_is_receiving (dev)
 *
 *    Check if the device driver is currently receiving data
 *
 */
int irda_device_is_receiving(struct net_device *dev)
{
	struct if_irda_req req;
	int ret;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	if (!dev->do_ioctl) {
		IRDA_ERROR("%s: do_ioctl not impl. by device driver\n",
			   __FUNCTION__);
		return -1;
	}

	ret = dev->do_ioctl(dev, (struct ifreq *) &req, SIOCGRECEIVING);
	if (ret < 0)
		return ret;

	return req.ifr_receiving;
}

void irda_task_next_state(struct irda_task *task, IRDA_TASK_STATE state)
{
	IRDA_DEBUG(2, "%s(), state = %s\n", __FUNCTION__, task_state[state]);

	task->state = state;
}
EXPORT_SYMBOL(irda_task_next_state);

static void __irda_task_delete(struct irda_task *task)
{
	del_timer(&task->timer);

	kfree(task);
}

void irda_task_delete(struct irda_task *task)
{
	/* Unregister task */
	hashbin_remove(tasks, (long) task, NULL);

	__irda_task_delete(task);
}
EXPORT_SYMBOL(irda_task_delete);

/*
 * Function irda_task_kick (task)
 *
 *    Tries to execute a task possible multiple times until the task is either
 *    finished, or askes for a timeout. When a task is finished, we do post
 *    processing, and notify the parent task, that is waiting for this task
 *    to complete.
 */
static int irda_task_kick(struct irda_task *task)
{
	int finished = TRUE;
	int count = 0;
	int timeout;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(task != NULL, return -1;);
	IRDA_ASSERT(task->magic == IRDA_TASK_MAGIC, return -1;);

	/* Execute task until it's finished, or askes for a timeout */
	do {
		timeout = task->function(task);
		if (count++ > 100) {
			IRDA_ERROR("%s: error in task handler!\n",
				   __FUNCTION__);
			irda_task_delete(task);
			return TRUE;
		}
	} while ((timeout == 0) && (task->state != IRDA_TASK_DONE));

	if (timeout < 0) {
		IRDA_ERROR("%s: Error executing task!\n", __FUNCTION__);
		irda_task_delete(task);
		return TRUE;
	}

	/* Check if we are finished */
	if (task->state == IRDA_TASK_DONE) {
		del_timer(&task->timer);

		/* Do post processing */
		if (task->finished)
			task->finished(task);

		/* Notify parent */
		if (task->parent) {
			/* Check if parent is waiting for us to complete */
			if (task->parent->state == IRDA_TASK_CHILD_WAIT) {
				task->parent->state = IRDA_TASK_CHILD_DONE;

				/* Stop timer now that we are here */
				del_timer(&task->parent->timer);

				/* Kick parent task */
				irda_task_kick(task->parent);
			}
		}
		irda_task_delete(task);
	} else if (timeout > 0) {
		irda_start_timer(&task->timer, timeout, (void *) task,
				 irda_task_timer_expired);
		finished = FALSE;
	} else {
		IRDA_DEBUG(0, "%s(), not finished, and no timeout!\n",
			   __FUNCTION__);
		finished = FALSE;
	}

	return finished;
}

/*
 * Function irda_task_execute (instance, function, finished)
 *
 *    This function registers and tries to execute tasks that may take some
 *    time to complete. We do it this hairy way since we may have been
 *    called from interrupt context, so it's not possible to use
 *    schedule_timeout()
 * Two important notes :
 *	o Make sure you irda_task_delete(task); in case you delete the
 *	  calling instance.
 *	o No real need to lock when calling this function, but you may
 *	  want to lock within the task handler.
 * Jean II
 */
struct irda_task *irda_task_execute(void *instance,
				    IRDA_TASK_CALLBACK function,
				    IRDA_TASK_CALLBACK finished,
				    struct irda_task *parent, void *param)
{
	struct irda_task *task;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	task = kmalloc(sizeof(struct irda_task), GFP_ATOMIC);
	if (!task)
		return NULL;

	task->state    = IRDA_TASK_INIT;
	task->instance = instance;
	task->function = function;
	task->finished = finished;
	task->parent   = parent;
	task->param    = param;
	task->magic    = IRDA_TASK_MAGIC;

	init_timer(&task->timer);

	/* Register task */
	hashbin_insert(tasks, (irda_queue_t *) task, (long) task, NULL);

	/* No time to waste, so lets get going! */
	return irda_task_kick(task) ? NULL : task;
}
EXPORT_SYMBOL(irda_task_execute);

/*
 * Function irda_task_timer_expired (data)
 *
 *    Task time has expired. We now try to execute task (again), and restart
 *    the timer if the task has not finished yet
 */
static void irda_task_timer_expired(void *data)
{
	struct irda_task *task;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	task = (struct irda_task *) data;

	irda_task_kick(task);
}

/*
 * Function irda_device_setup (dev)
 *
 *    This function should be used by low level device drivers in a similar way
 *    as ether_setup() is used by normal network device drivers
 */
static void irda_device_setup(struct net_device *dev)
{
        dev->hard_header_len = 0;
        dev->addr_len        = LAP_ALEN;

        dev->type            = ARPHRD_IRDA;
        dev->tx_queue_len    = 8; /* Window size + 1 s-frame */

	memset(dev->broadcast, 0xff, LAP_ALEN);

	dev->mtu = 2048;
	dev->flags = IFF_NOARP;
}

/*
 * Funciton  alloc_irdadev 
 * 	Allocates and sets up an IRDA device in a manner similar to
 * 	alloc_etherdev.
 */
struct net_device *alloc_irdadev(int sizeof_priv)
{
	return alloc_netdev(sizeof_priv, "irda%d", irda_device_setup);
}
EXPORT_SYMBOL(alloc_irdadev);

/*
 * Function irda_device_init_dongle (self, type, qos)
 *
 *    Initialize attached dongle.
 *
 * Important : request_module require us to call this function with
 * a process context and irq enabled. - Jean II
 */
dongle_t *irda_device_dongle_init(struct net_device *dev, int type)
{
	struct dongle_reg *reg;
	dongle_t *dongle = NULL;

	might_sleep();

	spin_lock(&dongles->hb_spinlock);
	reg = hashbin_find(dongles, type, NULL);

#ifdef CONFIG_KMOD
	/* Try to load the module needed */
	if (!reg && capable(CAP_SYS_MODULE)) {
		spin_unlock(&dongles->hb_spinlock);
	
		request_module("irda-dongle-%d", type);
		
		spin_lock(&dongles->hb_spinlock);
		reg = hashbin_find(dongles, type, NULL);
	}
#endif

	if (!reg || !try_module_get(reg->owner) ) {
		IRDA_ERROR("IrDA: Unable to find requested dongle type %x\n",
			   type);
		goto out;
	}

	/* Allocate dongle info for this instance */
	dongle = kmalloc(sizeof(dongle_t), GFP_KERNEL);
	if (!dongle)
		goto out;

	memset(dongle, 0, sizeof(dongle_t));

	/* Bind the registration info to this particular instance */
	dongle->issue = reg;
	dongle->dev = dev;

 out:
	spin_unlock(&dongles->hb_spinlock);
	return dongle;
}
EXPORT_SYMBOL(irda_device_dongle_init);

/*
 * Function irda_device_dongle_cleanup (dongle)
 */
int irda_device_dongle_cleanup(dongle_t *dongle)
{
	IRDA_ASSERT(dongle != NULL, return -1;);

	dongle->issue->close(dongle);
	module_put(dongle->issue->owner);
	kfree(dongle);

	return 0;
}
EXPORT_SYMBOL(irda_device_dongle_cleanup);

/*
 * Function irda_device_register_dongle (dongle)
 */
int irda_device_register_dongle(struct dongle_reg *new)
{
	spin_lock(&dongles->hb_spinlock);
	/* Check if this dongle has been registered before */
	if (hashbin_find(dongles, new->type, NULL)) {
		IRDA_MESSAGE("%s: Dongle type %x already registered\n", 
			     __FUNCTION__, new->type);
        } else {
		/* Insert IrDA dongle into hashbin */
		hashbin_insert(dongles, (irda_queue_t *) new, new->type, NULL);
	}
	spin_unlock(&dongles->hb_spinlock);

        return 0;
}
EXPORT_SYMBOL(irda_device_register_dongle);

/*
 * Function irda_device_unregister_dongle (dongle)
 *
 *    Unregister dongle, and remove dongle from list of registered dongles
 *
 */
void irda_device_unregister_dongle(struct dongle_reg *dongle)
{
	struct dongle *node;

	spin_lock(&dongles->hb_spinlock);
	node = hashbin_remove(dongles, dongle->type, NULL);
	if (!node) 
		IRDA_ERROR("%s: dongle not found!\n", __FUNCTION__);
	spin_unlock(&dongles->hb_spinlock);
}
EXPORT_SYMBOL(irda_device_unregister_dongle);

#ifdef CONFIG_ISA_DMA_API
/*
 * Function setup_dma (idev, buffer, count, mode)
 *
 *    Setup the DMA channel. Commonly used by LPC FIR drivers
 *
 */
void irda_setup_dma(int channel, dma_addr_t buffer, int count, int mode)
{
	unsigned long flags;

	flags = claim_dma_lock();

	disable_dma(channel);
	clear_dma_ff(channel);
	set_dma_mode(channel, mode);
	set_dma_addr(channel, buffer);
	set_dma_count(channel, count);
	enable_dma(channel);

	release_dma_lock(flags);
}
EXPORT_SYMBOL(irda_setup_dma);
#endif
