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
#include <nanonet.h>
#include <nanoutil.h>
#include "sdio.h"

/****************************************************************************
 *   D E F I N E S
 ****************************************************************************/
#define NANO_DEVICE_NAME "nanopci"
#define PCI_VENDOR_ID_NANORADIO 0xbabe
#define PCI_DEVICE_ID_NANORADIO_CARDBUS 0x0001

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define IRQ_FLAGS (SA_INTERRUPT | SA_SHIRQ)
#else
#define IRQ_FLAGS (IRQF_DISABLED | IRQF_SHARED)
#endif

#define MAX_PACKET_LEN 2048
#define NUM_DMA_BUFFERS 8
#define XMAC_ALIGNMENT 16

#define FW_HEADER_SIZE 64
#define FW_CHUNK_HEADER_SIZE 6
#define FW_MAX_CHUNK_SIZE 1024

#define STATUS_REMOVED 0xffffffff

/*
 * Uncomment the following line to pad outgoing packets,
 * and to un-pad incomming packets.
 */
//#define USE_PADDING

/*
 * Uncomment the following line to get
 * some statistics in /proc/nanopci/statistics.
 */
#define USE_STATISTICS


/****************************************************************************
 *  T Y P E S
 ****************************************************************************/
struct nano_pci_card {
    spinlock_t lock;
    unsigned long irq_flags;
    int active;
    void* pci_iomem0;
    void* pci_iomem1;
    struct resource* pci_res0;
    struct resource* pci_res1;
    size_t pci_res0_len;
    size_t pci_res1_len;

    struct sk_buff *tx_skb[NUM_DMA_BUFFERS];
    struct sk_buff *rx_skb[NUM_DMA_BUFFERS];
    struct sk_buff_head skb_rx_head;

    uint32_t rx_dma_ptr;
    uint32_t tx_dma_ptr;
    uint32_t tx_host_ptr;
    uint32_t tx_tail_ptr;

    struct net_device *iface;
    struct pci_dev *pcidev;

    struct work_struct rx_ws;

#ifdef NANOPCI_CDEV
    dev_t devno;
    struct cdev cdev;
    struct semaphore sem;

    wait_queue_head_t rx_waitqueue;
    struct sk_buff_head rx_queue;

    wait_queue_head_t tx_waitqueue;
    struct sk_buff_head tx_queue;
    struct work_struct tx_ws;

    struct nanopci_status status;
    unsigned long devst; /* status flags */
#define DEVST_OPEN 0
    struct class *device_class;
    struct device *device;
#endif

   int booting;
};

#define LOCKDEV(card) spin_lock_irqsave(&card->lock, card->irq_flags)
#define UNLOCKDEV(card) spin_unlock_irqrestore(&card->lock, card->irq_flags)

#ifdef USE_STATISTICS
struct pci_statistics_t {
    unsigned irq_rx;
    unsigned irq_tx;
    unsigned tx_pkts;
    unsigned rx_pkts;
    unsigned rx_dropped;
};
#endif


/****************************************************************************
 *  F U N C T I O N   D E C L A R A T I O N S
 ****************************************************************************/
static int __init nano_pci_init (void);
static void __exit nano_pci_exit (void);
static int nano_pci_probe (struct pci_dev* dev, const struct pci_device_id* id);
static inline void nano_ctrl_write32 (struct nano_pci_card* card, uint32_t offset, uint32_t value);
static inline uint32_t nano_ctrl_read32 (struct nano_pci_card* card, uint32_t offset);
static inline void nano_buff_write32 (struct nano_pci_card* card, uint32_t offset, uint32_t value);
static inline uint32_t nano_buff_read32 (struct nano_pci_card* card, uint32_t offset);
static void nano_pci_remove (struct pci_dev* dev);
static irqreturn_t nano_pci_irq (int irq, void* dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
             , struct pt_regs *regs
#endif
             );
static void nano_pci_rx_irq (struct nano_pci_card* card);
static int nano_tx (struct sk_buff *skb, void *data);
static int nano_pci_fw_download(const void *fw_buf, size_t fw_len, void *data);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void nano_pci_rx_wq (void* device);
#else
static void nano_pci_rx_wq (struct work_struct *work);
#endif
static void reset_fpga (struct nano_pci_card* card);
static void reset_device (struct nano_pci_card* card);
static void internal_boot_device (struct nano_pci_card* card);
static void init_sdio (struct nano_pci_card* card);
static int sdio_control (uint32_t command, uint32_t mode, void *data);


/****************************************************************************
 *  F I L E   S C O P E   V A R I A B L E' S
 ****************************************************************************/

static int fwDownload = 1;
module_param (fwDownload, int, S_IRUGO);

static struct nanonet_create_param create_param = {
    .size        = sizeof(struct nanonet_create_param),
    .chip_type   = CHIP_TYPE_UNKNOWN,
    .send        = nano_tx,
    .fw_download = NULL,
    .control     = sdio_control,
    .params_buf    = NULL,
    .params_len    = 0,
    .min_size    = 32,
    .size_align  = 4,
    .header_size = 18,
    .host_attention = HIC_CTRL_HOST_ATTENTION_GPIO,
    .byte_swap_mode = HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP,
    .host_wakeup = HIC_CTRL_ALIGN_HWAKEUP_NOT_USED,
    .force_interval = HIC_CTRL_ALIGN_FINTERVAL_NOT_USED,
    .tx_headroom = 0
};


static struct pci_device_id pci_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_NANORADIO, PCI_DEVICE_ID_NANORADIO_CARDBUS) },
    { 0, 0, 0, 0, 0, 0, 0 }
};

static struct pci_driver nano_pci_driver = {
    .name     = NANO_DEVICE_NAME,
    .id_table = pci_ids,
    .probe    = nano_pci_probe,
    .remove   = nano_pci_remove,
#ifdef CONFIG_PM
#if 0
    .suspend  = nano_pci_suspend,
    .resume   = nano_pci_resume,
#endif
#endif
};

#ifdef USE_STATISTICS
static struct pci_statistics_t statistics;
static struct proc_dir_entry *proc_dir;
#endif

static struct workqueue_struct* nanopci_wq;



/*
 *
 */
#ifdef USE_STATISTICS
int pci_read_proc (char* buf, char** start, off_t offset, int count, int* eof, void* data)
{
#ifdef sprintf
#undef sprintf
#endif
    int len = 0;
    KDEBUG (TRACE, "ENTER");

    len += sprintf (buf+len, "irq_tx     : %u\n", statistics.irq_tx);
    len += sprintf (buf+len, "irq_rx     : %u\n", statistics.irq_rx);
    len += sprintf (buf+len, "Tx packets : %u\n", statistics.tx_pkts);
    len += sprintf (buf+len, "Rx packets : %u\n", statistics.rx_pkts);
    len += sprintf (buf+len, "Rx dropped : %u\n", statistics.rx_dropped);
    len += sprintf (buf+len, "\n");

    *eof = 1;
    KDEBUG (TRACE, "EXIT");
    return len;
}
#endif


/**
 *
 */
static int __init nano_pci_init (void)
{
    int err;
    KDEBUG (TRACE, "ENTER");


    nanopci_wq = create_singlethread_workqueue ("nanopci");

#ifdef USE_STATISTICS
    /* Initialize /proc interface */
    proc_dir = proc_mkdir ("nanopci", NULL);
    create_proc_read_entry ("statistics",
                            0, // Default mode
                            proc_dir, // Parent dir
                            pci_read_proc,
                            NULL);
#endif

    /*
     * Register the PCI driver.
     */
    err = pci_register_driver (&nano_pci_driver);
    if (err) {
        KDEBUG (ERROR, "Error registering the PCI driver");
        KDEBUG (TRACE, "EXIT");
        return err;
    }

    KDEBUG (TRACE, "EXIT");
    return 0;
}


/**
 *
 */
static void __exit nano_pci_exit (void)
{
    KDEBUG (TRACE, "ENTER");
    /*
     * Unregister the PCI driver.
     */
    pci_unregister_driver (&nano_pci_driver);

#ifdef USE_STATISTICS
    remove_proc_entry ("statistics", proc_dir);
    remove_proc_entry ("nanopci", NULL);
#endif

    destroy_workqueue (nanopci_wq);
    KDEBUG (TRACE, "EXIT");
}


/**
 *
 */
static inline void nano_ctrl_write32 (struct nano_pci_card* card, uint32_t offset, uint32_t value)
{
    iowrite32 (value, card->pci_iomem0 + offset);
}


/**
 *
 */
static inline uint32_t nano_ctrl_read32 (struct nano_pci_card* card, uint32_t offset)
{
    return ioread32 (card->pci_iomem0 + offset);
}


/**
 *
 */
static inline void nano_buff_write32 (struct nano_pci_card* card, uint32_t offset, uint32_t value)
{
    iowrite32 (value, card->pci_iomem1 + offset);
}


/**
 *
 */
static inline uint32_t nano_buff_read32 (struct nano_pci_card* card, uint32_t offset)
{
    return ioread32 (card->pci_iomem1 + offset);
}


static inline void
set_interrupt_mode(struct nano_pci_card *card, uint32_t mode)
{
   uint32_t att;
   att = nano_buff_read32(card, HOST_ATTENTION_OFFSET);
   att = (att & ~1) | mode;
   nano_buff_write32(card, HOST_ATTENTION_OFFSET, att);
}

/*
 * +-------------------------------------------------+
 * |      GPIO_ID      | GPIO_TYPE  |  OVR  | POLICY |
 * +-------------------------------------------------+
 *  POLICY    = 0=GPIO, 1=SDIO
 *  OVR       = 0=Use default, 1=Override default
 *  GPIO_TYPE = 0=STD, 1=EXT
 *  GPIO_ID   = 0..31
 */
static inline void
setup_interrupt(struct nano_pci_card *card)
{
   uint32_t version = nano_buff_read32(card, FPGA_VERSION_OFFSET);
   uint32_t chip_type = version & 0x03;
   char     version_tag[4];

   version_tag[0] = (version >> 24) & 0xFF;
   version_tag[1] = (version >> 16) & 0xFF;
   version_tag[2] = (version >>  8) & 0xFF;
   version_tag[3] = 0;

   KDEBUG(ERROR, "FPGA %s (%u)", version_tag, version & 0xFF);

   if(version == 0) {
      /* old fpga */
      return;
   }

   if (chip_type == 0)
   {
      /* SIP */
      set_interrupt_mode(card, HOST_ATTENTION_GPIO1);
      create_param.host_attention = 
    HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO |
    HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM |
    HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_EXT |
    (1 << HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID);
      KDEBUG(ERROR, "Using EXT GPIO1 as interrupt");
   }
   else if (chip_type == 1)
   {
      /* COB */
      set_interrupt_mode(card, HOST_ATTENTION_SW);

      if (version_tag[1] == '8' && version_tag[2] >= 'A')
      {
         /* Use DAT1 as interrupt */
         create_param.host_attention =
            HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO |
            HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM |
            HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_STD |
            (11 << HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID);
         KDEBUG(TRACE, "Using SDIO DAT1 as interrupt");
      }
      else if (version_tag[1] == '9' && version_tag[2] >= 'A')
      {
         /* Use native sdio interrupt */
         create_param.host_attention = HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO;
         KDEBUG(TRACE, "Using native SDIO interrupt");
      }
      else
      {
         KDEBUG(TRACE, "Using default interrupt");
      }
   }
   else
   {
      KDEBUG(ERROR, "Unknown chip type %u", chip_type);
   }
}

/**
 *
 */
static int nano_pci_probe (struct pci_dev* dev, const struct pci_device_id* id)
{
    int err;
    uint32_t iomemfirst;
    uint32_t iomemlast;
    struct nano_pci_card* card = NULL;
    KDEBUG (TRACE, "ENTER");

    // Check for right device and vendor id.
    //
    if (id->vendor != PCI_VENDOR_ID_NANORADIO) {
        KDEBUG (TRACE, "EXIT");
        return -1;
    }
    if (id->device != PCI_DEVICE_ID_NANORADIO_CARDBUS) {
        KDEBUG (TRACE, "EXIT");
        return -1;
    }

    // Allocate memory for card specific info
    //
    card = kmalloc (sizeof(struct nano_pci_card), GFP_ATOMIC);
    if (!card) {
        KDEBUG (ERROR, "Unable to allocate memory for nano_pci_card structure");
        KDEBUG (TRACE, "EXIT");
        return -1;
    }
    memset (card, 0, sizeof(struct nano_pci_card));
    pci_set_drvdata (dev, card);
    card->pcidev = dev;
    spin_lock_init(&card->lock);

    // Setup work queue's
    //
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    INIT_WORK (&card->rx_ws, nano_pci_rx_wq, (void*)card);
#else
    INIT_WORK (&card->rx_ws, nano_pci_rx_wq);
#endif
    skb_queue_head_init(&card->skb_rx_head);

    card->booting = 0;

    // Enable the PCI device
    //
    err = pci_enable_device (dev);
    if (err) {
        kfree (card);
        KDEBUG (ERROR, "Error enabling PCI device");
        KDEBUG (TRACE, "EXIT");
        return -1;
    }
    pci_set_master (dev);

    // Setup the irq handler
    //
    err = request_irq (dev->irq,
                       nano_pci_irq,
                       IRQ_FLAGS,
                       NANO_DEVICE_NAME,
                       card);
    if (err) {
        kfree (card);
        KDEBUG (ERROR, "Unable to allocate interrupt handler for irq %d, "\
          "return code %d.\n", (unsigned int)dev->irq, err);

        pci_disable_device (dev);
        KDEBUG (TRACE, "EXIT");
        return -1;
    }

    // Setup resources for io region 0 (ctrl)
    //
    iomemfirst = pci_resource_start (dev, 0);
    iomemlast  = pci_resource_end (dev, 0);
    card->pci_res0_len = iomemlast - iomemfirst + 1;
    card->pci_res0 = request_mem_region (iomemfirst, card->pci_res0_len, NANO_DEVICE_NAME);
    card->pci_iomem0 = ioremap_nocache (iomemfirst, card->pci_res0_len);

    // Setup resources for io region 1 (buff)
    //
    iomemfirst = pci_resource_start (dev, 1);
    iomemlast  = pci_resource_end (dev, 1);
    card->pci_res1_len = iomemlast - iomemfirst + 1;
    card->pci_res1 = request_mem_region (iomemfirst, card->pci_res1_len, NANO_DEVICE_NAME);
    card->pci_iomem1 = ioremap_nocache (iomemfirst, card->pci_res1_len);

    reset_fpga(card);
    init_sdio (card);
    internal_boot_device (card);

    setup_interrupt(card);

    if (fwDownload)
        create_param.fw_download = nano_pci_fw_download;
    card->iface = nanonet_create(&card->pcidev->dev, card, &create_param);
    if (!card->iface) {
        KDEBUG (ERROR, "nanonet_create failed!");
        KDEBUG (TRACE, "EXIT");
        return -1;
    }

    KDEBUG (TRACE, "EXIT");
    return 0;
}


/**
 *
 */
static void nano_pci_remove (struct pci_dev* dev)
{
    int i=0;
    struct nano_pci_card* card;
    struct sk_buff* skb;
//    unsigned long timeout;

    KDEBUG (TRACE, "ENTER");

    card = (struct nano_pci_card*) pci_get_drvdata (dev);
    card->active = 0;

    nanonet_destroy (card->iface);

#if 0
    cancel_delayed_work (&card->rx_ws);
    flush_workqueue (nanopci_wq);
    while((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
        dev_kfree_skb_any (skb);
    }

    timeout = jiffies + 2*HZ;
    while (time_before(jiffies, timeout))
        schedule ();

    /* Disable global interrupts */
    nano_ctrl_write32 (card, REG_INT_OFFSET, INT_DISABLE);
#endif

    // Remove the interrupt handler
    free_irq (dev->irq, card);

    pci_disable_device (dev);

    iounmap (card->pci_iomem0);
    iounmap (card->pci_iomem1);
    release_mem_region (card->pci_res0->start, card->pci_res0_len);
    release_mem_region (card->pci_res1->start, card->pci_res1_len);

    while((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
        dev_kfree_skb_any (skb);
    }
    for(i=0; i<NUM_DMA_BUFFERS; i++) {
        if (card->tx_skb[i])
            dev_kfree_skb_any (card->tx_skb[i]);
        if (card->rx_skb[i])
            dev_kfree_skb_any (card->rx_skb[i]);
    }

    kfree (card);

    KDEBUG (TRACE, "EXIT");
}


/**
 *
 */
static irqreturn_t nano_pci_irq (int irq, void* dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
             , struct pt_regs *regs
#endif
             )
{
    uint32_t status, test;
    irqreturn_t retval = IRQ_NONE;

    struct nano_pci_card* card = (struct nano_pci_card*) dev_id;

    // Read interrupt status
    status = nano_ctrl_read32 (card, REG_INT_ACK_OFFSET);
    test   = nano_ctrl_read32 (card, REG_INT_OFFSET);

    if (status == STATUS_REMOVED) {
        KDEBUG (TRACE, "Nano card removed!");
        return IRQ_HANDLED;
    }

    // Check if the interrupt is for us
    if (!(test&2))
        return IRQ_NONE;

    if (!card->active) 
        return IRQ_HANDLED;

    if (status & SDIO_OUTPUT_INT) {
        unsigned int i;
#ifdef USE_STATISTICS
        statistics.irq_tx++;
#endif
        nano_ctrl_write32 (card, REG_INT_ACK_OFFSET, SDIO_OUTPUT_INT);
        card->tx_dma_ptr = (nano_buff_read32(card, OUTPUT_DMA_PTR_OFFSET) >> 8) & 0x07;
   for (i=card->tx_tail_ptr; i != card->tx_dma_ptr; i = ((i+1)&0x07)) {
       if (card->tx_skb[i]) {
      dev_kfree_skb_irq(card->tx_skb[i]);
      card->tx_skb[i] = NULL;
       }
   }
   card->tx_tail_ptr = i;
        retval = IRQ_HANDLED;
    }

    if (status & SDIO_INPUT_INT) {
#ifdef USE_STATISTICS
        statistics.irq_rx++;
#endif
        nano_pci_rx_irq (card);
        retval = IRQ_HANDLED;
    }

    return retval;
}


/**
 *
 */
static void nano_pci_rx_irq (struct nano_pci_card* card)
{
    unsigned int     buf_ptr;
    dma_addr_t       addr;
    unsigned int     rx_index;
    struct sk_buff * rx_buf;

    // Ack the interrupt
    nano_ctrl_write32 (card, REG_INT_ACK_OFFSET, SDIO_INPUT_INT);

    // Get DMA ptr
    buf_ptr = (nano_buff_read32(card, INPUT_DMA_PTR_OFFSET)>>8) & 0x07;

    rx_index = card->rx_dma_ptr;
    while (rx_index != buf_ptr) {
        // Handle packet
        pci_dma_sync_single_for_cpu (card->pcidev,
                                     nano_buff_read32 (card, INPUT_DMA_ADR_0_OFFSET + (rx_index<<3)),
                                     MAX_PACKET_LEN + XMAC_ALIGNMENT,
                                     DMA_FROM_DEVICE);

        // Extract the buffer from the current slot
        rx_buf = card->rx_skb[rx_index];
        // Insert new rx buffer in the slot
        card->rx_skb[rx_index] = dev_alloc_skb (MAX_PACKET_LEN + XMAC_ALIGNMENT);
        // Add the extracted buffer
        skb_queue_tail (&card->skb_rx_head, rx_buf);

//        skb_reserve (card->rx_skb[rx_index], 2); /* Align IP header */
        skb_put (card->rx_skb[rx_index], 2);
        addr = dma_map_single (NULL,
                               card->rx_skb[rx_index]->data,
                               MAX_PACKET_LEN + XMAC_ALIGNMENT,
                               DMA_FROM_DEVICE);
        nano_buff_write32 (card,
                           INPUT_DMA_ADR_0_OFFSET + (rx_index<<3),
                           addr);
        rx_index++;
        rx_index &= 0x07;
    }
    card->rx_dma_ptr = buf_ptr;
    buf_ptr--;
    buf_ptr &= 0x07;

    // Set host ptr
    nano_buff_write32 (card, INPUT_DMA_PTR_OFFSET, buf_ptr);

    // Let the higher level protocols handle the packets.
    queue_work (nanopci_wq, &card->rx_ws);
}


/*
 *
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void nano_pci_rx_wq(void *device)
#else
static void nano_pci_rx_wq(struct work_struct *work)
#endif
{
    struct nano_pci_card* card;
    struct sk_buff *skb;   
    unsigned short len;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    card = (struct nano_pci_card *)device;
#else
    card = container_of(work, struct nano_pci_card, rx_ws);
#endif
    while ((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
        skb->dev = card->iface;
        /* Get header containing length of data */
        len = HIC_MESSAGE_LENGTH_GET(skb->data);
        if (len >= 8 && len <= 2048) {
#ifdef USE_PADDING
       unsigned short pad;
            pad = HIC_MESSAGE_PADDING_GET(skb->data);
            if (len > 8 && pad) {
           HIC_MESSAGE_LENGTH_SET(skb->data, len - pad);
      HIC_MESSAGE_PADDING_SET(skb->data, 0);
                len -= pad;
            }
#endif
            skb_put (skb, len - 2); /* two bytes already allocated */
#if 0
            KDEBUG_BUF(TRANSPORT, skb->data, skb->len, "RX data:");
#endif
            /* send data to upper layer */
#ifdef USE_STATISTICS
            statistics.rx_pkts++;
#endif
            ns_net_rx (skb, card->iface);
        }else{
           skb_put (skb, 16); /* XXX */
#ifdef USE_STATISTICS
            statistics.rx_dropped++;
#endif
            KDEBUG(ERROR, "bad size of HIC message: %u\n", len);
            dev_kfree_skb_any (skb);
        }
    }
}


/*
 *
 */
static int nano_tx (struct sk_buff *skb, void *data)
{
#ifdef USE_PADDING
    int pad_len;
#endif
    struct nano_pci_card* card;
    dma_addr_t addr;

    // Get the pci card
    //
    card = (struct nano_pci_card*) data;

    // Is the card still active(inserted)
    //
    if (!card->active) {
        KDEBUG (TRACE, "PCI card not present");
        dev_kfree_skb_any (skb);
        KDEBUG (TRACE, "EXIT");
        return 0;
    }

    // Pad the packet length
    //
#ifdef USE_PADDING
    if(skb->len % XMAC_ALIGNMENT)
        pad_len = XMAC_ALIGNMENT - (skb->len % XMAC_ALIGNMENT);
    else 
        pad_len = 0;
    skb_put(skb, pad_len);
    HIC_MESSAGE_PADDING_SET(skb->data, pad_len);
    HIC_MESSAGE_LENGTH_SET(skb->data, 
            HIC_MESSAGE_LENGTH_GET(skb->data) + pad_len);
#endif
    // Get hardware address and flush cache
    //
    if(card->booting) {
       uint16_t *extra = (uint16_t*)skb_push(skb, 2);
       *extra = skb->len - 2;
    }
#if 0
    KDEBUG_BUF(TRANSPORT, skb->data, skb->len, "TX data:");
#endif

    addr = dma_map_single (NULL,
                           skb->data,
                           skb->len,
                           DMA_TO_DEVICE);
    pci_dma_sync_single_for_device (card->pcidev,
                                    addr,
                                    skb->len,
                                    DMA_TO_DEVICE);

    /* Set up DMA */
    LOCKDEV(card);
    {
        uint32_t currentOffset;
        uint32_t next_tx_host_ptr;
        unsigned long timeout;

        next_tx_host_ptr = card->tx_host_ptr + 1;
        next_tx_host_ptr &= 0x07;

        timeout = jiffies + HZ;
        while (next_tx_host_ptr == card->tx_dma_ptr) {
           UNLOCKDEV(card);
           /* TODO: replace usleep() with a tx queue */
           udelay (50);
           if (time_after(jiffies, timeout)) {
              dev_kfree_skb_any (skb);
              KDEBUG (ERROR, "Data sent too fast!");
              KDEBUG (TRACE, "EXIT");
              return 0;
           }
           LOCKDEV(card);
        }

#ifdef USE_STATISTICS
        statistics.tx_pkts++;
#endif
   WARN_ON(card->tx_skb[card->tx_host_ptr] != NULL);

        card->tx_skb[card->tx_host_ptr] = skb;

        currentOffset = OUTPUT_DMA_ADR_0_OFFSET + card->tx_host_ptr * 8;
        // Disable global host interrupts
        nano_ctrl_write32 (card, REG_INT_OFFSET, INT_DISABLE);
        nano_buff_write32 (card, currentOffset, addr);
        currentOffset += 4;
        nano_buff_write32 (card, currentOffset, skb->len);
        nano_buff_write32 (card, OUTPUT_DMA_PTR_OFFSET, next_tx_host_ptr);
        // Enable global host interrupts
        nano_ctrl_write32 (card, REG_INT_OFFSET, INT_ENABLE);

        card->tx_host_ptr = next_tx_host_ptr;
    }
    UNLOCKDEV(card);

    return 0;
}


/*
 *
 */
static ssize_t pci_send_blocking (struct nano_pci_card* card,
                                  dma_addr_t data, 
                                  uint32_t size)
{
    uint32_t currentOffset;
    uint32_t next_tx_host_ptr;
    unsigned long timeout;

    //KDEBUG (TRACE, "ENTER");
    //KDEBUG (TRACE, "Send %d bytes", (int)size);
    next_tx_host_ptr = card->tx_host_ptr + 1;
    next_tx_host_ptr &= 0x07;

    // Disable global host interrupts
    nano_ctrl_write32 (card, REG_INT_OFFSET, INT_DISABLE);

    currentOffset = OUTPUT_DMA_ADR_0_OFFSET + card->tx_host_ptr * 8;
    nano_buff_write32 (card, currentOffset, data);
    currentOffset += 4;
    nano_buff_write32 (card, currentOffset, size);

    nano_buff_write32 (card, OUTPUT_DMA_PTR_OFFSET, next_tx_host_ptr);

    // Enable global host interrupts
    nano_ctrl_write32 (card, REG_INT_OFFSET, INT_ENABLE);

    card->tx_host_ptr = next_tx_host_ptr;
    timeout = jiffies + 2*HZ; // Two second timeout
    while (card->tx_host_ptr != card->tx_dma_ptr) {
        udelay (50);
        if (time_after(jiffies, timeout)) {
            KDEBUG (ERROR, "Timeout while loading firmware");
            //KDEBUG (TRACE, "EXIT");
            return -1;
        }
    }

    //KDEBUG (TRACE, "EXIT");
    return (ssize_t)size;
}


static void boot_start(struct nano_pci_card* card)
{
    init_sdio (card);
    internal_boot_device (card);

    // Tell the FPGA to start send fw data.
    nano_buff_write32 (card, INIT_SLAVE_OFFSET, ENABLE_BOOT);
    card->booting = 1;
}

static void boot_stop(struct nano_pci_card* card)
{
    /* Wait for target to boot */
    msleep (500);
    msleep (500);

    // Tell the FPGA to stop send fw data.
    nano_buff_write32 (card, INIT_SLAVE_OFFSET, DISABLE_BOOT);

    init_sdio (card);
    msleep (50);

    card->booting = 0; /* XXX */

}

/*
 *
 */
static int nano_pci_fw_download (const void *fw_buf, size_t fw_len, void *data)
{
    struct nano_pci_card* card;
    unsigned short chunk_size;
    char* mem;
    dma_addr_t busmem;
    size_t written = 0;

    KDEBUG (TRACE, "ENTER");
    KDEBUG (TRACE, "fw_len: %d", (int)fw_len);

    card = (struct nano_pci_card*) data;

    boot_start(card);

    // Allocate DMA memory
    mem = dma_alloc_coherent (NULL,//&dev->dev,
                              FW_MAX_CHUNK_SIZE+2,
                              &busmem,
                              GFP_KERNEL);

    // Send the firmware header (64 bytes)
    *((unsigned short*)mem) = (unsigned short) FW_HEADER_SIZE;
    memcpy (mem+2, fw_buf, FW_HEADER_SIZE);
    if (-1 == pci_send_blocking(card, busmem, 2+FW_HEADER_SIZE)) {
        dma_free_coherent (NULL, FW_MAX_CHUNK_SIZE+2, mem, busmem);
        KDEBUG (ERROR, "Unable to load firmware");
        KDEBUG (TRACE, "EXIT");
        return 0;
    }
    fw_buf += FW_HEADER_SIZE;
    fw_len -= FW_HEADER_SIZE;
    written += FW_HEADER_SIZE;

    // Send the firmware
    while (fw_len) {
        chunk_size = *((unsigned short*)fw_buf);
        if (chunk_size>FW_MAX_CHUNK_SIZE || (size_t)chunk_size>fw_len) {
            dma_free_coherent (NULL, FW_MAX_CHUNK_SIZE+2, mem, busmem);
       KDEBUG (ERROR, "Unable to load firmware %zu", written);
            KDEBUG (ERROR, "Firmware chunk size too big: %d", (int)chunk_size);
            KDEBUG (TRACE, "EXIT");
            return -EIO;
        }
        *((unsigned short*)mem) = chunk_size + FW_CHUNK_HEADER_SIZE;
        memcpy (mem+2, fw_buf, chunk_size + FW_CHUNK_HEADER_SIZE);
        if (-1 == pci_send_blocking(card, busmem, 2 + chunk_size + FW_CHUNK_HEADER_SIZE)) {
            dma_free_coherent (NULL, FW_MAX_CHUNK_SIZE+2, mem, busmem);
       KDEBUG (ERROR, "Unable to load firmware %zu", written);
            KDEBUG (TRACE, "EXIT");
            return -EIO;
        }
        fw_buf += chunk_size + FW_CHUNK_HEADER_SIZE;
        fw_len -= chunk_size + FW_CHUNK_HEADER_SIZE;
   written += FW_HEADER_SIZE;
    }

    boot_stop(card);

    dma_free_coherent (NULL, FW_MAX_CHUNK_SIZE+2, mem, busmem);
    KDEBUG (TRACE, "EXIT");
    return 0;
}


/*
 *
 */
static void reset_device (struct nano_pci_card* card)
{
    struct sk_buff *skb;   
    int i;

    KDEBUG (TRACE, "ENTER");
    card->active = 0;

    while((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
        dev_kfree_skb_any (skb);
    }
    mdelay (50);

    nano_ctrl_write32(card, REG_GPIO_OUT_PIN, 3);
    nano_ctrl_write32(card, REG_GPIO_DIRECTION, 3);
    nano_ctrl_write32(card, REG_GPIO_OUT_PIN, 3);

    /* Activate reset_a and reset_d */
    nano_ctrl_write32(card, REG_GPIO_OUT_PIN, 0);

    mdelay(10);

    /* Release reset_a */
    nano_ctrl_write32(card, REG_GPIO_OUT_PIN, 2);
    mdelay(10);

    /* Relese reset_d */
    nano_ctrl_write32(card, REG_GPIO_OUT_PIN, 3);
    mdelay(10);

    reset_fpga(card);

    while((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
        dev_kfree_skb_any (skb);
    }

    // Setup readpointers for DMA buffer in FPGA registers
    //
    for(i=0; i<NUM_DMA_BUFFERS; i++) {
        dma_addr_t addr;
        if (!card->rx_skb[i]) {
            card->rx_skb[i] = dev_alloc_skb (MAX_PACKET_LEN + XMAC_ALIGNMENT);
//            skb_reserve (card->rx_skb[i], 2); /* Align IP header */
            skb_put (card->rx_skb[i], 2);
        }
        addr = dma_map_single (NULL,
                               card->rx_skb[i]->data,
                               MAX_PACKET_LEN + XMAC_ALIGNMENT,
                               DMA_FROM_DEVICE);
        nano_buff_write32 (card,
                           INPUT_DMA_ADR_0_OFFSET + i*8,
                           addr);
    }

    for(i=0; i<NUM_DMA_BUFFERS; i++) {
        if (card->tx_skb[i]) {
            dev_kfree_skb_any(card->tx_skb[i]);
       card->tx_skb[i] = NULL;
   }
    }

    card->tx_dma_ptr = card->tx_tail_ptr = card->tx_host_ptr = 0;
    card->rx_dma_ptr = 0;
    card->active = 1;
    nano_buff_write32 (card, INPUT_DMA_PTR_OFFSET, 7);

    KDEBUG (TRACE, "EXIT");
}

static void reset_fpga (struct nano_pci_card* card)
{
    /* Reset Fpga */
    nano_ctrl_write32 (card, REG_RESET_FPGA, RESET_TOGGLE_HIGH);
    nano_ctrl_write32 (card, REG_RESET_FPGA, RESET_TOGGLE_LOW);
    nano_ctrl_write32 (card, REG_RESET_FPGA, RESET_TOGGLE_HIGH);
}

/*
 *
 */
static void init_sdio (struct nano_pci_card* card)
{
    KDEBUG (TRACE, "ENTER");
    // Initiate SDIO-interface
    //
    nano_buff_write32 (card, INIT_SLAVE_OFFSET, CLOSE_SLAVE);
    // Enable global host interrupts
    //
    nano_ctrl_write32 (card, REG_INT_OFFSET, INT_ENABLE);
    nano_ctrl_write32 (card, REG_INT_UNGATE_OFFSET, REG_INT_UNGATE);

    // Initiate SDIO-interface
    //
    nano_buff_write32 (card, INIT_SLAVE_OFFSET, INIT_SLAVE);
    KDEBUG (TRACE, "EXIT");
}


/*
 *
 */
static void internal_boot_device (struct nano_pci_card* card)
{
    KDEBUG (TRACE, "ENTER");
    reset_device (card);
    if (!fwDownload) {
        mdelay (500);
        mdelay (500);
        mdelay (500);
        mdelay (500);
    }else{
        mdelay (500);
    }
    init_sdio (card);
    KDEBUG (TRACE, "EXIT");
}


/*
 *
 */
static int sdio_control (uint32_t command, uint32_t mode, void *data)
{
   struct nano_pci_card* card;

   card = (struct nano_pci_card*) data;

   switch (command) {
      case NANONET_SLEEP:
         // Disable global host interrupts
         nano_ctrl_write32 (card, REG_INT_OFFSET, INT_DISABLE);

         if (mode == NANONET_SLEEP_ON) {
            KDEBUG (TRACE, "ENABLE_SLEEP");
            nano_buff_write32 (card, INIT_SLAVE_OFFSET, ENABLE_SLEEP);
         }
         else if (mode == NANONET_SLEEP_OFF) {
            KDEBUG (TRACE, "DISABLE_SLEEP");
            nano_buff_write32 (card, INIT_SLAVE_OFFSET, DISABLE_SLEEP);
         }
         // Enable global host interrupts
         nano_ctrl_write32 (card, REG_INT_OFFSET, INT_ENABLE);
         return 0;

      case NANONET_BOOT:
         if(mode == NANONET_BOOT_ENABLE) {
            nano_buff_write32 (card, INIT_SLAVE_OFFSET, ENABLE_BOOT);
            card->booting = 1;
         } else if(mode == NANONET_BOOT_DISABLE) {
            nano_buff_write32 (card, INIT_SLAVE_OFFSET, DISABLE_BOOT);
            card->booting = 0;
         } else if(mode != NANONET_BOOT_TEST) {
            panic("hej");
         }
         return 0;
          
      case NANONET_INIT_SDIO:
         init_sdio(card);
         return 0;

      case NANONET_SHUTDOWN: {
         struct sk_buff* skb;

         flush_workqueue (nanopci_wq);

         while((skb = skb_dequeue(&card->skb_rx_head)) != NULL) {
            dev_kfree_skb_any (skb);
         }

         /* Disable global interrupts */
         nano_ctrl_write32 (card, REG_INT_OFFSET, INT_DISABLE);
         
         reset_fpga(card);
         break;
      }
      default:
         KDEBUG (ERROR, "Unknown command");
         break;
   }

   return -EOPNOTSUPP;
}



module_init (nano_pci_init);
module_exit (nano_pci_exit);

MODULE_DEVICE_TABLE(pci, pci_ids);

MODULE_LICENSE ("GPL");

/* Local Variables: */
/* c-basic-offset: 4 */
/* indent-tabs-mode: nil */
/* End: */
