/* $Id: capi.h,v 1.4.6.1 2001/09/23 22:25:05 kai Exp $
 * 
 * CAPI 2.0 Interface for Linux
 * 
 * Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __LINUX_CAPI_H__
#define __LINUX_CAPI_H__

#include <linux/types.h>
#include <linux/ioctl.h>
#ifndef __KERNEL__
#include <linux/kernelcapi.h>
#endif

/*
 * CAPI_REGISTER
 */

typedef struct capi_register_params {	/* CAPI_REGISTER */
	__u32 level3cnt;	/* No. of simulatneous user data connections */
	__u32 datablkcnt;	/* No. of buffered data messages */
	__u32 datablklen;	/* Size of buffered data messages */
} capi_register_params;

#define	CAPI_REGISTER	_IOW('C',0x01,struct capi_register_params)

/*
 * CAPI_GET_MANUFACTURER
 */

#define CAPI_MANUFACTURER_LEN		64

#define	CAPI_GET_MANUFACTURER	_IOWR('C',0x06,int)	/* broken: wanted size 64 (CAPI_MANUFACTURER_LEN) */

/*
 * CAPI_GET_VERSION
 */

typedef struct capi_version {
	__u32 majorversion;
	__u32 minorversion;
	__u32 majormanuversion;
	__u32 minormanuversion;
} capi_version;

#define CAPI_GET_VERSION	_IOWR('C',0x07,struct capi_version)

/*
 * CAPI_GET_SERIAL
 */

#define CAPI_SERIAL_LEN		8
#define CAPI_GET_SERIAL		_IOWR('C',0x08,int)	/* broken: wanted size 8 (CAPI_SERIAL_LEN) */

/*
 * CAPI_GET_PROFILE
 */

typedef struct capi_profile {
	__u16 ncontroller;	/* number of installed controller */
	__u16 nbchannel;	/* number of B-Channels */
	__u32 goptions;		/* global options */
	__u32 support1;		/* B1 protocols support */
	__u32 support2;		/* B2 protocols support */
	__u32 support3;		/* B3 protocols support */
	__u32 reserved[6];	/* reserved */
	__u32 manu[5];		/* manufacturer specific information */
} capi_profile;

#define CAPI_GET_PROFILE	_IOWR('C',0x09,struct capi_profile)

typedef struct capi_manufacturer_cmd {
	unsigned long cmd;
	void __user *data;
} capi_manufacturer_cmd;

/*
 * CAPI_MANUFACTURER_CMD
 */

#define CAPI_MANUFACTURER_CMD	_IOWR('C',0x20, struct capi_manufacturer_cmd)

/*
 * CAPI_GET_ERRCODE
 * capi errcode is set, * if read, write, or ioctl returns EIO,
 * ioctl returns errcode directly, and in arg, if != 0
 */

#define CAPI_GET_ERRCODE	_IOR('C',0x21, __u16)

/*
 * CAPI_INSTALLED
 */
#define CAPI_INSTALLED		_IOR('C',0x22, __u16)


/*
 * member contr is input for
 * CAPI_GET_MANUFACTURER, CAPI_GET_VERSION, CAPI_GET_SERIAL
 * and CAPI_GET_PROFILE
 */
typedef union capi_ioctl_struct {
	__u32 contr;
	capi_register_params rparams;
	__u8 manufacturer[CAPI_MANUFACTURER_LEN];
	capi_version version;
	__u8 serial[CAPI_SERIAL_LEN];
	capi_profile profile;
	capi_manufacturer_cmd cmd;
	__u16 errcode;
} capi_ioctl_struct;

/*
 * Middleware extension
 */

#define CAPIFLAG_HIGHJACKING	0x0001

#define CAPI_GET_FLAGS		_IOR('C',0x23, unsigned)
#define CAPI_SET_FLAGS		_IOR('C',0x24, unsigned)
#define CAPI_CLR_FLAGS		_IOR('C',0x25, unsigned)

#define CAPI_NCCI_OPENCOUNT	_IOR('C',0x26, unsigned)

#define CAPI_NCCI_GETUNIT	_IOR('C',0x27, unsigned)

#endif				/* __LINUX_CAPI_H__ */
