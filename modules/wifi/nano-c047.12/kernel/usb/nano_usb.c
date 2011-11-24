/*
 * NRX600 USB Driver
 *
 * Copyright (C) 2009 Nanoradio AB
 */
/* $Id: nano_usb.c 19321 2011-05-31 12:45:58Z peva $ */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <nanonet.h>
#include <nanoutil.h>
#include <nanoparam.h>

#define MAX_PACKET_SIZE 1600
#define RESET_POWER_CYCLE
#undef NANOUSB_DEBUG
#undef NANOUSB_LOOPBACK
#define NANOUSB_SDIO4BIT

/* Nanoradio specific SDIO control registers */
#define CCCR_INTACK     0xF0 /* Bit 1, Interrupt pending, write '1' to clear */
#define CCCR_WAKEUP     0xF1 /* Bit 0, Wakeup, read/write */
#define CCCR_NOCLK_INT  0xF2 /* Bit 0, Assert interrupt without SDIO CLK, read/write */
#define CCCR_HISPEED_EN 0xF3 /* Bit 0, Hi-Speed Mode Enable, read/write */
#define CCCR_FIFO_STAT  0xF4 /* Bit 0, Fifo Underrun ; Bit 1, Fifo Overrun, read/write */
#define CCCR_RESET      0xF5 /* Bit 0, Reset all ; Bit 1, Reset all but SDIO */

/* Structure to hold all of our device specific stuff */
struct nrxdev {
   spinlock_t      lock;
   unsigned long         irq_flags;
   struct usb_device *udev;          /* the usb device for this device */
   struct usb_interface *interface;     /* the interface for this device */
   void                 *netdev;
   struct usb_anchor     submitted;     /* in case we need to retract our submissions */
   struct urb           *cmd52_async_urb;
   unsigned              rcvpipe;       /* Bulk IN pipe */
   unsigned              sndpipe;       /* Bulk OUT pipe */
   unsigned              vendor;
   unsigned              mask_id;   
   unsigned              fpga_ver;
   unsigned              rca;           /* Relative Card Address */
   uint8_t               cmdbuf[8];
   uint8_t               rspbuf[8];
   int          errors;        /* the last request tanked */
   unsigned              clkdiv;
   unsigned              nr_timeout;
   struct kref     kref;
   struct work_struct    rx_work;
   struct sk_buff_head   rx_queue;
   unsigned int          rx_submit;
   unsigned int          rx_complete;
};

#define LOCKDEV(D)   spin_lock_irqsave(&(D)->lock, (D)->irq_flags)
#define UNLOCKDEV(D) spin_unlock_irqrestore(&(D)->lock, (D)->irq_flags)

static void cmd52_async_callback(struct urb *urb);
static void bulk_in_callback(struct urb *urb);
static void bulk_out_callback(struct urb *urb);
static void stop_rx(struct nrxdev *dev);

#define kref_to_nrxdev(d) container_of(d, struct nrxdev, kref)

#define SDIO_4BIT_DISABLE   0xC0
#define SDIO_4BIT_ENABLE    0xC1
#define SDIO_HS_DISABLE     0xC2
#define SDIO_HS_ENABLE      0xC3
#define SDIO_AUTORX_DISABLE 0xC4
#define SDIO_AUTORX_ENABLE  0xC5
#define SDIO_VDD_DISABLE    0xC6
#define SDIO_VDD_ENABLE     0xC7
#define SDIO_SEND_VER       0xCF
#define SDIO_CLKDIV(_x)    (0xD0 | (_x))

#define SDIO_CLKDIV_DEFAULT 5

#ifdef NANOUSB_LOOPBACK
#include "de_trace.h"
#include "loopback.c"
#endif

/*
 * Feature control for the SDIO host controller. A single byte in the range
 * 0xC0..0xDF is interpreted as a command that controls a feature in the
 * controller. No (uplink) response is sent from the controller for these
 * commands, with the once exception in 0xCF that replies with version info.
 *
 * As this funcion uses the synchronous usb_bulk_msg(), it may only be called
 * in process context.
 */
static int
sdio_ctrl(struct nrxdev *dev, unsigned cmd)
{
   int retval;
   int n;

   dev->cmdbuf[0] = cmd;
   retval = usb_bulk_msg(dev->udev, dev->sndpipe, dev->cmdbuf, 1, &n, 100);
   KDEBUG(TRANSPORT, "sdio_ctrl(%02x) = %d", cmd, retval);
   return retval;
}

/*
 * Send a single SDIO command specified by the 'cmd' and 'arg' parameters.
 *
 * As this funcion uses the synchronous usb_bulk_msg(), it may only be called
 * in process context.
 */
static int
sdio_cmd(struct nrxdev *dev, unsigned cmd, unsigned arg)
{
   int retval;
   int n;

   dev->cmdbuf[0] = cmd;
   dev->cmdbuf[1] = (arg >> 24) & 0xFF;
   dev->cmdbuf[2] = (arg >> 16) & 0xFF;
   dev->cmdbuf[3] = (arg >>  8) & 0xFF;
   dev->cmdbuf[4] = (arg >>  0) & 0xFF;

#ifdef NANOUSB_DEBUG
   printk("sdio_cmd(%d): cmdbuf = %02x %02x %02x %02x %02x\n",
          cmd,
          dev->cmdbuf[0],
          dev->cmdbuf[1],
          dev->cmdbuf[2],
          dev->cmdbuf[3],
          dev->cmdbuf[4]);
#endif

   /* Send SDIO command */
   retval = usb_bulk_msg(dev->udev, dev->sndpipe, dev->cmdbuf, 5, &n, 10000);
   if (retval < 0) {
      KDEBUG(TRANSPORT, "CMD%d failed, error %d", cmd, retval);
   } else {
      /* Receive SDIO response */
      retval = usb_bulk_msg(dev->udev, dev->rcvpipe, dev->rspbuf, 5, &n, 10000);
#ifdef NANOUSB_DEBUG
      printk("sdio_cmd(%d): rspbuf = %02x %02x %02x %02x %02x\n",
             cmd,
             dev->rspbuf[0],
             dev->rspbuf[1],
             dev->rspbuf[2],
             dev->rspbuf[3],
             dev->rspbuf[4]);
#endif
      if (retval < 0) {
         KDEBUG(TRANSPORT, "No response on CMD%d, error %d", cmd, retval);
      } else if (dev->rspbuf[0] == 0xFF) {
         KDEBUG(TRANSPORT, "Timeout on CMD%d, n=%d", cmd, n);
         retval = -ETIMEDOUT;
      } else if (dev->rspbuf[0] != 0xC0) {
         KDEBUG(TRANSPORT, "Invalid response on CMD%d, buf[0]=%#x", cmd, dev->rspbuf[0]);
         retval = -EINVAL;
      }
   }

   return retval;
}

/*
 * Send SDIO_RW_DIRECT command to write to specified addr.
 */
static unsigned
cmd52_write(struct nrxdev *dev, unsigned addr, unsigned value, int *err)
{
   *err = sdio_cmd(dev, 52, 0x88000000 | (addr << 9) | (value & 0xff));
   if (*err == 0)
   {
      value = dev->rspbuf[4];
   }
   return value;
}

/*
 * Send SDIO_RW_DIRECT command to read from specified addr.
 */
static unsigned
cmd52_read(struct nrxdev *dev, unsigned addr, int *err)
{
   unsigned value = 0;

   *err = sdio_cmd(dev, 52, 0x08000000 | (addr << 9));
   if (*err == 0)
   {
      value = dev->rspbuf[4];
   }

   return value;
}

/*
 * Issue a asynchronous CMD52.
 */
static int
cmd52_write_async(struct nrxdev *dev, unsigned addr, unsigned value)
{
   struct urb *urb;
   int err = -EAGAIN;

   LOCKDEV(dev);
   urb = dev->cmd52_async_urb;
   dev->cmd52_async_urb = NULL;
   UNLOCKDEV(dev);

   if (urb != NULL) {
      unsigned arg = 0x88000000 | (addr << 9) | (value & 0xff);

      dev->cmdbuf[0] = SD_IO_RW_DIRECT;
      dev->cmdbuf[1] = (arg >> 24) & 0xFF;
      dev->cmdbuf[2] = (arg >> 16) & 0xFF;
      dev->cmdbuf[3] = (arg >>  8) & 0xFF;
      dev->cmdbuf[4] = (arg >>  0) & 0xFF;

      usb_fill_bulk_urb(urb, dev->udev, dev->sndpipe,
                        dev->cmdbuf, 5,
                        cmd52_async_callback, dev);

      usb_anchor_urb(urb, &dev->submitted);

      KDEBUG(TRANSPORT, "wakeup=%d", value);

      if ((err = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
         KDEBUG(TRANSPORT, "Failed to submit CMD52, error %d", err);
         usb_unanchor_urb(urb);
      }
   } else {
      KDEBUG(TRANSPORT, "Already in progress");
   }

   return err;
}

/*
 * Asynchronous CMD52 completion function.
 */
static void
cmd52_async_callback(struct urb *urb)
{
   struct nrxdev *dev = (struct nrxdev *)urb->context;

   KDEBUG(TRANSPORT, "cmd52_async_callback: status = %d, len = %u", urb->status, urb->actual_length);
   LOCKDEV(dev);
   dev->cmd52_async_urb = urb;
   UNLOCKDEV(dev);
}

static int
identify(struct nrxdev *dev)
{
   unsigned cis_ptr;
   int err = 0;

   cis_ptr = cmd52_read(dev, 0x0b, &err);
   cis_ptr = (cis_ptr << 8) | cmd52_read(dev, 0x0a, &err);
   cis_ptr = (cis_ptr << 8) | cmd52_read(dev, 0x09, &err);
   /*printk("Identify cic_ptr : %x\n", cis_ptr);*/

   dev->vendor  = cmd52_read(dev, cis_ptr+2, &err) | (cmd52_read(dev, cis_ptr+3, &err) << 8);
   dev->mask_id = cmd52_read(dev, cis_ptr+4, &err) | (cmd52_read(dev, cis_ptr+5, &err) << 8);

   return err;
}

static int
sdio_init(struct nrxdev *dev)
{
   int err;

   /* Send CMD3 to receive relative card address */
   if ((err = sdio_cmd(dev, MMC_SET_RELATIVE_ADDR, 0)) == 0) {
      /* Extract RCA from response */
      dev->rca = (dev->rspbuf[1] << 8) | dev->rspbuf[2];
      /* Select card */
      err = sdio_cmd(dev, MMC_SELECT_CARD, dev->rca << 16); 
   }

   return err;
}

static int
nrx_reset(struct nrxdev *dev)
{
   int err = 0;

#ifdef RESET_POWER_CYCLE
   sdio_ctrl(dev, SDIO_VDD_DISABLE);
   msleep(100);
   sdio_ctrl(dev, SDIO_VDD_ENABLE);
#else
   if (((dev->mask_id >> 8) & 0xFF) == 0x20) {
      /* NRX600: Reset bits in CCCR_RESET are active high. */
      KDEBUG(TRANSPORT, "Reset NRX600");
      cmd52_write(dev, CCCR_RESET, 0x03, &err);
   } else {
      /* NRX700: Reset bits in CCCR_RESET are active low. */
      KDEBUG(TRANSPORT, "Reset NRX700");
      cmd52_write(dev, CCCR_RESET, 0x00, &err);
   }

   if (err != 0) {
      KDEBUG(TRANSPORT, "Reset failed, error = %d", err);
   }
#endif

   /* Await reset of chip - measured to approx 8 ms */  
   mdelay(20);

   return err;
}

static void
check_error(struct nrxdev *dev, struct urb *urb)
{
   /* sync/async unlink faults aren't errors */
   if(!(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
      KDEBUG(TRANSPORT, "%s - nonzero write bulk status received: %d", __func__, urb->status);
   LOCKDEV(dev);
   dev->errors = urb->status;
   UNLOCKDEV(dev);
}

/*
 * Initalize, assign a fresh skb and submit the given urb.
 * May be called from hardirq context, e.g. the competion callback where the
 * urb is resubmitted.
 */
static int
submit_rx_urb(struct nrxdev *dev, struct urb *urb)
{
   struct sk_buff *skb;
   int retval = 0;

   /* Atomic skb allocation */
   if ((skb = dev_alloc_skb(MAX_PACKET_SIZE)) == NULL) {
      return -ENOMEM;
   }

   /* XXX */
   skb->dev = (struct net_device*)dev;

   skb_put(skb, MAX_PACKET_SIZE);

   /* Initialize the urb properly. Pass the skb as urb context */
   usb_fill_bulk_urb(urb, dev->udev, dev->rcvpipe, skb->data, skb->len, bulk_in_callback, skb);
   usb_anchor_urb(urb, &dev->submitted);

   KDEBUG(TRANSPORT, "usb_submit() urb = %p skb = %pd", urb, skb);

   /* send the data out the bulk port */
   retval = usb_submit_urb(urb, GFP_ATOMIC);
   if (retval) {
      KDEBUG(TRANSPORT, "usb_submit_urb = %d", retval);
      usb_unanchor_urb(urb);
      dev_kfree_skb_any(skb);
   } else {
      dev->rx_submit++;
   }

   return retval;
}

/*
 * Bulk IN completion callback. Called in hardirq context when an urb
 * completed.
 */
static void
bulk_in_callback(struct urb *urb)
{
   struct sk_buff *skb = urb->context;
   struct nrxdev *dev = (struct nrxdev *)skb->dev;
   uint8_t *hdr = skb->data;
   int err;

   dev->rx_complete++;

   KDEBUG(TRANSPORT, "urb = %p skb = %p status = %d, len = %u",
          urb, skb, urb->status, urb->actual_length);
   KDEBUG(TRANSPORT, "rxbuf = %02x %02x %02x %02x %02x",
          hdr[0], hdr[1], hdr[2], hdr[3], hdr[4]);

   /* Remove the header byte */
   skb_pull(skb, 1);

   if (urb->status != 0) {
      /* Unsuccessful */
      dev_kfree_skb_any(skb);
   } else if (*hdr == 0xC1) {
      /* Byte 0xC1 in the header is CMD53 data packet */
      skb_trim(skb, urb->actual_length-1);
      skb_queue_tail(&dev->rx_queue, skb);
      schedule_work(&dev->rx_work);
   } else if (*hdr == 0xC0) {
      /* Byte 0xC0 is a command response */
      dev_kfree_skb_any(skb);
   } else {
      KDEBUG(TRANSPORT, "Invalid packet (tag=%#x) dropping rx frame", *hdr);
      dev->nr_timeout++;
      dev_kfree_skb_any(skb);
   }

   /* Resubmit */
   if ((err = submit_rx_urb(dev, urb)) != 0) {
      KDEBUG(TRANSPORT, "Failed to resubmit urb %p, error %d", urb, err);
      usb_free_urb(urb);
   }
}

static void 
nano_usb_rx_worker(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
   void *device
#else
   struct work_struct *work
#endif
   )
{
   struct nrxdev *dev;
   struct sk_buff *skb;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
   dev = (struct nrxdev*)device;
#else
   dev = container_of(work, struct nrxdev, rx_work);
#endif

   while ((skb = skb_dequeue(&dev->rx_queue)) != NULL) {
      int hl = HIC_MESSAGE_LENGTH_GET(skb->data);
      if (hl != skb->len) {
         KDEBUG(TRANSPORT, "Bad length, hiclen=%d skb->len=%d", hl, skb->len);
         dev_kfree_skb(skb);
      } else if(dev->netdev != NULL) {
         ns_net_rx(skb, dev->netdev);
      } else {
         KDEBUG(TRANSPORT, "dropping frame");
         dev_kfree_skb(skb);
      }
   }
}

/* this can be called from interrupt context, so we can't use any
 * sleeping locks here */
static int
nano_tx(struct sk_buff *skb, void *data)
{
   struct nrxdev *dev = data;
   int retval;
   struct urb *urb;
   uint8_t *hdr;
   size_t hic_len = skb->len;

   KDEBUG(TRANSPORT, "nano_tx: %p [%02x %02x %02x %02x %02x]",
          skb,
          skb->data[0],
          skb->data[1],
          skb->data[2],
          skb->data[3],
          skb->data[4]);

   if (dev->interface == NULL) {
      dev_kfree_skb_any(skb);
      return -ENODEV;
   }

   skb->dev = (struct net_device*)dev;

   if ((urb = usb_alloc_urb(0, GFP_ATOMIC)) == NULL) {
      KDEBUG(TRANSPORT, "failed to allocate urb");
      dev_kfree_skb(skb);
      return -ENOMEM;
   }
   
   if (skb_headroom(skb) < 3) {
      /* No headroom left -> reallocate */
      struct sk_buff *skb2 = skb_realloc_headroom(skb, 3);
      KDEBUG(TRANSPORT, "skb_realloc_headroom");
      if(skb2 == NULL) {
         KDEBUG(TRANSPORT, "No memory, packet dropped.");
         dev_kfree_skb(skb);
         return -ENOMEM;
      }
      dev_kfree_skb(skb);
      skb = skb2;
   }

   if ((hdr = (uint8_t *)skb_push(skb, 3)) == NULL)
   {
      KDEBUG(TRANSPORT, "failed to push skb");
      dev_kfree_skb(skb);
      return -ENOMEM;
   }

   /* Header needed by the USB-SDIO controller */
   hdr[0] = 0x40;
   hdr[1] = hic_len & 0xff;
   hdr[2] = (hic_len >> 8) & 0xff;

   /* initialize the urb properly */
   usb_fill_bulk_urb(urb, dev->udev, dev->sndpipe,
                     skb->data, skb->len, bulk_out_callback, skb);
   usb_anchor_urb(urb, &dev->submitted);

   /* Send the data out the bulk port */
   retval = usb_submit_urb(urb, GFP_ATOMIC);

   if (retval != 0) {
      usb_unanchor_urb(urb);
      KDEBUG(TRANSPORT, "nano_tx: usb_submit_urb = %d", retval);
      dev_kfree_skb(skb);
      usb_free_urb(urb);
   }

   return retval;
}

/*
 * Bulk OUT (TX) completion callback. Runs in hardirq context.
 */
static void
bulk_out_callback(struct urb *urb)
{
   struct sk_buff *skb = urb->context;
   struct nrxdev *dev = (struct nrxdev *)skb->dev;

   KDEBUG(TRANSPORT, "skb = %p[%u]", skb, skb->len);
   if (urb->status) {
      check_error(dev, urb);
   }
   dev_kfree_skb_any(skb);
   usb_free_urb(urb);
}


static int
start_rx(struct nrxdev *dev, size_t n)
{
   struct urb *urb;
   int err;

   while (n-- > 0) {
      if ((urb = usb_alloc_urb(0, GFP_KERNEL)) == NULL) {
         return -ENOMEM;
      }

      if ((err = submit_rx_urb(dev, urb)) != 0) {
         KDEBUG(TRANSPORT, "Failed to submit urb %p, error %d", urb, err);
      }
   }

   sdio_ctrl(dev, SDIO_AUTORX_ENABLE);

   return 0;
}

static void
stop_rx(struct nrxdev *dev)
{
   sdio_ctrl(dev, SDIO_AUTORX_DISABLE);
   usb_kill_anchored_urbs(&dev->submitted);
}

static int
nano_ctrl(uint32_t command, uint32_t arg, void *data)
{
   struct nrxdev *dev = data;
   int retval = 0;

   switch (command) {
      case NANONET_SLEEP:
         KDEBUG(TRANSPORT, "NANONET_SLEEP %u", arg);
         if (arg == NANONET_SLEEP_ON)
         {
            cmd52_write_async(dev, CCCR_WAKEUP, 0);
         }
         else if (arg == NANONET_SLEEP_OFF)
         {
            cmd52_write_async(dev, CCCR_WAKEUP, 1);
         }
         break;

      case NANONET_BOOT:
         /* Not needed */
         break;
          
      case NANONET_INIT_SDIO:
         KDEBUG(TRANSPORT, "NANONET_INIT_SDIO %u", arg);
         retval = sdio_init(dev);
         break;

      case NANONET_SHUTDOWN:
         KDEBUG(TRANSPORT, "NANONET_SHUTDOWN %u", arg);
         break;

      default:
         KDEBUG(TRANSPORT, "nano_usb: nano_ctrl(%u.%u) NOT SUPPORTED", command, arg);
         return -EOPNOTSUPP;
   }

   return 0;
}

#define FW_MAX_CHUNK_SIZE 512

static int
nano_download(const void *fw_buf, size_t fw_len, void *data)
{
   struct nrxdev *dev = data;
   uint8_t       *chunk_buf;
   const uint8_t *fw_p = fw_buf;
   int            len;
   int            ret = 0;

   KDEBUG (TRANSPORT, "nano_download: current=%s dev=%p", current->comm,  dev);

   stop_rx(dev);

   /* Set controlller in 4bit and HighSpeed mode */
#ifdef NANOUSB_SDIO4BIT
   sdio_ctrl(dev, SDIO_4BIT_ENABLE);
#endif
#if 0
   sdio_ctrl(dev, SDIO_HS_ENABLE);
#endif

   nrx_reset(dev);
   sdio_init(dev);
   KDEBUG (TRANSPORT, "RCA=%u", dev->rca);

   /* Enable 4bit mode */
#ifdef NANOUSB_SDIO4BIT
   cmd52_write(dev, SDIO_CCCR_IF, 0x02, &ret);
#endif
   /* Clear and Enable SDIO interrupt */
   cmd52_write(dev, CCCR_INTACK, 0x02, &ret);
   cmd52_write(dev, SDIO_CCCR_IENx, 0x03, &ret);
   /* Enable interrupt generation without clock */
   cmd52_write(dev, CCCR_NOCLK_INT, 0x01, &ret);

   KDEBUG (TRANSPORT, "fw_len: %d", (int)fw_len);

   chunk_buf = kmalloc(FW_MAX_CHUNK_SIZE, GFP_KERNEL);

   while (fw_len > 0) {
      size_t chunk_size = min(fw_len, (size_t)FW_MAX_CHUNK_SIZE - 4);
      chunk_buf[0] = 0x40;
      chunk_buf[1] = chunk_size & 0xff;
      chunk_buf[2] = (chunk_size >> 8) & 0xff;
      memcpy(&chunk_buf[3], fw_p, chunk_size);

      ret = usb_bulk_msg(dev->udev, dev->sndpipe, chunk_buf, chunk_size+3, &len, 100);
      if (ret < 0) {
         KDEBUG(TRANSPORT, "Chunk failed with %zu bytes left, error %d", fw_len, ret);
         goto out;
      }
      if(len != chunk_size + 3) {
         KDEBUG(TRANSPORT, "usb_bulk_msg, len = %u", len);
      }
      fw_len -= chunk_size;
      fw_p   += chunk_size;
   }

   if(fw_len != 0) {
      KDEBUG(TRANSPORT, "%zu remaining fw bytes", fw_len);
   }

   if (ret == 0) {
      KDEBUG(TRANSPORT, "Download succeeded");
      msleep(100);
   }

   start_rx(dev, 4);

  out:
   kfree(chunk_buf);
   return ret;
}

static ssize_t
show_clkdiv(struct device *dev, struct device_attribute *attr, char *buf)
{
   struct usb_interface *iface = to_usb_interface(dev);
   struct nrxdev *nrxdev = usb_get_intfdata(iface);
   return snprintf(buf, PAGE_SIZE, "%u\n", nrxdev->clkdiv);
}

static ssize_t
set_clkdiv(struct device *dev, struct device_attribute *attr,
             const char *buf, size_t count)
{
   struct usb_interface *iface = to_usb_interface(dev);
   struct nrxdev *nrxdev = usb_get_intfdata(iface);
   unsigned div = buf[0] - '0';

   if (!capable(CAP_NET_ADMIN))
      return -EACCES;

   if (div > 7) {
      return -EINVAL;
   }

   nrxdev->clkdiv = div;
   sdio_ctrl(nrxdev, SDIO_CLKDIV(div));

   return count;
}

static DEVICE_ATTR(clkdiv, 0644, show_clkdiv, set_clkdiv);

static ssize_t
show_counters(struct device *dev, struct device_attribute *attr, char *buf)
{
   struct usb_interface *iface = to_usb_interface(dev);
   struct nrxdev *nrxdev = usb_get_intfdata(iface);
   return snprintf(buf, PAGE_SIZE, "%u %u %u\n",
                   nrxdev->nr_timeout, nrxdev->rx_submit, nrxdev->rx_complete);
}

static ssize_t
set_counters(struct device *dev, struct device_attribute *attr,
             const char *buf, size_t count)
{
   struct usb_interface *iface = to_usb_interface(dev);
   struct nrxdev *nrxdev = usb_get_intfdata(iface);

   if (!capable(CAP_NET_ADMIN))
      return -EACCES;

   LOCKDEV(nrxdev);
   nrxdev->nr_timeout = 0;
   UNLOCKDEV(nrxdev);

   return count;
}

static DEVICE_ATTR(counters, 0644, show_counters, set_counters);

static struct nanonet_create_param create_param = {
   .size           = sizeof(struct nanonet_create_param),
   .chip_type      = CHIP_TYPE_UNKNOWN,   
   .send           = nano_tx,
   .fw_download    = nano_download,
   .control        = nano_ctrl,
   .params_buf     = NULL,
   .params_len     = 0,
   .min_size       = 32,
   .size_align     = 4,
   .header_size    = 0,
   .host_attention = HIC_CTRL_HOST_ATTENTION_SDIO,
   .byte_swap_mode = HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP,
   .host_wakeup    = HIC_CTRL_ALIGN_HWAKEUP_NOT_USED,
   .force_interval = HIC_CTRL_ALIGN_FINTERVAL_NOT_USED,
   .tx_headroom    = 4
};

static void nano_usb_delete(struct kref *kref)
{
   struct nrxdev *dev = kref_to_nrxdev(kref);

   KDEBUG(TRANSPORT, "nano_usb_delete: dev=%p", dev);

   stop_rx(dev);

   device_remove_file(&dev->interface->dev, &dev_attr_counters);
   device_remove_file(&dev->interface->dev, &dev_attr_clkdiv);

   if (dev->netdev != NULL) {
      struct net_device *tmp = dev->netdev;
      dev->netdev = NULL;
      nanonet_destroy(tmp);
   }

   usb_set_intfdata(dev->interface, NULL);

   /* prevent more I/O from starting */
   dev->interface = NULL;

   usb_kill_anchored_urbs(&dev->submitted);

   if (dev->cmd52_async_urb) {
      usb_free_urb(dev->cmd52_async_urb);
   }

   usb_put_dev(dev->udev);
   kfree(dev);
}


static int
nano_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
   struct usb_host_interface *alt0 = &interface->altsetting[0];
   struct nrxdev *dev;
   int retval = 0;
   int i;

   if (alt0 == NULL || alt0->string == NULL || strcmp(alt0->string, "SDIO") != 0)
      return -ENODEV;

   /* Allocate memory for our device state and initialize it */
   if ((dev = kzalloc(sizeof(*dev), GFP_KERNEL)) == NULL) {
      return -ENOMEM;
   }

   dev->udev = usb_get_dev(interface_to_usbdev(interface));
   dev->interface = interface;
   usb_set_intfdata(interface, dev);

   kref_init(&dev->kref);
   spin_lock_init(&dev->lock);
   init_usb_anchor(&dev->submitted);
   skb_queue_head_init(&dev->rx_queue);

   dev->clkdiv = SDIO_CLKDIV_DEFAULT;

   for (i=0; i<alt0->desc.bNumEndpoints; i++)
   {
      struct usb_endpoint_descriptor *desc = &alt0->endpoint[i].desc;

      if (usb_endpoint_dir_in(desc))
      {
         dev->rcvpipe = usb_rcvbulkpipe(dev->udev, desc->bEndpointAddress);
      }
      else
      {
         dev->sndpipe = usb_sndbulkpipe(dev->udev, desc->bEndpointAddress);
      }
   }

   /* Read FPGA version */
   sdio_ctrl(dev, SDIO_SEND_VER);
   retval = usb_bulk_msg(dev->udev, dev->rcvpipe, dev->rspbuf, 8, &i, 100);
   if (retval != 0 || dev->rspbuf[0] != 0xC3) {
      dev_warn(&dev->udev->dev, "Failed to read FPGA version\n");
   } else {
      memcpy(&dev->fpga_ver, &dev->rspbuf[1], sizeof dev->fpga_ver);
      dev_info(&dev->udev->dev, "FPGA version %u:%u%s%s\n",
               (dev->fpga_ver >> 15) & 0x7FFF, dev->fpga_ver & 0x7FFF,
               (dev->fpga_ver & (1<<31)) ? "S":"",
               (dev->fpga_ver & (1<<30)) ? "M":"");
   }

   sdio_ctrl(dev, SDIO_VDD_ENABLE);

   msleep(10);

   if ((retval = sdio_init(dev)) != 0) {
      dev_warn(&dev->udev->dev, "Failed to initialize SDIO card, error %d\n", retval);
      goto error;
   }

   if (identify(dev) < 0) {
      dev_warn(&dev->udev->dev, "Failed to read SDIO CIS\n");
      return -ENODEV;
   }

   if (dev->vendor == 0x03bb) {
      switch (dev->mask_id & 0xFF00) {
         case 0x0000:
            create_param.chip_type = CHIP_TYPE_NRX700;
            dev_info(&dev->udev->dev, "Nanoradio NRX700\n");
            break;
         case 0x2000:
            create_param.chip_type = CHIP_TYPE_NRX600;
            dev_info(&dev->udev->dev, "Nanoradio NRX600 rev %02x\n", dev->mask_id & 0xff);
            break;
         case 0x0100:
            create_param.chip_type = CHIP_TYPE_NRX900;
            dev_info(&dev->udev->dev, "Nanoradio NRX900 rev %02x\n", dev->mask_id & 0xff);
            break;
         default:
            dev_warn(&dev->udev->dev, "Unrecognized device (vendor=%#x mask_id=%#x)\n", dev->vendor, dev->mask_id);
            break;
      }
   } else {
      KDEBUG(TRANSPORT, "Unrecognized device (vendor=%#x mask_id=%#x)\n", dev->vendor, dev->mask_id);
      dev_warn(&dev->udev->dev, "Unrecognized device (vendor=%#x mask_id=%#x)\n", dev->vendor, dev->mask_id);
   }

   if ((dev->cmd52_async_urb = usb_alloc_urb(0, GFP_KERNEL)) == NULL) {
      KDEBUG(TRANSPORT, "Failed to allocate urb");
      retval = -ENOMEM;
      goto error;
   }

   INIT_WORK (&dev->rx_work, nano_usb_rx_worker 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
              ,(void*)dev
#endif
      );

   retval = device_create_file(&dev->interface->dev, &dev_attr_counters);
   if (retval < 0) {
      goto error;
   }

   retval = device_create_file(&dev->interface->dev, &dev_attr_clkdiv);
   if (retval < 0) {
      goto error;
   }

   dev->netdev = nanonet_create(&dev->udev->dev, dev, &create_param);
   if (!dev->netdev) {
      KDEBUG(TRANSPORT, "nanonet_create failed!");
      retval = -ENOSYS;
      goto error;
   }

   return retval;

error:
   if (dev) {
      /* This frees allocated memory */
      kref_put(&dev->kref, nano_usb_delete);
   }
   return retval;
}

static void
nano_usb_disconnect(struct usb_interface *interface)
{
   struct nrxdev *dev = usb_get_intfdata(interface);

   KDEBUG(TRANSPORT, "nano_usb_disconnect: dev=%p", dev);

   /* decrement our usage count */
   kref_put(&dev->kref, nano_usb_delete);
}

static int
nano_usb_pre_reset(struct usb_interface *intf)
{
   struct nrxdev *dev = usb_get_intfdata(intf);
   int time;

   KDEBUG(TRANSPORT, "ENTRY");

   time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
   if (!time)
      usb_kill_anchored_urbs(&dev->submitted);

   return 0;
}

static int
nano_usb_post_reset(struct usb_interface *intf)
{
   struct nrxdev *dev = usb_get_intfdata(intf);

   KDEBUG(TRANSPORT, "ENTRY");

   /* we are sure no URBs are active - no locking needed */
   dev->errors = -EPIPE;

   return 0;
}

/* table of devices that work with this driver */
static struct usb_device_id usb_id_table[3] = {
  { USB_DEVICE(0x1234, 0x2022) },
   { USB_DEVICE(0x1234, 0x4681) }
   /* Zero terminating entry */
};

static struct usb_driver nano_usb_driver = {
   .name       = "nano_usb",
   .probe      = nano_usb_probe,
   .disconnect = nano_usb_disconnect,
   .pre_reset  = nano_usb_pre_reset,
   .post_reset = nano_usb_post_reset,
   .id_table   = usb_id_table,
};


static int __init
nano_usb_init(void)
{
   int result;

   if ((result = usb_register(&nano_usb_driver)) != 0) {
      KDEBUG(TRANSPORT, "Failed to register, error %d", result);
   }

   return result;
}

static void __exit
nano_usb_exit(void)
{
   usb_deregister(&nano_usb_driver);
}

module_init(nano_usb_init);
module_exit(nano_usb_exit);

MODULE_LICENSE("GPL"); /* XXX */
MODULE_DEVICE_TABLE(usb, usb_id_table);
