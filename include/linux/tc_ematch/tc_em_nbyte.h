#ifndef __LINUX_TC_EM_NBYTE_H
#define __LINUX_TC_EM_NBYTE_H

#include <linux/pkt_cls.h>

struct tcf_em_nbyte
{
	__u16		off;
	__u16		len:12;
	__u8		layer:4;
};

#endif
