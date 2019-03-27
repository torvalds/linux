/*
 * ng_ubt_var.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_ubt_var.h,v 1.2 2003/03/22 23:44:36 max Exp $
 * $FreeBSD$
 */

#ifndef _NG_UBT_VAR_H_
#define	_NG_UBT_VAR_H_	1

/* Debug printf's */
#define	UBT_DEBUG(level, sc, fmt, ...)				\
do {								\
	if ((sc)->sc_debug >= (level))				\
		device_printf((sc)->sc_dev, "%s:%d: " fmt, 	\
			__FUNCTION__, __LINE__,## __VA_ARGS__);	\
} while (0)

#define	UBT_ALERT(...)		UBT_DEBUG(NG_UBT_ALERT_LEVEL, __VA_ARGS__)
#define	UBT_ERR(...)		UBT_DEBUG(NG_UBT_ERR_LEVEL, __VA_ARGS__)
#define	UBT_WARN(...)		UBT_DEBUG(NG_UBT_WARN_LEVEL, __VA_ARGS__)
#define	UBT_INFO(...)		UBT_DEBUG(NG_UBT_INFO_LEVEL, __VA_ARGS__)

#define UBT_NG_LOCK(sc)		mtx_lock(&(sc)->sc_ng_mtx)
#define UBT_NG_UNLOCK(sc)	mtx_unlock(&(sc)->sc_ng_mtx)

/* Bluetooth USB control request type */
#define	UBT_HCI_REQUEST		0x20
#define	UBT_DEFAULT_QLEN	64
#define	UBT_ISOC_NFRAMES	32	/* should be factor of 8 */

/* Bluetooth USB defines */
enum {
	/* Interface #0 transfers */
	UBT_IF_0_BULK_DT_WR = 0,
	UBT_IF_0_BULK_DT_RD,
	UBT_IF_0_INTR_DT_RD,
	UBT_IF_0_CTRL_DT_WR,
	
	/* Interface #1 transfers */
	UBT_IF_1_ISOC_DT_RD1,
	UBT_IF_1_ISOC_DT_RD2,
	UBT_IF_1_ISOC_DT_WR1,
	UBT_IF_1_ISOC_DT_WR2,

	UBT_N_TRANSFER,		/* total number of transfers */
};

/* USB device softc structure */
struct ubt_softc {
	device_t		sc_dev;		/* for debug printf */

	/* State */
	ng_ubt_node_debug_ep	sc_debug;	/* debug level */

	ng_ubt_node_stat_ep	sc_stat;	/* statistic */
#define	UBT_STAT_PCKTS_SENT(sc)		(sc)->sc_stat.pckts_sent ++
#define	UBT_STAT_BYTES_SENT(sc, n)	(sc)->sc_stat.bytes_sent += (n)
#define	UBT_STAT_PCKTS_RECV(sc)		(sc)->sc_stat.pckts_recv ++
#define	UBT_STAT_BYTES_RECV(sc, n)	(sc)->sc_stat.bytes_recv += (n)
#define	UBT_STAT_OERROR(sc)		(sc)->sc_stat.oerrors ++
#define	UBT_STAT_IERROR(sc)		(sc)->sc_stat.ierrors ++
#define	UBT_STAT_RESET(sc)	bzero(&(sc)->sc_stat, sizeof((sc)->sc_stat))

	/* USB device specific */
	struct mtx		sc_if_mtx;	/* interfaces lock */
	struct usb_xfer	*sc_xfer[UBT_N_TRANSFER];

	struct mtx		sc_ng_mtx;	/* lock for shared NG data */

	/* HCI commands */
	struct ng_bt_mbufq	sc_cmdq;	/* HCI command queue */
#define	UBT_CTRL_BUFFER_SIZE	(sizeof(struct usb_device_request) +	\
				 sizeof(ng_hci_cmd_pkt_t) + NG_HCI_CMD_PKT_SIZE)
#define	UBT_INTR_BUFFER_SIZE	(MCLBYTES-1)	/* reserve 1 byte for ID-tag */

	/* ACL data */
	struct ng_bt_mbufq	sc_aclq;	/* ACL data queue */
#define	UBT_BULK_READ_BUFFER_SIZE (MCLBYTES-1)	/* reserve 1 byte for ID-tag */
#define	UBT_BULK_WRITE_BUFFER_SIZE (MCLBYTES)

	/* SCO data */
	struct ng_bt_mbufq	sc_scoq;	/* SCO data queue */
	struct mbuf		*sc_isoc_in_buffer; /* SCO reassembly buffer */

	/* Netgraph specific */
	node_p			sc_node;	/* pointer back to node */
	hook_p			sc_hook;	/* upstream hook */

	/* Glue */
	int			sc_task_flags;	/* task flags */
#define UBT_FLAG_T_PENDING	(1 << 0)	/* task pending */
#define UBT_FLAG_T_STOP_ALL	(1 << 1)	/* stop all xfers */
#define UBT_FLAG_T_START_ALL	(1 << 2)	/* start all read and isoc
						   write xfers */
#define UBT_FLAG_T_START_CTRL	(1 << 3)	/* start control xfer (write) */
#define UBT_FLAG_T_START_BULK	(1 << 4)	/* start bulk xfer (write) */

	struct task		sc_task;
};
typedef struct ubt_softc	ubt_softc_t;
typedef struct ubt_softc *	ubt_softc_p;

#endif /* ndef _NG_UBT_VAR_H_ */

