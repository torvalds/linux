/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_GSMMUX_H
#define _LINUX_GSMMUX_H

#include <linux/if.h>
#include <linux/ioctl.h>
#include <linux/types.h>

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
	unsigned int unused[8];	/* Can not be used */
};

#define GSMIOC_GETCONF		_IOR('G', 0, struct gsm_config)
#define GSMIOC_SETCONF		_IOW('G', 1, struct gsm_config)

struct gsm_netconfig {
	unsigned int adaption;  /* Adaption to use in network mode */
	unsigned short protocol;/* Protocol to use - only ETH_P_IP supported */
	unsigned short unused2;	/* Can not be used */
	char if_name[IFNAMSIZ];	/* interface name format string */
	__u8 unused[28];        /* Can not be used */
};

#define GSMIOC_ENABLE_NET      _IOW('G', 2, struct gsm_netconfig)
#define GSMIOC_DISABLE_NET     _IO('G', 3)

/* get the base tty number for a configured gsmmux tty */
#define GSMIOC_GETFIRST		_IOR('G', 4, __u32)

struct gsm_config_ext {
	__u32 keep_alive;	/* Control channel keep-alive in 1/100th of a
				 * second (0 to disable)
				 */
	__u32 reserved[7];	/* For future use, must be initialized to zero */
};

#define GSMIOC_GETCONF_EXT	_IOR('G', 5, struct gsm_config_ext)
#define GSMIOC_SETCONF_EXT	_IOW('G', 6, struct gsm_config_ext)

#endif
