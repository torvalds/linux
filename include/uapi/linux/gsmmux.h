#ifndef _LINUX_GSMMUX_H
#define _LINUX_GSMMUX_H

#include <linux/if.h>
#include <linux/ioctl.h>

struct gsm_config
{
	unsigned int adaption;
	unsigned int encapsulation;
	unsigned int initiator;
	unsigned int t1;
	unsigned int t2;
	unsigned int t3;
	unsigned int n2;
	unsigned int mru;
	unsigned int mtu;
	unsigned int k;
	unsigned int i;
	unsigned int unused[8];		/* Padding for expansion without
					   breaking stuff */
};

#define GSMIOC_GETCONF		_IOR('G', 0, struct gsm_config)
#define GSMIOC_SETCONF		_IOW('G', 1, struct gsm_config)

struct gsm_netconfig {
	unsigned int adaption;  /* Adaption to use in network mode */
	unsigned short protocol;/* Protocol to use - only ETH_P_IP supported */
	unsigned short unused2;
	char if_name[IFNAMSIZ];	/* interface name format string */
	__u8 unused[28];        /* For future use */
};

#define GSMIOC_ENABLE_NET      _IOW('G', 2, struct gsm_netconfig)
#define GSMIOC_DISABLE_NET     _IO('G', 3)


#endif
