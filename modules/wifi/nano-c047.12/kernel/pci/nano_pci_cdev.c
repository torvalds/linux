/* $Id: nano_pci_cdev.c 18446 2011-03-22 15:42:28Z joda $ */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>

#include <linux/poll.h>
#include "nano_pci_var.h"

#define NANOPCI_CDEV
#include "nano_pci.c"


unsigned int nrx_debug = ~0;

void ns_net_rx(struct sk_buff *skb, struct net_device *dev)
{
   struct nano_pci_card *card = (struct nano_pci_card *)dev;

   if(down_interruptible(&card->sem)) {
      kfree_skb(skb);
      return;
   }

   if(skb_queue_len(&card->rx_queue)>300) {
      up(&card->sem);
      KDEBUG(ERROR, "more then 300 pkt's in rx queue; dropping frame of size %d",skb->len);
      kfree_skb(skb);
      return;
   }

   if(!test_bit(DEVST_OPEN, &card->devst)) {
      kfree_skb(skb);
      up(&card->sem);
      return;
   }

   skb_queue_tail(&card->rx_queue, skb);
   up(&card->sem);
   card->status.irq_count++;
   wake_up_interruptible(&card->rx_waitqueue);
   return;
}

static int
nanopci_open(struct inode *inode, struct file *filp)
{
   struct nano_pci_card *card = container_of(inode->i_cdev, struct nano_pci_card, cdev);

   if(test_and_set_bit(DEVST_OPEN, &card->devst))
      return -EAGAIN;
   filp->private_data = card;
   
   return 0;
}

static int
nanopci_release(struct inode *inode, struct file *filp)
{
   struct nano_pci_card *card = filp->private_data;

   clear_bit(DEVST_OPEN, &card->devst);
   return 0;
}
   
static ssize_t
nanopci_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
   struct nano_pci_card *card = filp->private_data;
   struct sk_buff *skb;

   size_t n;

   if(down_interruptible(&card->sem))
      return -ERESTARTSYS;

   while((skb = skb_peek(&card->rx_queue)) == NULL) {
      up(&card->sem);
      if(filp->f_flags & O_NONBLOCK)
	 return -EAGAIN;
      if(wait_event_interruptible(card->rx_waitqueue, 
				  !skb_queue_empty(&card->rx_queue)))
	 return -ERESTARTSYS;
      if(down_interruptible(&card->sem))
	 return -ERESTARTSYS;
   }

   n = skb->len;
   if(n > count)
      n = count;
   if(copy_to_user(buf, skb->data, n)) {
      KDEBUG(ERROR, "EFAULT copying out data");
      up(&card->sem);
      return -EFAULT;
   }
   skb_pull(skb, n);
   if(skb->len == 0) {
      skb_unlink(skb, &card->rx_queue);
      dev_kfree_skb(skb);
   }
   card->status.irq_handled = card->status.irq_count - 
     skb_queue_len(&card->rx_queue);

   up(&card->sem);
   return n;
}

static void nano_pci_tx_wq(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
			   void *device
#else
			   struct work_struct *work
#endif
			   )
{
    struct nano_pci_card* card;
    struct sk_buff *skb;	

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    card = (struct nano_pci_card *)device;
#else
    card = container_of(work, struct nano_pci_card, tx_ws);
#endif
    while ((skb = skb_dequeue(&card->tx_queue)) != NULL) {
       nano_tx(skb, card);
    }
}

static ssize_t
nanopci_write(struct file *filp, 
	      const char __user *buf, 
	      size_t count, 
	      loff_t *off)
{
   struct nano_pci_card *card = filp->private_data;
   struct sk_buff *skb;
   unsigned char *data;

   if(down_interruptible(&card->sem))
      return -ERESTARTSYS;

   skb = alloc_skb(count + 32, GFP_KERNEL);

   if(skb == NULL) {
      KDEBUG(ERROR, "failed to allocate %lu bytes", 
	     (unsigned long)count);
      up(&card->sem);
      return -ENOMEM;
   }

   skb_reserve(skb, 32);
   data = skb_put(skb, count);

   if(copy_from_user(data, buf, count)) {
      KDEBUG(ERROR, "EFAULT copying in data");
      kfree_skb(skb);
      up(&card->sem);
      return -EFAULT;
   }

   skb_queue_tail(&card->tx_queue, skb);
   up(&card->sem);
   queue_work (nanopci_wq, &card->tx_ws);
   return count;
}
   

static void boot_start(struct nano_pci_card* card);
static void boot_stop(struct nano_pci_card* card);

static void flush_queues(struct nano_pci_card *card)
{
   struct sk_buff* skb;

   while((skb = skb_dequeue(&card->tx_queue)) != NULL) {
      dev_kfree_skb_any (skb);
   }
   while((skb = skb_dequeue(&card->rx_queue)) != NULL) {
      dev_kfree_skb_any (skb);
   }
}

static int
nanopci_ioctl_body(struct nano_pci_card *card,
                   unsigned int cmd, 
                   unsigned long arg)
{
   int retval = 0;
   
   if(down_interruptible(&card->sem))
      return -ERESTARTSYS;

   switch(cmd) {
   case NANOPCI_IOCGSTATUS: {
      card->status.tx_queue = skb_queue_len(&card->tx_queue);
      card->status.booting = card->booting;
      if(copy_to_user((void __user*)arg, 
		      &card->status, sizeof(card->status))) {
	 KDEBUG(ERROR, "EFAULT copying out data");
         retval = -EFAULT;
      }
      break;
   }
   case NANOPCI_IOCRESET: {
      reset_device(card);
      mdelay(500);
      break;
   }
   case NANOPCI_IOCSDIO: {
      init_sdio(card);
      break;
   }
   case NANOPCI_IOCFLUSH: {
      while(skb_queue_len(&card->tx_queue) > 0) {
	 up(&card->sem);
	 set_current_state(TASK_INTERRUPTIBLE);
	 if(schedule_timeout(HZ / 10) != 0) {
	    return -ERESTARTSYS;
	 }
	 if(down_interruptible(&card->sem))
	    return -ERESTARTSYS;
      }
      /* need some form of delay here, probably what we need to wait
	 for is for the final dma to finish, plus some extra short
	 time */
      set_current_state(TASK_INTERRUPTIBLE);
      schedule_timeout(HZ / 10);
      break;
   }
   case NANOPCI_IOCBOOTENABLE: {
      if(card->booting == 0) {
	 flush_queues(card);
	 sdio_control(NANONET_BOOT, NANONET_BOOT_ENABLE, card);
      } else {
         retval = -EFAULT;
      }
      
      break;
   }
   case NANOPCI_IOCBOOTDISABLE: {
      if(card->booting == 1) {
	 flush_queues(card);
	 sdio_control(NANONET_BOOT, NANONET_BOOT_DISABLE, card);
      } else {
         retval = -EFAULT;
      }
      break;
   }
   case NANOPCI_IOCSLEEP: {
      sdio_control(NANONET_SLEEP, NANONET_SLEEP_ON, card);
      break;
   }
   case NANOPCI_IOCWAKEUP: {
      sdio_control(NANONET_SLEEP, NANONET_SLEEP_OFF, card);
      break;
   }
   case NANOPCI_IOCGATTN: {
      int attn = create_param.host_attention;
      if(copy_to_user((void __user*)arg, &attn, sizeof(attn))) {
	 KDEBUG(ERROR, "EFAULT copying out data");
         retval = -EFAULT;
      }
      break;
   }
   default:
      KDEBUG(TRACE, "unknown ioctl %x", cmd);
      retval = -EFAULT;
      break;
   }
   up(&card->sem);
   return retval;
}

#ifdef HAVE_UNLOCKED_IOCTL
static long
nanopci_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
   return (long)nanopci_ioctl_body(filp->private_data, cmd, arg);
}
#else
static int
nanopci_ioctl(struct inode *inode, 
	      struct file *filp, 
	      unsigned int cmd, 
	      unsigned long arg)
{
   return nanopci_ioctl_body(filp->private_data, cmd, arg);
}
#endif /* HAVE_UNLOCKED_IOCTL */

static unsigned int
nanopci_poll(struct file *filp, poll_table *wait)
{
   struct nano_pci_card *card = filp->private_data;
   unsigned int mask = 0;

   if(down_interruptible(&card->sem))
      return -ERESTARTSYS;

   poll_wait(filp, &card->rx_waitqueue, wait);

   mask |= POLLOUT | POLLWRNORM;
   if(!skb_queue_empty(&card->rx_queue)) {
      mask |= POLLIN | POLLRDNORM;
   }
   up(&card->sem);
   return mask;
}

struct file_operations nanopci_fileops = {
   .owner = THIS_MODULE,
   .open = nanopci_open,
   .release = nanopci_release,
   .read = nanopci_read,
   .write = nanopci_write,
#ifdef HAVE_UNLOCKED_IOCTL
   .unlocked_ioctl = nanopci_unlocked_ioctl,
#else
   .ioctl = nanopci_ioctl,
#endif
   .poll = nanopci_poll,
};

static int
nanopci_cdev_init(struct nano_pci_card* card)
{
   int ret;

   sema_init(&card->sem, 1);

   init_waitqueue_head(&card->rx_waitqueue);
   init_waitqueue_head(&card->tx_waitqueue);

   skb_queue_head_init(&card->rx_queue);
   skb_queue_head_init(&card->tx_queue);

    INIT_WORK (&card->tx_ws, nano_pci_tx_wq 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	       ,(void*)card
#endif
	       );

   ret = alloc_chrdev_region(&card->devno, 0, 1, "nanopci");
   if(ret != 0) {
      KDEBUG(ERROR, "failed to allocate cdev region: %d", ret);
      return ret;
   }
   cdev_init(&card->cdev, &nanopci_fileops);
   card->cdev.owner = THIS_MODULE;
   ret = cdev_add(&card->cdev, card->devno, 1);
   if(ret) {
      KDEBUG(ERROR, "failed to add device: %d", ret);
      unregister_chrdev_region(card->devno, 1);
      card->devno = 0;
      return ret;
   }
   
   card->device_class = class_create(THIS_MODULE, "nanoradio");
   if(card->device_class == NULL) {
      cdev_del(&card->cdev);
      unregister_chrdev_region(card->devno, 1);
      card->devno = 0;
      return -EINVAL;
   }
   card->device = device_create(card->device_class, NULL,
				card->devno, "%s", "nanopci");
   if(card->device == NULL) {
      class_destroy(card->device_class);
      cdev_del(&card->cdev);
      unregister_chrdev_region(card->devno, 1);
      card->devno = 0;
      return -EINVAL;
   }

   return 0;
}

struct net_device *
nanonet_create(struct device *pdev, void *data, struct nanonet_create_param *param)
{
   struct nano_pci_card* card = data;
   if(nanopci_cdev_init(card))
      return NULL;
   return (struct net_device*)card; /* XXX */
}

static int
nanopci_cdev_release(struct nano_pci_card* card)
{
   device_destroy(card->device_class, card->devno);
   class_destroy(card->device_class);

   cdev_del(&card->cdev);
   unregister_chrdev_region(card->devno, 1);
   card->devno = 0;
   return 0;
}

void
nanonet_destroy(struct net_device *dev)
{
   nanopci_cdev_release((struct nano_pci_card*)dev);
}

