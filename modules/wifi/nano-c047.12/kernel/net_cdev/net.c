/*
 * net.c -- Simple Network Utility
 *
 * create /dev/nano_net_cdev to interface with the linux network stack
 *
 * $Id: $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/sched.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/in.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/skbuff.h>

#include <linux/in6.h>
#include <asm/checksum.h>

#define ENTRY 2
#define EXIT  2
#define TRACE 3
#define ERROR 1

#define trace(_tr,_fmt, ...) do { if(_tr) printk( KERN_DEBUG "%s:%d " _fmt "\n", __func__,__LINE__, ##__VA_ARGS__); } while(0)

void *cdev_priv_p = NULL;
int  cdev_create(void** priv_pp, char *dev_name);
int  cdev_pkt_rx(void* priv_p, struct sk_buff *skb);
void cdev_destroy(void *priv);
static void nano_net_tx_timeout(struct net_device *dev);


MODULE_AUTHOR("Mikael Wikstrom");
MODULE_LICENSE("Dual BSD/GPL");


static int timeout = 10; /* jiffies */
module_param(timeout, int, 0);


struct nano_net_priv {
        struct net_device_stats stats;
        int status;
        int rx_int_enabled;
        int tx_packetlen;
        u8 *tx_packetdata;
        struct sk_buff *skb;
        spinlock_t lock;
        struct net_device *dev;
};


/*
 * The devices
 */
struct net_device *nano_net_dev;


/*
 * ifconfig nano_net0 up
 */
int nano_net_open(struct net_device *dev)
{
        unsigned char mac_addr[ETH_ALEN] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50};

        trace(ENTRY,"ENTER");

        memcpy(dev->dev_addr, mac_addr, ETH_ALEN);
        netif_start_queue(dev);

        trace(EXIT,"EXIT");
        return 0;
}

/*
 * ifconfig nano_net0 down
 */
int nano_net_release(struct net_device *dev)
{
        trace(ENTRY,"ENTER");

        netif_stop_queue(dev); /* can't transmit any more */

        trace(EXIT,"EXIT");
        return 0;
}

/*
 * Configuration changes (passed on by ifconfig)
 */
int nano_net_config(struct net_device *dev, struct ifmap *map)
{
        trace(ENTRY,"ENTER");

        if (dev->flags & IFF_UP) { /* can't act on a running interface */
                trace(EXIT,"EXIT");
                return -EBUSY;
        }

        /* Don't allow changing the I/O address */
        if (map->base_addr != dev->base_addr) {
                printk(KERN_WARNING "nano_net: Can't change I/O address\n");
                trace(EXIT,"EXIT");
                return -EOPNOTSUPP;
        }

        /* Allow changing the IRQ */
        if (map->irq != dev->irq) {
                dev->irq = map->irq;
        }

        /* ignore other fields */

        trace(EXIT,"EXIT");
        return 0;
}

/*
 * Receive a packet from ethernet and pass on to the network stack:
 *
 * retrieve, encapsulate and pass over to upper levels
 */
void nano_net_rx(struct sk_buff *skb)
{
        trace(TRACE,"skb: %p", skb);

        skb->dev = nano_net_dev;
        skb->protocol = eth_type_trans(skb, nano_net_dev);
        skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
        //priv->stats.rx_packets++;
        //priv->stats.rx_bytes += pkt->datalen;
        trace(TRACE,"netif_rx(%p)", skb);
        netif_rx(skb);
        trace(EXIT,"EXIT");
}


/*
 * Transmit a packet
 *
 * the kernel requests to send a packet onto the driver medium
 * protected by a spinlock
 *
 */
int nano_net_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
        /* our simple device can not fail */
        cdev_pkt_rx(cdev_priv_p, skb);
        return 0;
}


/*
 * Deal with a transmit timeout.
 */
void nano_net_tx_timeout(struct net_device *dev)
{
        struct nano_net_priv *priv = netdev_priv(dev);

        trace(ENTRY,"ENTER");
        trace(ERROR,"Transmit timeout at %ld, latency %ld\n", jiffies,
              jiffies - dev->trans_start);
        priv->stats.tx_errors++;
        netif_wake_queue(dev);
        trace(EXIT,"EXIT");
        return;
}



/*
 * Ioctl commands
 */
int nano_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
        trace(ENTRY,"ENTER");
        trace(EXIT,"EXIT");
        return 0;
}


/*
 * Return statistics to the caller
 */
struct net_device_stats *nano_net_stats(struct net_device *dev) {
        struct nano_net_priv *priv = netdev_priv(dev);
        trace(ENTRY,"ENTER");

        trace(EXIT,"EXIT");
        return &priv->stats;
}


/*
 * This function is called to fill up an eth header, since arp is not
 * available on the interface
 */
int nano_net_rebuild_header(struct sk_buff *skb)
{
        struct ethhdr *eth = (struct ethhdr *) skb->data;
        struct net_device *dev = skb->dev;

        trace(ENTRY,"ENTER");

        memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
        memcpy(eth->h_dest, dev->dev_addr, dev->addr_len);
        eth->h_dest[ETH_ALEN-1]   ^= 0x01;   /* dest is us xor 1 */

        trace(EXIT,"EXIT");
        return 0;
}


int nano_net_header(struct sk_buff *skb, struct net_device *dev,
                    unsigned short type, const void *daddr, const void *saddr,
                    unsigned int len)
{
        trace(ENTRY,"ENTER");

        trace(EXIT,"EXIT");

        return (dev->hard_header_len);
}


/*
 * The "change_mtu" method is usually not needed.
 * If you need it, it must be like this.
 */
int nano_net_change_mtu(struct net_device *dev, int new_mtu)
{
        unsigned long flags;
        struct nano_net_priv *priv = netdev_priv(dev);
        spinlock_t *lock = &priv->lock;

        trace(ENTRY,"ENTER");

        /* check ranges */
        if ((new_mtu < 68) || (new_mtu > 1500)) {
                trace(EXIT,"EXIT");
                return -EINVAL;
        }
        /*
         * Do anything you need, and the accept the value
         */
        spin_lock_irqsave(lock, flags);
        dev->mtu = new_mtu;
        spin_unlock_irqrestore(lock, flags);

        trace(EXIT,"EXIT");
        return 0; /* success */
}

static const struct header_ops nano_net_header_ops = {
        .create  = nano_net_header,
        .rebuild = nano_net_rebuild_header,
        .cache   = NULL,  /* disable caching */
};

/*
 * The init function (sometimes called probe).
 * It is invoked by register_netdev()
 */
void nano_net_init(struct net_device *dev)
{
        unsigned char mac_addr[ETH_ALEN] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50};
        struct nano_net_priv *priv;

        /*
         * Make the usual checks: check_region(), probe irq, ...  -ENODEV
         * should be returned if no device found.  No resource should be
         * grabbed: this is done on open().
         */
        trace(ENTRY,"ENTER");

        /*
         * Then, assign other fields in dev, using ether_setup() and some
         * hand assignments
         */
        ether_setup(dev); /* assign some of the fields */

        dev->open            = nano_net_open;
        dev->stop            = nano_net_release;
        dev->set_config      = nano_net_config;
        dev->hard_start_xmit = nano_net_hard_start_xmit; // tx
        dev->do_ioctl        = nano_net_ioctl;
        dev->get_stats       = nano_net_stats;
        dev->change_mtu      = nano_net_change_mtu;
        dev->header_ops      = &nano_net_header_ops;
        dev->tx_timeout      = nano_net_tx_timeout;
        dev->watchdog_timeo = timeout;
        //dev->features        |= NETIF_F_NO_CSUM;
        /*
         * Then, initialize the priv field. This encloses the statistics
         * and a few private fields.
         */
        memcpy(dev->dev_addr, mac_addr, ETH_ALEN);

        priv = netdev_priv(dev);
        memset(priv, 0, sizeof(struct nano_net_priv));
        priv->dev = dev;
        /* The last parameter above is the NAPI "weight". */
        spin_lock_init(&priv->lock);
}


void nano_net_cleanup(void)
{
        trace(ENTRY,"ENTER");

        if (nano_net_dev) {
                unregister_netdev(nano_net_dev);
                free_netdev(nano_net_dev);
        }

        if (cdev_priv_p) {
                cdev_destroy(cdev_priv_p);
                cdev_priv_p = NULL;
        }
        trace(EXIT,"EXIT");
        return;
}


int nano_net_init_module(void)
{
        int result, ret = -ENOMEM;

        trace(ENTRY,"ENTER");

        /* Allocate the devices */
        nano_net_dev = alloc_netdev(sizeof(struct nano_net_priv), "nano_net%d",
                                    nano_net_init);
        if (nano_net_dev == NULL)
                goto out;

        ret = -ENODEV;
        if ((result = register_netdev(nano_net_dev)))
                printk("nano_net: error %i registering device \"%s\"\n",
                       result, nano_net_dev->name);
        else
                ret = 0;

        if (cdev_create(&cdev_priv_p, "nano_net_cdev")) {
                ret = -ENOMEM;
                goto out;
        }
out:
        if (ret)
                nano_net_cleanup();
        trace(EXIT,"EXIT");
        return ret;
}


module_init(nano_net_init_module);
module_exit(nano_net_cleanup);

