#ifndef _LOG_H_
#define _LOG_H_

#include "fifo.h"

struct log_t {
   struct fifo_t fifo;
   /* entries currently in fifo */
   unsigned int entries;
   driver_trylock_t rwlock;
   unsigned int version;
};

struct trace_entry {
   const char *file;
   const char *func;
   unsigned int line;
   const char *message;
   unsigned char data_type;
};

struct log_event_t {
   /* add any number of fields */
#ifdef C_LOGGING_WITH_TIMESTAMPS
   de_time_t ts;
#endif

   const struct trace_entry *trace_blob;
   unsigned int  data_len;
};

/* header for saving log to file */
struct log_file_s {
        char ident[64];
        uint32_t major;
        uint32_t minor;
        /* buffer */
        uint32_t size;
        uintptr_t addr;
};

/* data types */
#define    LOGDATA_STATIC_STRING    1
#define    LOGDATA_INTS             2
#define    LOGDATA_HEX              3
#define    LOGDATA_INC_HIC          4
#define    LOGDATA_OUT_HIC          5
#define    LOGDATA_STRING           6
#define    LOGDATA_PTR              7
#define    LOGDATA_MIB              8

/*!
 * \brief append event and data to buffer.
 * 
 * Copies the event and data to at the end of a circular buffer. Data length is
 * determined by event.data_len
 * @param src Null-terminated string to copy. 
 * 
 * @return dest is returned.
 */
void logger_init(struct log_t *lg, char *buffer,unsigned int size);

/*!
 * \brief append event and data to fifo buffer.
 * 
 * Copies the event and data to at the end of a circular buffer. Data length is
 * determined by event.data_len
 */
int logger_put(struct log_t *lg, struct log_event_t *event, const char *data);

/*!
 * \brief retrieve an event and data from fifo buffer.
 * 
 * Retrieve an event and data from fifo buffer. Data must be a buffer 
 * of sufficient size. The actual length of the data is determined by
 * event->data_len.
 * @param event where to storethe event.
 * @param data where to store the data.
 * @param len size of *data buffer to avoid buffer overflow.
 * 
 * @return bytes read is returned
 */
unsigned int logger_get(struct log_t *lg, struct log_event_t *event, char *data, unsigned int len);

/*!
 * \brief bytes of allocated data in buffer.
 * 
 * Bytes of allocated data in buffer.
 *
 * @return bytes of allocated data in buffer.
 */
unsigned int logger_len(struct log_t *lg);

/*!
 * \brief Create a new header for file storage of a log.
 */
void logger_get_file_header(struct log_file_s *h);

/*!
 * \brief Write log buffer to *fwrite(buf,size,...);
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
                void *ctx);

/* logger macros */

/* require a global 'struct log_t logger' variable */

/* save a ref to a static string */
#define LOG_STATIC(logger,m,num,a,b,c,d,e,f)            \
do {                                                    \
    struct log_event_t __event__;                       \
    char* __payload__[6];                               \
    static const struct trace_entry __blob = {          \
       __FILE__,                                        \
       __func__,                                        \
       __LINE__,                                        \
       m,                                               \
       LOGDATA_STATIC_STRING                            \
    };                                                  \
    __payload__[0] = a;                                 \
    __payload__[1] = b;                                 \
    __payload__[2] = c;                                 \
    __payload__[3] = d;                                 \
    __payload__[4] = e;                                 \
    __payload__[5] = f;                                 \
    __event__.trace_blob = &__blob;                     \
    __event__.data_len = num*sizeof(char*);             \
    logger_put(logger,&__event__,(char*)&__payload__);  \
} while(0)

/* save up to NUM ints and a ref to a static fmt string */
#define LOG_PTR(logger,m,num,a,b,c,d,e,f)                       \
do {                                                            \
    struct log_event_t __event__;                               \
    uintptr_t __payload__[6]; /* save address, not content */   \
    static const struct trace_entry __blob = {                  \
       __FILE__,                                                \
       __func__,                                                \
       __LINE__,                                                \
       m,                                                       \
       LOGDATA_PTR                                              \
    };                                                          \
    __payload__[0] = (uintptr_t)a;                              \
    __payload__[1] = (uintptr_t)b;                              \
    __payload__[2] = (uintptr_t)c;                              \
    __payload__[3] = (uintptr_t)d;                              \
    __payload__[4] = (uintptr_t)e;                              \
    __payload__[5] = (uintptr_t)f;                              \
    __event__.trace_blob = &__blob;                             \
    __event__.data_len = num*sizeof(uintptr_t);                 \
    logger_put(logger,&__event__,(char*)&__payload__);          \
} while(0)

/* save up to NUM ints and a ref to a static fmt string */
#define LOG_INT(logger,m,num,a,b,c,d,e,f)               \
do {                                                    \
    struct log_event_t __event__;                       \
    int __payload__[6];                                 \
    static const struct trace_entry __blob = {          \
       __FILE__,                                        \
       __func__,                                        \
       __LINE__,                                        \
       m,                                               \
       LOGDATA_INTS                                     \
    };                                                  \
    __payload__[0] = a;                                 \
    __payload__[1] = b;                                 \
    __payload__[2] = c;                                 \
    __payload__[3] = d;                                 \
    __payload__[4] = e;                                 \
    __payload__[5] = f;                                 \
    __event__.trace_blob = &__blob;                     \
    __event__.data_len = num*sizeof(int);               \
    logger_put(logger,&__event__,(char*)&__payload__);  \
} while(0)

/* save some data and a ref to a static string */
#define LOG_DATA(logger,m,data,len)                     \
do {                                                    \
    struct log_event_t __event__;                       \
    static const struct trace_entry __blob = {          \
       __FILE__,                                        \
       __func__,                                        \
       __LINE__,                                        \
       m,                                               \
       LOGDATA_HEX                                      \
    };                                                  \
    __event__.trace_blob = &__blob;                     \
    __event__.data_len = len;                           \
    logger_put(logger,&__event__,(const char*)data);    \
} while(0)

/* save some data and a ref to a static string */
#define LOG_MIB(logger,m,data,len)                     \
do {                                                    \
    struct log_event_t __event__;                       \
    static const struct trace_entry __blob = {          \
       __FILE__,                                        \
       __func__,                                        \
       __LINE__,                                        \
       m,                                               \
       LOGDATA_MIB                                      \
    };                                                  \
    __event__.trace_blob = &__blob;                     \
    __event__.data_len = len;                           \
    logger_put(logger,&__event__,(const char*)data);    \
} while(0)

/* save a string and a ref to a static fmt string */
#define LOG_STRING(logger,_fmt,string)          \
do {                                            \
    struct log_event_t __event__;               \
    static const struct trace_entry __blob = {  \
       __FILE__,                                \
       __func__,                                \
       __LINE__,                                \
       _fmt,                                    \
       LOGDATA_STRING                           \
    };                                          \
    __event__.trace_blob = &__blob;             \
    __event__.data_len = 1+strlen(string);      \
    logger_put(logger,&__event__,string);       \
} while(0)

#endif /* _LOG_H_ */
