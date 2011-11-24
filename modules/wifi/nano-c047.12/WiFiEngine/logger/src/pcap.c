
#include "driverenv.h"

#if (DE_ENABLE_PCAPLOG > CFG_OFF)

#define PCAP_MAGIC 0xa1b2c3d4
#define PCAP_MAJOR 2
#define PCAP_MINOR 4
#define PCAP_LINKTYPE 147

#include "pcap.h"

#define LOCK_LOCKED 1

#define MAX_PCAP_FILES 100

struct pcap_ctx {
   driver_trylock_t lock;
   int file;
   WEI_TQ_HEAD(pcap_head, pcap_e) pcap_head;
	iobject_t *core_start;
	iobject_t *core_complete;
};

struct pcap_hdr {
   uint32_t tv_sec;
   uint32_t tv_usec;
   uint32_t caplen;
   uint32_t len;
};

struct pcap_e {
   void* data;
   size_t len;
   uint16_t flags;
   WEI_TQ_ENTRY(pcap_e) next; 
   struct pcap_hdr hdr;
};

struct pcap_ctx *g_pcap_ctx = NULL;

#if 0

static void pcap_core_dump_start(wi_msg_param_t param, void* priv)
{ nrx_pcap_close(); }

static void pcap_core_dump_complete(wi_msg_param_t param, void* priv)
{ nrx_pcap_open("/e/wlan/hic-core-%d.pcap"); }

#endif

static int prep_new_pcap_file(const char* path)
{
    char name[80];
    int i;
    int file;

    for(i=1 ; i <= MAX_PCAP_FILES ; i++) {
        DE_SNPRINTF(name, sizeof(name), path, i);
        file = MC_GFL_OPEN(name, GFL_O_RDONLY, GFL_S_IROTH);
        if(file>=0) {
            MC_GFL_CLOSE(file);
        } else { 
            return MC_GFL_OPEN(name, GFL_O_CREAT|GFL_O_WRONLY|GFL_O_TRUNC, GFL_S_IROTH);
        }
    }
    return -1;
}

int
nrx_pcap_open(const char* path)
{ 
   struct {
      uint32_t magic;
      uint16_t major;
      uint16_t minor;
      uint32_t timezone;
      uint32_t time_accuracy;
      uint32_t snap_len;
      uint32_t linktype;
   } pcap_header;
   int written = 0;

   struct pcap_ctx *ctx = g_pcap_ctx;

   if(ctx)
      return 0;

   ctx = (struct pcap_ctx*)DriverEnvironment_Malloc(sizeof(struct pcap_ctx));
   g_pcap_ctx = ctx;
   if(!ctx)
      return 0;

   DriverEnvironment_init_trylock(&ctx->lock);
   WEI_TQ_INIT(&ctx->pcap_head);

   ctx->file = prep_new_pcap_file(path);

   if(ctx->file<0) {
      DriverEnvironment_Free(ctx);
      g_pcap_ctx = NULL;
      return 0;
   }

   pcap_header.magic = PCAP_MAGIC;
   pcap_header.major = PCAP_MAJOR;
   pcap_header.minor = PCAP_MINOR;
   pcap_header.timezone = 0;
   pcap_header.time_accuracy = 0;
   pcap_header.snap_len = 2000;
   pcap_header.linktype = PCAP_LINKTYPE;
   written += MC_GFL_WRITE(ctx->file, &pcap_header, sizeof(pcap_header));
   if( written < sizeof(pcap_header) ) {
      DE_TRACE_INT3(TR_PCAP, "Ops, file write to short. expected %d got %d. ignoring closing file handle %d\n", 
            sizeof(pcap_header), written, ctx->file);
      /*
      MC_GFL_CLOSE(ctx->file);
      file = -1;
      */
   }

   DE_TRACE_INT(TR_PCAP, "file handle is %d\n", ctx->file);

#if 0
   we_ind_cond_register( &ctx->core_start,
		  WE_IND_CORE_DUMP_START,
		  "WE_IND_CORE_DUMP_START",
		  pcap_core_dump_start,NULL,0,NULL);

   we_ind_cond_register( &ctx->core_complete,
		  WE_IND_CORE_DUMP_COMPLETE,
		  "WE_IND_CORE_DUMP_COMPLETE",
		  pcap_core_dump_complete,NULL,0,NULL);
#endif

   return written;
}

int
nrx_pcap_close(void)
{
   struct pcap_ctx *ctx = g_pcap_ctx;
   if(!ctx || ctx->file<0)
      return 0;

   MC_GFL_CLOSE(ctx->file);
   DriverEnvironment_Free(ctx);
   g_pcap_ctx = NULL;
   return 1;
}


/* queue packets from other threads */
void
nrx_pcap_q(void* data, size_t len, uint16_t flags)
{
   struct pcap_e *e;
   struct pcap_ctx *ctx = g_pcap_ctx;

   if(!ctx || ctx->file<0)
      return;

   e = (struct pcap_e*)DriverEnvironment_Malloc(sizeof(struct pcap_e));
   if(!e)
      return;

   e->flags = flags;
   e->data = DriverEnvironment_Malloc(len);
   if(!e->data) {
      DriverEnvironment_Free(e);
      return;
   }

   e->hdr.tv_sec = DriverEnvironment_GetTicks();
   e->hdr.tv_usec = 0;
   e->hdr.caplen = len + sizeof(flags);
   e->hdr.len = len + sizeof(flags);
   DE_MEMCPY(e->data, data, len);
   e->len = len;

   if(DriverEnvironment_acquire_trylock(&ctx->lock) == LOCK_LOCKED) {
      DriverEnvironment_Free(e->data);
      DriverEnvironment_Free(e);
      return;
   }
   WEI_TQ_INSERT_TAIL(&ctx->pcap_head, e, next);

   DriverEnvironment_release_trylock(&ctx->lock);
}

static void
nrx_pcap_q_write(struct pcap_ctx *ctx)
{
   struct pcap_e *e = NULL;

   if(DriverEnvironment_acquire_trylock(&ctx->lock) == LOCK_LOCKED) {
      return;
   }

   WEI_TQ_FOREACH(e, &ctx->pcap_head, next) { 
      WEI_TQ_REMOVE(&ctx->pcap_head, e, next);
      DriverEnvironment_release_trylock(&ctx->lock);

      MC_GFL_WRITE(ctx->file, &e->hdr, sizeof(struct pcap_hdr));
      MC_GFL_WRITE(ctx->file, &e->flags, sizeof(e->flags));
      MC_GFL_WRITE(ctx->file, e->data, e->hdr.caplen - sizeof(e->flags));

      DriverEnvironment_Free(e->data);
      DriverEnvironment_Free(e);

      if(DriverEnvironment_acquire_trylock(&ctx->lock) == LOCK_LOCKED) {
         return;
      }
   }

   DriverEnvironment_release_trylock(&ctx->lock);
}

/* 
 * write packet and queued packets to disk. 
 * TODO: sort packets before writing to file.
 */
int
nrx_pcap_write(void* data, size_t len, uint16_t flags)
{
   int written = 0;
   struct pcap_hdr hdr;
   struct pcap_ctx *ctx = g_pcap_ctx;

   if(ctx==NULL)
      return 0;

   if(ctx->file<0) {
      DE_TRACE_INT(TR_PCAP, "no file handle (%d)\n", ctx->file);
      return written;
   }

   hdr.tv_sec = DriverEnvironment_GetTicks();
   hdr.tv_usec = 0;
   hdr.caplen = len + sizeof(flags);
   hdr.len = len + sizeof(flags);

   nrx_pcap_q_write(ctx);

   written += MC_GFL_WRITE(ctx->file, &hdr, sizeof(hdr));
   written += MC_GFL_WRITE(ctx->file, &flags, sizeof(flags));
   written += MC_GFL_WRITE(ctx->file, data, hdr.caplen - sizeof(flags));

   DE_TRACE_INT(TR_PCAP, "wrote %d bytes to file\n", written);

   return written;
}

#endif
