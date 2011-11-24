#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/wireless.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <net/dsfield.h>
#include <net/pkt_sched.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <asm/byteorder.h>
#include "nanoparam.h"
#include "nanonet.h"
#include "nanoutil.h"
#include "nanoproc.h"

unsigned nrx_debug = 0; /* -1 => KDEBUG, 0 => OFF */

EXPORT_SYMBOL(nrx_debug);

struct btdev
{
   struct hci_dev *hdev;
   struct nanonet_create_param *transport;
   void *obj;
   struct semaphore rx;
};

struct hic_hdr {
   uint16_t len;
   uint8_t  type;
   uint8_t  id;
   uint8_t  hdr_size;
   uint8_t  reserved;
   uint16_t padding;
} __attribute__((packed));

struct set_alignment_req {
   uint32_t trans_id;
   uint16_t min_sz;
   uint16_t padding_sz;
   uint8_t  hAttention;
   uint8_t  ul_header_size;
   uint8_t  swap;
   uint8_t  hWakeup;
   uint8_t  hForceInterval;
   uint8_t  tx_window_size;
   uint16_t block_mode_bug_workaround_block_size;
   uint8_t  reserved[2];
} __attribute__((packed));

struct echo_msg {
   uint32_t trans_id;
};

static struct sk_buff *
create_set_alignment_req(void)
{
   size_t total_size = 32;
   struct sk_buff *skb;
   struct hic_hdr *hdr;
   struct set_alignment_req *setalign;

   skb = dev_alloc_skb(total_size);

   hdr = (struct hic_hdr *) skb_put(skb, sizeof *hdr);
   hdr->len      = total_size - 2;
   hdr->type     = 7;
   hdr->id       = 3;
   hdr->hdr_size = sizeof *hdr;
   hdr->padding  = total_size - sizeof *hdr - sizeof *setalign;

   setalign = (struct set_alignment_req *) skb_put(skb, sizeof *setalign);
   setalign->trans_id = 0x12345678;
   setalign->min_sz = 32;
   setalign->padding_sz = 4;
   setalign->hAttention = 1;
   setalign->ul_header_size = 8;
   setalign->swap = 0;
   setalign->hWakeup = 0xff;
   setalign->hForceInterval = 0xff;
   setalign->tx_window_size = 3;
   setalign->block_mode_bug_workaround_block_size = 0;
   setalign->reserved[0] = 0xff;
   setalign->reserved[1] = 0xff;

   skb_put(skb, hdr->padding);

   return skb;
}

static void
hexdump(const char *tag, void *buf, size_t len)
{
   uint8_t *p = (uint8_t *)buf;

   printk("%s: ", tag);
   while (len-- > 0) {
      printk("%02X ", *p++);
   }
   printk("\n");
}

static int nanohci_open_dev(struct hci_dev *hdev)
{
   struct btdev *data = hdev->driver_data;
   struct sk_buff *skb;

   KDEBUG(TRANSPORT, "nanohci_open_dev(hdev=%p) current=%s\n", hdev, current->comm);

   skb = create_set_alignment_req();
   data->transport->send(skb, data->obj);
   KDEBUG(TRANSPORT, "nanonet_create: Set Alignment Req sent\n");

   /* if set aligment fails, rx = -1, blocking */
   if (down_interruptible(&data->rx)) {
      KDEBUG(TRANSPORT, "No response on set_alignment");
      return 1;
   }

   set_bit(HCI_RUNNING, &hdev->flags);

   return 0;
}

static int nanohci_close_dev(struct hci_dev *hdev)
{
   struct btdev *data = hdev->driver_data;

   KDEBUG(TRANSPORT, "nanohci_close_dev(hdev=%p, data=%p)\n", hdev, data);

   if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
      return 0;

   return 0;

}

static int nanohci_flush(struct hci_dev *hdev)
{
   struct btdev *data = hdev->driver_data;

   KDEBUG(TRANSPORT, "nanohci_flush(hdev=%p, data=%p)\n", hdev, data);

   return 0;
}

static int nanohci_send_frame(struct sk_buff *skb)
{
   struct hci_dev* hdev = (struct hci_dev *) skb->dev;
   struct btdev *data;
   struct hic_hdr *hdr;
   uint8_t data_length;

   KDEBUG(TRANSPORT, "nanohci_send_frame(hdev=%p, len=%d\n", hdev, skb->len);

   if (!hdev) {
      BT_ERR("Frame for unknown HCI device (hdev=NULL)");
      return -ENODEV;
   }

   if (!test_bit(HCI_RUNNING, &hdev->flags))
      return -EBUSY;

   data = hdev->driver_data;

   data_length = sizeof *hdr + 1;
   KDEBUG(TRANSPORT, "%s: skb_headroom = %d, expanding %d bytes\n", __func__, skb_headroom(skb), data_length);

   if (skb_headroom(skb) < data_length) {
       /* No headroom left -> reallocate */
       struct sk_buff *skb2 = skb_realloc_headroom(skb, data_length);
       KDEBUG(TRANSPORT, "%s: skb_realloc_headroom", __func__);
       if(skb2 == NULL) {
          KDEBUG(TRANSPORT, "%s: No memory, packet dropped.", __func__);
          return -ENOMEM;
       }
       dev_kfree_skb(skb);
       skb = skb2;
    }

   /* Add HCI packet type */
   KDEBUG(TRANSPORT, "HCI packet type = %d\n",bt_cb(skb)->pkt_type);
   memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

   /* Prepend HIC message header to skb */
   hdr = (struct hic_hdr *) skb_push(skb, sizeof *hdr);

   /* 6 = HIC_hdr_size - sizeof(length) */
   hdr->len      = skb->len - sizeof hdr->len;
   hdr->type     = 9;
   hdr->id       = 0;
   hdr->hdr_size = sizeof *hdr;
   hdr->padding  = 0;
   hdr->reserved = 0;

   KDEBUG(TRANSPORT, "%s: HIC_packet_length = %d\n", __func__, hdr->len);

   if (bt_cb(skb)->pkt_type == 1) /* HCI command */
   {
     hexdump("HCI CMD", skb->data, skb->len);
   }

   /* Push HCI frame to transport driver */
   data->transport->send(skb, data->obj);

   return 0;
}

static void nanohci_destruct(struct hci_dev *hdev)
{
   struct btdev *data = hdev->driver_data;

   KDEBUG(TRANSPORT, "nanohci_destruct(hdev=%p, data=%p\n", hdev, data);
}

void ns_net_rx(struct sk_buff *skb, struct net_device *dev)
{
  struct btdev *data = (struct btdev *)dev;
  struct echo_msg *msg;
  struct hic_hdr *hdr;
  uint16_t hci_len;
  uint8_t error;

  /* connect skb object with hci object */
  skb->dev = (struct net_device *)data->hdev;

  /* Set rx to = 0 to unblock */
  up(&data->rx);

  /* Using skb_pull to remove HIC header */
  hdr = (struct hic_hdr *) skb_pull(skb, 0);
  msg = (struct echo_msg *) skb_pull(skb, hdr->hdr_size);

  if (hdr->type == 0x89)
  { 
    if (hdr->id == 0x81)
    {
      /* HCI CFM - empty or Number_Of_Completed_Packets_Event. */
      bt_cb(skb)->pkt_type = *((__u8 *) skb->data);
      skb_pull(skb, 1); 

      if (hdr->padding == 0x18)
      {
        /* Empty CFM - do nothing. */ 
        /*printk("Empty HIC CFM!\n");*/
      }
      else
      {
        /* HCI event - Number_Of_Completed_Packets_Event */
        /* hci packet length */
        ASSERT(bt_cb(skb)->pkt_type == 0x04);
        hci_len = skb->data[1];
        /* trim the rest, event type + len = 2 */
        skb_trim(skb, hci_len+2);

        /*hexdump("HCI_RX_EVENT_L2CAP",skb->data, skb->len);*/
        /* Pass received frame to Linux BT stack */
        error = hci_recv_frame(skb);
        KDEBUG(TRANSPORT, "%s: hci_recv_frame error = %d\n", __func__, error);
      }
    }
    else /* if (hdr->id == 0x81) */
    {
      ASSERT(hdr->id == 0x00);
      /* HCI packet type */
      bt_cb(skb)->pkt_type = *((__u8 *) skb->data);
      KDEBUG(TRANSPORT, "\n%s: HCI packet type = %d", __func__, bt_cb(skb)->pkt_type);
      skb_pull(skb, 1); 

      if (bt_cb(skb)->pkt_type == 0x04)
      {
        /* HCI event */
        /* hci packet length */
        hci_len = skb->data[1];
        /* trim the rest, event type + len = 2 */
        skb_trim(skb, hci_len+2);

        hexdump("HCI EVT",skb->data, skb->len);
        /* Pass received frame to Linux BT stack */
        error = hci_recv_frame(skb);
        KDEBUG(TRANSPORT, "%s: hci_recv_frame error = %d\n", __func__, error);
      }
      else
      {
        /* HCI acl data */ 
        ASSERT(bt_cb(skb)->pkt_type == 0x02);
        hci_len = skb->data[2] + (skb->data[3] << 8);
        /*printk("acl data length: %d\n", hci_len);*/

        /* trim the rest, conn handle + len = 4 */
        skb_trim(skb, hci_len+4);

        /*hexdump("HCI_RX_L2CAP",skb->data, skb->len);*/
        /* Pass received frame to Linux BT stack */
        error = hci_recv_frame(skb);
        KDEBUG(TRANSPORT, "%s: hci_recv_frame error = %d\n", __func__, error);
      }
    }
  }
  else /* if (hdr->type == 0x89) */
  {
    if (hdr->type == 0x84) /* && (hdr->id == 0x81 || hdr->id == 0x80))*/ 
    {
      /* HCI console, e.g. DCP_DISPLAY */
      skb_pull(skb, 4);
      printk("%s\n", skb->data);
    }
    else
    {
      printk("%s: Unkown hdr->type = 0x%X received. Discarded!\n", __func__, hdr->type);
      KDEBUG(TRANSPORT, "%s: Unkown data frame. Discarded!\n", __func__);
    }
  }
}

EXPORT_SYMBOL(ns_net_rx);

struct net_device *
nanonet_create(struct device *pdev, void *obj, struct nanonet_create_param *param)
{
   struct btdev *data;
   struct hci_dev *hdev;

   data = kzalloc(sizeof(struct btdev), GFP_KERNEL);
   if (!data)
      return NULL;

   hdev = hci_alloc_dev();
   if (!hdev) {
      kfree(data);
      return NULL;
   }

   data->hdev = hdev;

   data->transport = param;
   data->obj       = obj;
 
   /* Init semaphore to 0 */
   sema_init(&data->rx, 0);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
   hdev->type = HCI_USB;
#else
   hdev->bus  = HCI_USB;
#endif
   hdev->driver_data = data;

   hdev->open     = nanohci_open_dev;
   hdev->close    = nanohci_close_dev;
   hdev->flush    = nanohci_flush;
   hdev->send     = nanohci_send_frame;
   hdev->destruct = nanohci_destruct;

   hdev->owner = THIS_MODULE;

   /* FIXME: Load real firmware */
   data->transport->fw_download(NULL, 0, data->obj);
   KDEBUG(TRANSPORT, "nanonet_create: fw_download sent\n");

   if (hci_register_dev(hdev) < 0) {
      BT_ERR("Can't register HCI device");
      kfree(data);
      hci_free_dev(hdev);
      return NULL;
   }

   KDEBUG(TRANSPORT, "nanonet_create: created btdev %p\n", data);

   return (struct net_device *)data;
}

EXPORT_SYMBOL(nanonet_create);

void
nanonet_destroy(struct net_device *dev)
{
   struct btdev *data = (struct btdev *)dev;
   struct hci_dev *hdev = data->hdev;

   KDEBUG(TRANSPORT, "nanonet_destroy: destroying btdev %p\n", data);

   if (hci_unregister_dev(hdev) < 0) {
      BT_ERR("Can't unregister HCI device %s", hdev->name);
   }

   if(data != NULL)
	   kfree(data);
   if(hdev != NULL)
	   hci_free_dev(hdev);
}

EXPORT_SYMBOL(nanonet_destroy);

static int __init
nanobt_init(void)
{
   KDEBUG(TRACE, "ENTRY");
   KDEBUG(TRACE, "EXIT");
   return 0;
}


static void __exit
nanobt_cleanup(void)
{
   KDEBUG(TRACE, "ENTRY");
   KDEBUG(TRACE, "EXIT");
}

MODULE_LICENSE("GPL");
module_init(nanobt_init);
module_exit(nanobt_cleanup);
