/*****************************************************************************
* wanmain.c	WAN Multiprotocol Router Module. Main code.
*
*		This module is completely hardware-independent and provides
*		the following common services for the WAN Link Drivers:
*		 o WAN device management (registering, unregistering)
*		 o Network interface management
*		 o Physical connection management (dial-up, incoming calls)
*		 o Logical connection management (switched virtual circuits)
*		 o Protocol encapsulation/decapsulation
*
* Author:	Gideon Hack
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Nov 24, 2000  Nenad Corbic	Updated for 2.4.X kernels
* Nov 07, 2000  Nenad Corbic	Fixed the Mulit-Port PPP for kernels 2.2.16 and
*  				greater.
* Aug 2,  2000  Nenad Corbic	Block the Multi-Port PPP from running on
*  			        kernels 2.2.16 or greater.  The SyncPPP
*  			        has changed.
* Jul 13, 2000  Nenad Corbic	Added SyncPPP support
* 				Added extra debugging in device_setup().
* Oct 01, 1999  Gideon Hack     Update for s514 PCI card
* Dec 27, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
* Jan 16, 1997	Gene Kozin	router_devlist made public
* Jan 31, 1997  Alan Cox	Hacked it about a bit for 2.1
* Jun 27, 1997  Alan Cox	realigned with vendor code
* Oct 15, 1997  Farhan Thawar   changed wan_encapsulate to add a pad byte of 0
* Apr 20, 1998	Alan Cox	Fixed 2.1 symbols
* May 17, 1998  K. Baranowski	Fixed SNAP encapsulation in wan_encapsulate
* Dec 15, 1998  Arnaldo Melo    support for firmwares of up to 128000 bytes
*                               check wandev->setup return value
* Dec 22, 1998  Arnaldo Melo    vmalloc/vfree used in device_setup to allocate
*                               kernel memory and copy configuration data to
*                               kernel space (for big firmwares)
* Jun 02, 1999  Gideon Hack	Updates for Linux 2.0.X and 2.2.X kernels.
*****************************************************************************/

#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/capability.h>
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/module.h>	/* support for loadable modules */
#include <linux/slab.h>		/* kmalloc(), kfree() */
#include <linux/mm.h>
#include <linux/string.h>	/* inline mem*, str* functions */

#include <asm/byteorder.h>	/* htons(), etc. */
#include <linux/wanrouter.h>	/* WAN router API definitions */

#include <linux/vmalloc.h>	/* vmalloc, vfree */
#include <asm/uaccess.h>        /* copy_to/from_user */
#include <linux/init.h>         /* __initfunc et al. */

#define KMEM_SAFETYZONE 8

/*
 * 	Function Prototypes
 */

/*
 *	WAN device IOCTL handlers
 */

static int wanrouter_device_setup(struct wan_device *wandev,
				  wandev_conf_t __user *u_conf);
static int wanrouter_device_stat(struct wan_device *wandev,
				 wandev_stat_t __user *u_stat);
static int wanrouter_device_shutdown(struct wan_device *wandev);
static int wanrouter_device_new_if(struct wan_device *wandev,
				   wanif_conf_t __user *u_conf);
static int wanrouter_device_del_if(struct wan_device *wandev,
				   char __user *u_name);

/*
 *	Miscellaneous
 */

static struct wan_device *wanrouter_find_device(char *name);
static int wanrouter_delete_interface(struct wan_device *wandev, char *name);
static void lock_adapter_irq(spinlock_t *lock, unsigned long *smp_flags);
static void unlock_adapter_irq(spinlock_t *lock, unsigned long *smp_flags);



/*
 *	Global Data
 */

static char wanrouter_fullname[]  = "Sangoma WANPIPE Router";
static char wanrouter_copyright[] = "(c) 1995-2000 Sangoma Technologies Inc.";
static char wanrouter_modname[] = ROUTER_NAME; /* short module name */
struct wan_device* wanrouter_router_devlist; /* list of registered devices */

/*
 *	Organize Unique Identifiers for encapsulation/decapsulation
 */

#if 0
static unsigned char wanrouter_oui_ether[] = { 0x00, 0x00, 0x00 };
static unsigned char wanrouter_oui_802_2[] = { 0x00, 0x80, 0xC2 };
#endif

static int __init wanrouter_init(void)
{
	int err;

	printk(KERN_INFO "%s v%u.%u %s\n",
	       wanrouter_fullname, ROUTER_VERSION, ROUTER_RELEASE,
	       wanrouter_copyright);

	err = wanrouter_proc_init();
	if (err)
		printk(KERN_INFO "%s: can't create entry in proc filesystem!\n",
		       wanrouter_modname);

	return err;
}

static void __exit wanrouter_cleanup (void)
{
	wanrouter_proc_cleanup();
}

/*
 * This is just plain dumb.  We should move the bugger to drivers/net/wan,
 * slap it first in directory and make it module_init().  The only reason
 * for subsys_initcall() here is that net goes after drivers (why, BTW?)
 */
subsys_initcall(wanrouter_init);
module_exit(wanrouter_cleanup);

/*
 * 	Kernel APIs
 */

/*
 * 	Register WAN device.
 * 	o verify device credentials
 * 	o create an entry for the device in the /proc/net/router directory
 * 	o initialize internally maintained fields of the wan_device structure
 * 	o link device data space to a singly-linked list
 * 	o if it's the first device, then start kernel 'thread'
 * 	o increment module use count
 *
 * 	Return:
 *	0	Ok
 *	< 0	error.
 *
 * 	Context:	process
 */


int register_wan_device(struct wan_device *wandev)
{
	int err, namelen;

	if ((wandev == NULL) || (wandev->magic != ROUTER_MAGIC) ||
	    (wandev->name == NULL))
		return -EINVAL;

	namelen = strlen(wandev->name);
	if (!namelen || (namelen > WAN_DRVNAME_SZ))
		return -EINVAL;

	if (wanrouter_find_device(wandev->name))
		return -EEXIST;

#ifdef WANDEBUG
	printk(KERN_INFO "%s: registering WAN device %s\n",
	       wanrouter_modname, wandev->name);
#endif

	/*
	 *	Register /proc directory entry
	 */
	err = wanrouter_proc_add(wandev);
	if (err) {
		printk(KERN_INFO
			"%s: can't create /proc/net/router/%s entry!\n",
			wanrouter_modname, wandev->name);
		return err;
	}

	/*
	 *	Initialize fields of the wan_device structure maintained by the
	 *	router and update local data.
	 */

	wandev->ndev = 0;
	wandev->dev  = NULL;
	wandev->next = wanrouter_router_devlist;
	wanrouter_router_devlist = wandev;
	return 0;
}

/*
 *	Unregister WAN device.
 *	o shut down device
 *	o unlink device data space from the linked list
 *	o delete device entry in the /proc/net/router directory
 *	o decrement module use count
 *
 *	Return:		0	Ok
 *			<0	error.
 *	Context:	process
 */


int unregister_wan_device(char *name)
{
	struct wan_device *wandev, *prev;

	if (name == NULL)
		return -EINVAL;

	for (wandev = wanrouter_router_devlist, prev = NULL;
		wandev && strcmp(wandev->name, name);
		prev = wandev, wandev = wandev->next)
		;
	if (wandev == NULL)
		return -ENODEV;

#ifdef WANDEBUG
	printk(KERN_INFO "%s: unregistering WAN device %s\n",
	       wanrouter_modname, name);
#endif

	if (wandev->state != WAN_UNCONFIGURED)
		wanrouter_device_shutdown(wandev);

	if (prev)
		prev->next = wandev->next;
	else
		wanrouter_router_devlist = wandev->next;

	wanrouter_proc_delete(wandev);
	return 0;
}

#if 0

/*
 *	Encapsulate packet.
 *
 *	Return:	encapsulation header size
 *		< 0	- unsupported Ethertype
 *
 *	Notes:
 *	1. This function may be called on interrupt context.
 */


int wanrouter_encapsulate(struct sk_buff *skb, struct net_device *dev,
			  unsigned short type)
{
	int hdr_len = 0;

	switch (type) {
	case ETH_P_IP:		/* IP datagram encapsulation */
		hdr_len += 1;
		skb_push(skb, 1);
		skb->data[0] = NLPID_IP;
		break;

	case ETH_P_IPX:		/* SNAP encapsulation */
	case ETH_P_ARP:
		hdr_len += 7;
		skb_push(skb, 7);
		skb->data[0] = 0;
		skb->data[1] = NLPID_SNAP;
		skb_copy_to_linear_data_offset(skb, 2, wanrouter_oui_ether,
					       sizeof(wanrouter_oui_ether));
		*((unsigned short*)&skb->data[5]) = htons(type);
		break;

	default:		/* Unknown packet type */
		printk(KERN_INFO
			"%s: unsupported Ethertype 0x%04X on interface %s!\n",
			wanrouter_modname, type, dev->name);
		hdr_len = -EINVAL;
	}
	return hdr_len;
}


/*
 *	Decapsulate packet.
 *
 *	Return:	Ethertype (in network order)
 *			0	unknown encapsulation
 *
 *	Notes:
 *	1. This function may be called on interrupt context.
 */


__be16 wanrouter_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	int cnt = skb->data[0] ? 0 : 1;	/* there may be a pad present */
	__be16 ethertype;

	switch (skb->data[cnt]) {
	case NLPID_IP:		/* IP datagramm */
		ethertype = htons(ETH_P_IP);
		cnt += 1;
		break;

	case NLPID_SNAP:	/* SNAP encapsulation */
		if (memcmp(&skb->data[cnt + 1], wanrouter_oui_ether,
			   sizeof(wanrouter_oui_ether))){
			printk(KERN_INFO
				"%s: unsupported SNAP OUI %02X-%02X-%02X "
				"on interface %s!\n", wanrouter_modname,
				skb->data[cnt+1], skb->data[cnt+2],
				skb->data[cnt+3], dev->name);
			return 0;
		}
		ethertype = *((__be16*)&skb->data[cnt+4]);
		cnt += 6;
		break;

	/* add other protocols, e.g. CLNP, ESIS, ISIS, if needed */

	default:
		printk(KERN_INFO
			"%s: unsupported NLPID 0x%02X on interface %s!\n",
			wanrouter_modname, skb->data[cnt], dev->name);
		return 0;
	}
	skb->protocol = ethertype;
	skb->pkt_type = PACKET_HOST;	/*	Physically point to point */
	skb_pull(skb, cnt);
	skb_reset_mac_header(skb);
	return ethertype;
}

#endif  /*  0  */

/*
 *	WAN device IOCTL.
 *	o find WAN device associated with this node
 *	o execute requested action or pass command to the device driver
 */

long wanrouter_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int err = 0;
	struct proc_dir_entry *dent;
	struct wan_device *wandev;
	void __user *data = (void __user *)arg;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if ((cmd >> 8) != ROUTER_IOCTL)
		return -EINVAL;

	dent = PDE(inode);
	if ((dent == NULL) || (dent->data == NULL))
		return -EINVAL;

	wandev = dent->data;
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;

	lock_kernel();
	switch (cmd) {
	case ROUTER_SETUP:
		err = wanrouter_device_setup(wandev, data);
		break;

	case ROUTER_DOWN:
		err = wanrouter_device_shutdown(wandev);
		break;

	case ROUTER_STAT:
		err = wanrouter_device_stat(wandev, data);
		break;

	case ROUTER_IFNEW:
		err = wanrouter_device_new_if(wandev, data);
		break;

	case ROUTER_IFDEL:
		err = wanrouter_device_del_if(wandev, data);
		break;

	case ROUTER_IFSTAT:
		break;

	default:
		if ((cmd >= ROUTER_USER) &&
		    (cmd <= ROUTER_USER_MAX) &&
		    wandev->ioctl)
			err = wandev->ioctl(wandev, cmd, arg);
		else err = -EINVAL;
	}
	unlock_kernel();
	return err;
}

/*
 *	WAN Driver IOCTL Handlers
 */

/*
 *	Setup WAN link device.
 *	o verify user address space
 *	o allocate kernel memory and copy configuration data to kernel space
 *	o if configuration data includes extension, copy it to kernel space too
 *	o call driver's setup() entry point
 */

static int wanrouter_device_setup(struct wan_device *wandev,
				  wandev_conf_t __user *u_conf)
{
	void *data = NULL;
	wandev_conf_t *conf;
	int err = -EINVAL;

	if (wandev->setup == NULL) {	/* Nothing to do ? */
		printk(KERN_INFO "%s: ERROR, No setup script: wandev->setup()\n",
				wandev->name);
		return 0;
	}

	conf = kmalloc(sizeof(wandev_conf_t), GFP_KERNEL);
	if (conf == NULL){
		printk(KERN_INFO "%s: ERROR, Failed to allocate kernel memory !\n",
				wandev->name);
		return -ENOBUFS;
	}

	if (copy_from_user(conf, u_conf, sizeof(wandev_conf_t))) {
		printk(KERN_INFO "%s: Failed to copy user config data to kernel space!\n",
				wandev->name);
		kfree(conf);
		return -EFAULT;
	}

	if (conf->magic != ROUTER_MAGIC) {
		kfree(conf);
		printk(KERN_INFO "%s: ERROR, Invalid MAGIC Number\n",
				wandev->name);
		return -EINVAL;
	}

	if (conf->data_size && conf->data) {
		if (conf->data_size > 128000) {
			printk(KERN_INFO
			    "%s: ERROR, Invalid firmware data size %i !\n",
					wandev->name, conf->data_size);
			kfree(conf);
			return -EINVAL;
		}

		data = vmalloc(conf->data_size);
		if (!data) {
			printk(KERN_INFO
				"%s: ERROR, Faild allocate kernel memory !\n",
				wandev->name);
			kfree(conf);
			return -ENOBUFS;
		}
		if (!copy_from_user(data, conf->data, conf->data_size)) {
			conf->data = data;
			err = wandev->setup(wandev, conf);
		} else {
			printk(KERN_INFO
			     "%s: ERROR, Faild to copy from user data !\n",
			       wandev->name);
			err = -EFAULT;
		}
		vfree(data);
	} else {
		printk(KERN_INFO
		    "%s: ERROR, No firmware found ! Firmware size = %i !\n",
				wandev->name, conf->data_size);
	}

	kfree(conf);
	return err;
}

/*
 *	Shutdown WAN device.
 *	o delete all not opened logical channels for this device
 *	o call driver's shutdown() entry point
 */

static int wanrouter_device_shutdown(struct wan_device *wandev)
{
	struct net_device *dev;
	int err=0;

	if (wandev->state == WAN_UNCONFIGURED)
		return 0;

	printk(KERN_INFO "\n%s: Shutting Down!\n",wandev->name);

	for (dev = wandev->dev; dev;) {
		err = wanrouter_delete_interface(wandev, dev->name);
		if (err)
			return err;
		/* The above function deallocates the current dev
		 * structure. Therefore, we cannot use dev->priv
		 * as the next element: wandev->dev points to the
		 * next element */
		dev = wandev->dev;
	}

	if (wandev->ndev)
		return -EBUSY;	/* there are opened interfaces  */

	if (wandev->shutdown)
		err=wandev->shutdown(wandev);

	return err;
}

/*
 *	Get WAN device status & statistics.
 */

static int wanrouter_device_stat(struct wan_device *wandev,
				 wandev_stat_t __user *u_stat)
{
	wandev_stat_t stat;

	memset(&stat, 0, sizeof(stat));

	/* Ask device driver to update device statistics */
	if ((wandev->state != WAN_UNCONFIGURED) && wandev->update)
		wandev->update(wandev);

	/* Fill out structure */
	stat.ndev  = wandev->ndev;
	stat.state = wandev->state;

	if (copy_to_user(u_stat, &stat, sizeof(stat)))
		return -EFAULT;

	return 0;
}

/*
 *	Create new WAN interface.
 *	o verify user address space
 *	o copy configuration data to kernel address space
 *	o allocate network interface data space
 *	o call driver's new_if() entry point
 *	o make sure there is no interface name conflict
 *	o register network interface
 */

static int wanrouter_device_new_if(struct wan_device *wandev,
				   wanif_conf_t __user *u_conf)
{
	wanif_conf_t *cnf;
	struct net_device *dev = NULL;
	int err;

	if ((wandev->state == WAN_UNCONFIGURED) || (wandev->new_if == NULL))
		return -ENODEV;

	cnf = kmalloc(sizeof(wanif_conf_t), GFP_KERNEL);
	if (!cnf)
		return -ENOBUFS;

	err = -EFAULT;
	if (copy_from_user(cnf, u_conf, sizeof(wanif_conf_t)))
		goto out;

	err = -EINVAL;
	if (cnf->magic != ROUTER_MAGIC)
		goto out;

	if (cnf->config_id == WANCONFIG_MPPP) {
		printk(KERN_INFO "%s: Wanpipe Mulit-Port PPP support has not been compiled in!\n",
				wandev->name);
		err = -EPROTONOSUPPORT;
		goto out;
	} else {
		dev = kzalloc(sizeof(struct net_device), GFP_KERNEL);
		err = -ENOBUFS;
		if (dev == NULL)
			goto out;
		err = wandev->new_if(wandev, dev, cnf);
	}

	if (!err) {
		/* Register network interface. This will invoke init()
		 * function supplied by the driver.  If device registered
		 * successfully, add it to the interface list.
		 */

		if (dev->name == NULL) {
			err = -EINVAL;
		} else {

			#ifdef WANDEBUG
			printk(KERN_INFO "%s: registering interface %s...\n",
				wanrouter_modname, dev->name);
			#endif

			err = register_netdev(dev);
			if (!err) {
				struct net_device *slave = NULL;
				unsigned long smp_flags=0;

				lock_adapter_irq(&wandev->lock, &smp_flags);

				if (wandev->dev == NULL) {
					wandev->dev = dev;
				} else {
					for (slave=wandev->dev;
					 *((struct net_device **)slave->priv);
				 slave = *((struct net_device **)slave->priv));

				     *((struct net_device **)slave->priv) = dev;
				}
				++wandev->ndev;

				unlock_adapter_irq(&wandev->lock, &smp_flags);
				err = 0;	/* done !!! */
				goto out;
			}
		}
		if (wandev->del_if)
			wandev->del_if(wandev, dev);
	}

	/* This code has moved from del_if() function */
	kfree(dev->priv);
	dev->priv = NULL;

	/* Sync PPP is disabled */
	if (cnf->config_id != WANCONFIG_MPPP)
		kfree(dev);
out:
	kfree(cnf);
	return err;
}


/*
 *	Delete WAN logical channel.
 *	 o verify user address space
 *	 o copy configuration data to kernel address space
 */

static int wanrouter_device_del_if(struct wan_device *wandev, char __user *u_name)
{
	char name[WAN_IFNAME_SZ + 1];
	int err = 0;

	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;

	memset(name, 0, sizeof(name));

	if (copy_from_user(name, u_name, WAN_IFNAME_SZ))
		return -EFAULT;

	err = wanrouter_delete_interface(wandev, name);
	if (err)
		return err;

	/* If last interface being deleted, shutdown card
	 * This helps with administration at leaf nodes
	 * (You can tell if the person at the other end of the phone
	 * has an interface configured) and avoids DoS vulnerabilities
	 * in binary driver files - this fixes a problem with the current
	 * Sangoma driver going into strange states when all the network
	 * interfaces are deleted and the link irrecoverably disconnected.
	 */

	if (!wandev->ndev && wandev->shutdown)
		err = wandev->shutdown(wandev);

	return err;
}

/*
 *	Miscellaneous Functions
 */

/*
 *	Find WAN device by name.
 *	Return pointer to the WAN device data space or NULL if device not found.
 */

static struct wan_device *wanrouter_find_device(char *name)
{
	struct wan_device *wandev;

	for (wandev = wanrouter_router_devlist;
	     wandev && strcmp(wandev->name, name);
		wandev = wandev->next);
	return wandev;
}

/*
 *	Delete WAN logical channel identified by its name.
 *	o find logical channel by its name
 *	o call driver's del_if() entry point
 *	o unregister network interface
 *	o unlink channel data space from linked list of channels
 *	o release channel data space
 *
 *	Return:	0		success
 *		-ENODEV		channel not found.
 *		-EBUSY		interface is open
 *
 *	Note: If (force != 0), then device will be destroyed even if interface
 *	associated with it is open. It's caller's responsibility to make
 *	sure that opened interfaces are not removed!
 */

static int wanrouter_delete_interface(struct wan_device *wandev, char *name)
{
	struct net_device *dev = NULL, *prev = NULL;
	unsigned long smp_flags=0;

	lock_adapter_irq(&wandev->lock, &smp_flags);
	dev = wandev->dev;
	prev = NULL;
	while (dev && strcmp(name, dev->name)) {
		struct net_device **slave = dev->priv;
		prev = dev;
		dev = *slave;
	}
	unlock_adapter_irq(&wandev->lock, &smp_flags);

	if (dev == NULL)
		return -ENODEV;	/* interface not found */

	if (netif_running(dev))
		return -EBUSY;	/* interface in use */

	if (wandev->del_if)
		wandev->del_if(wandev, dev);

	lock_adapter_irq(&wandev->lock, &smp_flags);
	if (prev) {
		struct net_device **prev_slave = prev->priv;
		struct net_device **slave = dev->priv;

		*prev_slave = *slave;
	} else {
		struct net_device **slave = dev->priv;
		wandev->dev = *slave;
	}
	--wandev->ndev;
	unlock_adapter_irq(&wandev->lock, &smp_flags);

	printk(KERN_INFO "%s: unregistering '%s'\n", wandev->name, dev->name);

	/* Due to new interface linking method using dev->priv,
	 * this code has moved from del_if() function.*/
	kfree(dev->priv);
	dev->priv=NULL;

	unregister_netdev(dev);

	free_netdev(dev);

	return 0;
}

static void lock_adapter_irq(spinlock_t *lock, unsigned long *smp_flags)
{
	spin_lock_irqsave(lock, *smp_flags);
}


static void unlock_adapter_irq(spinlock_t *lock, unsigned long *smp_flags)
{
	spin_unlock_irqrestore(lock, *smp_flags);
}

EXPORT_SYMBOL(register_wan_device);
EXPORT_SYMBOL(unregister_wan_device);

MODULE_LICENSE("GPL");

/*
 *	End
 */
