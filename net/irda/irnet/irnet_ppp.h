/*
 *	IrNET protocol module : Synchronous PPP over an IrDA socket.
 *
 *		Jean II - HPL `00 - <jt@hpl.hp.com>
 *
 * This file contains all definitions and declarations necessary for the
 * PPP part of the IrNET module.
 * This file is a private header, so other modules don't want to know
 * what's in there...
 */

#ifndef IRNET_PPP_H
#define IRNET_PPP_H

/***************************** INCLUDES *****************************/

#include "irnet.h"		/* Module global include */

/************************ CONSTANTS & MACROS ************************/

/* /dev/irnet file constants */
#define IRNET_MAJOR	10	/* Misc range */
#define IRNET_MINOR	187	/* Official allocation */

/* IrNET control channel stuff */
#define IRNET_MAX_COMMAND	256	/* Max length of a command line */

/* PPP hardcore stuff */

/* Bits in rbits (PPP flags in irnet struct) */
#define SC_RCV_BITS	(SC_RCV_B7_1|SC_RCV_B7_0|SC_RCV_ODDP|SC_RCV_EVNP)

/* Bit numbers in busy */
#define XMIT_BUSY	0
#define RECV_BUSY	1
#define XMIT_WAKEUP	2
#define XMIT_FULL	3

/* Queue management */
#define PPPSYNC_MAX_RQLEN	32	/* arbitrary */

/****************************** TYPES ******************************/


/**************************** PROTOTYPES ****************************/

/* ----------------------- CONTROL CHANNEL ----------------------- */
static inline ssize_t
	irnet_ctrl_write(irnet_socket *,
			 const char *,
			 size_t);
static inline ssize_t
	irnet_ctrl_read(irnet_socket *,
			struct file *,
			char *,
			size_t);
static inline unsigned int
	irnet_ctrl_poll(irnet_socket *,
			struct file *,
			poll_table *);
/* ----------------------- CHARACTER DEVICE ----------------------- */
static int
	dev_irnet_open(struct inode *,	/* fs callback : open */
		       struct file *),
	dev_irnet_close(struct inode *,
			struct file *);
static ssize_t
	dev_irnet_write(struct file *,
			const char __user *,
			size_t,
			loff_t *),
	dev_irnet_read(struct file *,
		       char __user *,
		       size_t,
		       loff_t *);
static unsigned int
	dev_irnet_poll(struct file *,
		       poll_table *);
static long
	dev_irnet_ioctl(struct file *,
			unsigned int,
			unsigned long);
/* ------------------------ PPP INTERFACE ------------------------ */
static inline struct sk_buff *
	irnet_prepare_skb(irnet_socket *,
			  struct sk_buff *);
static int
	ppp_irnet_send(struct ppp_channel *,
		      struct sk_buff *);
static int
	ppp_irnet_ioctl(struct ppp_channel *,
			unsigned int,
			unsigned long);

/**************************** VARIABLES ****************************/

/* Filesystem callbacks (to call us) */
static const struct file_operations irnet_device_fops =
{
	.owner		= THIS_MODULE,
	.read		= dev_irnet_read,
	.write		= dev_irnet_write,
	.poll		= dev_irnet_poll,
	.unlocked_ioctl	= dev_irnet_ioctl,
	.open		= dev_irnet_open,
	.release	= dev_irnet_close,
	.llseek		= noop_llseek,
  /* Also : llseek, readdir, mmap, flush, fsync, fasync, lock, readv, writev */
};

/* Structure so that the misc major (drivers/char/misc.c) take care of us... */
static struct miscdevice irnet_misc_device =
{
	IRNET_MINOR,
	"irnet",
	&irnet_device_fops
};

#endif /* IRNET_PPP_H */
