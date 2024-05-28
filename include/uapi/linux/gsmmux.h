/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2022/23 Siemens Mobility GmbH */
#ifndef _LINUX_GSMMUX_H
#define _LINUX_GSMMUX_H

#include <linux/const.h>
#include <linux/if.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * flags definition for n_gsm
 *
 * Used by:
 * struct gsm_config_ext.flags
 * struct gsm_dlci_config.flags
 */
/* Forces a DLCI reset if set. Otherwise, a DLCI reset is only done if
 * incompatible settings were provided. Always cleared on retrieval.
 */
#define GSM_FL_RESTART	_BITUL(0)

/**
 * struct gsm_config - n_gsm basic configuration parameters
 *
 * This structure is used in combination with GSMIOC_GETCONF and GSMIOC_SETCONF
 * to retrieve and set the basic parameters of an n_gsm ldisc.
 * struct gsm_config_ext can be used to configure extended ldisc parameters.
 *
 * All timers are in units of 1/100th of a second.
 *
 * @adaption:      Convergence layer type
 * @encapsulation: Framing (0 = basic option, 1 = advanced option)
 * @initiator:     Initiator or responder
 * @t1:            Acknowledgment timer
 * @t2:            Response timer for multiplexer control channel
 * @t3:            Response timer for wake-up procedure
 * @n2:            Maximum number of retransmissions
 * @mru:           Maximum incoming frame payload size
 * @mtu:           Maximum outgoing frame payload size
 * @k:             Window size
 * @i:             Frame type (1 = UIH, 2 = UI)
 * @unused:        Can not be used
 */
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
	unsigned int unused[8];
};

#define GSMIOC_GETCONF		_IOR('G', 0, struct gsm_config)
#define GSMIOC_SETCONF		_IOW('G', 1, struct gsm_config)

/**
 * struct gsm_netconfig - n_gsm network configuration parameters
 *
 * This structure is used in combination with GSMIOC_ENABLE_NET and
 * GSMIOC_DISABLE_NET to enable or disable a network data connection
 * over a mux virtual tty channel. This is for modems that support
 * data connections with raw IP frames instead of PPP.
 *
 * @adaption: Adaption to use in network mode.
 * @protocol: Protocol to use - only ETH_P_IP supported.
 * @unused2:  Can not be used.
 * @if_name:  Interface name format string.
 * @unused:   Can not be used.
 */
struct gsm_netconfig {
	unsigned int adaption;
	unsigned short protocol;
	unsigned short unused2;
	char if_name[IFNAMSIZ];
	__u8 unused[28];
};

#define GSMIOC_ENABLE_NET      _IOW('G', 2, struct gsm_netconfig)
#define GSMIOC_DISABLE_NET     _IO('G', 3)

/* get the base tty number for a configured gsmmux tty */
#define GSMIOC_GETFIRST		_IOR('G', 4, __u32)

/**
 * struct gsm_config_ext - n_gsm extended configuration parameters
 *
 * This structure is used in combination with GSMIOC_GETCONF_EXT and
 * GSMIOC_SETCONF_EXT to retrieve and set the extended parameters of an
 * n_gsm ldisc.
 *
 * All timers are in units of 1/100th of a second.
 *
 * @keep_alive:  Control channel keep-alive in 1/100th of a second (0 to disable).
 * @wait_config: Wait for DLCI config before opening virtual link?
 * @flags:       Mux specific flags.
 * @reserved:    For future use, must be initialized to zero.
 */
struct gsm_config_ext {
	__u32 keep_alive;
	__u32 wait_config;
	__u32 flags;
	__u32 reserved[5];
};

#define GSMIOC_GETCONF_EXT	_IOR('G', 5, struct gsm_config_ext)
#define GSMIOC_SETCONF_EXT	_IOW('G', 6, struct gsm_config_ext)

/**
 * struct gsm_dlci_config - n_gsm channel configuration parameters
 *
 * This structure is used in combination with GSMIOC_GETCONF_DLCI and
 * GSMIOC_SETCONF_DLCI to retrieve and set the channel specific parameters
 * of an n_gsm ldisc.
 *
 * Set the channel accordingly before calling GSMIOC_GETCONF_DLCI.
 *
 * @channel:  DLCI (0 for the associated DLCI).
 * @adaption: Convergence layer type.
 * @mtu:      Maximum transfer unit.
 * @priority: Priority (0 for default value).
 * @i:        Frame type (1 = UIH, 2 = UI).
 * @k:        Window size (0 for default value).
 * @flags:    DLCI specific flags.
 * @reserved: For future use, must be initialized to zero.
 */
struct gsm_dlci_config {
	__u32 channel;
	__u32 adaption;
	__u32 mtu;
	__u32 priority;
	__u32 i;
	__u32 k;
	__u32 flags;
	__u32 reserved[7];
};

#define GSMIOC_GETCONF_DLCI	_IOWR('G', 7, struct gsm_dlci_config)
#define GSMIOC_SETCONF_DLCI	_IOW('G', 8, struct gsm_dlci_config)

#endif
