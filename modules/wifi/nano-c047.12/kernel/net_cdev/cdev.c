/* $Id: $ */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <linux/poll.h>

#define KDEBUG(_tr,_fmt, ...) do { if(_tr) printk("%s:%d " _fmt "\n", __func__,__LINE__, ##__VA_ARGS__); } while(0)
#define trace(_tr,_fmt, ...) do { if(_tr) printk("%s:%d " _fmt "\n", __func__,__LINE__, ##__VA_ARGS__); } while(0)

#define ERROR 1
#define TRACE 2
#define ENTRY TRACE
#define EXIT  TRACE

/****************************************************************************
 *   D E F I N E S
 ****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define IRQ_FLAGS (SA_INTERRUPT | SA_SHIRQ)
#else
#define IRQ_FLAGS (IRQF_DISABLED | IRQF_SHARED)
#endif

#ifndef DEBUG_TRACE
#define DEBUG_TRACE 0
#endif

struct cdev_status {
        unsigned int irq_count;
        unsigned int irq_handled;

        unsigned int tx_queue;

        unsigned int booting;
};

struct cdev_priv {
        dev_t devno;
        struct cdev cdev;
        struct semaphore sem;

        wait_queue_head_t rx_waitqueue;
        struct sk_buff_head rx_queue;

        /* not sure if this is need */
        /* it is implemented under the assumption that rx can not sleep/use rx_queue->sem */
        struct sk_buff_head rx_uh_queue;
        spinlock_t rx_uh_lock;

        wait_queue_head_t tx_waitqueue;
        struct sk_buff_head tx_queue;
        struct work_struct tx_ws;

        struct cdev_status status;
        unsigned long devst; /* status flags */
#define DEVST_OPEN 0
        struct class *device_class;
        struct device *device;

        int booting;
};


/****************************************************************************
 *  F U N C T I O N   D E C L A R A T I O N S
 ****************************************************************************/
static void flush_queues(struct cdev_priv *card);


/****************************************************************************
 *  F I L E   S C O P E   V A R I A B L E' S
 ****************************************************************************/

static struct workqueue_struct* cdev_wq;


/* open("/dev/cdev")
 *
 */
static int
cdev_open(struct inode *inode, struct file *filp)
{
        struct cdev_priv *priv = container_of(inode->i_cdev, struct cdev_priv, cdev);

        KDEBUG(ENTRY, "ENTER");

        if (test_and_set_bit(DEVST_OPEN, &priv->devst)) {
                KDEBUG(EXIT, "EXIT");
                return -EAGAIN;
        }
        filp->private_data = priv;

        KDEBUG(EXIT, "EXIT");
        return 0;
}

/* close("/dev/cdev")
 *
 */
static int
cdev_release(struct inode *inode, struct file *filp)
{
        struct cdev_priv *priv = filp->private_data;

        KDEBUG(ENTRY, "ENTER");

        clear_bit(DEVST_OPEN, &priv->devst);
        wait_event_interruptible(priv->rx_waitqueue, !skb_queue_empty(&priv->rx_queue));
        flush_queues(priv);

        KDEBUG(EXIT, "EXIT");

        return 0;
}

int
cdev_pkt_rx(void* priv_p, struct sk_buff *skb)
{
        struct cdev_priv *priv = priv_p;
        int len;

        KDEBUG(ENTRY, "ENTER");

        /* XXX: FIXME: queue incomming packets using a workqueue */
        if (down_interruptible(&priv->sem)) {
                KDEBUG(ERROR, "failed to down semaphore; dropping rx pkt of len %d", skb->len);
                kfree_skb(skb);
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        if (!test_bit(DEVST_OPEN, &priv->devst)) {
                up(&priv->sem);
                KDEBUG(ERROR, "failed to test bit devst_open; dropping rx pkt of len %d", skb->len);
                kfree_skb(skb);
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        len = skb_queue_len(&priv->rx_queue);
        if (len > 300) {
                up(&priv->sem);
                KDEBUG(ERROR, "rx_queue at %d dopping pkt", len);
                kfree_skb(skb);
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        skb_queue_tail(&priv->rx_queue, skb);
        len = skb_queue_len(&priv->rx_queue);
        up(&priv->sem);

        KDEBUG(3, "rx pkt %d from net, queue now at %d", skb->len, len);

        wake_up_interruptible(&priv->rx_waitqueue);

        KDEBUG(EXIT, "EXIT");
        return 0;
}

/* read("/dev/cdev")
 *
 * read ethernet packets from the rx queue
 */
static ssize_t
cdev_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
        struct cdev_priv *priv = filp->private_data;
        struct sk_buff *skb = NULL;

        size_t n;

        KDEBUG(ENTRY, "ENTER %d", count);

        if (down_interruptible(&priv->sem)) {
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        while ((skb = skb_peek(&priv->rx_queue)) == NULL) {
                int len;

                len = skb_queue_len(&priv->rx_queue);

                KDEBUG(ERROR, "pkt dequeued; now at %d", len);

                up(&priv->sem);
                if (filp->f_flags & O_NONBLOCK) {
                        KDEBUG(EXIT, "EXIT");
                        return -EAGAIN;
                }
                if (wait_event_interruptible(priv->rx_waitqueue,
                                             !skb_queue_empty(&priv->rx_queue))) {
                        KDEBUG(EXIT, "EXIT");
                        return -EAGAIN;
                }
                return -ERESTARTSYS;
                if (down_interruptible(&priv->sem)) {
                        KDEBUG(EXIT, "EXIT");
                        return -ERESTARTSYS;
                }
        }

        if (skb == NULL) {
                up(&priv->sem);
                /* should never happen; read trigged by poll */
                KDEBUG(EXIT, "EXIT");
                return -EFAULT;
        }

        n = skb->len;
        if (n > count)
                n = count;
        if (copy_to_user(buf, skb->data, n)) {
                KDEBUG(ERROR, "EFAULT copying out data");
                up(&priv->sem);
                KDEBUG(EXIT, "EXIT");
                return -EFAULT;
        }
        skb_pull(skb, n);
        if (skb->len == 0) {
                skb_unlink(skb, &priv->rx_queue);
        }
        priv->status.irq_handled = priv->status.irq_count -
                                   skb_queue_len(&priv->rx_queue);

        up(&priv->sem);
        KDEBUG(EXIT, "EXIT");
        return n;
}

void snull_rx(struct sk_buff *skb);

static void cdev_tx_wq(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
        void *device
#else
        struct work_struct *work
#endif
)
{
        struct cdev_priv* card;
        struct sk_buff *skb;

        KDEBUG(ENTRY, "%s:%d", __func__,__LINE__);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
        card = (struct cdev_priv *)device;
#else
        card = container_of(work, struct cdev_priv, tx_ws);
#endif

        while ((skb = skb_dequeue(&card->tx_queue)) != NULL) {
                //nano_tx(skb, card);
                //kfree_skb(skb);
                snull_rx(skb);
        }
}

/* write("/dev/cdev")
 *
 * send ethernet packet
 */
static ssize_t
cdev_write(struct file *filp,
           const char __user *buf,
           size_t count,
           loff_t *off)
{
        struct cdev_priv *card = filp->private_data;
        struct sk_buff *skb;
        unsigned char *data;
        int len;

        KDEBUG(ENTRY, "ENTER %d", count);

        if (down_interruptible(&card->sem)) {
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        len = skb_queue_len(&card->tx_queue);
        if (len > 300) {
                up(&card->sem);
                KDEBUG(ERROR, "tx queue at %d, dropping tx pkt", len);
                KDEBUG(EXIT, "EXIT");
                return -ENOMEM;
        }

        skb = alloc_skb(count + 32, GFP_KERNEL);

        if (skb == NULL) {
                up(&card->sem);
                KDEBUG(ERROR, "failed to allocate %lu bytes",
                       (unsigned long)count);
                KDEBUG(EXIT, "EXIT");
                return -ENOMEM;
        }

        skb_reserve(skb, 32);
        data = skb_put(skb, count);

        if (copy_from_user(data, buf, count)) {
                up(&card->sem);
                KDEBUG(ERROR, "EFAULT copying in data");
                kfree_skb(skb);
                KDEBUG(EXIT, "EXIT");
                return -EFAULT;
        }

        skb_queue_tail(&card->tx_queue, skb);
        up(&card->sem);
        queue_work(cdev_wq, &card->tx_ws);

        KDEBUG(TRACE, "write to cdev; tx queue now at %d", len+1);

        KDEBUG(EXIT, "EXIT");

        return count;
}


static void flush_queues(struct cdev_priv *card)
{
        struct sk_buff* skb;

        KDEBUG(ENTRY, "ENTER");

        while ((skb = skb_dequeue(&card->tx_queue)) != NULL) {
                dev_kfree_skb_any (skb);
        }
        while ((skb = skb_dequeue(&card->rx_queue)) != NULL) {
                dev_kfree_skb_any (skb);
        }
}

/* ioctl("/dev/cdev") */
static int
cdev_ioctl(struct inode *inode,
           struct file *filp,
           unsigned int cmd,
           unsigned long arg)
{
        struct cdev_priv *card = filp->private_data;

        KDEBUG(ENTRY, "ENTER");

        if (down_interruptible(&card->sem)) {
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        switch (cmd) {
#if 0
        case NANOPCI_IOCGSTATUS: {
                card->status.tx_queue = skb_queue_len(&card->tx_queue);
                card->status.booting = card->booting;
                if (copy_to_user((void __user*)arg,
                                 &card->status, sizeof(card->status))) {
                        KDEBUG(ERROR, "EFAULT copying out data");
                        up(&card->sem);
                        return -EFAULT;
                }
                break;
        }
        case NANOPCI_IOCFLUSH: {
                while (skb_queue_len(&card->tx_queue) > 0) {
                        up(&card->sem);
                        set_current_state(TASK_INTERRUPTIBLE);
                        if (schedule_timeout(HZ / 10) != 0) {
                                return -ERESTARTSYS;
                        }
                        if (down_interruptible(&card->sem))
                                return -ERESTARTSYS;
                }
                /* need some form of delay here, probably what we need to wait
                for is for the final dma to finish, plus some extra short
                time */
                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(HZ / 10);
                break;
        }
#endif
        default:
                up(&card->sem);
                KDEBUG(TRACE, "unknown ioctl %x", cmd);
                return -ENOTTY;
        }
        up(&card->sem);
        return 0;
}

/* poll("/dev/cdev") */
static unsigned int
cdev_poll(struct file *filp, poll_table *wait)
{
        struct cdev_priv *card = filp->private_data;
        unsigned int mask = 0;

        KDEBUG(ENTRY, "ENTER");

        if (down_interruptible(&card->sem)) {
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        /* XXX: FIXME: queue incomming packets using a workqueue */
        up(&card->sem);
        poll_wait(filp, &card->rx_waitqueue, wait);
        if (down_interruptible(&card->sem)) {
                KDEBUG(EXIT, "EXIT");
                return -ERESTARTSYS;
        }

        mask |= POLLOUT | POLLWRNORM;
        if (!skb_queue_empty(&card->rx_queue)) {
                mask |= POLLIN | POLLRDNORM;
        }
        up(&card->sem);
        KDEBUG(EXIT, "EXIT");
        return mask;
}

struct file_operations cdev_fileops = {
        .owner = THIS_MODULE,
        .open = cdev_open,
        .release = cdev_release,
        .read = cdev_read,
        .write = cdev_write,
        .ioctl = cdev_ioctl,
        .poll = cdev_poll,
};

struct cdev_priv cdev_priv;

int
cdev_create(void** priv_pp, char *dev_name)
{
        int ret;
        struct cdev_priv* priv;

        KDEBUG(ENTRY, "ENTER");

        /* TODO: kmalloc */
        priv = &cdev_priv;
        *priv_pp = priv;

        sema_init(&priv->sem, 1);

        init_waitqueue_head(&priv->rx_waitqueue);
        init_waitqueue_head(&priv->tx_waitqueue);

        skb_queue_head_init(&priv->rx_queue);
        skb_queue_head_init(&priv->tx_queue);

        //cdev_wq = create_workqueue("nano_net");
        cdev_wq = create_singlethread_workqueue ("nanonet");

        INIT_WORK (&priv->tx_ws, cdev_tx_wq
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
                   ,(void*)priv
#endif
                  );

        ret = alloc_chrdev_region(&priv->devno, 0, 1, dev_name);
        if (ret != 0) {
                KDEBUG(ERROR, "failed to allocate cdev region: %d", ret);
                KDEBUG(EXIT, "EXIT");
                return ret;
        }
        cdev_init(&priv->cdev, &cdev_fileops);
        priv->cdev.owner = THIS_MODULE;
        ret = cdev_add(&priv->cdev, priv->devno, 1);
        if (ret) {
                KDEBUG(ERROR, "failed to add device: %d", ret);
                unregister_chrdev_region(priv->devno, 1);
                priv->devno = 0;
                KDEBUG(EXIT, "EXIT");
                return ret;
        }

        priv->device_class = class_create(THIS_MODULE, "nanoradio");
        if (priv->device_class == NULL) {
                cdev_del(&priv->cdev);
                unregister_chrdev_region(priv->devno, 1);
                priv->devno = 0;
                KDEBUG(EXIT, "EXIT");
                return -EINVAL;
        }
        priv->device = device_create(priv->device_class, NULL,
                                     priv->devno, "%s", dev_name);
        if (priv->device == NULL) {
                class_destroy(priv->device_class);
                cdev_del(&priv->cdev);
                unregister_chrdev_region(priv->devno, 1);
                priv->devno = 0;
                KDEBUG(EXIT, "EXIT");
                return -EINVAL;
        }

        KDEBUG(EXIT, "EXIT");
        return 0;
}

void
cdev_destroy(void *data)
{
        struct cdev_priv* priv = (struct cdev_priv*)data;

        KDEBUG(ENTRY, "ENTER");

        flush_workqueue(cdev_wq);

        device_destroy(priv->device_class, priv->devno);
        class_destroy(priv->device_class);

        cdev_del(&priv->cdev);
        unregister_chrdev_region(priv->devno, 1);
        priv->devno = 0;

        KDEBUG(EXIT, "EXIT");
        destroy_workqueue(cdev_wq);
}

