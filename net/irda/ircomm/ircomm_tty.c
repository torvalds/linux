/*********************************************************************
 *
 * Filename:      ircomm_tty.c
 * Version:       1.0
 * Description:   IrCOMM serial TTY driver
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Jun  6 21:00:56 1999
 * Modified at:   Wed Feb 23 00:09:02 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       serial.c and previous IrCOMM work by Takahide Higuchi
 *
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes <jt@hpl.hp.com>
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
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ********************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/device.h>		/* for MODULE_ALIAS_CHARDEV_MAJOR */

#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>

#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>
#include <net/irda/ircomm_tty_attach.h>
#include <net/irda/ircomm_tty.h>

static int ircomm_tty_install(struct tty_driver *driver,
		struct tty_struct *tty);
static int  ircomm_tty_open(struct tty_struct *tty, struct file *filp);
static void ircomm_tty_close(struct tty_struct * tty, struct file *filp);
static int  ircomm_tty_write(struct tty_struct * tty,
			     const unsigned char *buf, int count);
static int  ircomm_tty_write_room(struct tty_struct *tty);
static void ircomm_tty_throttle(struct tty_struct *tty);
static void ircomm_tty_unthrottle(struct tty_struct *tty);
static int  ircomm_tty_chars_in_buffer(struct tty_struct *tty);
static void ircomm_tty_flush_buffer(struct tty_struct *tty);
static void ircomm_tty_send_xchar(struct tty_struct *tty, char ch);
static void ircomm_tty_wait_until_sent(struct tty_struct *tty, int timeout);
static void ircomm_tty_hangup(struct tty_struct *tty);
static void ircomm_tty_do_softint(struct work_struct *work);
static void ircomm_tty_shutdown(struct ircomm_tty_cb *self);
static void ircomm_tty_stop(struct tty_struct *tty);

static int ircomm_tty_data_indication(void *instance, void *sap,
				      struct sk_buff *skb);
static int ircomm_tty_control_indication(void *instance, void *sap,
					 struct sk_buff *skb);
static void ircomm_tty_flow_indication(void *instance, void *sap,
				       LOCAL_FLOW cmd);
#ifdef CONFIG_PROC_FS
static const struct file_operations ircomm_tty_proc_fops;
#endif /* CONFIG_PROC_FS */
static struct tty_driver *driver;

static hashbin_t *ircomm_tty = NULL;

static const struct tty_operations ops = {
	.install	 = ircomm_tty_install,
	.open            = ircomm_tty_open,
	.close           = ircomm_tty_close,
	.write           = ircomm_tty_write,
	.write_room      = ircomm_tty_write_room,
	.chars_in_buffer = ircomm_tty_chars_in_buffer,
	.flush_buffer    = ircomm_tty_flush_buffer,
	.ioctl           = ircomm_tty_ioctl,	/* ircomm_tty_ioctl.c */
	.tiocmget        = ircomm_tty_tiocmget,	/* ircomm_tty_ioctl.c */
	.tiocmset        = ircomm_tty_tiocmset,	/* ircomm_tty_ioctl.c */
	.throttle        = ircomm_tty_throttle,
	.unthrottle      = ircomm_tty_unthrottle,
	.send_xchar      = ircomm_tty_send_xchar,
	.set_termios     = ircomm_tty_set_termios,
	.stop            = ircomm_tty_stop,
	.start           = ircomm_tty_start,
	.hangup          = ircomm_tty_hangup,
	.wait_until_sent = ircomm_tty_wait_until_sent,
#ifdef CONFIG_PROC_FS
	.proc_fops       = &ircomm_tty_proc_fops,
#endif /* CONFIG_PROC_FS */
};

static void ircomm_port_raise_dtr_rts(struct tty_port *port, int raise)
{
	struct ircomm_tty_cb *self = container_of(port, struct ircomm_tty_cb,
			port);
	/*
	 * Here, we use to lock those two guys, but as ircomm_param_request()
	 * does it itself, I don't see the point (and I see the deadlock).
	 * Jean II
	 */
	if (raise)
		self->settings.dte |= IRCOMM_RTS | IRCOMM_DTR;
	else
		self->settings.dte &= ~(IRCOMM_RTS | IRCOMM_DTR);

	ircomm_param_request(self, IRCOMM_DTE, TRUE);
}

static int ircomm_port_carrier_raised(struct tty_port *port)
{
	struct ircomm_tty_cb *self = container_of(port, struct ircomm_tty_cb,
			port);
	return self->settings.dce & IRCOMM_CD;
}

static const struct tty_port_operations ircomm_port_ops = {
	.dtr_rts = ircomm_port_raise_dtr_rts,
	.carrier_raised = ircomm_port_carrier_raised,
};

/*
 * Function ircomm_tty_init()
 *
 *    Init IrCOMM TTY layer/driver
 *
 */
static int __init ircomm_tty_init(void)
{
	driver = alloc_tty_driver(IRCOMM_TTY_PORTS);
	if (!driver)
		return -ENOMEM;
	ircomm_tty = hashbin_new(HB_LOCK);
	if (ircomm_tty == NULL) {
		net_err_ratelimited("%s(), can't allocate hashbin!\n",
				    __func__);
		put_tty_driver(driver);
		return -ENOMEM;
	}

	driver->driver_name     = "ircomm";
	driver->name            = "ircomm";
	driver->major           = IRCOMM_TTY_MAJOR;
	driver->minor_start     = IRCOMM_TTY_MINOR;
	driver->type            = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype         = SERIAL_TYPE_NORMAL;
	driver->init_termios    = tty_std_termios;
	driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	driver->flags           = TTY_DRIVER_REAL_RAW;
	tty_set_operations(driver, &ops);
	if (tty_register_driver(driver)) {
		net_err_ratelimited("%s(): Couldn't register serial driver\n",
				    __func__);
		put_tty_driver(driver);
		return -1;
	}
	return 0;
}

static void __exit __ircomm_tty_cleanup(struct ircomm_tty_cb *self)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	ircomm_tty_shutdown(self);

	self->magic = 0;
	tty_port_destroy(&self->port);
	kfree(self);
}

/*
 * Function ircomm_tty_cleanup ()
 *
 *    Remove IrCOMM TTY layer/driver
 *
 */
static void __exit ircomm_tty_cleanup(void)
{
	int ret;

	ret = tty_unregister_driver(driver);
	if (ret) {
		net_err_ratelimited("%s(), failed to unregister driver\n",
				    __func__);
		return;
	}

	hashbin_delete(ircomm_tty, (FREE_FUNC) __ircomm_tty_cleanup);
	put_tty_driver(driver);
}

/*
 * Function ircomm_startup (self)
 *
 *
 *
 */
static int ircomm_tty_startup(struct ircomm_tty_cb *self)
{
	notify_t notify;
	int ret = -ENODEV;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	/* Check if already open */
	if (tty_port_initialized(&self->port)) {
		pr_debug("%s(), already open so break out!\n", __func__);
		return 0;
	}
	tty_port_set_initialized(&self->port, 1);

	/* Register with IrCOMM */
	irda_notify_init(&notify);
	/* These callbacks we must handle ourselves */
	notify.data_indication       = ircomm_tty_data_indication;
	notify.udata_indication      = ircomm_tty_control_indication;
	notify.flow_indication       = ircomm_tty_flow_indication;

	/* Use the ircomm_tty interface for these ones */
	notify.disconnect_indication = ircomm_tty_disconnect_indication;
	notify.connect_confirm       = ircomm_tty_connect_confirm;
	notify.connect_indication    = ircomm_tty_connect_indication;
	strlcpy(notify.name, "ircomm_tty", sizeof(notify.name));
	notify.instance = self;

	if (!self->ircomm) {
		self->ircomm = ircomm_open(&notify, self->service_type,
					   self->line);
	}
	if (!self->ircomm)
		goto err;

	self->slsap_sel = self->ircomm->slsap_sel;

	/* Connect IrCOMM link with remote device */
	ret = ircomm_tty_attach_cable(self);
	if (ret < 0) {
		net_err_ratelimited("%s(), error attaching cable!\n", __func__);
		goto err;
	}

	return 0;
err:
	tty_port_set_initialized(&self->port, 0);
	return ret;
}

/*
 * Function ircomm_block_til_ready (self, filp)
 *
 *
 *
 */
static int ircomm_tty_block_til_ready(struct ircomm_tty_cb *self,
		struct tty_struct *tty, struct file *filp)
{
	struct tty_port *port = &self->port;
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	int		do_clocal = 0;
	unsigned long	flags;

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if (tty_io_error(tty)) {
		tty_port_set_active(port, 1);
		return 0;
	}

	if (filp->f_flags & O_NONBLOCK) {
		/* nonblock mode is set */
		if (C_BAUD(tty))
			tty_port_raise_dtr_rts(port);
		tty_port_set_active(port, 1);
		pr_debug("%s(), O_NONBLOCK requested!\n", __func__);
		return 0;
	}

	if (C_CLOCAL(tty)) {
		pr_debug("%s(), doing CLOCAL!\n", __func__);
		do_clocal = 1;
	}

	/* Wait for carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, port->count is dropped by one, so that
	 * mgsl_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */

	retval = 0;
	add_wait_queue(&port->open_wait, &wait);

	pr_debug("%s(%d):block_til_ready before block on %s open_count=%d\n",
		 __FILE__, __LINE__, tty->driver->name, port->count);

	spin_lock_irqsave(&port->lock, flags);
	port->count--;
	port->blocked_open++;
	spin_unlock_irqrestore(&port->lock, flags);

	while (1) {
		if (C_BAUD(tty) && tty_port_initialized(port))
			tty_port_raise_dtr_rts(port);

		set_current_state(TASK_INTERRUPTIBLE);

		if (tty_hung_up_p(filp) || !tty_port_initialized(port)) {
			retval = (port->flags & ASYNC_HUP_NOTIFY) ?
					-EAGAIN : -ERESTARTSYS;
			break;
		}

		/*
		 * Check if link is ready now. Even if CLOCAL is
		 * specified, we cannot return before the IrCOMM link is
		 * ready
		 */
		if ((do_clocal || tty_port_carrier_raised(port)) &&
		    self->state == IRCOMM_TTY_READY)
		{
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}

		pr_debug("%s(%d):block_til_ready blocking on %s open_count=%d\n",
			 __FILE__, __LINE__, tty->driver->name, port->count);

		schedule();
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->open_wait, &wait);

	spin_lock_irqsave(&port->lock, flags);
	if (!tty_hung_up_p(filp))
		port->count++;
	port->blocked_open--;
	spin_unlock_irqrestore(&port->lock, flags);

	pr_debug("%s(%d):block_til_ready after blocking on %s open_count=%d\n",
		 __FILE__, __LINE__, tty->driver->name, port->count);

	if (!retval)
		tty_port_set_active(port, 1);

	return retval;
}


static int ircomm_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct ircomm_tty_cb *self;
	unsigned int line = tty->index;

	/* Check if instance already exists */
	self = hashbin_lock_find(ircomm_tty, line, NULL);
	if (!self) {
		/* No, so make new instance */
		self = kzalloc(sizeof(struct ircomm_tty_cb), GFP_KERNEL);
		if (self == NULL)
			return -ENOMEM;

		tty_port_init(&self->port);
		self->port.ops = &ircomm_port_ops;
		self->magic = IRCOMM_TTY_MAGIC;
		self->flow = FLOW_STOP;

		self->line = line;
		INIT_WORK(&self->tqueue, ircomm_tty_do_softint);
		self->max_header_size = IRCOMM_TTY_HDR_UNINITIALISED;
		self->max_data_size = IRCOMM_TTY_DATA_UNINITIALISED;

		/* Init some important stuff */
		init_timer(&self->watchdog_timer);
		spin_lock_init(&self->spinlock);

		/*
		 * Force TTY into raw mode by default which is usually what
		 * we want for IrCOMM and IrLPT. This way applications will
		 * not have to twiddle with printcap etc.
		 *
		 * Note this is completely usafe and doesn't work properly
		 */
		tty->termios.c_iflag = 0;
		tty->termios.c_oflag = 0;

		/* Insert into hash */
		hashbin_insert(ircomm_tty, (irda_queue_t *) self, line, NULL);
	}

	tty->driver_data = self;

	return tty_port_install(&self->port, driver, tty);
}

/*
 * Function ircomm_tty_open (tty, filp)
 *
 *    This routine is called when a particular tty device is opened. This
 *    routine is mandatory; if this routine is not filled in, the attempted
 *    open will fail with ENODEV.
 */
static int ircomm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct ircomm_tty_cb *self = tty->driver_data;
	unsigned long	flags;
	int ret;

	/* ++ is not atomic, so this should be protected - Jean II */
	spin_lock_irqsave(&self->port.lock, flags);
	self->port.count++;
	spin_unlock_irqrestore(&self->port.lock, flags);
	tty_port_tty_set(&self->port, tty);

	pr_debug("%s(), %s%d, count = %d\n", __func__ , tty->driver->name,
		 self->line, self->port.count);

	/* Not really used by us, but lets do it anyway */
	self->port.low_latency = (self->port.flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/* Check if this is a "normal" ircomm device, or an irlpt device */
	if (self->line < 0x10) {
		self->service_type = IRCOMM_3_WIRE | IRCOMM_9_WIRE;
		self->settings.service_type = IRCOMM_9_WIRE; /* 9 wire as default */
		/* Jan Kiszka -> add DSR/RI -> Conform to IrCOMM spec */
		self->settings.dce = IRCOMM_CTS | IRCOMM_CD | IRCOMM_DSR | IRCOMM_RI; /* Default line settings */
		pr_debug("%s(), IrCOMM device\n", __func__);
	} else {
		pr_debug("%s(), IrLPT device\n", __func__);
		self->service_type = IRCOMM_3_WIRE_RAW;
		self->settings.service_type = IRCOMM_3_WIRE_RAW; /* Default */
	}

	ret = ircomm_tty_startup(self);
	if (ret)
		return ret;

	ret = ircomm_tty_block_til_ready(self, tty, filp);
	if (ret) {
		pr_debug("%s(), returning after block_til_ready with %d\n",
			 __func__, ret);

		return ret;
	}
	return 0;
}

/*
 * Function ircomm_tty_close (tty, filp)
 *
 *    This routine is called when a particular tty device is closed.
 *
 */
static void ircomm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	struct tty_port *port = &self->port;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	if (tty_port_close_start(port, tty, filp) == 0)
		return;

	ircomm_tty_shutdown(self);

	tty_driver_flush_buffer(tty);

	tty_port_close_end(port, tty);
	tty_port_tty_set(port, NULL);
}

/*
 * Function ircomm_tty_flush_buffer (tty)
 *
 *
 *
 */
static void ircomm_tty_flush_buffer(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/*
	 * Let do_softint() do this to avoid race condition with
	 * do_softint() ;-)
	 */
	schedule_work(&self->tqueue);
}

/*
 * Function ircomm_tty_do_softint (work)
 *
 *    We use this routine to give the write wakeup to the user at at a
 *    safe time (as fast as possible after write have completed). This
 *    can be compared to the Tx interrupt.
 */
static void ircomm_tty_do_softint(struct work_struct *work)
{
	struct ircomm_tty_cb *self =
		container_of(work, struct ircomm_tty_cb, tqueue);
	struct tty_struct *tty;
	unsigned long flags;
	struct sk_buff *skb, *ctrl_skb;

	if (!self || self->magic != IRCOMM_TTY_MAGIC)
		return;

	tty = tty_port_tty_get(&self->port);
	if (!tty)
		return;

	/* Unlink control buffer */
	spin_lock_irqsave(&self->spinlock, flags);

	ctrl_skb = self->ctrl_skb;
	self->ctrl_skb = NULL;

	spin_unlock_irqrestore(&self->spinlock, flags);

	/* Flush control buffer if any */
	if(ctrl_skb) {
		if(self->flow == FLOW_START)
			ircomm_control_request(self->ircomm, ctrl_skb);
		/* Drop reference count - see ircomm_ttp_data_request(). */
		dev_kfree_skb(ctrl_skb);
	}

	if (tty->hw_stopped)
		goto put;

	/* Unlink transmit buffer */
	spin_lock_irqsave(&self->spinlock, flags);

	skb = self->tx_skb;
	self->tx_skb = NULL;

	spin_unlock_irqrestore(&self->spinlock, flags);

	/* Flush transmit buffer if any */
	if (skb) {
		ircomm_tty_do_event(self, IRCOMM_TTY_DATA_REQUEST, skb, NULL);
		/* Drop reference count - see ircomm_ttp_data_request(). */
		dev_kfree_skb(skb);
	}

	/* Check if user (still) wants to be waken up */
	tty_wakeup(tty);
put:
	tty_kref_put(tty);
}

/*
 * Function ircomm_tty_write (tty, buf, count)
 *
 *    This routine is called by the kernel to write a series of characters
 *    to the tty device. The characters may come from user space or kernel
 *    space. This routine will return the number of characters actually
 *    accepted for writing. This routine is mandatory.
 */
static int ircomm_tty_write(struct tty_struct *tty,
			    const unsigned char *buf, int count)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned long flags;
	struct sk_buff *skb;
	int tailroom = 0;
	int len = 0;
	int size;

	pr_debug("%s(), count=%d, hw_stopped=%d\n", __func__ , count,
		 tty->hw_stopped);

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	/* We may receive packets from the TTY even before we have finished
	 * our setup. Not cool.
	 * The problem is that we don't know the final header and data size
	 * to create the proper skb, so any skb we would create would have
	 * bogus header and data size, so need care.
	 * We use a bogus header size to safely detect this condition.
	 * Another problem is that hw_stopped was set to 0 way before it
	 * should be, so we would drop this skb. It should now be fixed.
	 * One option is to not accept data until we are properly setup.
	 * But, I suspect that when it happens, the ppp line discipline
	 * just "drops" the data, which might screw up connect scripts.
	 * The second option is to create a "safe skb", with large header
	 * and small size (see ircomm_tty_open() for values).
	 * We just need to make sure that when the real values get filled,
	 * we don't mess up the original "safe skb" (see tx_data_size).
	 * Jean II */
	if (self->max_header_size == IRCOMM_TTY_HDR_UNINITIALISED) {
		pr_debug("%s() : not initialised\n", __func__);
#ifdef IRCOMM_NO_TX_BEFORE_INIT
		/* We didn't consume anything, TTY will retry */
		return 0;
#endif
	}

	if (count < 1)
		return 0;

	/* Protect our manipulation of self->tx_skb and related */
	spin_lock_irqsave(&self->spinlock, flags);

	/* Fetch current transmit buffer */
	skb = self->tx_skb;

	/*
	 * Send out all the data we get, possibly as multiple fragmented
	 * frames, but this will only happen if the data is larger than the
	 * max data size. The normal case however is just the opposite, and
	 * this function may be called multiple times, and will then actually
	 * defragment the data and send it out as one packet as soon as
	 * possible, but at a safer point in time
	 */
	while (count) {
		size = count;

		/* Adjust data size to the max data size */
		if (size > self->max_data_size)
			size = self->max_data_size;

		/*
		 * Do we already have a buffer ready for transmit, or do
		 * we need to allocate a new frame
		 */
		if (skb) {
			/*
			 * Any room for more data at the end of the current
			 * transmit buffer? Cannot use skb_tailroom, since
			 * dev_alloc_skb gives us a larger skb than we
			 * requested
			 * Note : use tx_data_size, because max_data_size
			 * may have changed and we don't want to overwrite
			 * the skb. - Jean II
			 */
			if ((tailroom = (self->tx_data_size - skb->len)) > 0) {
				/* Adjust data to tailroom */
				if (size > tailroom)
					size = tailroom;
			} else {
				/*
				 * Current transmit frame is full, so break
				 * out, so we can send it as soon as possible
				 */
				break;
			}
		} else {
			/* Prepare a full sized frame */
			skb = alloc_skb(self->max_data_size+
					self->max_header_size,
					GFP_ATOMIC);
			if (!skb) {
				spin_unlock_irqrestore(&self->spinlock, flags);
				return -ENOBUFS;
			}
			skb_reserve(skb, self->max_header_size);
			self->tx_skb = skb;
			/* Remember skb size because max_data_size may
			 * change later on - Jean II */
			self->tx_data_size = self->max_data_size;
		}

		/* Copy data */
		memcpy(skb_put(skb,size), buf + len, size);

		count -= size;
		len += size;
	}

	spin_unlock_irqrestore(&self->spinlock, flags);

	/*
	 * Schedule a new thread which will transmit the frame as soon
	 * as possible, but at a safe point in time. We do this so the
	 * "user" can give us data multiple times, as PPP does (because of
	 * its 256 byte tx buffer). We will then defragment and send out
	 * all this data as one single packet.
	 */
	schedule_work(&self->tqueue);

	return len;
}

/*
 * Function ircomm_tty_write_room (tty)
 *
 *    This routine returns the numbers of characters the tty driver will
 *    accept for queuing to be written. This number is subject to change as
 *    output buffers get emptied, or if the output flow control is acted.
 */
static int ircomm_tty_write_room(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned long flags;
	int ret;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

#ifdef IRCOMM_NO_TX_BEFORE_INIT
	/* max_header_size tells us if the channel is initialised or not. */
	if (self->max_header_size == IRCOMM_TTY_HDR_UNINITIALISED)
		/* Don't bother us yet */
		return 0;
#endif

	/* Check if we are allowed to transmit any data.
	 * hw_stopped is the regular flow control.
	 * Jean II */
	if (tty->hw_stopped)
		ret = 0;
	else {
		spin_lock_irqsave(&self->spinlock, flags);
		if (self->tx_skb)
			ret = self->tx_data_size - self->tx_skb->len;
		else
			ret = self->max_data_size;
		spin_unlock_irqrestore(&self->spinlock, flags);
	}
	pr_debug("%s(), ret=%d\n", __func__ , ret);

	return ret;
}

/*
 * Function ircomm_tty_wait_until_sent (tty, timeout)
 *
 *    This routine waits until the device has written out all of the
 *    characters in its transmitter FIFO.
 */
static void ircomm_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned long orig_jiffies, poll_time;
	unsigned long flags;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	orig_jiffies = jiffies;

	/* Set poll time to 200 ms */
	poll_time = msecs_to_jiffies(200);
	if (timeout)
		poll_time = min_t(unsigned long, timeout, poll_time);

	spin_lock_irqsave(&self->spinlock, flags);
	while (self->tx_skb && self->tx_skb->len) {
		spin_unlock_irqrestore(&self->spinlock, flags);
		schedule_timeout_interruptible(poll_time);
		spin_lock_irqsave(&self->spinlock, flags);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	spin_unlock_irqrestore(&self->spinlock, flags);
	__set_current_state(TASK_RUNNING);
}

/*
 * Function ircomm_tty_throttle (tty)
 *
 *    This routine notifies the tty driver that input buffers for the line
 *    discipline are close to full, and it should somehow signal that no
 *    more characters should be sent to the tty.
 */
static void ircomm_tty_throttle(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/* Software flow control? */
	if (I_IXOFF(tty))
		ircomm_tty_send_xchar(tty, STOP_CHAR(tty));

	/* Hardware flow control? */
	if (C_CRTSCTS(tty)) {
		self->settings.dte &= ~IRCOMM_RTS;
		self->settings.dte |= IRCOMM_DELTA_RTS;

		ircomm_param_request(self, IRCOMM_DTE, TRUE);
	}

	ircomm_flow_request(self->ircomm, FLOW_STOP);
}

/*
 * Function ircomm_tty_unthrottle (tty)
 *
 *    This routine notifies the tty drivers that it should signals that
 *    characters can now be sent to the tty without fear of overrunning the
 *    input buffers of the line disciplines.
 */
static void ircomm_tty_unthrottle(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/* Using software flow control? */
	if (I_IXOFF(tty))
		ircomm_tty_send_xchar(tty, START_CHAR(tty));

	/* Using hardware flow control? */
	if (C_CRTSCTS(tty)) {
		self->settings.dte |= (IRCOMM_RTS|IRCOMM_DELTA_RTS);

		ircomm_param_request(self, IRCOMM_DTE, TRUE);
		pr_debug("%s(), FLOW_START\n", __func__);
	}
	ircomm_flow_request(self->ircomm, FLOW_START);
}

/*
 * Function ircomm_tty_chars_in_buffer (tty)
 *
 *    Indicates if there are any data in the buffer
 *
 */
static int ircomm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned long flags;
	int len = 0;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	spin_lock_irqsave(&self->spinlock, flags);

	if (self->tx_skb)
		len = self->tx_skb->len;

	spin_unlock_irqrestore(&self->spinlock, flags);

	return len;
}

static void ircomm_tty_shutdown(struct ircomm_tty_cb *self)
{
	unsigned long flags;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	if (!tty_port_initialized(&self->port))
		return;
	tty_port_set_initialized(&self->port, 0);

	ircomm_tty_detach_cable(self);

	spin_lock_irqsave(&self->spinlock, flags);

	del_timer(&self->watchdog_timer);

	/* Free parameter buffer */
	if (self->ctrl_skb) {
		dev_kfree_skb(self->ctrl_skb);
		self->ctrl_skb = NULL;
	}

	/* Free transmit buffer */
	if (self->tx_skb) {
		dev_kfree_skb(self->tx_skb);
		self->tx_skb = NULL;
	}

	if (self->ircomm) {
		ircomm_close(self->ircomm);
		self->ircomm = NULL;
	}

	spin_unlock_irqrestore(&self->spinlock, flags);
}

/*
 * Function ircomm_tty_hangup (tty)
 *
 *    This routine notifies the tty driver that it should hangup the tty
 *    device.
 *
 */
static void ircomm_tty_hangup(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	struct tty_port *port = &self->port;
	unsigned long	flags;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	/* ircomm_tty_flush_buffer(tty); */
	ircomm_tty_shutdown(self);

	spin_lock_irqsave(&port->lock, flags);
	if (port->tty) {
		set_bit(TTY_IO_ERROR, &port->tty->flags);
		tty_kref_put(port->tty);
	}
	port->tty = NULL;
	port->count = 0;
	spin_unlock_irqrestore(&port->lock, flags);
	tty_port_set_active(port, 0);

	wake_up_interruptible(&port->open_wait);
}

/*
 * Function ircomm_tty_send_xchar (tty, ch)
 *
 *    This routine is used to send a high-priority XON/XOFF character to
 *    the device.
 */
static void ircomm_tty_send_xchar(struct tty_struct *tty, char ch)
{
	pr_debug("%s(), not impl\n", __func__);
}

/*
 * Function ircomm_tty_start (tty)
 *
 *    This routine notifies the tty driver that it resume sending
 *    characters to the tty device.
 */
void ircomm_tty_start(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	ircomm_flow_request(self->ircomm, FLOW_START);
}

/*
 * Function ircomm_tty_stop (tty)
 *
 *     This routine notifies the tty driver that it should stop outputting
 *     characters to the tty device.
 */
static void ircomm_tty_stop(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	ircomm_flow_request(self->ircomm, FLOW_STOP);
}

/*
 * Function ircomm_check_modem_status (self)
 *
 *    Check for any changes in the DCE's line settings. This function should
 *    be called whenever the dce parameter settings changes, to update the
 *    flow control settings and other things
 */
void ircomm_tty_check_modem_status(struct ircomm_tty_cb *self)
{
	struct tty_struct *tty;
	int status;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	tty = tty_port_tty_get(&self->port);

	status = self->settings.dce;

	if (status & IRCOMM_DCE_DELTA_ANY) {
		/*wake_up_interruptible(&self->delta_msr_wait);*/
	}
	if (tty_port_check_carrier(&self->port) && (status & IRCOMM_DELTA_CD)) {
		pr_debug("%s(), ircomm%d CD now %s...\n", __func__ , self->line,
			 (status & IRCOMM_CD) ? "on" : "off");

		if (status & IRCOMM_CD) {
			wake_up_interruptible(&self->port.open_wait);
		} else {
			pr_debug("%s(), Doing serial hangup..\n", __func__);
			if (tty)
				tty_hangup(tty);

			/* Hangup will remote the tty, so better break out */
			goto put;
		}
	}
	if (tty && tty_port_cts_enabled(&self->port)) {
		if (tty->hw_stopped) {
			if (status & IRCOMM_CTS) {
				pr_debug("%s(), CTS tx start...\n", __func__);
				tty->hw_stopped = 0;

				/* Wake up processes blocked on open */
				wake_up_interruptible(&self->port.open_wait);

				schedule_work(&self->tqueue);
				goto put;
			}
		} else {
			if (!(status & IRCOMM_CTS)) {
				pr_debug("%s(), CTS tx stop...\n", __func__);
				tty->hw_stopped = 1;
			}
		}
	}
put:
	tty_kref_put(tty);
}

/*
 * Function ircomm_tty_data_indication (instance, sap, skb)
 *
 *    Handle incoming data, and deliver it to the line discipline
 *
 */
static int ircomm_tty_data_indication(void *instance, void *sap,
				      struct sk_buff *skb)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;
	struct tty_struct *tty;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	tty = tty_port_tty_get(&self->port);
	if (!tty) {
		pr_debug("%s(), no tty!\n", __func__);
		return 0;
	}

	/*
	 * If we receive data when hardware is stopped then something is wrong.
	 * We try to poll the peers line settings to check if we are up todate.
	 * Devices like WinCE can do this, and since they don't send any
	 * params, we can just as well declare the hardware for running.
	 */
	if (tty->hw_stopped && (self->flow == FLOW_START)) {
		pr_debug("%s(), polling for line settings!\n", __func__);
		ircomm_param_request(self, IRCOMM_POLL, TRUE);

		/* We can just as well declare the hardware for running */
		ircomm_tty_send_initial_parameters(self);
		ircomm_tty_link_established(self);
	}
	tty_kref_put(tty);

	/*
	 * Use flip buffer functions since the code may be called from interrupt
	 * context
	 */
	tty_insert_flip_string(&self->port, skb->data, skb->len);
	tty_flip_buffer_push(&self->port);

	/* No need to kfree_skb - see ircomm_ttp_data_indication() */

	return 0;
}

/*
 * Function ircomm_tty_control_indication (instance, sap, skb)
 *
 *    Parse all incoming parameters (easy!)
 *
 */
static int ircomm_tty_control_indication(void *instance, void *sap,
					 struct sk_buff *skb)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;
	int clen;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);
	IRDA_ASSERT(skb != NULL, return -1;);

	clen = skb->data[0];

	irda_param_extract_all(self, skb->data+1, IRDA_MIN(skb->len-1, clen),
			       &ircomm_param_info);

	/* No need to kfree_skb - see ircomm_control_indication() */

	return 0;
}

/*
 * Function ircomm_tty_flow_indication (instance, sap, cmd)
 *
 *    This function is called by IrTTP when it wants us to slow down the
 *    transmission of data. We just mark the hardware as stopped, and wait
 *    for IrTTP to notify us that things are OK again.
 */
static void ircomm_tty_flow_indication(void *instance, void *sap,
				       LOCAL_FLOW cmd)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) instance;
	struct tty_struct *tty;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return;);

	tty = tty_port_tty_get(&self->port);

	switch (cmd) {
	case FLOW_START:
		pr_debug("%s(), hw start!\n", __func__);
		if (tty)
			tty->hw_stopped = 0;

		/* ircomm_tty_do_softint will take care of the rest */
		schedule_work(&self->tqueue);
		break;
	default:  /* If we get here, something is very wrong, better stop */
	case FLOW_STOP:
		pr_debug("%s(), hw stopped!\n", __func__);
		if (tty)
			tty->hw_stopped = 1;
		break;
	}

	tty_kref_put(tty);
	self->flow = cmd;
}

#ifdef CONFIG_PROC_FS
static void ircomm_tty_line_info(struct ircomm_tty_cb *self, struct seq_file *m)
{
	struct tty_struct *tty;
	char sep;

	seq_printf(m, "State: %s\n", ircomm_tty_state[self->state]);

	seq_puts(m, "Service type: ");
	if (self->service_type & IRCOMM_9_WIRE)
		seq_puts(m, "9_WIRE");
	else if (self->service_type & IRCOMM_3_WIRE)
		seq_puts(m, "3_WIRE");
	else if (self->service_type & IRCOMM_3_WIRE_RAW)
		seq_puts(m, "3_WIRE_RAW");
	else
		seq_puts(m, "No common service type!\n");
	seq_putc(m, '\n');

	seq_printf(m, "Port name: %s\n", self->settings.port_name);

	seq_printf(m, "DTE status:");
	sep = ' ';
	if (self->settings.dte & IRCOMM_RTS) {
		seq_printf(m, "%cRTS", sep);
		sep = '|';
	}
	if (self->settings.dte & IRCOMM_DTR) {
		seq_printf(m, "%cDTR", sep);
		sep = '|';
	}
	seq_putc(m, '\n');

	seq_puts(m, "DCE status:");
	sep = ' ';
	if (self->settings.dce & IRCOMM_CTS) {
		seq_printf(m, "%cCTS", sep);
		sep = '|';
	}
	if (self->settings.dce & IRCOMM_DSR) {
		seq_printf(m, "%cDSR", sep);
		sep = '|';
	}
	if (self->settings.dce & IRCOMM_CD) {
		seq_printf(m, "%cCD", sep);
		sep = '|';
	}
	if (self->settings.dce & IRCOMM_RI) {
		seq_printf(m, "%cRI", sep);
		sep = '|';
	}
	seq_putc(m, '\n');

	seq_puts(m, "Configuration: ");
	if (!self->settings.null_modem)
		seq_puts(m, "DTE <-> DCE\n");
	else
		seq_puts(m, "DTE <-> DTE (null modem emulation)\n");

	seq_printf(m, "Data rate: %d\n", self->settings.data_rate);

	seq_puts(m, "Flow control:");
	sep = ' ';
	if (self->settings.flow_control & IRCOMM_XON_XOFF_IN) {
		seq_printf(m, "%cXON_XOFF_IN", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_XON_XOFF_OUT) {
		seq_printf(m, "%cXON_XOFF_OUT", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_RTS_CTS_IN) {
		seq_printf(m, "%cRTS_CTS_IN", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_RTS_CTS_OUT) {
		seq_printf(m, "%cRTS_CTS_OUT", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_DSR_DTR_IN) {
		seq_printf(m, "%cDSR_DTR_IN", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_DSR_DTR_OUT) {
		seq_printf(m, "%cDSR_DTR_OUT", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_ENQ_ACK_IN) {
		seq_printf(m, "%cENQ_ACK_IN", sep);
		sep = '|';
	}
	if (self->settings.flow_control & IRCOMM_ENQ_ACK_OUT) {
		seq_printf(m, "%cENQ_ACK_OUT", sep);
		sep = '|';
	}
	seq_putc(m, '\n');

	seq_puts(m, "Flags:");
	sep = ' ';
	if (tty_port_cts_enabled(&self->port)) {
		seq_printf(m, "%cASYNC_CTS_FLOW", sep);
		sep = '|';
	}
	if (tty_port_check_carrier(&self->port)) {
		seq_printf(m, "%cASYNC_CHECK_CD", sep);
		sep = '|';
	}
	if (tty_port_initialized(&self->port)) {
		seq_printf(m, "%cASYNC_INITIALIZED", sep);
		sep = '|';
	}
	if (self->port.flags & ASYNC_LOW_LATENCY) {
		seq_printf(m, "%cASYNC_LOW_LATENCY", sep);
		sep = '|';
	}
	if (tty_port_active(&self->port)) {
		seq_printf(m, "%cASYNC_NORMAL_ACTIVE", sep);
		sep = '|';
	}
	seq_putc(m, '\n');

	seq_printf(m, "Role: %s\n", self->client ? "client" : "server");
	seq_printf(m, "Open count: %d\n", self->port.count);
	seq_printf(m, "Max data size: %d\n", self->max_data_size);
	seq_printf(m, "Max header size: %d\n", self->max_header_size);

	tty = tty_port_tty_get(&self->port);
	if (tty) {
		seq_printf(m, "Hardware: %s\n",
			       tty->hw_stopped ? "Stopped" : "Running");
		tty_kref_put(tty);
	}
}

static int ircomm_tty_proc_show(struct seq_file *m, void *v)
{
	struct ircomm_tty_cb *self;
	unsigned long flags;

	spin_lock_irqsave(&ircomm_tty->hb_spinlock, flags);

	self = (struct ircomm_tty_cb *) hashbin_get_first(ircomm_tty);
	while (self != NULL) {
		if (self->magic != IRCOMM_TTY_MAGIC)
			break;

		ircomm_tty_line_info(self, m);
		self = (struct ircomm_tty_cb *) hashbin_get_next(ircomm_tty);
	}
	spin_unlock_irqrestore(&ircomm_tty->hb_spinlock, flags);
	return 0;
}

static int ircomm_tty_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ircomm_tty_proc_show, NULL);
}

static const struct file_operations ircomm_tty_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ircomm_tty_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_PROC_FS */

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("IrCOMM serial TTY driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(IRCOMM_TTY_MAJOR);

module_init(ircomm_tty_init);
module_exit(ircomm_tty_cleanup);
