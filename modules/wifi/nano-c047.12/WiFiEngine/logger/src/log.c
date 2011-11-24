
#include "driverenv.h"
#include "log.h"

#define LOCK_UNLOCKED 0
#define LOCK_LOCKED 1

void logger_init(struct log_t *lg, char *buffer, unsigned int size) {
   fifo_init(&lg->fifo,buffer,size);
   DriverEnvironment_init_trylock(&lg->rwlock);
   lg->entries = 0;
   lg->version = 1;
}

unsigned int logger_len(struct log_t *lg) {
   return fifo_len(&lg->fifo);
}

/* non locking version */
static unsigned int __logger_get(struct log_t *lg, struct log_event_t *event, char *data, unsigned int len) {
   int ret=0;

   /* exit on no data available */
   if( fifo_len(&lg->fifo) < sizeof(struct log_event_t) )
      return 0;

   ret = fifo_get( &lg->fifo, (char*)event, sizeof(struct log_event_t) );
   if(event->data_len>0) {
      if( len==0 ) {
         /* only free memory, no need to save data */
         ret += fifo_inc_read( &lg->fifo, event->data_len );
      } else {
         ret += fifo_get(&lg->fifo, data, event->data_len);
      }
   }

   lg->entries--;

   return ret;
}

unsigned int logger_get(struct log_t *lg, struct log_event_t *event, char *data, unsigned int len) {
    unsigned int ret = 0;
    /* should spin */
    if (DriverEnvironment_acquire_trylock(&lg->rwlock) == LOCK_UNLOCKED) {
        ret = __logger_get(lg,event,data,len);
        DriverEnvironment_release_trylock(&lg->rwlock);
    }
    return ret;
}

int logger_put(struct log_t *lg, struct log_event_t *event, const char *data) {
    unsigned int needed;
    struct log_event_t fe; /* only used by logger_get(...) */

#ifdef C_LOGGING_WITH_TIMESTAMPS
    DriverEnvironment_get_current_time(&event->ts);
#endif

    needed = sizeof(struct log_event_t) + event->data_len;

    if (needed >= fifo_buff_size(&lg->fifo))
        return 0;

    if (DriverEnvironment_acquire_trylock(&lg->rwlock) == LOCK_UNLOCKED) {
        /* free memory even if it fit to avoid fifo.in==fifo.out */
        while ( fifo_buff_size(&lg->fifo) - fifo_len(&lg->fifo) <= needed )
            __logger_get(lg,&fe,NULL,0);

        fifo_put(&lg->fifo, (char*)event, sizeof(struct log_event_t));
        fifo_put(&lg->fifo, data, event->data_len);

        lg->entries++;
        DriverEnvironment_release_trylock(&lg->rwlock);
    }

    return needed;
}

/* ident string */
const char* ident_string = "c_log file build: " __DATE__ " " __TIME__;

void logger_get_file_header(struct log_file_s *h) {
        DE_MEMSET(h,0,sizeof(struct log_file_s));

        memcpy(h->ident,ident_string,strlen(ident_string));
        h->major = 1;
        h->minor = 2;
}

/* Write log buffer to *fwrite(buf,size,...);
 * 
 * Write the buffer to a file api, copy to pre allocated memory or
 * allocate mem in fwrite function
 *
 * The log lock will be taken during call to *fp and will thereby 
 * prevent messages to be written to the log during this time.
 *
 * @param *fp function pointer to write the buffer to. May be called 
 * several times.
 * @param *ctx context to be passed to *fp.
 *
 * - return number of bytes successfully reported as written by *fp
 *              (negative on: driver lock taken, invalid params, ...)
 */
int logger_write_to_fp( struct log_t *lg, 
                int (*fp)(const void* ptr, size_t size, void *ctx),
                void *ctx) {

        size_t read;
        size_t written = 0;
        void *ptr;
        struct log_file_s f_header;

        logger_get_file_header(&f_header);

        ptr = lg->fifo.buffer + lg->fifo.out;


        /* sanity check */
        if (!fp)
                return -1;

        /* check if lock is taken */
        if (DriverEnvironment_acquire_trylock(&lg->rwlock) == LOCK_LOCKED)
                return -2;

        DE_ASSERT(lg->fifo.in <= lg->fifo.size && 
                  lg->fifo.out <= lg->fifo.size);

        f_header.size = logger_len(lg);
        f_header.addr = (uintptr_t)lg->fifo.buffer;

        written += fp(&f_header, sizeof(f_header), ctx);

        if (lg->fifo.in > lg->fifo.out) {
                /* can be written in one call */
                read = lg->fifo.in - lg->fifo.out;
                written += fp(ptr, read, ctx);
        } else {
                read = lg->fifo.size - lg->fifo.out;
                written += fp(ptr, read, ctx);
                ptr = lg->fifo.buffer;
                read = lg->fifo.in;
                written += fp(ptr, read, ctx);
        }

        DriverEnvironment_release_trylock(&lg->rwlock);

        if( written < (f_header.size + sizeof(f_header)) ) {
                return -3;
        }

        return written;
}

