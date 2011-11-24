/* $Id: logging_io.c,v 1.1 2007/10/17 10:42:01 miwi Exp $ */

#include "driverenv.h"
#include "de_file.h"
#include "log.h"

#include "file_numbers.h"

#undef FILE_NUMBER
#define FILE_NUMBER FILE_LOGGING_C
#define LOG_MAX_DATA_SIZE 250

#if (DE_TRACE_MODE & CFG_DYNAMIC_BUFFER)
char *log_buf = NULL;
#elif (DE_TRACE_MODE & CFG_MEMORY)
char log_buf[DE_LOG_SIZE];
#endif

struct log_t logger;

#define SHORT_FORM_OF_FILE(_f_) \
(strrchr(_f_,'\\') \
? strrchr(_f_,'\\')+1 \
: _f_ \
)

/*! Eval buf and write to file
 * @return 1 on successfull write, 0 otherwise
 */
/*----------------------------------------------------------------------------*/
#undef PROCEDURE_NUMBER
#define PROCEDURE_NUMBER 1
/*----------------------------------------------------------------------------*/
static int print_event(int file, struct log_event_t *event,const char *data) {
    int n;
    int *i;
    unsigned char type = event->trace_blob->data_type;

    const int buf_len = 300; /* should be enuff */
    char buf[buf_len];
    int written = 0;
    int ret = 0;

#ifdef C_LOGGING_WITH_TIMESTAMPS
    written = DE_SNPRINTF(buf, buf_len, "%u ", event->ts );
    ret += 0 > MC_GFL_WRITE(file, buf, written);
#endif

#ifdef C_LOGGING_WITH_FILE
    written = DE_SNPRINTF(buf, buf_len,
                          "%s:%d %s ", SHORT_FORM_OF_FILE(event->trace_blob->file), event->trace_blob->line,
                          event->trace_blob->func);
#else
    written = DE_SNPRINTF(buf, buf_len,
                          "%s ", event->trace_blob->func);
#endif
    ret += 0 > MC_GFL_WRITE(file, buf, written);

    switch (type) {
    case LOGDATA_STATIC_STRING:
        written = DE_SNPRINTF(buf, buf_len,
                              event->trace_blob->message);
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        break;
    case LOGDATA_INTS:
        i = (int*)data;
        /* if event->message has more '%d' in it then sizeof(int)*time */
        /* this line may cause 'Oops!' */
        written = DE_SNPRINTF(buf, buf_len,
                              event->trace_blob->message, i[0],i[1],i[2],i[3],i[4]);
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        break;
    case LOGDATA_HEX:
        written = DE_SNPRINTF(buf, buf_len - written,
                              "%s HEX: ", event->trace_blob->message );
        for ( n = 0 ; n < event->data_len && buf_len > written ; n++ ) {
            written += DE_SNPRINTF(buf + written, buf_len - written,
                                   "%02x ",data[n]);
        }
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        ret += 0 > MC_GFL_WRITE(file, "\n", 1);
        break;
    case LOGDATA_INC_HIC:
        break;
    case LOGDATA_OUT_HIC:
        break;
    case LOGDATA_STRING:
        written = DE_SNPRINTF(buf,
                              buf_len,
                              event->trace_blob->message, data);
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        break;
    case LOGDATA_PTR:
        written = DE_SNPRINTF(buf,
                              buf_len,
                              event->trace_blob->message, data);
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        break;
    default:
        written = DE_SNPRINTF(buf, buf_len,
                              "unknown LOGDATA type %d\n",type);
        ret += 0 > MC_GFL_WRITE(file, buf, written);
        break;
    }
    return ( ret == 0 );
}

/*----------------------------------------------------------------------------*/
#undef PROCEDURE_NUMBER
#define PROCEDURE_NUMBER 2
/*----------------------------------------------------------------------------*/
int log_to_text_file(const char *path,int append) {
    struct log_event_t event;
    char buf[LOG_MAX_DATA_SIZE];
    int file;
    int ok_to_write = 1;

    if (logger_len(&logger) == 0)
        return 1;

    if(append)
        file = MC_GFL_OPEN( path, GFL_O_CREAT|GFL_O_WRONLY|GFL_O_APPEND, GFL_S_IROTH);
    else
        file = MC_GFL_OPEN( path, GFL_O_CREAT|GFL_O_WRONLY|GFL_O_TRUNC, GFL_S_IROTH);

    if (file < 0)
        return 0;

    while (logger_len(&logger) > 0 && ok_to_write ) {
        logger_get(&logger, &event, buf, LOG_MAX_DATA_SIZE);
        if (event.data_len < LOG_MAX_DATA_SIZE)
            ok_to_write = print_event(file,&event,buf);
        else
            DE_TRACE_INT(TR_ALL,"messages to large (%d), discarding\n", event.data_len);
    }

    MC_GFL_CLOSE(file);
    return ok_to_write;
}

/*----------------------------------------------------------------------------*/
#undef PROCEDURE_NUMBER
#define PROCEDURE_NUMBER 3
/*----------------------------------------------------------------------------*/
void logger_io_init(void) {
#if !(DE_TRACE_MODE & CFG_DYNAMIC_BUFFER)  
  logger_init(&logger, log_buf, DE_LOG_SIZE);
  DE_TRACE2(TR_INITIALIZE, "Binary log initiated; %d bytes in static buffer\n",DE_LOG_SIZE);
#else
    unsigned int size = DE_LOG_SIZE;
    unsigned int pool_id = NRDRV_DYN_POOL_ID;
    unsigned int max     = MC_RTK_AVAILABLE_DYN_POOL_SIZE(pool_id);
    if (max < size) size = max;

    if(log_buf == NULL)
    {
        log_buf = MC_RTK_GET_DYN_POOL_MEMORY(pool_id, size);
        DE_ASSERT(log_buf!=NULL);
        logger_init(&logger, log_buf, size);
        DE_TRACE_INT3(TR_INITIALIZE, "Binary log initiated; %d bytes in pool %u, "
                      "%d bytes allocated for buffer\n",max,pool_id, size);
    }
    else
    {
        DE_TRACE_STATIC(TR_NOISE, "logger allready initiated\n");
    }
#endif
}

static int file_write(const void* ptr, size_t size, void *ctx) {
       int w = de_fwrite((de_file_ref_t)ctx, ptr, size);
       return w;
}

static de_file_ref_t
prep_new_file(const char *path, int max)
{
    char name[60];
    de_file_ref_t file;
    int i;

    for(i=1 ; i <= max ; i++) {
        DE_SNPRINTF(name, sizeof(name), path, i);
        file = de_fopen(name, DE_FRDONLY | DE_FSD_PATH_ONLY);
        if(de_f_is_open(file)) {
            de_fclose(file);
        } else { 
            DE_TRACE_STRING(TR_INITIALIZE, "log file %s opened for writing\n",name);
            DE_SNPRINTF(nr_at_rsp, sizeof(nr_at_rsp), "%s",name);
            return de_fopen(name, DE_FCREATE | DE_FWRONLY | DE_FSD_PATH_ONLY);
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------*/
#undef PROCEDURE_NUMBER
#define PROCEDURE_NUMBER 4
/*----------------------------------------------------------------------------*/
int log_to_file(const char *path,int append)
{
    de_file_ref_t file;
    int i;
    int done = 0;

    if (logger_len(&logger) == 0)
        return 1;

    // ignore append flag
    file = prep_new_file(path, 20);

    if(!de_f_is_open(file))
        return 0;

    for(i=1; i<10 && !done ; i++) {
        int w = logger_write_to_fp(&logger, &file_write, (void*)file);
        if( w < 0) {
            DE_TRACE_INT2(TR_SEVERE,"failed to write to file (attempt %d) error %d\n", i, w);
            DriverEnvironment_Yield(10);
        } else {
            done = 1;
            DE_TRACE_STATIC(TR_SEVERE,"log written to file\n");
        }
    }

    de_fclose(file);

    return 1;
}
