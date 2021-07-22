/* $Id: capiutil.h,v 1.5.6.2 2001/09/23 22:24:33 kai Exp $
 *
 * CAPI 2.0 defines & types
 *
 * From CAPI 2.0 Development Kit AVM 1995 (msg.c)
 * Rewritten for Linux 1996 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __CAPIUTIL_H__
#define __CAPIUTIL_H__

#include <asm/types.h>

#define CAPIMSG_BASELEN		8
#define CAPIMSG_U8(m, off)	(m[off])
#define CAPIMSG_U16(m, off)	(m[off]|(m[(off)+1]<<8))
#define CAPIMSG_U32(m, off)	(m[off]|(m[(off)+1]<<8)|(m[(off)+2]<<16)|(m[(off)+3]<<24))
#define	CAPIMSG_LEN(m)		CAPIMSG_U16(m,0)
#define	CAPIMSG_APPID(m)	CAPIMSG_U16(m,2)
#define	CAPIMSG_COMMAND(m)	CAPIMSG_U8(m,4)
#define	CAPIMSG_SUBCOMMAND(m)	CAPIMSG_U8(m,5)
#define CAPIMSG_CMD(m)		(((m[4])<<8)|(m[5]))
#define	CAPIMSG_MSGID(m)	CAPIMSG_U16(m,6)
#define CAPIMSG_CONTROLLER(m)	(m[8] & 0x7f)
#define CAPIMSG_CONTROL(m)	CAPIMSG_U32(m, 8)
#define CAPIMSG_NCCI(m)		CAPIMSG_CONTROL(m)
#define CAPIMSG_DATALEN(m)	CAPIMSG_U16(m,16) /* DATA_B3_REQ */

static inline void capimsg_setu8(void *m, int off, __u8 val)
{
	((__u8 *)m)[off] = val;
}

static inline void capimsg_setu16(void *m, int off, __u16 val)
{
	((__u8 *)m)[off] = val & 0xff;
	((__u8 *)m)[off+1] = (val >> 8) & 0xff;
}

static inline void capimsg_setu32(void *m, int off, __u32 val)
{
	((__u8 *)m)[off] = val & 0xff;
	((__u8 *)m)[off+1] = (val >> 8) & 0xff;
	((__u8 *)m)[off+2] = (val >> 16) & 0xff;
	((__u8 *)m)[off+3] = (val >> 24) & 0xff;
}

#define	CAPIMSG_SETLEN(m, len)		capimsg_setu16(m, 0, len)
#define	CAPIMSG_SETAPPID(m, applid)	capimsg_setu16(m, 2, applid)
#define	CAPIMSG_SETCOMMAND(m,cmd)	capimsg_setu8(m, 4, cmd)
#define	CAPIMSG_SETSUBCOMMAND(m, cmd)	capimsg_setu8(m, 5, cmd)
#define	CAPIMSG_SETMSGID(m, msgid)	capimsg_setu16(m, 6, msgid)
#define	CAPIMSG_SETCONTROL(m, contr)	capimsg_setu32(m, 8, contr)
#define	CAPIMSG_SETDATALEN(m, len)	capimsg_setu16(m, 16, len)

#endif				/* __CAPIUTIL_H__ */
