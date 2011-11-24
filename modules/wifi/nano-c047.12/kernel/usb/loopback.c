/*
 * $Id: loopback.c 14480 2010-03-04 20:07:59Z phth $
 */

/*
 * Implements the loop back test for the sdio, spi and kspi transports.
 * This is for transport reliability verification and throughput assessment.
 */

#include <linux/file.h>
#include <linux/time.h>

#define XMAC_ALIGNMENT 2048

unsigned int nrx_debug = 0;

struct rawdev {
   struct device               *pdev;
   struct nanonet_create_param *transport;
   void                        *transport_data;
   unsigned long                flags;
   wait_queue_head_t            read_wait;
   struct sk_buff_head          readq;
   int                          pkt_count;
   unsigned long                jiffies_base;
   unsigned long                total_bytes;
   struct semaphore             loopback_sema;
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

static struct rawdev *ldev;

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

static int
do_test(void* dummy)
{
   struct rawdev *ldev = dummy;
   int j;
   int echo_size = 1500;
   int num_msg = 100000;
   int count;
   struct sk_buff *skb;
   int retval = 0;

   KDEBUG(TRANSPORT, "ENTER");

   skb = create_set_alignment_req();
   ldev->transport->send(skb, ldev->transport_data);

   if (down_interruptible(&ldev->loopback_sema)) {
      KDEBUG(TRANSPORT, "No response on set_alignment");
      return 1;
   }

   KDEBUG(TRANSPORT, "Set alignment complete.");

   sema_init(&ldev->loopback_sema, 3);

   msleep(200);

   ldev->jiffies_base = jiffies;

   for (count = 0; count < num_msg; count++)
   {
      struct hic_hdr *hdr;
      struct echo_msg *echo;
      uint8_t *data;

      KDEBUG(TRANSPORT, "Loopback Pkt [%d]", count);

      skb = dev_alloc_skb(sizeof *hdr + sizeof *echo + echo_size);  
      if (!skb) {
         retval = -ENOMEM;
         break;
      }

      hdr = (struct hic_hdr *) skb_put(skb, sizeof *hdr);
      hdr->type     = 3;
      hdr->id       = 0;
      hdr->hdr_size = sizeof *hdr;
      hdr->padding  = 0;

      echo = (struct echo_msg *) skb_put(skb, sizeof *echo);
      echo->trans_id = count+1;

      data = skb_put(skb, echo_size);

      hdr->len = skb->len - 2;

      for (j=0; j<echo_size; j++) {
         data[j] = j & 0xff;
      }

      if (down_interruptible(&ldev->loopback_sema))
         break;

      ldev->transport->send(skb, ldev->transport_data);
   }

   msleep(100); /* wait for packets to be received before exiting */

   KDEBUG(TRANSPORT, "EXIT");
   return retval;
}

void nano_util_printbuf(const void *data, size_t len, const char *prefix)
{
   int i, j;
   const unsigned char *p = data;

   for (i = 0; i < len; i += 16) {
      printk("%s %04x: ", prefix, i);
      for (j = 0; j < 16; j++) {
         if (i + j < len)
            printk("%02x ", p[i+j]);
         else
            printk("   ");
      }
      printk(" : ");
      for (j = 0; j < 16; j++) {
         if (i + j < len) {
#define isprint(c) ((c) >= 32 && (c) <= 126)
            if (isprint(p[i+j]))
               printk("%c", p[i+j]);
            else
               printk(".");
         } else
            printk(" ");
      }
      printk("\n");
   }
}

struct net_device *
nanonet_create(struct device *pdev, void *obj, struct nanonet_create_param *param)
{
   const struct firmware *fw;
   int ret;

   ldev = kzalloc(sizeof(struct rawdev), GFP_KERNEL);
   if (!ldev) {
      return NULL;
   }

   ldev->pdev  = pdev;
   ldev->transport = param;
   ldev->transport_data = obj;
   sema_init(&ldev->loopback_sema, 0);

   ret = request_firmware(&fw, "x_mac.bin", pdev);
   if (ret != 0) {
      printk("request_firmware() = %d\n", ret);
      kfree(ldev);
      return NULL;
   }

   ret = ldev->transport->fw_download(fw->data, fw->size, ldev->transport_data);
   KDEBUG(TRANSPORT, "fw_download() = %d\n", ret);
   release_firmware(fw);

   msleep(100);

   kernel_thread(do_test, ldev, 0);

   return (struct net_device *)ldev;
}

void
nanonet_destroy(struct net_device *dev)
{
   struct rawdev *ldev = (struct rawdev *)dev;
   kfree(ldev);
}

void
ns_net_rx(struct sk_buff *skb, struct net_device *dev)
{
   struct rawdev *ldev = (struct rawdev *)dev;
   struct hic_hdr *hdr;
   struct echo_msg *msg;
   uint8_t *data;
   int i = 0;

   hdr = (struct hic_hdr *) skb_pull(skb, 0);
   msg = (struct echo_msg *) skb_pull(skb, hdr->hdr_size);
   data = (uint8_t *) skb_pull(skb, sizeof *msg);

   if (hdr->type == 0x83 && hdr->id == 0x80) {
      /* ECHO_CFM */

      ldev->total_bytes += skb->len;

      if ((ldev->pkt_count & 0x3ff) == 0) {
         if (ldev->jiffies_base != 0) {
            unsigned long msec = jiffies_to_msecs(jiffies - ldev->jiffies_base);
            unsigned long throughput = 2 * (8 * ldev->total_bytes) / msec;
            printk("RX: %d packets, %lu msec, %lu kbit/sec\n", ldev->pkt_count, msec, throughput);
         }
         ldev->jiffies_base = jiffies;
         ldev->total_bytes  = 0;
      }

      /* Perform test */
      for (i = 0; i < skb->len; ++i) {
         if (data[i] != (i & 0xff))
            break;
      }

      if (msg->trans_id != ldev->pkt_count || i < skb->len) {
         printk("RX Pkt[%d] failed (i=%d, len=%d)\n", ldev->pkt_count, i, skb->len);
         nano_util_printbuf(skb->data, skb->len, "RX failed");
      }
   }

   up(&ldev->loopback_sema);

   ldev->pkt_count++;

   dev_kfree_skb(skb);
}
