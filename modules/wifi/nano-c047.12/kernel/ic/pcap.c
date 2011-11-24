/* $Id: pcap.c 15801 2010-07-15 13:58:11Z joda $ */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/ctype.h>

#include "nanoutil.h"
#include "nanoparam.h"
#include "nanonet.h"
#include "wifi_engine.h"

#include "nrx_stream.h"
#include "px.h"

#if DE_ENABLE_PCAPLOG >= CFG_ON
/*------------------------------------------------------------*/
struct pcap_event {
   size_t size;
   size_t used;
   uint16_t flags;
   struct timeval time;
   WEI_TQ_ENTRY(pcap_event) next;
   unsigned char data[0];
};

static WEI_TQ_HEAD(, pcap_event) 
     pcap_free = WEI_TQ_HEAD_INITIALIZER(pcap_free),
   pcap_active = WEI_TQ_HEAD_INITIALIZER(pcap_active);
static spinlock_t pcap_free_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t pcap_active_lock = SPIN_LOCK_UNLOCKED;
/*------------------------------------------------------------*/

static int nrx_pcap_write(struct pcap_event *pe);

static struct nrx_stream *pcap_stream = NULL;
static char pcap_filename[128];
static unsigned long pcap_snaplen = 160;
static unsigned long pcap_snaplen_min = 64;
static unsigned long pcap_snaplen_max = 2000;

static int
nrx_pcap_write(struct pcap_event *pe)
{
   struct {
      uint32_t tv_sec;
      uint32_t tv_usec;
      uint32_t caplen;
      uint32_t len;
   } hdr;
   size_t len;
   uint16_t flags = cpu_to_le16(pe->flags);

   if(pcap_stream == NULL)
      return 0;

   len = pe->used + sizeof(flags);
   
   hdr.tv_sec = pe->time.tv_sec;
   hdr.tv_usec = pe->time.tv_usec;
   hdr.caplen = max((unsigned long)len, pcap_snaplen);
   hdr.len = len;

   nrx_stream_write(pcap_stream, &hdr, sizeof(hdr));
   nrx_stream_write(pcap_stream, &flags, sizeof(flags));
   nrx_stream_write(pcap_stream, pe->data, hdr.caplen - sizeof(flags));

   return 0;
}

#define PCAP_MAGIC 0xa1b2c3d4
#define PCAP_MAJOR 2
#define PCAP_MINOR 4
#define PCAP_LINKTYPE 147

static int
pcap_open(const char *filename)
{
   int ret;
   ssize_t len;
   struct nrx_stream *s;

   struct {
      uint32_t magic;
      uint16_t major;
      uint16_t minor;
      uint32_t timezone;
      uint32_t time_accuracy;
      uint32_t snap_len;
      uint32_t linktype;
   } pcap_header;

   KDEBUG(TRACE, "ENTRY");

   ret = nrx_stream_open_file(filename, O_RDWR | O_CREAT, 0600, &s);
   if(ret != 0)
      return ret;
   len = nrx_stream_read(s, &pcap_header, sizeof(pcap_header));
   if(len < 0) {
      KDEBUG(TRACE, "read error");
      nrx_stream_close(s);
      return len;
   }
   if(len == 0) {
      pcap_header.magic = PCAP_MAGIC;
      pcap_header.major = PCAP_MAJOR;
      pcap_header.minor = PCAP_MINOR;
      pcap_header.timezone = 0;
      pcap_header.time_accuracy = 0;
      pcap_header.snap_len = 2000;
      pcap_header.linktype = PCAP_LINKTYPE;
      len = nrx_stream_write(s, &pcap_header, sizeof(pcap_header));
      if(len < 0) {
         nrx_stream_close(s);
         return len;
      }
      if(len != sizeof(pcap_header)) {
         KDEBUG(TRACE, "short write");
         nrx_stream_close(s);
         return -EIO;
      }
   } else if(len != sizeof(pcap_header)) {
      KDEBUG(TRACE, "short read");
      nrx_stream_close(s);
      return -EIO;
   } else if(pcap_header.magic != PCAP_MAGIC ||
             pcap_header.major != PCAP_MAJOR ||
             pcap_header.minor != PCAP_MINOR ||
             pcap_header.linktype != PCAP_LINKTYPE) {
      KDEBUG(TRACE, "bad pcap format");
      nrx_stream_close(s);
      return -EIO;
   }
   pcap_stream = s;
   return 0;
}

static int
pcap_close(void)
{
   KDEBUG(TRACE, "ENTRY");
   
   if(pcap_stream != NULL) {
      nrx_stream_flush(pcap_stream);
      nrx_stream_close(pcap_stream);
      pcap_stream = NULL;
   }
   return 0;
}

static void pcap_process(void);

static unsigned long kthread_flags;
static DECLARE_WAIT_QUEUE_HEAD(kthread_wq);

static inline void jsleep(unsigned long timeout)
{
   while (timeout) {
      set_current_state(TASK_UNINTERRUPTIBLE);
      timeout = schedule_timeout(timeout);
   }
}

static int
pcap_kthread(void *context)
{
   KDEBUG(TRACE, "ENTRY");

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
   daemonize("pcap");
#else
   daemonize();
   strcpy(current->comm, "pcap");
#endif
    
   set_bit(1, &kthread_flags);
   while(test_bit(0, &kthread_flags)) {
      pcap_process();
      wait_event(kthread_wq, 
		 test_bit(0, &kthread_flags) == 0 ||
		 WEI_TQ_FIRST(&pcap_active) != NULL);
   }
	
   clear_bit(1, &kthread_flags);
   return 1;
}

SYSCTL_FUNCTION(nrx_pcap_handler)
{
   int status;
   char *p;
   
   KDEBUG(TRACE, "ENTRY file = %s, write = %d", pcap_filename, write);

   if(write) {
      clear_bit(0, &kthread_flags);
      wake_up(&kthread_wq);
      while(test_bit(1, &kthread_flags))
         jsleep(1);
      pcap_close();

      status = SYSCTL_CALL(proc_dostring);
      if (status) {
         KDEBUG(ERROR, "proc_dostring failed, status %d", status);
         return status;
      }

      for(p = pcap_filename; *p != '\0'; p++) {
         if(!isalnum(*p) && 
            *p != '_' && 
            *p != '.' && 
            *p != '/') {
            return 0;
         }
      }
      *p = '\0';
      KDEBUG(TRACE, "New pcap_filename: %s", pcap_filename);

      if(p == pcap_filename)
         return 0;

      if(pcap_open(pcap_filename) != 0) {
         KDEBUG(ERROR, "pcap_open failed");
         pcap_stream = NULL;
      } else {
         int pid;
         set_bit(0, &kthread_flags);
         pid = kernel_thread(pcap_kthread, NULL, CLONE_FS | CLONE_FILES | SIGCHLD);
         KDEBUG(TRACE, "pid == %u", pid);
      }
   } else {
      status = SYSCTL_CALL(proc_dostring);
   }

   return status;
}

static ctl_table nrx_pcap_ctable[] = {
  { SYSCTLENTRY(pcap_file, pcap_filename, nrx_pcap_handler) },
  { SYSCTLENTRY(pcap_snaplen, pcap_snaplen, proc_doulongvec_minmax), 
    .extra1 = &pcap_snaplen_min, .extra2 = &pcap_snaplen_max },
  { SYSCTLEND }
};
#endif /* DE_ENABLE_PCAPLOG >= CFG_ON */

int nrx_pcap_setup(void)
{
#if DE_ENABLE_PCAPLOG >= CFG_ON
   KDEBUG(TRACE, "ENTRY");

   nano_util_register_sysctl(nrx_pcap_ctable);
#endif
   return 0;
}

int nrx_pcap_cleanup(void)
{
#if DE_ENABLE_PCAPLOG >= CFG_ON
   struct pcap_event *pe;

   KDEBUG(TRACE, "ENTRY");

   pcap_close();
   
   spin_lock(&pcap_free_lock);
   while((pe = WEI_TQ_FIRST(&pcap_free)) != NULL) {
      WEI_TQ_REMOVE(&pcap_free, pe, next);
      memset(pe, 0, sizeof(pe) + pe->size);
      kfree(pe);
   }
   spin_unlock(&pcap_free_lock);

   spin_lock(&pcap_active_lock);
   while((pe = WEI_TQ_FIRST(&pcap_active)) != NULL) {
      WEI_TQ_REMOVE(&pcap_free, pe, next);
      memset(pe, 0, sizeof(pe) + pe->size);
      kfree(pe);
   }
   spin_unlock(&pcap_active_lock);
   
   nano_util_unregister_sysctl(nrx_pcap_ctable);
#endif
   return 0;
}

/*------------------------------------------------------------*/


#if DE_ENABLE_PCAPLOG >= CFG_ON
static struct pcap_event *pcap_new_event(size_t size)
{
   struct pcap_event *pe;
   
   size += sizeof(*pe);
   if(size < 2048)
      size = 2048;
   pe = kmalloc(size, GFP_ATOMIC);
   pe->size = size - sizeof(*pe);
   return pe;
}

static void set_current_time(struct timeval *tv)
{
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,35)
   struct timespec now;
   /* timespec */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,48)
   now = current_kernel_time();
#else
   now = xtime;
#endif
   tv->tv_sec = now.tv_sec;
   tv->tv_usec = now.tv_nsec / 1000;
#else /* < 2.5.35 */
   /* xtime is timeval */
   *tv = xtime;
#endif
#else /* 0 */
   /* should use xtime current_kernel_time */
   unsigned long j = jiffies;
   tv->tv_sec = j / HZ;
   tv->tv_usec = (j % HZ) * 1000000 / HZ;
#endif
}

static struct pcap_event *pcap_get_event(size_t size)
{
   struct pcap_event *pe;

   if(pcap_stream == NULL)
      return NULL;

   spin_lock(&pcap_free_lock);
   for(pe = WEI_TQ_FIRST(&pcap_free); pe != NULL; pe = WEI_TQ_NEXT(pe, next)) {
      if(pe->size >= size) {
	 WEI_TQ_REMOVE(&pcap_free, pe, next);
	 break;
      }
   }
   spin_unlock(&pcap_free_lock);
   if(pe == NULL)
      pe = pcap_new_event(size);
   set_current_time(&pe->time);
   return pe;
}

static void pcap_process(void)
{
   struct pcap_event *pe;

   while(1) {
      spin_lock(&pcap_active_lock);
      if((pe = WEI_TQ_FIRST(&pcap_active)) == NULL) {
         spin_unlock(&pcap_active_lock);
         break;
      }
      WEI_TQ_REMOVE(&pcap_active, pe, next);
      spin_unlock(&pcap_active_lock);
      nrx_pcap_write(pe);
      spin_lock(&pcap_free_lock);
      WEI_TQ_INSERT_TAIL(&pcap_free, pe, next);
      spin_unlock(&pcap_free_lock);
   }
   if(pcap_stream != NULL)
      nrx_stream_flush(pcap_stream);
}
#endif /* DE_ENABLE_PCAPLOG >= CFG_ON */

int
nrx_pcap_append(uint16_t flags, const void *pkt, size_t len)
{
#if DE_ENABLE_PCAPLOG >= CFG_ON
   struct pcap_event *pe;

   KDEBUG(TRACE, "ENTRY");
   pe = pcap_get_event(len);
   if(pe == NULL)
      return -ENOMEM;
   
   memcpy(pe->data, pkt, len);
   pe->used = len;
   pe->flags = flags;

   spin_lock(&pcap_active_lock);
   WEI_TQ_INSERT_TAIL(&pcap_active, pe, next);
   spin_unlock(&pcap_active_lock);
   wake_up(&kthread_wq);

#endif
   return 0;
}
EXPORT_SYMBOL(nrx_pcap_append);

void nrx_pcap_log_add(uint32_t id)
{
#if DE_ENABLE_PCAPLOG >= CFG_ON 
   unsigned char buf[] = { 0x08, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
			   0x00, 0x00 };
   struct pcap_event *pe;

   pe = pcap_get_event(sizeof(buf));
   if(pe == NULL)
      return;

   memcpy(pe->data, buf, sizeof(buf));
   pe->used = sizeof(buf);
   pe->flags = 0;

   memcpy(pe->data + 6, &id, sizeof(id));
   cpu_to_le32s(pe->data + 6);
   
   spin_lock(&pcap_active_lock);
   WEI_TQ_INSERT_TAIL(&pcap_active, pe, next);
   spin_unlock(&pcap_active_lock);
   wake_up(&kthread_wq);
#endif
}
EXPORT_SYMBOL(nrx_pcap_log_add);

