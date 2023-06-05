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
	__u32 wait_config;	/* Wait for DLCI config before opening virtual link? */
	__u32 reserved[6];	/* For future use, must be initialized to zero */
};

#define GSMIOC_GETCONF_EXT	_IOR('G', 5, struct gsm_config_ext)
#define GSMIOC_SETCONF_EXT	_IOW('G', 6, struct gsm_config_ext)

/* Set channel accordingly before calling GSMIOC_GETCONF_DLCI. */
struct gsm_dlci_config {
	__u32 channel;		/* DLCI (0 for the associated DLCI) */
	__u32 adaption;		/* Convergence layer type */
	__u32 mtu;		/* Maximum transfer unit */
	__u32 priority;		/* Priority (0 for default value) */
	__u32 i;		/* Frame type (1 = UIH, 2 = UI) */
	__u32 k;		/* Window size (0 for default value) */
	__u32 reserved[8];	/* For future use, must be initialized to zero */
};

#define GSMIOC_GETCONF_DLCI	_IOWR('G', 7, struct gsm_dlci_config)
#define GSMIOC_SETCONF_DLCI	_IOW('G', 8, struct gsm_dlci_config)

#endif
