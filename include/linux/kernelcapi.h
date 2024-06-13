/* SPDX-License-Identifier: GPL-2.0 */
/*
 * $Id: kernelcapi.h,v 1.8.6.2 2001/02/07 11:31:31 kai Exp $
 * 
 * Kernel CAPI 2.0 Interface for Linux
 * 
 * (c) Copyright 1997 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 */
#ifndef __KERNELCAPI_H__
#define __KERNELCAPI_H__

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <uapi/linux/kernelcapi.h>

#define CAPI_NOERROR                      0x0000

#define CAPI_TOOMANYAPPLS		  0x1001
#define CAPI_LOGBLKSIZETOSMALL	          0x1002
#define CAPI_BUFFEXECEEDS64K 	          0x1003
#define CAPI_MSGBUFSIZETOOSMALL	          0x1004
#define CAPI_ANZLOGCONNNOTSUPPORTED	  0x1005
#define CAPI_REGRESERVED		  0x1006
#define CAPI_REGBUSY 		          0x1007
#define CAPI_REGOSRESOURCEERR	          0x1008
#define CAPI_REGNOTINSTALLED 	          0x1009
#define CAPI_REGCTRLERNOTSUPPORTEXTEQUIP  0x100a
#define CAPI_REGCTRLERONLYSUPPORTEXTEQUIP 0x100b

#define CAPI_ILLAPPNR		          0x1101
#define CAPI_ILLCMDORSUBCMDORMSGTOSMALL   0x1102
#define CAPI_SENDQUEUEFULL		  0x1103
#define CAPI_RECEIVEQUEUEEMPTY	          0x1104
#define CAPI_RECEIVEOVERFLOW 	          0x1105
#define CAPI_UNKNOWNNOTPAR		  0x1106
#define CAPI_MSGBUSY 		          0x1107
#define CAPI_MSGOSRESOURCEERR	          0x1108
#define CAPI_MSGNOTINSTALLED 	          0x1109
#define CAPI_MSGCTRLERNOTSUPPORTEXTEQUIP  0x110a
#define CAPI_MSGCTRLERONLYSUPPORTEXTEQUIP 0x110b

#endif				/* __KERNELCAPI_H__ */
