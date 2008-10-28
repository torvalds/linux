#ifndef _TP_SAMPLES_TRACE_H
#define _TP_SAMPLES_TRACE_H

#include <linux/proc_fs.h>	/* for struct inode and struct file */
#include <linux/tracepoint.h>

DEFINE_TRACE(subsys_event,
	TPPROTO(struct inode *inode, struct file *file),
	TPARGS(inode, file));
DEFINE_TRACE(subsys_eventb,
	TPPROTO(void),
	TPARGS());
#endif
