#ifndef	_CYCX_X25_H
#define	_CYCX_X25_H
/*
* cycx_x25.h	Cyclom X.25 firmware API definitions.
*
* Author:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
*
* Copyright:	(c) 1998-2003 Arnaldo Carvalho de Melo
*
* Based on sdla_x25.h by Gene Kozin <74604.152@compuserve.com>
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* 2000/04/02	acme		dprintk and cycx_debug
* 1999/01/03	acme		judicious use of data types
* 1999/01/02	acme		#define X25_ACK_N3	0x4411
* 1998/12/28	acme		cleanup: lot'o'things removed
*					 commands listed,
*					 TX25Cmd & TX25Config structs
*					 typedef'ed
*/
#ifndef PACKED
#define PACKED __attribute__((packed))
#endif 

/* X.25 shared memory layout. */
#define	X25_MBOX_OFFS	0x300	/* general mailbox block */
#define	X25_RXMBOX_OFFS	0x340	/* receive mailbox */

/* Debug */
#define dprintk(level, format, a...) if (cycx_debug >= level) printk(format, ##a)

extern unsigned int cycx_debug;

/* Data Structures */
/* X.25 Command Block. */
struct cycx_x25_cmd {
	u16 command;
	u16 link;	/* values: 0 or 1 */
	u16 len;	/* values: 0 thru 0x205 (517) */
	u32 buf;
} PACKED;

/* Defines for the 'command' field. */
#define X25_CONNECT_REQUEST             0x4401
#define X25_CONNECT_RESPONSE            0x4402
#define X25_DISCONNECT_REQUEST          0x4403
#define X25_DISCONNECT_RESPONSE         0x4404
#define X25_DATA_REQUEST                0x4405
#define X25_ACK_TO_VC			0x4406
#define X25_INTERRUPT_RESPONSE          0x4407
#define X25_CONFIG                      0x4408
#define X25_CONNECT_INDICATION          0x4409
#define X25_CONNECT_CONFIRM             0x440A
#define X25_DISCONNECT_INDICATION       0x440B
#define X25_DISCONNECT_CONFIRM          0x440C
#define X25_DATA_INDICATION             0x440E
#define X25_INTERRUPT_INDICATION        0x440F
#define X25_ACK_FROM_VC			0x4410
#define X25_ACK_N3			0x4411
#define X25_CONNECT_COLLISION           0x4413
#define X25_N3WIN                       0x4414
#define X25_LINE_ON                     0x4415
#define X25_LINE_OFF                    0x4416
#define X25_RESET_REQUEST               0x4417
#define X25_LOG                         0x4500
#define X25_STATISTIC                   0x4600
#define X25_TRACE                       0x4700
#define X25_N2TRACEXC                   0x4702
#define X25_N3TRACEXC                   0x4703

/**
 *	struct cycx_x25_config - cyclom2x x25 firmware configuration
 *	@link - link number
 *	@speed - line speed
 *	@clock - internal/external
 *	@n2 - # of level 2 retransm.(values: 1 thru FF)
 *	@n2win - level 2 window (values: 1 thru 7)
 *	@n3win - level 3 window (values: 1 thru 7)
 *	@nvc - # of logical channels (values: 1 thru 64)
 *	@pktlen - level 3 packet lenght - log base 2 of size
 *	@locaddr - my address
 *	@remaddr - remote address
 *	@t1 - time, in seconds
 *	@t2 - time, in seconds
 *	@t21 - time, in seconds
 *	@npvc - # of permanent virt. circuits (1 thru nvc)
 *	@t23 - time, in seconds
 *	@flags - see dosx25.doc, in portuguese, for details
 */
struct cycx_x25_config {
	u8  link;
	u8  speed;
	u8  clock;
	u8  n2;
	u8  n2win;
	u8  n3win;
	u8  nvc;
	u8  pktlen;
	u8  locaddr;
	u8  remaddr;
	u16 t1;
	u16 t2;
	u8  t21;
	u8  npvc;
	u8  t23;
	u8  flags;
} PACKED;

struct cycx_x25_stats {
	u16 rx_crc_errors;
	u16 rx_over_errors;
	u16 n2_tx_frames;
	u16 n2_rx_frames;
	u16 tx_timeouts;
	u16 rx_timeouts;
	u16 n3_tx_packets;
	u16 n3_rx_packets;
	u16 tx_aborts;
	u16 rx_aborts;
} PACKED;
#endif	/* _CYCX_X25_H */
