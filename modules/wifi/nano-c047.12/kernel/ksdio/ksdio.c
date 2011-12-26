/*
 *
 * nano_ksdio.c - Tranport driver unsing Linux SDIO stack.
 *
 * WARNING: This driver supports only one NRX device on the system.
 *
 * Configuration defines:
 *
 * KSDIO_CHIP_TYPE - If this macro is not defined, the driver works with
 * any chip type. If it is defined, the driver is forced to operate
 * only with the chip type indicated by the macro's value.
 * Supported values are 1 for NRX700 and 2 for NRX600.
 *
 * KSDIO_SIZE_ALIGN - Defined to set HIC alignment request "size_align"
 * to specified setting. The default is 4 when KSDIO_SIZE_ALIGN not defined.
 *
 * KSDIO_SEPARATE_WORKQUEUE - Defined to use a separate workqueue for
 * RX and TX work. This may improve performance on some platforms.
 *
 * KSDIO_FW_CHUNK_LEN - Is the size of firmware chunk during firmware
 * download. The default is 16 when KSDIO_FW_CHUNK_LEN is not defined.
 * If FW download fails on devices with low XO-freq it is better to decrease
 * the SDIO-clock. (less CPU-load and overall quicker download)
 *
 * KSDIO_FORCE_CLOCK - Defined to force the hosts SDIO clock frequency to
 * specified setting. The macros value is the clock frequency in Hz.
 *
 * KSDIO_FW_DOWNLOAD_CLOCK - Defined to force the hosts SDIO clock
 * frequency during firmware download. The macros value is the clock
 * frequency in Hz. If KSDIO_FW_DOWNLOAD_CLOCK is not defined,
 * clock frequency during firmware download will not be changed.
 *
 * KSDIO_NO_BLOCK_MODE - Defined to inhibit the use of SDIO block mode
 * transfers.
 *
 * KSDIO_BLOCK_SIZE - Defines the length of the block mode transfer in bytes.
 * It is effective if SDIO block mode is used. 
 * If zero, the length of the block mode transfer is set by the kernel driver.
 *
 * KSDIO_NO_4_BIT_MODE - Defined to inhibit the use of 4-bit SDIO mode.
 *
 * KSDIO_WAKEUP_CLOCK - Defined to use a lower SDIO clock when waking up the
 * chip. The macros value is the clock frequency in Hz.
 *
 * KSDIO_HIGH_SPEED - Defined to enable high speed mode.
 *
 * KSDIO_HOST_GPIO_IRQ - Defined to signal interrupt via a host GPIO pin.
 * The macro's value is the interrupt number of the host.
 *
 * KSDIO_TARGET_GPIO_IRQ - Defined to configure firmware to generate interrupt
 * via pins in the WiFi chip (pins GPIO0 or GPIO1). The macros value is the
 * target's GPIO pin to use (19->GPIO0, 20->GPIO1).
 *
 * KSDIO_HOST_WAKEUP_PIN - Defined to configure firmware to signal host
 * wake up via pins in the WiFi chip (pins GPIO0 or GPIO1). The macros
 * value is the GPIO line to use (19->GPIO0, 20->GPIO1).
 *
 * KSDIO_HOST_RESET_PIN - Defined to reset NRX600 via a host GPIO pin.
 * The macro value is the host GPIO pin which drives the SHUTDOWN_N pin
 * of the WiFi chip.
 *
 * KSDIO_IGNORE_REMOVE - If this macro is defined, our network interface is
 * not destroyed when the transport driver is removed and normal operation
 * will be resumed when the transport driver is probed again. Usefull when
 * the host controller driver goes suspended to save power.
 *
 * KSDIO_HIC_MIN_SIZE - Minimum size of HIC message send by the device.
 *
 * USE_TARGET_PARAMS
 * Allows custom target configuration for each platform.
 * When defined, host_get_target_params() must be defined in host.c
 * and will be called to get the target configuration data.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/irq.h>
#include <asm/ioctl.h>
#include <nanonet.h>
#include <nanoutil.h>

#include <linux/freezer.h>
#include <linux/completion.h> 
#include <linux/sched.h>


#if defined WINNER_POWER_ONOFF_CTRL && defined CONFIG_MMC_SUNXI_POWER_CONTROL
#define SDIOID  (CONFIG_CHIP_ID==1123 ? 3 : 1)
extern void sunximmc_rescan_card(unsigned id, unsigned insert);
extern int mmc_pm_get_mod_type(void);
extern int mmc_pm_gpio_ctrl(char* name, int level);
extern int mmc_pm_get_io_val(char* name);
int nano_sdio_powerup(void)
{
    mmc_pm_gpio_ctrl("swl_n20_vcc_en", 1);
    udelay(100);
    mmc_pm_gpio_ctrl("swl_n20_shdn", 1);
    udelay(50);
    mmc_pm_gpio_ctrl("swl_n20_vdd_en", 1);
    return 0;
}
int nano_sdio_poweroff(void)
{
    mmc_pm_gpio_ctrl("swl_n20_shdn", 0);
    mmc_pm_gpio_ctrl("swl_n20_vdd_en", 0);
    mmc_pm_gpio_ctrl("swl_n20_vcc_en", 0);
    return 0;
}
#endif

#undef  PROCFS_HIC

#ifndef KSDIO_SIZE_ALIGN
#define KSDIO_SIZE_ALIGN 4
#endif

#ifndef KSDIO_FW_CHUNK_LEN
#define KSDIO_FW_CHUNK_LEN 16
#endif

#if KSDIO_FW_CHUNK_LEN % KSDIO_SIZE_ALIGN
#define "KSDIO_FW_CHUNK_LEN must be aligned to KSDIO_SIZE_ALIGN"
#endif

#ifndef KSDIO_HOST_WAKEUP_PIN
#define KSDIO_HOST_WAKEUP_PIN HIC_CTRL_ALIGN_HWAKEUP_NOT_USED
#endif

#if defined(KSDIO_HOST_RESET_PIN) && !defined(KSDIO_IGNORE_REMOVE)
#error "KSDIO_HOST_RESET_PIN depends on KSDIO_IGNORE_REMOVE"
#endif

#ifndef KSDIO_BLOCK_SIZE
#define KSDIO_BLOCK_SIZE 0
#endif

#ifndef KSDIO_HIC_MIN_SIZE
#define KSDIO_HIC_MIN_SIZE 32
#endif

#define HIC_MAX_SIZE 2048

#define VENDOR_NANORADIO  0x03BB
#define DEVICE_ID_MASK    0xFF00
#define DEVICE_ID_NRX600  0x2000
#define DEVICE_ID_NRX700  0x0000

const char* chip_names[] = {
   STR_CHIP_TYPE_UNKNOWN,
   STR_CHIP_TYPE_NRX700,
   STR_CHIP_TYPE_NRX600
};

#if defined(USE_THREAD_RX) || defined(USE_THREAD_TX)
int thread_prio = 98;
#endif

#ifdef USE_THREAD_RX
static int do_rx_thread(void *data);
static int do_rx_to_net_thread(void *data);
#else
static void do_rx_work(struct work_struct *work);
static void do_rx_to_net_work(struct work_struct *work);
#endif

#ifdef USE_THREAD_TX
static int do_tx_thread(void *data);
#else
static void do_tx_work(struct work_struct *work);
#endif

/* Nanoradio specific SDIO control registers */
#define CCCR_INTACK     0xF0 /* Bit 1, Interrupt pending, write '1' to clear */
#define CCCR_WAKEUP     0xF1 /* Bit 0, Wakeup, read/write */
#define CCCR_NOCLK_INT  0xF2 /* Bit 0, Assert interrupt without SDIO CLK, read/write */
#define CCCR_HISPEED_EN 0xF3 /* Bit 0, Hi-Speed Mode Enable, read/write */
#define CCCR_FIFO_STAT  0xF4 /* Bit 0, Fifo Underrun ; Bit 1, Fifo Overrun, read/write */
#define CCCR_RESET      0xF5 /* Bit 0, Reset all ; Bit 1, Reset all but SDIO */

#define XMAC_ALIGNMENT 2048

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define NRX_MODULE_PARM(N, T1, T2, M) MODULE_PARM(N, T2)
#else
#define NRX_MODULE_PARM(N, T1, T2, M) module_param(N, T1, M)
#endif

/*!
 * Module param nrx_debug normally belongs to nano_if module.
 * Defined here only when WiFiEngine is not present.
 */
#ifndef USE_WIFI
unsigned int nrx_debug = (unsigned int) -1;
NRX_MODULE_PARM(nrx_debug, int, "i", S_IRUGO);
#else
extern unsigned int nrx_debug;
#endif /* !USE_WIFI */


#ifdef PROCFS_HIC
static struct proc_dir_entry *proc;
#endif

struct nrxdev {
   struct sdio_func   *func;
   struct net_device  *netdev;
   /* Thread based operation */
#ifdef USE_THREAD_RX
   long rx_thread_pid;
   long rx_to_net_thread_pid;
   struct semaphore rx_thread_sem;
   struct semaphore rx_to_net_thread_sem;
   struct completion rx_thread_exited;
   struct completion rx_to_net_thread_exited;
#else
   struct work_struct  rx_work;
   struct work_struct  rx_to_net_work;
#endif
   
#ifdef USE_THREAD_TX
   long tx_thread_pid;
   struct semaphore tx_thread_sem;
   struct completion tx_thread_exited;
#else
   struct work_struct  tx_work;
#endif
   
   struct sk_buff_head rx;
   struct sk_buff_head tx;
   
   spinlock_t          tx_lock;
#ifdef KSDIO_SEPARATE_WORKQUEUE
   struct workqueue_struct *workqueue;
#endif
   bool                irq_claimed; /* is not protected by any lock! */
   bool                active;
   bool                opened;
   bool                reading;
   wait_queue_head_t   readq;
   bool                is_in_hard_shutdown;
   chip_type_t         chip_type;
   uint8_t             rx_hic_start[KSDIO_HIC_MIN_SIZE];
   struct mmc_ios      init_mmc_ios;
};

/* Save a version of nrxdev to be able to detect double initalization */
static struct nrxdev *nrxdev_prev = NULL;

/* Saves nrxdev while the device is in hard shutdown and the driver removed */
#if defined(KSDIO_HOST_RESET_PIN) && defined(USE_WIFI)
static struct nrxdev *nrxdev_saved = NULL;
#endif

/* Saves net device when this driver is requested to remove */
#if defined(KSDIO_IGNORE_REMOVE) && defined(USE_WIFI)
static struct net_device *netdev_saved;
#endif

static int nano_download(const void *buf, size_t size, void *_nrxdev);
static int  nano_tx(struct sk_buff *skb, void *nrxdev);
static int  sdio_nrx_probe(struct sdio_func *func, const struct sdio_device_id *id);
static void sdio_nrx_remove(struct sdio_func *func);

#define PRINTBUF(_ptr,_len,_tag) /* nano_util_printbuf(_ptr, _len, _tag) */

#ifdef USE_WIFI
static int nano_sdio_ctrl(uint32_t command, uint32_t mode, void *nrxdev);
static int nano_hard_shutdown(struct nrxdev *nrxdev, uint32_t arg);
#else /* !USE_WIFI */
static int dump_bin_to_file(const char *fname, const void *data,
              size_t len) __attribute__ ((unused));
static int do_test(void* dummy);
static void do_test_rx(struct sk_buff *skb);
#endif /* USE_WIFI */

#ifdef KSDIO_TARGET_GPIO_IRQ
#define HOST_ATTENTION \
   (HIC_CTRL_ALIGN_HATTN_VAL_POLICY_GPIO              | \
    HIC_CTRL_ALIGN_HATTN_VAL_OVERRIDE_DEFAULT_PARAM   | \
    HIC_CTRL_ALIGN_HATTN_VAL_GPIOPARAMS_GPIO_TYPE_STD | \
    (KSDIO_TARGET_GPIO_IRQ << HIC_CTRL_ALIGN_HATTN_OFFSET_GPIOPARAMS_GPIO_ID))
#else
#define HOST_ATTENTION HIC_CTRL_HOST_ATTENTION_SDIO
#endif

#ifdef KSDIO_SEPARATE_WORKQUEUE
#define QUEUE_WORK(pwork) queue_work(nrxdev->workqueue, (pwork))
#else
#define QUEUE_WORK(pwork) schedule_work(pwork)
#endif

/* If host_get_target_params() is not defined in host.c,
 * create a dummy one here.
 */
#ifdef USE_TARGET_PARAMS
size_t host_get_target_params(const void** params_buf);
#else /* !USE_TARGET_PARAMS */
inline size_t host_get_target_params(const void** params_buf)
{
   params_buf = NULL;
   return 0;
}
#endif /* USE_TARGET_PARAMS */

#ifdef USE_WIFI

/* This module can be loaded with fwDownload = 0 to avoid firmware
 * download when we work with FPGA platforms.
 */
static int fwDownload = 1;
module_param (fwDownload, int, S_IRUGO);

static struct nanonet_create_param create_param = {
    .size        = sizeof(struct nanonet_create_param),
    .chip_type   = CHIP_TYPE_UNKNOWN,
    .send        = nano_tx,
    .fw_download = nano_download,
    .control     = nano_sdio_ctrl,
    .params_buf  = NULL,
    .params_len  = 0,
    .min_size    = KSDIO_HIC_MIN_SIZE,
    .size_align  = KSDIO_SIZE_ALIGN,
    .header_size = 18,
    .host_attention = HOST_ATTENTION,
    .byte_swap_mode = HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP,
    .host_wakeup = KSDIO_HOST_WAKEUP_PIN,
    .force_interval = HIC_CTRL_ALIGN_FINTERVAL_NOT_USED,
    .tx_headroom = 0
};
#else /* !USE_WIFI */
struct nrxdev *loopback_nrxdev;
#endif /* USE_WIFI */

static void
nano_set_wakeup(struct nrxdev *nrxdev, unsigned wakeup)
{
   struct sk_buff *skb;
   unsigned *ptr;

   if ((skb = dev_alloc_skb(4)) == NULL) {
      KDEBUG(ERROR, "out of memor");
      return;
   }
   ptr = (unsigned *)skb_put(skb,4);
   *ptr = wakeup;
   nano_tx(skb, nrxdev);
}

static int nano_tx(struct sk_buff *skb, void *data)
{
   struct nrxdev *nrxdev = data;

#ifdef USE_WIFI
   if (nrxdev->netdev == NULL) {
      dev_kfree_skb_any(skb);
      KDEBUG(ERROR, "Attemt to transmit after device removed");
   } else
#endif
   {
      KDEBUG(TRANSPORT, "(%u bytes) current=%s", skb->len, current->comm);
      skb_queue_tail(&nrxdev->tx, skb);
#ifdef USE_THREAD_TX
      up(&nrxdev->tx_thread_sem);
#else
      QUEUE_WORK(&nrxdev->tx_work);
#endif
   }
   return 0;
}

#ifdef USE_WIFI
static int nano_sdio_ctrl(uint32_t command, uint32_t arg, void *data)
{
   struct nrxdev *nrxdev = data;
   int ret;

   switch (command) {
      case NANONET_SLEEP:
         KDEBUG(TRANSPORT, "%s sleep", arg?"Enable":"Disable");
         nano_set_wakeup(nrxdev, !arg);
         ret = 0;
         break;

      case NANONET_BOOT:
         ret = 0;
         break;
          
      case NANONET_INIT_SDIO:
         KDEBUG(TRANSPORT, "INIT SDIO");
         ret = 0;
         break;

      case NANONET_SHUTDOWN:
         {
            struct sk_buff* skb;

            while ((skb = skb_dequeue(&nrxdev->rx)) != NULL) {
               dev_kfree_skb_any(skb);
            }
         }
         ret = 0;
         break;

      case NANONET_HARD_SHUTDOWN:
         ret = nano_hard_shutdown(nrxdev, arg);
         break;

      default:
         KDEBUG(ERROR, "(%#x) NOT SUPPORTED", command);
         ret = -EOPNOTSUPP;
   }

   return ret;
}
#endif /* USE_WIFI */

static inline struct nrxdev *
nrxdev_from_file(struct file *file)
{
   struct inode *ino = file->f_path.dentry->d_inode;
   struct proc_dir_entry *dp = PDE(ino);
   return (struct nrxdev *)dp->data;
}

static int proc_open(struct inode *inode, struct file *file)
{
   struct nrxdev *nrxdev = nrxdev_from_file(file);
   nrxdev->opened = 1;
   return 0;
}

static int proc_release(struct inode *inode, struct file *file)
{
   struct nrxdev *nrxdev = nrxdev_from_file(file);
   nrxdev->opened = 0;
   return 0;
}

ssize_t proc_read(struct file *file, char __user *buf, size_t count, loff_t *poff)
{
   struct nrxdev *nrxdev = nrxdev_from_file(file);
   struct sk_buff *skb;
   ssize_t ret = 0;

   if (skb_queue_empty(&nrxdev->rx)) {
      if (file->f_flags & O_NONBLOCK) {
         return -EAGAIN;
      } else {
         KDEBUG(TRACE, "Read sleeping");
         nrxdev->reading = 1;
         if (wait_event_interruptible(nrxdev->readq, !skb_queue_empty(&nrxdev->rx))) {
            KDEBUG(TRACE, "Read interrupted");
            nrxdev->reading = 0;
            return -ERESTARTSYS;
         }
         KDEBUG(TRACE, "Read wakeup");
      }
   }

   skb = skb_dequeue(&nrxdev->rx);
   if (copy_to_user(buf, skb->data, skb->len)) {
      ret = -EFAULT;
   } else {
      ret = skb->len;
   }
   dev_kfree_skb(skb);
   nrxdev->reading = 0;

   return ret;
}

ssize_t proc_write(struct file *file, const char __user *buf, size_t count, loff_t *poff)
{
   struct nrxdev *nrxdev = nrxdev_from_file(file);
   int ret;
   void *kbuf;
  
   KDEBUG(TRANSPORT, "write %d bytes", count);
   kbuf = kmalloc(count, GFP_KERNEL | GFP_DMA);
   if (copy_from_user(kbuf, buf, count)) {
      return -EFAULT;
   }
   PRINTBUF(kbuf, count, "TX");
   sdio_claim_host(nrxdev->func);
   ret = sdio_writesb(nrxdev->func, 0, kbuf, count);
   sdio_release_host(nrxdev->func);
   if (ret) {
      KDEBUG(ERROR, "write failed, err %d", ret);
   } else {
      KDEBUG(TRANSPORT, "write succeeded, %d bytes", count);
      ret = count;
   }
   return ret;
}

static unsigned int proc_poll(struct file *file, struct poll_table_struct *wait)
{
   struct nrxdev *nrxdev = nrxdev_from_file(file);
   unsigned int flags;

   poll_wait(file, &nrxdev->readq, wait);

   /* Always writable */
   flags = POLLOUT;
   /* Readable if queue non-empty */
   if (!skb_queue_empty(&nrxdev->rx)) {
      flags |= POLLIN;
   }

   return flags;
}

static int
proc_ioctl_body(struct nrxdev *nrxdev, unsigned int cmd, unsigned long arg)
{
   int err;

   KDEBUG(TRACE, "ioctl(%#x, %#lx)", cmd, arg);

   nano_set_wakeup(nrxdev, arg);
   err = 0;
   return err;
}

#ifdef HAVE_UNLOCKED_IOCTL
static long
proc_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
   return proc_ioctl_body(nrxdev_from_file(file), cmd, arg);
}
#else
static int
proc_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
   return proc_ioctl_body(nrxdev_from_file(file), cmd, arg);
}
#endif

unsigned
mmc_set_clock(struct mmc_host *host, unsigned clk)
{
   struct mmc_ios *ios = &host->ios;
   unsigned  prev_clk = ios->clock;
   ios->clock = clk;
   host->ops->set_ios(host, ios);
   return prev_clk;
}

static void
do_rx(struct nrxdev *nrxdev)
{
   struct sdio_func *func = nrxdev->func;
   int err;
   struct sk_buff *skb = NULL;
   unsigned short len;

   sdio_claim_host(func);

#ifdef KSDIO_HOST_GPIO_IRQ
   sdio_f0_writeb(nrxdev->func, 0x02, CCCR_INTACK, &err);
   if (err != 0) {
      KDEBUG(ERROR, "Ack irq failed, err %d", err);
   }
#endif

   err = sdio_readsb(func, nrxdev->rx_hic_start, 0, KSDIO_HIC_MIN_SIZE);
   len = (*(unsigned short *)nrxdev->rx_hic_start) + 2;
   KDEBUG(TRANSPORT, "Receiving %d bytes", len);
   if (err != 0) {
      KDEBUG(ERROR, "readsb() failed, err %d", err);
      printk("readsb() failed, err %d\n", err);
      goto exit;
   } else if (len > HIC_MAX_SIZE) {
      KDEBUG(ERROR, "Invalid length %u", len);
      printk("Invalid length %u\n", len);
      KDEBUG(ERROR, "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x",
               nrxdev->rx_hic_start[0], nrxdev->rx_hic_start[1],
               nrxdev->rx_hic_start[2], nrxdev->rx_hic_start[3],
               nrxdev->rx_hic_start[4], nrxdev->rx_hic_start[5],
               nrxdev->rx_hic_start[6], nrxdev->rx_hic_start[7]);
      goto exit;
   }
   else if(nrxdev->rx_hic_start[0] == nrxdev->rx_hic_start[1] && 
           nrxdev->rx_hic_start[0] == nrxdev->rx_hic_start[2] &&
           nrxdev->rx_hic_start[0] == nrxdev->rx_hic_start[3])
   {
      printk("Invalid data\n");
      KDEBUG(ERROR, "Invalid data");
      KDEBUG(ERROR, "Buffer: %02x %02x %02x %02x %02x %02x %02x %02x",
               nrxdev->rx_hic_start[0], nrxdev->rx_hic_start[1],
               nrxdev->rx_hic_start[2], nrxdev->rx_hic_start[3],
               nrxdev->rx_hic_start[4], nrxdev->rx_hic_start[5],
               nrxdev->rx_hic_start[6], nrxdev->rx_hic_start[7]);
      goto exit;
   } else {
      if ((skb = dev_alloc_skb(len)) == NULL) {
         KDEBUG(ERROR, "Out of memory (%u bytes)", len);
         printk("Out of memory (%u bytes)\n", len);
         goto exit;
      }
      memcpy(skb_put(skb,KSDIO_HIC_MIN_SIZE), nrxdev->rx_hic_start,
             KSDIO_HIC_MIN_SIZE);
      if (len > KSDIO_HIC_MIN_SIZE) {
         len -= KSDIO_HIC_MIN_SIZE;
         err = sdio_readsb(func, skb_put(skb, len), 0, len);
         if (err != 0) {
            KDEBUG(ERROR, "readsb() failed, err %d", err);
            printk("readsb() failed, err %d\n", err);
            dev_kfree_skb(skb); 
            skb = NULL;
         }
      }
   }

exit:
   sdio_release_host(func);

   if (skb) {
      KDEBUG(TRANSPORT, "RX(%u bytes)", skb->len);
      PRINTBUF(skb->data, skb->len, "RX");

#ifdef USE_WIFI
      if (nrxdev->opened) {
         skb_queue_tail(&nrxdev->rx, skb);
         wake_up(&nrxdev->readq);
      } else if (!nrxdev->netdev) {
         KDEBUG(ERROR, "Dropping buffer (%u bytes)", skb->len);
         dev_kfree_skb(skb);
      } else
#endif /* USE_WIFI */
      {
         skb_queue_tail(&nrxdev->rx, skb);
#ifdef USE_THREAD_RX
         up(&nrxdev->rx_to_net_thread_sem);
#else
         QUEUE_WORK(&nrxdev->rx_to_net_work);
#endif /* USE_THREAD_RX */
      }
   }
}

static void
do_tx(struct nrxdev *nrxdev)
{
   struct sdio_func *func = nrxdev->func;
   int err;

   sdio_claim_host(func);

   while (!skb_queue_empty(&nrxdev->tx)) {
      struct sk_buff *skb = skb_dequeue(&nrxdev->tx);
      if (skb->len == 4) {
         unsigned wakeup = *(unsigned *)skb->data;
#ifdef KSDIO_WAKEUP_CLOCK
         unsigned prev_clk = 0;
         if (wakeup) {
            prev_clk = mmc_set_clock(func->card->host, KSDIO_WAKEUP_CLOCK);
      }
#endif
         sdio_f0_writeb(func, wakeup, CCCR_WAKEUP, &err);
         KDEBUG(TRANSPORT, "set_wakeup(%d) = %d", wakeup, err);
#ifdef KSDIO_WAKEUP_CLOCK
         if (wakeup) {
            mmc_set_clock(func->card->host, prev_clk);
         }
#endif
      } else {
         err = sdio_writesb(nrxdev->func, 0, skb->data, skb->len);
         if (err) {
            nano_util_printbuf(skb->data, skb->len, "TX");
            printk("TX(%u bytes), err=%d\n", skb->len, err);
         }
         KDEBUG(TRANSPORT, "TX(%u bytes), err=%d", skb->len, err);
         PRINTBUF(skb->data, skb->len, "TX");
      }
      dev_kfree_skb(skb);
   }

   sdio_release_host(func);
}

/********************************************************************/
/* About IRQ-s from NRX-chip                                        */
/* We are usualy using one of two methods of requesting attention   */
/* from the host, GPIO or SDIO IRQ:s                                */
/* SDIO-irqs are done according to SDIO-standard which is ofcourse  */
/* a great bennefit.                                                */
/* GPIO-irqs use a toggeling GPIO which is faster because it does   */
/* not require ACK with CMD52, reading data is considered an ACK    */
/********************************************************************/

#ifdef KSDIO_HOST_GPIO_IRQ
/*
 * Interrupt service-routine that handles GPIO-interrupts from target.
 */
irqreturn_t nrx_gpio_isr(int irq_no, void* data)
{
   struct nrxdev *nrxdev = data;
   
#ifdef USE_THREAD_RX
   up(&nrxdev->rx_thread_sem);
#else
   QUEUE_WORK(&nrxdev->rx_work);
#endif
   
   return IRQ_HANDLED;
}

#else /* !KSDIO_HOST_GPIO_IRQ */

/*
 * Interrupt service-routine that handles SDIO-interrupts from target.
 */
static void nrx_sdio_isr(struct sdio_func *func)
{
   struct nrxdev *nrxdev = dev_get_drvdata(&func->dev);
   {
      int err;
      sdio_f0_writeb(nrxdev->func, 0x02, CCCR_INTACK, &err);
      if (err != 0) {
         KDEBUG(ERROR, "Ack irq failed, err %d", err);
         printk("[nano] Ack irq failed, err %d\n", err);
      }
   }

#ifdef USE_THREAD_RX
   up(&nrxdev->rx_thread_sem);
#else
   QUEUE_WORK(&nrxdev->rx_work);
#endif
}

#endif /* KSDIO_HOST_GPIO_IRQ */

/*
 * Configure and enable IRQ:s, unless that is already done
 * This function will NOT clear any IRQ that is already queued
 *
 * Returns 0 on success, else returns linux error code
 */
static int nrx_enable_irq(struct nrxdev *nrxdev)
{
   int err;

   if (nrxdev->irq_claimed) {
      KDEBUG(ERROR, "IRQ already enabled");
      return 0;
   }

#ifdef KSDIO_HOST_GPIO_IRQ
   err = request_irq(KSDIO_HOST_GPIO_IRQ, nrx_gpio_isr, 0x00000000,
                     "SDIO_NRX", nrxdev);
   if (err) {
      KDEBUG(ERROR, "request_irq failed, err %d", err);
      return err;
   }
     
   err = set_irq_type(KSDIO_HOST_GPIO_IRQ, IRQ_TYPE_EDGE_BOTH);
   if (err) {
      KDEBUG(ERROR, "set_irq_type failed, err %d", err);
      return err;
   }
#else /* !KSDIO_HOST_GPIO_IRQ */
   err = sdio_claim_irq(nrxdev->func, nrx_sdio_isr);
   if (err) {
      KDEBUG(ERROR, "nano_ksdio: Failed to claim IRQ, err %d", err);
      return err;
   }
#endif /* KSDIO_HOST_GPIO_IRQ */

   KDEBUG(TRACE, "IRQ enabled");
   nrxdev->irq_claimed = 1;
   return 0;
}

/*
 * Disable IRQ:s
 * This function will NOT clear any IRQ that are already queued
 *
 * Returns 0 on success, else returns linux error code
 */
static int nrx_disable_irq(struct nrxdev *nrxdev)
{
   int err = 0;

   if (!nrxdev->irq_claimed) {
      KDEBUG(ERROR, "IRQ already disabled");
      return err;
   }

#ifdef KSDIO_HOST_GPIO_IRQ
   free_irq(KSDIO_HOST_GPIO_IRQ, nrxdev);
#else
   err = sdio_release_irq(nrxdev->func);
#endif
   KDEBUG(TRACE, "IRQs disabled");
   nrxdev->irq_claimed = 0;
   return err;
}


#define MMC_CMD_RETRIES 1

static
int mmc_send_relative_addr(struct mmc_card *card, unsigned int *rca)
{
   int err;
   struct mmc_command cmd;

   memset(&cmd, 0, sizeof(struct mmc_command));

   cmd.opcode = SD_SEND_RELATIVE_ADDR;
   cmd.arg = 0;
   cmd.flags = MMC_RSP_R6 | MMC_CMD_BCR;

   err = mmc_wait_for_cmd(card->host, &cmd, MMC_CMD_RETRIES);
   if (err)
      return err;

   *rca = cmd.resp[0] >> 16;

   return 0;
}

static int mmc_select_card(struct mmc_card *card)
{
   int err;
   struct mmc_command cmd;

   memset(&cmd, 0, sizeof(struct mmc_command));

   cmd.opcode = MMC_SELECT_CARD;
   cmd.arg = card->rca << 16;
   cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

   err = mmc_wait_for_cmd(card->host, &cmd, MMC_CMD_RETRIES);
   if (err)
      return err;

   return 0;
}

static int mmc_io_rw_direct(struct mmc_card *card, unsigned int arg)
{
   int err;
   struct mmc_command cmd;

   memset(&cmd, 0, sizeof(struct mmc_command));

   cmd.opcode = SD_IO_RW_DIRECT;
   cmd.arg = arg;
   cmd.flags = MMC_RSP_R5 | MMC_CMD_AC;

   err = mmc_wait_for_cmd(card->host, &cmd, MMC_CMD_RETRIES);
   if (err)
      return err;

   return 0;
}

static int
nrx_init(struct sdio_func *func) 
{
   unsigned rca = 0;
   int err;

   err = mmc_send_relative_addr(func->card, &rca);
   KDEBUG(TRACE, "mmc_set_relative_addr() = %d", err);
   if (err) return err;

   KDEBUG(TRACE, "Card using SDIO address %u", rca);
   func->card->rca = rca;

   err = mmc_select_card(func->card);
   KDEBUG(TRACE, "mmc_select_card() = %d", err);
   if (err) return err;

   /* Enable card */
   err = sdio_enable_func(func);
   KDEBUG(TRACE, "sdio_enable_func() = %d", err);
   if (err) return err;

#ifndef KSDIO_NO_4_BIT_MODE
   /* Enable 4bit mode */
   err = mmc_io_rw_direct(func->card, 0x88000E02);
   KDEBUG(TRACE, "mmc_io_rw_direct(enable 4-bit SDIO) = %d", err);
   func->card->host->ios.bus_width = MMC_BUS_WIDTH_4;
   func->card->host->ops->set_ios(func->card->host, &func->card->host->ios);
#endif

#ifdef KSDIO_HIGH_SPEED
   /* Enable high speed */
   sdio_f0_writeb(func, 0x01, CCCR_HISPEED_EN, &err);
   KDEBUG(TRACE, "CCCR_HISPEED_EN, err %d", err);
   if (err) printk ("CCCR_HISPEED_EN, err %d\n", err);
   mmc_card_set_highspeed(func->card);           
   func->card->host->ios.timing = MMC_TIMING_SD_HS;
   func->card->host->ops->set_ios(func->card->host, &func->card->host->ios);
#endif

#ifdef KSDIO_FORCE_CLOCK
   mmc_set_clock(func->card->host, KSDIO_FORCE_CLOCK);
   printk("%s: Clock switched to %uMHz\n", sdio_func_id(func), func->card->host->ios.clock/1000000);
#endif

   /* Enable SDIO interrupt generation without need for running CLK */
   sdio_f0_writeb(func, 0x01, CCCR_NOCLK_INT, &err);
   KDEBUG(ERROR, "CCCR_NOCLK_INT, err %d", err);

#ifndef KSDIO_HOST_GPIO_IRQ
   /* Enable interrupts */
   err = mmc_io_rw_direct(func->card, 0x88000803);
   KDEBUG(TRACE, "mmc_io_rw_direct(enable IRQ) = %d", err);
#endif

   return err;
}

#define mmc_card_clear_highspeed(c) ((c)->state &= ~MMC_STATE_HIGHSPEED)

static int
nrx_reset(struct sdio_func *func) 
{
   int err;
   struct nrxdev *nrxdev = dev_get_drvdata(&func->dev);

#if defined(KSDIO_HOST_RESET_PIN)

   err = host_gpio_set_level(KSDIO_HOST_RESET_PIN, 0);
   mdelay(3);  /* let SHUTDOWN_N assertion take effect */
   if (!err) {
      err = host_gpio_set_level(KSDIO_HOST_RESET_PIN, 1);
      mdelay(10); /* let NRX600 to reset */
   }

#else /* !defined(KSDIO_HOST_RESET_PIN) */
   {
      unsigned char cccr_reset_val = 0;
      
      /* Reset bits in CCCR_RESET are active high for NRX600
       * and they are active low for NRX700
       */
      switch (nrxdev->chip_type) {
         case CHIP_TYPE_NRX700:
            cccr_reset_val = 0x00; break; 
         case CHIP_TYPE_NRX600:
            cccr_reset_val = 0x03; break;
         default:
            KDEBUG(ERROR, "Unknown chip type!");
            BUG();
      }
      sdio_f0_writeb(func, cccr_reset_val, CCCR_RESET, &err);

      /* Wait until the chip exits the reset state.
       * The necessary time has been measured to approx 8 ms for NRX600.
       */  
      mdelay(10);
   }
#endif /* KSDIO_HOST_RESET_PIN */

   if (!err) {
      /* Since we have just reset the chip, its SD module goes back to
       * identification state. We have to reporgram the SD host controller
       * clock, timing and bus width are compatible with this state.
       */
      struct mmc_host *host = func->card->host;
      mmc_card_clear_highspeed(func->card);
      nrxdev->init_mmc_ios.clock = 400000; /* 400kHz */
      nrxdev->init_mmc_ios.timing = MMC_TIMING_LEGACY;
      nrxdev->init_mmc_ios.bus_width = MMC_BUS_WIDTH_1;
      host->ops->set_ios(host, &nrxdev->init_mmc_ios);
      KDEBUG(TRACE, "Reseted MMC ios after chip reset\n");
   }

   KDEBUG(TRACE, "EXIT: err = %d", err);
   return err;
}

static int
nano_download(const void *buf, size_t size, void *_nrxdev)
{
   struct nrxdev *nrxdev = _nrxdev;
   struct sdio_func *func = nrxdev->func;
   unsigned char *data = (unsigned char *) buf;
   unsigned char * load_data;
   size_t remain = size;
   unsigned int chunk = 0;
   int err = 0;
#ifdef KSDIO_FW_DOWNLOAD_CLOCK
   unsigned prev_clk;
#endif

   KDEBUG(TRACE, "Loading firmware %u bytes", size);

   sdio_claim_host(func);

   /* We can sometimes get false IRQ:s, especialy if we use GPIO-irq */
   nrx_disable_irq(nrxdev);

   err = nrx_reset(func);
   if (err) goto exit;
   err = nrx_init(func);
   if (err) goto exit;

   load_data = kmalloc(KSDIO_FW_CHUNK_LEN, GFP_KERNEL | GFP_DMA);
   if (!load_data) {
      KDEBUG(ERROR, "kmalloc failed");
      printk("[nano] kmalloc failed\n");
      err = -ENOMEM;
      goto exit;
   }

#ifdef KSDIO_FW_DOWNLOAD_CLOCK
   /* What is this?
    * If the chip runs on a clock slower than 40MHz the CPU will run slowly untill FW is downloaded.
    * It can therefor have some issues with accepting to fast fw download.
    * 16MHz SDIO-clock seems to work well for 26MHz XO.
    * It is safe to decrease this as much as you like.
    */
   prev_clk = mmc_set_clock(func->card->host, KSDIO_FW_DOWNLOAD_CLOCK);
#endif

   while (remain > 0) {
      int write_len;
      size_t chunk_len = min(remain, (unsigned) KSDIO_FW_CHUNK_LEN);
      memcpy(load_data, data, chunk_len);
      write_len = chunk_len;
      if (write_len % KSDIO_SIZE_ALIGN)
          write_len += KSDIO_SIZE_ALIGN - (write_len % KSDIO_SIZE_ALIGN);
      err = sdio_writesb(func, 0, load_data, write_len);
      if (err) {
         printk("fw download failed, err = %d "
                "- chunk #%u, %zu bytes (%d written), %zu bytes remain\n",
                err, chunk, chunk_len, write_len, remain);
         break;
      }
      remain -= chunk_len;
      data   += chunk_len;
      chunk++;
   }

#ifdef KSDIO_FW_DOWNLOAD_CLOCK
   /* Restore the SDIO-clock */
   mmc_set_clock(func->card->host, prev_clk);
   KDEBUG(TRACE, "SDIO clock back to %u", prev_clk);
#endif
   
   if (err == 0) {
      KDEBUG(TRACE, "FW Download success");
      /* Give fw time to restart. Measured req time for NRX700
       * (orfr 090415) was 3.5 ms with some margin
       */
      mdelay(15); 
      
      sdio_f0_writeb(nrxdev->func, 0x02, CCCR_INTACK, &err);
      if (err != 0) {
         KDEBUG(ERROR, "Ack irq failed, err %d", err);
         printk("[nano] Ack irq failed, err %d\n", err);
      }
      	
      err = nrx_enable_irq(nrxdev);
      if (err) {
         KDEBUG(ERROR, "Failed to enable IRQs, err = %d", err);
         printk("Failed to enable IRQs, err = %d \n", err);
         sdio_disable_func(func);
         goto exit;
      }
   }

   kfree(load_data);
   err = sdio_set_block_size(func, KSDIO_BLOCK_SIZE);
   KDEBUG(TRACE, "sdio_set_block_size() = %d", err);

exit:
   sdio_release_host(func);
   return err;
}


static void __attribute__((unused))
nrx_dump_regs(struct nrxdev *nrxdev)
{
#ifdef WIFI_DEBUG_ON
   unsigned i;
   int err;

   for (i=0; i<16; i++) {
      unsigned b = sdio_f0_readb(nrxdev->func, i, &err);
      KDEBUG(TRACE, "CCCR[%02x]=%#x err=%d", i, b, err);
   }
   for (i=0xF0; i<0xFF; i++) {
      unsigned b = sdio_f0_readb(nrxdev->func, i, &err);
      KDEBUG(TRACE, "CCCR[%02x]=%#x err=%d", i, b, err);
   }
#endif /* WIFI_DEBUG_ON */
}

struct file_operations proc_fops = {
   .owner   = THIS_MODULE,
   .open    = proc_open,
   .release = proc_release,
   .read    = proc_read,
   .write   = proc_write,
#ifdef HAVE_UNLOCKED_IOCTL
   .unlocked_ioctl   = proc_unlocked_ioctl,
#else
   .ioctl   = proc_ioctl,
#endif
   .poll    = proc_poll
};

static int nrx_soft_shutdown(struct sdio_func *func)
{
   /* put hardware in soft shutdown mode */
   unsigned char *tmp;
   const unsigned char sleep_request[2+4+4+2+4+1+2+4+1] = {
      0x04, 0x00,
      0x00, 0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x01, 0x00,
      0x19, 0x00, 0xfc, 0xff,
      0x07, 
      0x01, 0x00,
      0x00, 0x00, 0xfc, 0xff,
      0x01
   };
   int ret;

   KDEBUG(TRACE, "ENTRY");
   if(func->device == 0) {
      /* this code is for NRX600 B11 only */
      return 0;
   }
   KDEBUG(TRACE, "sz %u", sizeof(sleep_request));
   tmp = kmalloc(sizeof(sleep_request), GFP_KERNEL|GFP_DMA);
   if(tmp == NULL) {
      KDEBUG(ERROR, "nano_ksdio: Failed to allocate memory");
      /* sdio_release_host(func); */
      return -ENOMEM;
   }
   memcpy(tmp, sleep_request, sizeof(sleep_request));
   KDEBUG(TRACE, "mblock %u sz %u", func->card->cccr.multi_block,
          func->max_blksize);
   ret = sdio_writesb(func, 0, tmp, sizeof(sleep_request));
   kfree(tmp);
   if(ret != 0) {
      KDEBUG(ERROR, "nano_ksdio: Failed to enter shutdown mode, err %d", ret);
      printk("[nano] failed to enter soft shutdown mode\n");
   }
   return ret;
}

#ifdef USE_WIFI

/* Called to destroy or to suspend the network interface.
 * If KSDIO_IGNORE_REMOVE is defined, the network interface is never
 * destroyed, unless this module is unloaded. Instead, the network
 * interface is detached from the device when the device is removed.
 * But if hard shutdown is also done (symbol KSDIO_HOST_RESET_PIN is
 * also defined) the network interface is not even detached, else the
 * user could not bring the network interface up after forcing it down.
 */
static void nano_netif_off(struct nrxdev *nrxdev)
{
#ifdef KSDIO_IGNORE_REMOVE
#ifndef KSDIO_HOST_RESET_PIN
   nanonet_detach(nrxdev->netdev, nrxdev);
#endif /* KSDIO_HOST_RESET_PIN */
   nrxdev->netdev = NULL;
#else /* !KSDIO_IGNORE_REMOVE */
   if (nrxdev->netdev != NULL) {
      nanonet_destroy(nrxdev->netdev);
      nrxdev->netdev = NULL;
   }
#endif /* KSDIO_IGNORE_REMOVE */
}

/* Called to destroy or to suspend the network interface.
 * See comments at nano_netif_off()
 */
static int nano_netif_on(struct nrxdev *nrxdev,
                         struct nanonet_create_param *create_param)
{
#ifdef KSDIO_IGNORE_REMOVE
   if (netdev_saved) {
      nrxdev->netdev = netdev_saved;
#ifndef KSDIO_HOST_RESET_PIN
      nanonet_attach(nrxdev->netdev, nrxdev);
#endif /* KSDIO_HOST_RESET_PIN */
   } else
#endif /* KSDIO_IGNORE_REMOVE*/
   {
      nrxdev->netdev = nanonet_create(&nrxdev->func->dev,
                                      nrxdev, create_param);
      if (!nrxdev->netdev) {
         KDEBUG(ERROR, "nanonet_create failed!");
         printk("[nano] nanonet_create failed!\n");
         return -ENOSYS;
      }
#ifdef KSDIO_IGNORE_REMOVE
      netdev_saved = nrxdev->netdev;
#endif /* KSDIO_IGNORE_REMOVE*/
   }
   return 0;
}

#endif /* USE_WIFI */

static int sdio_nrx_identify(chip_type_t *chip_type, struct sdio_func *func)
{
   *chip_type = CHIP_TYPE_UNKNOWN;
   if (func->vendor != VENDOR_NANORADIO)
      return -EINVAL;

   switch (func->device & DEVICE_ID_MASK) {
      case DEVICE_ID_NRX600:
         *chip_type = CHIP_TYPE_NRX600;
         return 0;
      case DEVICE_ID_NRX700:
         *chip_type = CHIP_TYPE_NRX700;
         return 0;
      default:
         return -EINVAL;
   }
}

static int sdio_nrx_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
   struct nrxdev *nrxdev;
   int err;
   chip_type_t chip_type;
   const char* chip_name = chip_names[0];

   err = sdio_nrx_identify(&chip_type, func);
   if (err) {
      printk("%s: sdio_nrx_probe failed, unknown device\n", sdio_func_id(func));
         return err;
   }

   if (chip_type < sizeof(chip_names) / sizeof(chip_names[0]))
      chip_name = chip_names[chip_type];

   printk("%s: sdio_nrx_probe (class %u, vendor %04x, device %04x), target is %s\n",
          sdio_func_id(func), func->class, func->vendor, func->device, chip_name);
          
#ifdef KSDIO_CHIP_TYPE
   if (chip_type != KSDIO_CHIP_TYPE) {
      printk("%s: sdio_nrx_probe failed, unsupported device\n", sdio_func_id(func));
      return -EINVAL;
   }
#endif

   if (nrxdev_prev != NULL && !nrxdev_prev->is_in_hard_shutdown) {
      KDEBUG(ERROR, "Driver already loaded for %s:"
                    " class %u, vendor %04x, device %04x\n",
                    sdio_func_id(nrxdev_prev->func),
                    nrxdev_prev->func->class,
                    nrxdev_prev->func->vendor,
                    nrxdev_prev->func->device);
      return -EFAULT;
   }

#ifdef KSDIO_HOST_RESET_PIN
   if (nrxdev_saved)
      nrxdev = nrxdev_saved;
   else
#endif
   {
      nrxdev = kmalloc(sizeof *nrxdev, GFP_KERNEL);
      if (nrxdev == NULL) {
          KDEBUG(ERROR, "kmalloc(%u) failed", sizeof *nrxdev);
          return -ENOMEM;
      }
      nrxdev_prev = nrxdev;
      memset(nrxdev, 0, sizeof *nrxdev);
   }

   dev_set_drvdata(&func->dev, nrxdev);
   nrxdev->func = func;
   spin_lock_init(&nrxdev->tx_lock);
   
   /* Set up the bottom half handler */
#ifdef USE_THREAD_RX
   sema_init(&nrxdev->rx_thread_sem, 0);
   sema_init(&nrxdev->rx_to_net_thread_sem, 0);
   init_completion(&nrxdev->rx_thread_exited);
   init_completion(&nrxdev->rx_to_net_thread_exited);
   nrxdev->rx_thread_pid = kernel_thread(do_rx_thread, nrxdev, 0);
   nrxdev->rx_to_net_thread_pid = kernel_thread(do_rx_to_net_thread, nrxdev, 0);
#else
   INIT_WORK(&nrxdev->rx_work, do_rx_work);
   INIT_WORK(&nrxdev->rx_to_net_work, do_rx_to_net_work);
#endif
   
#ifdef USE_THREAD_TX
   sema_init(&nrxdev->tx_thread_sem, 0);
   init_completion(&nrxdev->tx_thread_exited);
   nrxdev->tx_thread_pid = kernel_thread(do_tx_thread, nrxdev, 0);
#else
   INIT_WORK(&nrxdev->tx_work, do_tx_work);
#endif
   
   init_waitqueue_head(&nrxdev->readq);
   skb_queue_head_init(&nrxdev->rx);
   skb_queue_head_init(&nrxdev->tx);
   nrxdev->chip_type = chip_type;
   nrxdev->is_in_hard_shutdown = 0;
   nrxdev->init_mmc_ios = func->card->host->ios;
   printk("%s: Initial clock %uMHz, timing mode %u, bus_width %u\n",
          sdio_func_id(func), nrxdev->init_mmc_ios.clock/1000000,
          (unsigned) nrxdev->init_mmc_ios.timing,
          (unsigned) nrxdev->init_mmc_ios.bus_width);

#ifdef KSDIO_SEPARATE_WORKQUEUE
   nrxdev->workqueue = create_workqueue("nano_wq");
   if (nrxdev->workqueue == NULL) {
     KDEBUG(ERROR, "nano workqueue creation failed\n");     
     return -ENOMEM;
   }
#endif /* KSDIO_SEPARATE_WORKQUEUE */

#ifdef KSDIO_NO_BLOCK_MODE
   printk("Disable block mode cap\n");
   if (func->card->cccr.multi_block) {
      func->card->cccr.multi_block = 0;
   }
   /* If no block mode is used, we set func->max_blksize = 512,
    * to overcome that NRX (wrongly) reports max_size 16.
    */
   func->max_blksize = 512;

#endif /* KSDIO_NO_4_BIT_MODE */

   sdio_claim_host(func);

   err = sdio_enable_func(func);
   if (err) {
      KDEBUG(ERROR, "nano_ksdio: Failed to enable function, err %d", err);
      sdio_release_host(func);
      return err;
   }

   err = nrx_reset(func);
   if (err) {
      KDEBUG(ERROR, "nano_ksdio: Failed to reset target, err %d", err);
      sdio_disable_func(func);
      sdio_release_host(func);
      return err;
   }

   err = nrx_init(func);
   if (err) {
      KDEBUG(ERROR, "nano_ksdio: Failed to init target, err %d", err);
      sdio_disable_func(func);
      sdio_release_host(func);
      return err;
   }
   
   err = nrx_soft_shutdown(func);
   if (err) {
      KDEBUG(ERROR, "nano_ksdio: nrx_soft_shutdown failed, err %d", err);
      sdio_disable_func(func);
      sdio_release_host(func);
      return err;
   }

   sdio_release_host(func);

#ifdef PROCFS_HIC
    proc =  proc_create_data("hic", 0666, NULL, &proc_fops, nrxdev);
#endif

#ifdef USE_WIFI
   if (!fwDownload)
      create_param.fw_download = NULL;

   create_param.chip_type = nrxdev->chip_type;
   create_param.params_len = host_get_target_params(&create_param.params_buf);

   err = nano_netif_on(nrxdev, &create_param);
   if (err)
      return err;
#else /* !USE_WIFI */
   loopback_nrxdev = nrxdev;
   kernel_thread(do_test, 0, 0);
#endif  /* USE_WIFI */

   return 0;
}

static void sdio_nrx_remove(struct sdio_func *func)
{
   struct nrxdev *nrxdev = dev_get_drvdata(&func->dev);
   struct sk_buff *skb;
   int err;

   KDEBUG(TRACE, "%s: sdio_nrx_remove", sdio_func_id(func));

#ifdef USE_WIFI
   nano_netif_off(nrxdev);
#endif /* USE_WIFI */

   while ((skb=skb_dequeue(&nrxdev->tx)) != NULL) {
      dev_kfree_skb_any(skb);
   }

   sdio_claim_host(nrxdev->func);
   nrx_disable_irq(nrxdev);

   /* sdio_nrx_remove might be called when the hardware is already removed,
    * in that case is it totaly normal that this will fail. sdio_nrx_remove
    * might also be called when the hw is present but the host is going to
    * sleep soon, or our driver will be unloaded. In that case we should
    * shut down to save power.
    */
#if 0 // allwinner specific
   if (nrx_reset(func) == 0 && nrx_init(func) == 0)
      nrx_soft_shutdown(func);
#endif
   err = nrx_reset(func);
   KDEBUG(TRACE, "nrx_reset returned %d", err);
   if (err) printk("nrx_reset returned %d\n", err);
   //sdio_disable_func(nrxdev->func);
   sdio_release_host(nrxdev->func);

   flush_scheduled_work();
#ifdef KSDIO_SEPARATE_WORKQUEUE
   flush_workqueue(nrxdev->workqueue);
   destroy_workqueue(nrxdev->workqueue);
#endif

   while ((skb=skb_dequeue(&nrxdev->rx)) != NULL) {
      dev_kfree_skb_any(skb);
   }

#if defined(USE_THREAD_TX) || defined(USE_THREAD_RX)
   
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
#define KILL_PROC(pid, sig) \
   { \
       struct task_struct *tsk; \
       tsk = pid_task(find_vpid(pid), PIDTYPE_PID); \
       if (tsk) send_sig(sig, tsk, 1); \
   }
#else
#define KILL_PROC(pid, sig) \
   { \
       kill_proc(pid, sig, 1); \
   }
#endif 
   
   /* Remove threads */
   #ifdef USE_THREAD_TX 
   KILL_PROC(nrxdev->tx_thread_pid, SIGTERM);
   #endif
   
   #ifdef USE_THREAD_RX 
   KILL_PROC(nrxdev->rx_thread_pid, SIGTERM);
   KILL_PROC(nrxdev->rx_to_net_thread_pid, SIGTERM);
   #endif
   
   #ifdef USE_THREAD_TX 
   wait_for_completion(&nrxdev->tx_thread_exited);
   #endif
   
   #ifdef USE_THREAD_RX
   wait_for_completion(&nrxdev->rx_thread_exited);
   wait_for_completion(&nrxdev->rx_to_net_thread_exited);
   #endif
   
#undef KILL_PROC
#endif
   
/*
   remove_proc_entry("hic", NULL);
*/

   /* If not going into hard shutdown,
    * signal that there are no active devices.
    */
   if (!nrxdev->is_in_hard_shutdown && nrxdev_prev == nrxdev)
      nrxdev_prev = NULL;

#ifdef KSDIO_HOST_RESET_PIN
   nrxdev_saved = nrxdev;
#else
   kfree(nrxdev);
#endif
}

static const struct sdio_device_id sdio_nrx_ids[] = {
   { SDIO_DEVICE(VENDOR_NANORADIO, SDIO_ANY_ID) },
   { 0, 0, 0, 0 /* end: all zeroes */  }
};

MODULE_DEVICE_TABLE(sdio, sdio_nrx_ids);

static struct sdio_driver sdio_nrx_driver = {
    .probe      = sdio_nrx_probe,
    .remove     = sdio_nrx_remove,
    .name       = "nano_ksdio",
    .id_table   = sdio_nrx_ids,
};

static int __init sdio_nrx_init(void)
{
#ifdef KSDIO_HOST_RESET_PIN
   int status = host_gpio_allocate(KSDIO_HOST_RESET_PIN);
   if (status) {
      KDEBUG(ERROR, "Failed to allocate host GPIO for chip reset (status: %d)", status);
      return status;
   }

   /* In an ideal world, this is where we set the SHUTDOWN_N pin high and
    * tell the MMC/SD/SDIO driver to look for a new "card"....
    */
#endif
    
#if defined WINNER_POWER_ONOFF_CTRL && defined CONFIG_MMC_SUNXI_POWER_CONTROL
    nano_sdio_powerup();
    sunximmc_rescan_card(SDIOID, 1);
#endif

    return sdio_register_driver(&sdio_nrx_driver);
}

static void __exit sdio_nrx_exit(void)
{
#ifdef KSDIO_IGNORE_REMOVE
   if (netdev_saved != NULL) {
      nanonet_destroy(netdev_saved);
      netdev_saved = NULL;
   }
#endif
#ifdef KSDIO_HOST_RESET_PIN
   {
      int status;

      status = host_gpio_free(KSDIO_HOST_RESET_PIN);
      if (status)
         KDEBUG(ERROR, "Failed to free host GPIO for chip reset (status: %d)", status);
   }
#endif

    sdio_unregister_driver(&sdio_nrx_driver);
    
#if defined WINNER_POWER_ONOFF_CTRL && defined CONFIG_MMC_SUNXI_POWER_CONTROL
    nano_sdio_poweroff();
    sunximmc_rescan_card(SDIOID, 0);
#endif
}

#ifdef USE_WIFI
static int nano_hard_shutdown(struct nrxdev *nrxdev, uint32_t arg)
{
#ifdef KSDIO_HOST_RESET_PIN
   int ret = 0;

   /* nrxdev is ignored, we assume that _only_ one device exists */
   switch (arg) {
      case NANONET_HARD_SHUTDOWN_ENTER:
         if (!nrxdev->is_in_hard_shutdown) {
            nrxdev->is_in_hard_shutdown = 1;
            sdio_unregister_driver(&sdio_nrx_driver);
            ret = host_gpio_set_level(KSDIO_HOST_RESET_PIN, 0);
         }
         return ret;

      case NANONET_HARD_SHUTDOWN_EXIT:
         if (nrxdev->is_in_hard_shutdown) {
            ret = host_gpio_set_level(KSDIO_HOST_RESET_PIN, 1);
            if (ret)
               return ret;
            msleep(100);
            /* Kick off the kernel to probe the driver again! */
            sdio_register_driver(&sdio_nrx_driver);
            /* At this point, the kernel calls nrx_sdio_probe()
             * which will zero nrxdev->is_in_hard_shutdown
             * XXX Usually no sleep time is necessary here,
             * as nrx_sdio_probe() is apparently called from this thread
             * before sdio_register_driver() returns.
             * However, this may not hold for all platforms.
             */
         }
         return ret;

      case NANONET_HARD_SHUTDOWN_TEST:
         return 0;

      default:
         return -EINVAL;
   }
#else /* !KSDIO_HOST_RESET_PIN */
   return -EOPNOTSUPP;
#endif /* KSDIO_HOST_RESET_PIN */
}
#endif /* USE_WIFI */

#ifndef USE_WIFI

#include <de_trace.h>

#define MAX_PACKET_SIZE       1600
#define TRSP_ASSERT(x)        DE_ASSERT(x)
#define GPIO_START_MEASURE()
#define GPIO_STOP_MEASURE()
#define host_exit()

#define LOOPBACK_SEMA_INIT             7
#define LOOPBACK_HIC_MIN_SIZE          KSDIO_HIC_MIN_SIZE
#define LOOPBACK_SIZE_ALIGN            KSDIO_SIZE_ALIGN
#define LOOPBACK_HIC_CTRL_ALIGN_HATTN  0x01
#define LOOPBACK_FW_DOWNLOAD(a, b, c)  nano_download(a, b, loopback_nrxdev)
#define LOOPBACK_SEND(a)               nano_tx(a, loopback_nrxdev)
#define LOOPBACK_INTERRUPT(x)
#define LOOPBACK_SHUTDOWN_TARGET()
#include "../loopback/loopback.c"

#endif /* !USE_WIFI */

//////////////////////////////////////////////////////////// Packet Hundling part 

#if defined(USE_THREAD_RX) || defined(USE_THREAD_TX)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
allow_signal(SIGKILL); \
allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define RAISE_RX_SOFTIRQ() \
cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
    do { if (a) \
    strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
    } while (0);
#endif /* LINUX_VERSION_CODE  */
#endif /* USE_THREAD_RX || USE_THREAD_TX */
    
#ifdef USE_THREAD_RX
//////////////////////////////////////////////////////////////////////////////////////////////// Thread RX
static int
do_rx_thread(void *data)
{
    struct nrxdev *nrxdev = (struct nrxdev *) data;
    
    
    /* This thread doesn't need any user-level access,
     * so get rid of all our resources
     */
    if (thread_prio > 0)
    {
        struct sched_param param;
        param.sched_priority = (thread_prio < MAX_RT_PRIO)?thread_prio:(MAX_RT_PRIO-1);
        sched_setscheduler(current, SCHED_FIFO, &param);
    }
    
    set_freezable();
    
    DAEMONIZE("do_thread_rx");
    
    /* Run until signal received */
    while (1) {
        if (down_interruptible(&nrxdev->rx_thread_sem) == 0) {
            do_rx(nrxdev);
        }
        else
            break;
    }
    
    complete_and_exit(&nrxdev->rx_thread_exited, 0);
}

static int
    do_rx_to_net_thread(void *data)
{
    struct nrxdev *nrxdev = (struct nrxdev *) data;
    struct sk_buff *skb;   
    
    /* This thread doesn't need any user-level access,
    * so get rid of all our resources
    */
    if (thread_prio > 0)
    {
        struct sched_param param;
        param.sched_priority = (thread_prio < MAX_RT_PRIO)?thread_prio:(MAX_RT_PRIO-1);
        sched_setscheduler(current, SCHED_FIFO, &param);
    }
    
    set_freezable();
    
    DAEMONIZE("do_thread_rx_to_net");
    
    /* Run until signal received */
    while (1) {
        if (down_interruptible(&nrxdev->rx_to_net_thread_sem) == 0) {
            while ((skb = skb_dequeue(&nrxdev->rx)) != NULL) {
#ifdef USE_WIFI
               ns_net_rx(skb, nrxdev->netdev);
#else
               do_test_rx(skb);
#endif /* USE_WIFI */
            }
        }
        else
            break;
    }

    complete_and_exit(&nrxdev->rx_to_net_thread_exited, 0);
}

#else
//////////////////////////////////////////////////////////////////////////////////////////////// Work queue RX
static void
    do_rx_to_net_work(struct work_struct *work)
{
    struct sk_buff *skb;    
    struct nrxdev *nrxdev = container_of(work, struct nrxdev, rx_to_net_work);
    
    while ((skb = skb_dequeue(&nrxdev->rx)) != NULL) {
#ifdef USE_WIFI
         ns_net_rx(skb, nrxdev->netdev);
#else
         do_test_rx(skb);
#endif /* USE_WIFI */
     }
}


static void
    do_rx_work(struct work_struct *work)
{
    struct nrxdev *nrxdev = container_of(work, struct nrxdev, rx_work);
    
    do_rx(nrxdev);
}
#endif

#ifdef USE_THREAD_TX
//////////////////////////////////////////////////////////////////////////////////////////////// Thread TX
static int
do_tx_thread(void *data)
{
    struct nrxdev *nrxdev = (struct nrxdev *) data;
    
    
    /* This thread doesn't need any user-level access,
     * so get rid of all our resources
     */
    if (thread_prio > 0)
    {
        struct sched_param param;
        param.sched_priority = (thread_prio < MAX_RT_PRIO)?thread_prio:(MAX_RT_PRIO-1);
        sched_setscheduler(current, SCHED_FIFO, &param);
    }

    set_freezable();

    DAEMONIZE("do_thread_tx");

    /* Run until signal received */
    while (1) {
        if (down_interruptible(&nrxdev->tx_thread_sem) == 0) {
            do_tx(nrxdev);
        }
        else
            break;
    }

complete_and_exit(&nrxdev->tx_thread_exited, 0);
}


#else
//////////////////////////////////////////////////////////////////////////////////////////////// Work queue TX
static void
do_tx_work(struct work_struct *work)
{
    struct nrxdev *nrxdev = container_of(work, struct nrxdev, tx_work);
    
    do_tx(nrxdev);
}
#endif


module_init(sdio_nrx_init);
module_exit(sdio_nrx_exit);

MODULE_LICENSE("GPL");
