/*
 * Linux Socket Filter Data Structures
 */

#ifndef _UAPI__LINUX_FILTER_H__
#define _UAPI__LINUX_FILTER_H__

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/bpf_common.h>

/*
 * Current version of the filter code architecture.
 */
#define BPF_MAJOR_VERSION 1
#define BPF_MINOR_VERSION 1

/*
 *	Try and keep these values and structures similar to BSD, especially
 *	the BPF code definitions which need to match so you can share filters
 */
 
struct sock_filter {	/* Filter block */
	__u16	code;   /* Actual filter code */
	__u8	jt;	/* Jump true */
	__u8	jf;	/* Jump false */
	__u32	k;      /* Generic multiuse field */
};

struct sock_fprog {	/* Required for SO_ATTACH_FILTER. */
	unsigned short		len;	/* Number of filter blocks */
	struct sock_filter __user *filter;
};

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)  ((code) & 0x18)
#define         BPF_A           0x10

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define         BPF_TAX         0x00
#define         BPF_TXA         0x80

/*
 * Macros for filter block array initializers.
 */
#ifndef BPF_STMT
#define BPF_STMT(code, k) { (unsigned short)(code), 0, 0, k }
#endif
#ifndef BPF_JUMP
#define BPF_JUMP(code, k, jt, jf) { (unsigned short)(code), jt, jf, k }
#endif

/*
 * Number of scratch memory words for: BPF_ST and BPF_STX
 */
#define BPF_MEMWORDS 16

/* RATIONALE. Negative offsets are invalid in BPF.
   We use them to reference ancillary data.
   Unlike introduction new instructions, it does not break
   existing compilers/optimizers.
 */
#define SKF_AD_OFF    (-0x1000)
#define SKF_AD_PROTOCOL 0
#define SKF_AD_PKTTYPE 	4
#define SKF_AD_IFINDEX 	8
#define SKF_AD_NLATTR	12
#define SKF_AD_NLATTR_NEST	16
#define SKF_AD_MARK 	20
#define SKF_AD_QUEUE	24
#define SKF_AD_HATYPE	28
#define SKF_AD_RXHASH	32
#define SKF_AD_CPU	36
#define SKF_AD_ALU_XOR_X	40
#define SKF_AD_VLAN_TAG	44
#define SKF_AD_VLAN_TAG_PRESENT 48
#define SKF_AD_PAY_OFFSET	52
#define SKF_AD_RANDOM	56
#define SKF_AD_MAX	60
#define SKF_NET_OFF   (-0x100000)
#define SKF_LL_OFF    (-0x200000)


#endif /* _UAPI__LINUX_FILTER_H__ */
