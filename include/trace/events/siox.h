#undef TRACE_SYSTEM
#define TRACE_SYSTEM siox

#if !defined(_TRACE_SIOX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SIOX_H

#include <linux/tracepoint.h>

TRACE_EVENT(siox_set_data,
	    TP_PROTO(const struct siox_master *smaster,
		     const struct siox_device *sdevice,
		     unsigned int devno, size_t bufoffset),
	    TP_ARGS(smaster, sdevice, devno, bufoffset),
	    TP_STRUCT__entry(
			     __field(int, busno)
			     __field(unsigned int, devno)
			     __field(size_t, inbytes)
			     __dynamic_array(u8, buf, sdevice->inbytes)
			    ),
	    TP_fast_assign(
			   __entry->busno = smaster->busno;
			   __entry->devno = devno;
			   __entry->inbytes = sdevice->inbytes;
			   memcpy(__get_dynamic_array(buf),
				  smaster->buf + bufoffset, sdevice->inbytes);
			  ),
	    TP_printk("siox-%d-%u [%*phD]",
		      __entry->busno,
		      __entry->devno,
		      (int)__entry->inbytes, __get_dynamic_array(buf)
		     )
);

TRACE_EVENT(siox_get_data,
	    TP_PROTO(const struct siox_master *smaster,
		     const struct siox_device *sdevice,
		     unsigned int devno, u8 status_clean,
		     size_t bufoffset),
	    TP_ARGS(smaster, sdevice, devno, status_clean, bufoffset),
	    TP_STRUCT__entry(
			     __field(int, busno)
			     __field(unsigned int, devno)
			     __field(u8, status_clean)
			     __field(size_t, outbytes)
			     __dynamic_array(u8, buf, sdevice->outbytes)
			    ),
	    TP_fast_assign(
			   __entry->busno = smaster->busno;
			   __entry->devno = devno;
			   __entry->status_clean = status_clean;
			   __entry->outbytes = sdevice->outbytes;
			   memcpy(__get_dynamic_array(buf),
				  smaster->buf + bufoffset, sdevice->outbytes);
			  ),
	    TP_printk("siox-%d-%u (%02hhx) [%*phD]",
		      __entry->busno,
		      __entry->devno,
		      __entry->status_clean,
		      (int)__entry->outbytes, __get_dynamic_array(buf)
		     )
);

#endif /* if !defined(_TRACE_SIOX_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
