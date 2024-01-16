/*
 * SyncLink Multiprotocol Serial Adapter Driver
 *
 * $Id: synclink.h,v 3.14 2006/07/17 20:15:43 paulkf Exp $
 *
 * Copyright (C) 1998-2000 by Microgate Corporation
 *
 * Redistribution of this file is permitted under
 * the terms of the GNU Public License (GPL)
 */
#ifndef _SYNCLINK_H_
#define _SYNCLINK_H_

#include <uapi/linux/synclink.h>

/* provide 32 bit ioctl compatibility on 64 bit systems */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
struct MGSL_PARAMS32 {
	compat_ulong_t	mode;
	unsigned char	loopback;
	unsigned short	flags;
	unsigned char	encoding;
	compat_ulong_t	clock_speed;
	unsigned char	addr_filter;
	unsigned short	crc_type;
	unsigned char	preamble_length;
	unsigned char	preamble;
	compat_ulong_t	data_rate;
	unsigned char	data_bits;
	unsigned char	stop_bits;
	unsigned char	parity;
};
#define MGSL_IOCSPARAMS32 _IOW(MGSL_MAGIC_IOC,0,struct MGSL_PARAMS32)
#define MGSL_IOCGPARAMS32 _IOR(MGSL_MAGIC_IOC,1,struct MGSL_PARAMS32)
#endif
#endif /* _SYNCLINK_H_ */
