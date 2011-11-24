/* $Id: log_proc.c 9119 2008-06-09 11:50:52Z joda $ */

#include <linux/proc_fs.h>

#include "driverenv.h"
#include "log.h"

/*------------------------------------------------------------*/

#ifdef C_LOGGING

#define SHORT_FORM_OF_FILE(_f_) \
(strrchr(_f_,'/') \
? strrchr(_f_,'/')+1 \
: _f_ \
)

struct proc_dir_entry *proc_fifo_data;

#define PROCFS_MAX_SIZE    1024
static char procfs_buffer[PROCFS_MAX_SIZE];
static int procfs_buffer_len = 0;
static int procfs_buffer_offset = 0;

static void print_event(struct log_event_t *event,const unsigned char *data) {
   int n;
   int *i;
   unsigned char type = event->data_type;

   procfs_buffer_len = 0;
   procfs_buffer_offset = 0;

#ifdef C_LOGGING_WITH_TIMESTAMPS
   procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                  PROCFS_MAX_SIZE-procfs_buffer_len,
                                  "%lu.%06lu ", event->ts.tv_sec, event->ts.tv_usec);
#endif
   
   procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                  PROCFS_MAX_SIZE-procfs_buffer_len,
                                  "%s:%d %s ", SHORT_FORM_OF_FILE(event->file), event->line,
                                  event->func);
   switch(type) {
   case LOGDATA_STATIC_STRING:
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     event->message);
      break;
   case LOGDATA_INTS:
      i = (int*)data;
      /* if event->message has more '%d' in it then sizeof(int)*time */
      /* this line may cause 'Oops!' */
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     event->message, i[0],i[1],i[2],i[3],i[4]);
      break;
   case LOGDATA_HEX:
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     "%s HEX: ", event->message );
      for( n = 0 ; n < event->data_len ; n++ ) {
         procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     "%02x ",data[n]);
      }
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                  PROCFS_MAX_SIZE-procfs_buffer_len,"\n");
      break;
   case LOGDATA_INC_HIC:
      break;
   case LOGDATA_OUT_HIC:
      break;
   case LOGDATA_STRING:
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     event->message, data);
      break;
   case LOGDATA_PTR:
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     event->message, data);
      break;
   default:
      procfs_buffer_len += scnprintf(procfs_buffer + procfs_buffer_len,
                                     PROCFS_MAX_SIZE-procfs_buffer_len,
                                     "unknown LOGDATA type %d\n",type);
      break;
   }
}

/* use for debugging to generate log events */
static int write_data(struct file *file, const char *buffer,
                      unsigned long count, void *data) {

   unsigned char test_data[8] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
   char stack_m[10] = {'s','t','a','c','k',0};
   DE_TRACE_STATIC(TR_DATA,"write to proc!\n");
   DE_TRACE_STATIC(TR_DATA,"test start\n");
   DE_TRACE_STRING(TR_DATA,"save a fmt string and a string \"%s\"\n",stack_m);
   DE_TRACE_STRING(TR_DATA,"save a fmt string and a string \"%s\"\n","copy me");
   DE_TRACE_INT(TR_DATA,"a static string with an int %d\n", 42);
   DE_TRACE_INT2(TR_DATA,"a static string with two ints %d, %d\n", 42, 24);
   DE_TRACE_INT3(TR_DATA,"a static string with three ints %d, %d, %d\n", 42, 24,3);
   DE_TRACE_INT4(TR_DATA,"a static string with four ints %d, %d, %d, %d\n", 42, 24,3,4);
   DE_TRACE_DATA(TR_DATA,"some hex data\n", test_data, 8);
   DE_TRACE_PTR(TR_DATA,"the address of stack_m: %p\n", stack_m);
   DE_TRACE_VA(TR_DATA," _VA_ %%s:\"%s\" %%d: \"%d\"\n","foo",3);
   DE_TRACE1(TR_DATA,"DE_TRACE1 %d\n",1,2,3,4);
   DE_TRACE2(TR_DATA,"DE_TRACE2 %d,%d\n",1,2,3,4);
   DE_TRACE3(TR_DATA,"DE_TRACE3 %d.%d,%d\n",1,2,3,4);
   DE_TRACE4(TR_DATA,"DE_TRACE4 %d,%d,%d,%d\n",1,2,3,4);
   DE_TRACE_STATIC(TR_DATA,"test end\n");

   return 1;
}

static int proc_read_len(char *page, char **start, off_t offset,
                         int count, int *eof, void *data) {
   int len = 0;
   KDEBUG (TRACE, "ENTER");

   len += sprintf (page+len, "size     : %u\n", logger.fifo.size );
   len += sprintf (page+len, "len      : %u\n", logger_len(&logger) );
   len += sprintf (page+len, "entries  : %u\n", logger.entries);

   KDEBUG(TRACE, "EXIT");
   return len;
}

static int proc_read_data (char *page, char **start, off_t offset,
                           int count, int *eof, void *data) {
   struct log_event_t event;
   int len = 0;
   char *ed=NULL;
   int ed_size=500;

   KDEBUG(TRACE, "ENTER");
   *start = page;

   if( logger_len(&logger) > 0 && procfs_buffer_len == procfs_buffer_offset )
   {
      if(ed==NULL) {
         ed = (char*)kmalloc(ed_size,GFP_KERNEL);
         if(ed==NULL) {
            KDEBUG(ERROR,"malloc failed");
            return -1;
         }
      }
      logger_get(&logger, &event, ed, ed_size);
      print_event(&event,ed);
      kfree( ed );
   }

   len = min( procfs_buffer_len - procfs_buffer_offset, count );
   if( len>0 ) {
      memcpy( *start, procfs_buffer + procfs_buffer_offset, len );
      KDEBUG(TRACE,"wrote %d bytes",len);
      procfs_buffer_offset += len;
   }

   if( logger_len(&logger) == 0 && procfs_buffer_len == procfs_buffer_offset )
      *eof = 1;

   KDEBUG(TRACE, "EXIT");
   return len;
}

#endif /* C_LOGGING */

int nrx_log_setup(void)
{
   KDEBUG(TRACE, "ENTRY");
#ifdef C_LOGGING

   proc_fifo_data = create_proc_read_entry( "sys/nano/log.txt",
                    0, // Default mode
                    NULL, // Parent dir
                    proc_read_data,
                    NULL);
   if (!proc_fifo_data) {
      printk(KERN_ERR "cannot create /proc/sys/nano/log.txt\n");
   } else {
      proc_fifo_data->write_proc = write_data;
      create_proc_read_entry( "sys/nano/log.info",
                              0,
                              NULL,
                              proc_read_len,
                              NULL);
   }
#endif /* C_LOGGING */
   return 0;
}

int nrx_log_cleanup(void)
{
   KDEBUG(TRACE, "ENTRY");
#ifdef C_LOGGING
   remove_proc_entry("sys/nano/log.info", 0);
   remove_proc_entry("sys/nano/log.txt", 0);
#endif /* C_LOGGING */
   KDEBUG(TRACE, "EXIT");
   return 0;
}

/*------------------------------------------------------------*/

