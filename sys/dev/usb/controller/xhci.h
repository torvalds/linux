/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _XHCI_H_
#define	_XHCI_H_

#define	XHCI_MAX_DEVICES	MIN(USB_MAX_DEVICES, 128)
#define	XHCI_MAX_ENDPOINTS	32	/* hardcoded - do not change */
#define	XHCI_MAX_SCRATCHPADS	256	/* theoretical max is 1023 */
#define	XHCI_MAX_EVENTS		(16 * 13)
#define	XHCI_MAX_COMMANDS	(16 * 1)
#define	XHCI_MAX_RSEG		1
#define	XHCI_MAX_TRANSFERS	4
#if USB_MAX_EP_STREAMS == 8
#define	XHCI_MAX_STREAMS	8
#define	XHCI_MAX_STREAMS_LOG	3
#elif USB_MAX_EP_STREAMS == 1
#define	XHCI_MAX_STREAMS	1
#define	XHCI_MAX_STREAMS_LOG	0
#else
#error "The USB_MAX_EP_STREAMS value is not supported."
#endif
#define	XHCI_DEV_CTX_ADDR_ALIGN		64	/* bytes */
#define	XHCI_DEV_CTX_ALIGN		64	/* bytes */
#define	XHCI_INPUT_CTX_ALIGN		64	/* bytes */
#define	XHCI_SLOT_CTX_ALIGN		32	/* bytes */
#define	XHCI_ENDP_CTX_ALIGN		32	/* bytes */
#define	XHCI_STREAM_CTX_ALIGN		16	/* bytes */
#define	XHCI_TRANS_RING_SEG_ALIGN	16	/* bytes */
#define	XHCI_CMD_RING_SEG_ALIGN		64	/* bytes */
#define	XHCI_EVENT_RING_SEG_ALIGN	64	/* bytes */
#define	XHCI_SCRATCH_BUF_ARRAY_ALIGN	64	/* bytes */
#define	XHCI_SCRATCH_BUFFER_ALIGN	USB_PAGE_SIZE
#define	XHCI_TRB_ALIGN			16	/* bytes */
#define	XHCI_TD_ALIGN			64	/* bytes */
#define	XHCI_PAGE_SIZE			4096	/* bytes */

struct xhci_dev_ctx_addr {
	volatile uint64_t	qwBaaDevCtxAddr[USB_MAX_DEVICES + 1];
	struct {
		volatile uint64_t dummy;
	} __aligned(64) padding;
	volatile uint64_t	qwSpBufPtr[XHCI_MAX_SCRATCHPADS];
};

#define	XHCI_EPNO2EPID(x) \
    ((((x) & UE_DIR_IN) ? 1 : 0) | (2 * ((x) & UE_ADDR)))

struct xhci_slot_ctx {
	volatile uint32_t	dwSctx0;
#define	XHCI_SCTX_0_ROUTE_SET(x)		((x) & 0xFFFFF)
#define	XHCI_SCTX_0_ROUTE_GET(x)		((x) & 0xFFFFF)
#define	XHCI_SCTX_0_SPEED_SET(x)		(((x) & 0xF) << 20)
#define	XHCI_SCTX_0_SPEED_GET(x)		(((x) >> 20) & 0xF)
#define	XHCI_SCTX_0_MTT_SET(x)			(((x) & 0x1) << 25)
#define	XHCI_SCTX_0_MTT_GET(x)			(((x) >> 25) & 0x1)
#define	XHCI_SCTX_0_HUB_SET(x)			(((x) & 0x1) << 26)
#define	XHCI_SCTX_0_HUB_GET(x)			(((x) >> 26) & 0x1)
#define	XHCI_SCTX_0_CTX_NUM_SET(x)		(((x) & 0x1F) << 27)
#define	XHCI_SCTX_0_CTX_NUM_GET(x)		(((x) >> 27) & 0x1F)
	volatile uint32_t	dwSctx1;
#define	XHCI_SCTX_1_MAX_EL_SET(x)		((x) & 0xFFFF)
#define	XHCI_SCTX_1_MAX_EL_GET(x)		((x) & 0xFFFF)
#define	XHCI_SCTX_1_RH_PORT_SET(x)		(((x) & 0xFF) << 16)
#define	XHCI_SCTX_1_RH_PORT_GET(x)		(((x) >> 16) & 0xFF)
#define	XHCI_SCTX_1_NUM_PORTS_SET(x)		(((x) & 0xFF) << 24)
#define	XHCI_SCTX_1_NUM_PORTS_GET(x)		(((x) >> 24) & 0xFF)
	volatile uint32_t	dwSctx2;
#define	XHCI_SCTX_2_TT_HUB_SID_SET(x)		((x) & 0xFF)
#define	XHCI_SCTX_2_TT_HUB_SID_GET(x)		((x) & 0xFF)
#define	XHCI_SCTX_2_TT_PORT_NUM_SET(x)		(((x) & 0xFF) << 8)
#define	XHCI_SCTX_2_TT_PORT_NUM_GET(x)		(((x) >> 8) & 0xFF)
#define	XHCI_SCTX_2_TT_THINK_TIME_SET(x)	(((x) & 0x3) << 16)
#define	XHCI_SCTX_2_TT_THINK_TIME_GET(x)	(((x) >> 16) & 0x3)
#define	XHCI_SCTX_2_IRQ_TARGET_SET(x)		(((x) & 0x3FF) << 22)
#define	XHCI_SCTX_2_IRQ_TARGET_GET(x)		(((x) >> 22) & 0x3FF)
	volatile uint32_t	dwSctx3;
#define	XHCI_SCTX_3_DEV_ADDR_SET(x)		((x) & 0xFF)
#define	XHCI_SCTX_3_DEV_ADDR_GET(x)		((x) & 0xFF)
#define	XHCI_SCTX_3_SLOT_STATE_SET(x)		(((x) & 0x1F) << 27)
#define	XHCI_SCTX_3_SLOT_STATE_GET(x)		(((x) >> 27) & 0x1F)
	volatile uint32_t	dwSctx4;
	volatile uint32_t	dwSctx5;
	volatile uint32_t	dwSctx6;
	volatile uint32_t	dwSctx7;
};

struct xhci_endp_ctx {
	volatile uint32_t	dwEpCtx0;
#define	XHCI_EPCTX_0_EPSTATE_SET(x)		((x) & 0x7)
#define	XHCI_EPCTX_0_EPSTATE_GET(x)		((x) & 0x7)
#define	XHCI_EPCTX_0_MULT_SET(x)		(((x) & 0x3) << 8)
#define	XHCI_EPCTX_0_MULT_GET(x)		(((x) >> 8) & 0x3)
#define	XHCI_EPCTX_0_MAXP_STREAMS_SET(x)	(((x) & 0x1F) << 10)
#define	XHCI_EPCTX_0_MAXP_STREAMS_GET(x)	(((x) >> 10) & 0x1F)
#define	XHCI_EPCTX_0_LSA_SET(x)			(((x) & 0x1) << 15)
#define	XHCI_EPCTX_0_LSA_GET(x)			(((x) >> 15) & 0x1)
#define	XHCI_EPCTX_0_IVAL_SET(x)		(((x) & 0xFF) << 16)
#define	XHCI_EPCTX_0_IVAL_GET(x)		(((x) >> 16) & 0xFF)
	volatile uint32_t	dwEpCtx1;
#define	XHCI_EPCTX_1_CERR_SET(x)		(((x) & 0x3) << 1)
#define	XHCI_EPCTX_1_CERR_GET(x)		(((x) >> 1) & 0x3)
#define	XHCI_EPCTX_1_EPTYPE_SET(x)		(((x) & 0x7) << 3)
#define	XHCI_EPCTX_1_EPTYPE_GET(x)		(((x) >> 3) & 0x7)
#define	XHCI_EPCTX_1_HID_SET(x)			(((x) & 0x1) << 7)
#define	XHCI_EPCTX_1_HID_GET(x)			(((x) >> 7) & 0x1)
#define	XHCI_EPCTX_1_MAXB_SET(x)		(((x) & 0xFF) << 8)
#define	XHCI_EPCTX_1_MAXB_GET(x)		(((x) >> 8) & 0xFF)
#define	XHCI_EPCTX_1_MAXP_SIZE_SET(x)		(((x) & 0xFFFF) << 16)
#define	XHCI_EPCTX_1_MAXP_SIZE_GET(x)		(((x) >> 16) & 0xFFFF)
	volatile uint64_t	qwEpCtx2;
#define	XHCI_EPCTX_2_DCS_SET(x)			((x) & 0x1)
#define	XHCI_EPCTX_2_DCS_GET(x)			((x) & 0x1)
#define	XHCI_EPCTX_2_TR_DQ_PTR_MASK		0xFFFFFFFFFFFFFFF0U
	volatile uint32_t	dwEpCtx4;
#define	XHCI_EPCTX_4_AVG_TRB_LEN_SET(x)		((x) & 0xFFFF)
#define	XHCI_EPCTX_4_AVG_TRB_LEN_GET(x)		((x) & 0xFFFF)
#define	XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_SET(x)	(((x) & 0xFFFF) << 16)
#define	XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_GET(x)	(((x) >> 16) & 0xFFFF)
	volatile uint32_t	dwEpCtx5;
	volatile uint32_t	dwEpCtx6;
	volatile uint32_t	dwEpCtx7;
};

struct xhci_input_ctx {
#define	XHCI_INCTX_NON_CTRL_MASK	0xFFFFFFFCU
	volatile uint32_t	dwInCtx0;
#define	XHCI_INCTX_0_DROP_MASK(n)	(1U << (n))
	volatile uint32_t	dwInCtx1;
#define	XHCI_INCTX_1_ADD_MASK(n)	(1U << (n))
	volatile uint32_t	dwInCtx2;
	volatile uint32_t	dwInCtx3;
	volatile uint32_t	dwInCtx4;
	volatile uint32_t	dwInCtx5;
	volatile uint32_t	dwInCtx6;
	volatile uint32_t	dwInCtx7;
};

struct xhci_input_dev_ctx {
	struct xhci_input_ctx	ctx_input;
	struct xhci_slot_ctx	ctx_slot;
	struct xhci_endp_ctx	ctx_ep[XHCI_MAX_ENDPOINTS - 1];
};

struct xhci_dev_ctx {
	struct xhci_slot_ctx	ctx_slot;
	struct xhci_endp_ctx	ctx_ep[XHCI_MAX_ENDPOINTS - 1];
} __aligned(XHCI_DEV_CTX_ALIGN);

struct xhci_stream_ctx {
	volatile uint64_t	qwSctx0;
#define	XHCI_SCTX_0_DCS_GET(x)		((x) & 0x1)
#define	XHCI_SCTX_0_DCS_SET(x)		((x) & 0x1)
#define	XHCI_SCTX_0_SCT_SET(x)		(((x) & 0x7) << 1)
#define	XHCI_SCTX_0_SCT_GET(x)		(((x) >> 1) & 0x7)
#define	XHCI_SCTX_0_SCT_SEC_TR_RING	0x0
#define	XHCI_SCTX_0_SCT_PRIM_TR_RING	0x1
#define	XHCI_SCTX_0_SCT_PRIM_SSA_8	0x2
#define	XHCI_SCTX_0_SCT_PRIM_SSA_16	0x3
#define	XHCI_SCTX_0_SCT_PRIM_SSA_32	0x4
#define	XHCI_SCTX_0_SCT_PRIM_SSA_64	0x5
#define	XHCI_SCTX_0_SCT_PRIM_SSA_128	0x6
#define	XHCI_SCTX_0_SCT_PRIM_SSA_256	0x7
#define	XHCI_SCTX_0_TR_DQ_PTR_MASK	0xFFFFFFFFFFFFFFF0U
	volatile uint32_t	dwSctx2;
	volatile uint32_t	dwSctx3;
};

struct xhci_trb {
	volatile uint64_t	qwTrb0;
#define	XHCI_TRB_0_DIR_IN_MASK		(0x80ULL << 0)
#define	XHCI_TRB_0_WLENGTH_MASK		(0xFFFFULL << 48)
	volatile uint32_t	dwTrb2;
#define	XHCI_TRB_2_ERROR_GET(x)		(((x) >> 24) & 0xFF)
#define	XHCI_TRB_2_ERROR_SET(x)		(((x) & 0xFF) << 24)
#define	XHCI_TRB_2_TDSZ_GET(x)		(((x) >> 17) & 0x1F)
#define	XHCI_TRB_2_TDSZ_SET(x)		(((x) & 0x1F) << 17)
#define	XHCI_TRB_2_REM_GET(x)		((x) & 0xFFFFFF)
#define	XHCI_TRB_2_REM_SET(x)		((x) & 0xFFFFFF)
#define	XHCI_TRB_2_BYTES_GET(x)		((x) & 0x1FFFF)
#define	XHCI_TRB_2_BYTES_SET(x)		((x) & 0x1FFFF)
#define	XHCI_TRB_2_IRQ_GET(x)		(((x) >> 22) & 0x3FF)
#define	XHCI_TRB_2_IRQ_SET(x)		(((x) & 0x3FF) << 22)
#define	XHCI_TRB_2_STREAM_GET(x)	(((x) >> 16) & 0xFFFF)
#define	XHCI_TRB_2_STREAM_SET(x)	(((x) & 0xFFFF) << 16)

	volatile uint32_t	dwTrb3;
#define	XHCI_TRB_3_TYPE_GET(x)		(((x) >> 10) & 0x3F)
#define	XHCI_TRB_3_TYPE_SET(x)		(((x) & 0x3F) << 10)
#define	XHCI_TRB_3_CYCLE_BIT		(1U << 0)
#define	XHCI_TRB_3_TC_BIT		(1U << 1)	/* command ring only */
#define	XHCI_TRB_3_ENT_BIT		(1U << 1)	/* transfer ring only */
#define	XHCI_TRB_3_ISP_BIT		(1U << 2)
#define	XHCI_TRB_3_NSNOOP_BIT		(1U << 3)
#define	XHCI_TRB_3_CHAIN_BIT		(1U << 4)
#define	XHCI_TRB_3_IOC_BIT		(1U << 5)
#define	XHCI_TRB_3_IDT_BIT		(1U << 6)
#define	XHCI_TRB_3_TBC_GET(x)		(((x) >> 7) & 3)
#define	XHCI_TRB_3_TBC_SET(x)		(((x) & 3) << 7)
#define	XHCI_TRB_3_BEI_BIT		(1U << 9)
#define	XHCI_TRB_3_DCEP_BIT		(1U << 9)
#define	XHCI_TRB_3_PRSV_BIT		(1U << 9)
#define	XHCI_TRB_3_BSR_BIT		(1U << 9)
#define	XHCI_TRB_3_TRT_MASK		(3U << 16)
#define	XHCI_TRB_3_TRT_NONE		(0U << 16)
#define	XHCI_TRB_3_TRT_OUT		(2U << 16)
#define	XHCI_TRB_3_TRT_IN		(3U << 16)
#define	XHCI_TRB_3_DIR_IN		(1U << 16)
#define	XHCI_TRB_3_TLBPC_GET(x)		(((x) >> 16) & 0xF)
#define	XHCI_TRB_3_TLBPC_SET(x)		(((x) & 0xF) << 16)
#define	XHCI_TRB_3_EP_GET(x)		(((x) >> 16) & 0x1F)
#define	XHCI_TRB_3_EP_SET(x)		(((x) & 0x1F) << 16)
#define	XHCI_TRB_3_FRID_GET(x)		(((x) >> 20) & 0x7FF)
#define	XHCI_TRB_3_FRID_SET(x)		(((x) & 0x7FF) << 20)
#define	XHCI_TRB_3_ISO_SIA_BIT		(1U << 31)
#define	XHCI_TRB_3_SUSP_EP_BIT		(1U << 23)
#define	XHCI_TRB_3_SLOT_GET(x)		(((x) >> 24) & 0xFF)
#define	XHCI_TRB_3_SLOT_SET(x)		(((x) & 0xFF) << 24)

/* Commands */
#define	XHCI_TRB_TYPE_RESERVED		0x00
#define	XHCI_TRB_TYPE_NORMAL		0x01
#define	XHCI_TRB_TYPE_SETUP_STAGE	0x02
#define	XHCI_TRB_TYPE_DATA_STAGE	0x03
#define	XHCI_TRB_TYPE_STATUS_STAGE	0x04
#define	XHCI_TRB_TYPE_ISOCH		0x05
#define	XHCI_TRB_TYPE_LINK		0x06
#define	XHCI_TRB_TYPE_EVENT_DATA	0x07
#define	XHCI_TRB_TYPE_NOOP		0x08
#define	XHCI_TRB_TYPE_ENABLE_SLOT	0x09
#define	XHCI_TRB_TYPE_DISABLE_SLOT	0x0A
#define	XHCI_TRB_TYPE_ADDRESS_DEVICE	0x0B
#define	XHCI_TRB_TYPE_CONFIGURE_EP	0x0C
#define	XHCI_TRB_TYPE_EVALUATE_CTX	0x0D
#define	XHCI_TRB_TYPE_RESET_EP		0x0E
#define	XHCI_TRB_TYPE_STOP_EP		0x0F
#define	XHCI_TRB_TYPE_SET_TR_DEQUEUE	0x10
#define	XHCI_TRB_TYPE_RESET_DEVICE	0x11
#define	XHCI_TRB_TYPE_FORCE_EVENT	0x12
#define	XHCI_TRB_TYPE_NEGOTIATE_BW	0x13
#define	XHCI_TRB_TYPE_SET_LATENCY_TOL  	0x14
#define	XHCI_TRB_TYPE_GET_PORT_BW	0x15
#define	XHCI_TRB_TYPE_FORCE_HEADER	0x16
#define	XHCI_TRB_TYPE_NOOP_CMD		0x17

/* Events */
#define	XHCI_TRB_EVENT_TRANSFER		0x20
#define	XHCI_TRB_EVENT_CMD_COMPLETE	0x21
#define	XHCI_TRB_EVENT_PORT_STS_CHANGE  0x22
#define	XHCI_TRB_EVENT_BW_REQUEST      	0x23
#define	XHCI_TRB_EVENT_DOORBELL		0x24
#define	XHCI_TRB_EVENT_HOST_CTRL	0x25
#define	XHCI_TRB_EVENT_DEVICE_NOTIFY	0x26
#define	XHCI_TRB_EVENT_MFINDEX_WRAP	0x27

/* Error codes */
#define	XHCI_TRB_ERROR_INVALID		0x00
#define	XHCI_TRB_ERROR_SUCCESS		0x01
#define	XHCI_TRB_ERROR_DATA_BUF		0x02
#define	XHCI_TRB_ERROR_BABBLE		0x03
#define	XHCI_TRB_ERROR_XACT		0x04
#define	XHCI_TRB_ERROR_TRB		0x05
#define	XHCI_TRB_ERROR_STALL		0x06
#define	XHCI_TRB_ERROR_RESOURCE		0x07
#define	XHCI_TRB_ERROR_BANDWIDTH	0x08
#define	XHCI_TRB_ERROR_NO_SLOTS		0x09
#define	XHCI_TRB_ERROR_STREAM_TYPE	0x0A
#define	XHCI_TRB_ERROR_SLOT_NOT_ON	0x0B
#define	XHCI_TRB_ERROR_ENDP_NOT_ON	0x0C
#define	XHCI_TRB_ERROR_SHORT_PKT	0x0D
#define	XHCI_TRB_ERROR_RING_UNDERRUN	0x0E
#define	XHCI_TRB_ERROR_RING_OVERRUN	0x0F
#define	XHCI_TRB_ERROR_VF_RING_FULL	0x10
#define	XHCI_TRB_ERROR_PARAMETER	0x11
#define	XHCI_TRB_ERROR_BW_OVERRUN	0x12
#define	XHCI_TRB_ERROR_CONTEXT_STATE	0x13
#define	XHCI_TRB_ERROR_NO_PING_RESP	0x14
#define	XHCI_TRB_ERROR_EV_RING_FULL	0x15
#define	XHCI_TRB_ERROR_INCOMPAT_DEV	0x16
#define	XHCI_TRB_ERROR_MISSED_SERVICE	0x17
#define	XHCI_TRB_ERROR_CMD_RING_STOP	0x18
#define	XHCI_TRB_ERROR_CMD_ABORTED	0x19
#define	XHCI_TRB_ERROR_STOPPED		0x1A
#define	XHCI_TRB_ERROR_LENGTH		0x1B
#define	XHCI_TRB_ERROR_BAD_MELAT	0x1D
#define	XHCI_TRB_ERROR_ISOC_OVERRUN	0x1F
#define	XHCI_TRB_ERROR_EVENT_LOST	0x20
#define	XHCI_TRB_ERROR_UNDEFINED	0x21
#define	XHCI_TRB_ERROR_INVALID_SID	0x22
#define	XHCI_TRB_ERROR_SEC_BW		0x23
#define	XHCI_TRB_ERROR_SPLIT_XACT	0x24
} __aligned(4);

struct xhci_dev_endpoint_trbs {
	struct xhci_trb		trb[(XHCI_MAX_STREAMS *
	    XHCI_MAX_TRANSFERS) + XHCI_MAX_STREAMS];
};

#if (USB_PAGE_SIZE < 4096)
#error "The XHCI driver needs a pagesize above or equal to 4K"
#endif

/* Define the maximum payload which we will handle in a single TRB */
#define	XHCI_TD_PAYLOAD_MAX	65536	/* bytes */

/* Define the maximum payload of a single scatter-gather list element */
#define	XHCI_TD_PAGE_SIZE \
  ((USB_PAGE_SIZE < XHCI_TD_PAYLOAD_MAX) ? USB_PAGE_SIZE : XHCI_TD_PAYLOAD_MAX)

/* Define the maximum length of the scatter-gather list */
#define	XHCI_TD_PAGE_NBUF \
  (((XHCI_TD_PAYLOAD_MAX + XHCI_TD_PAGE_SIZE - 1) / XHCI_TD_PAGE_SIZE) + 1)

struct xhci_td {
	/* one LINK TRB has been added to the TRB array */
	struct xhci_trb		td_trb[XHCI_TD_PAGE_NBUF + 1];

/*
 * Extra information needed:
 */
	uint64_t		td_self;
	struct xhci_td		*next;
	struct xhci_td		*alt_next;
	struct xhci_td		*obj_next;
	struct usb_page_cache	*page_cache;
	uint32_t		len;
	uint32_t		remainder;
	uint8_t			ntrb;
	uint8_t			status;
} __aligned(XHCI_TRB_ALIGN);

struct xhci_command {
	struct xhci_trb		trb;
	TAILQ_ENTRY(xhci_command) entry;
};

struct xhci_event_ring_seg {
	volatile uint64_t	qwEvrsTablePtr;
	volatile uint32_t	dwEvrsTableSize;
	volatile uint32_t	dwEvrsReserved;
};

struct xhci_hw_root {
	struct xhci_event_ring_seg	hwr_ring_seg[XHCI_MAX_RSEG];
	struct {
		volatile uint64_t dummy;
	} __aligned(64)			padding;
	struct xhci_trb			hwr_events[XHCI_MAX_EVENTS];
	struct xhci_trb			hwr_commands[XHCI_MAX_COMMANDS];
};

struct xhci_endpoint_ext {
	struct xhci_trb		*trb;
	struct usb_xfer		*xfer[XHCI_MAX_TRANSFERS * XHCI_MAX_STREAMS];
	struct usb_page_cache	*page_cache;
	uint64_t		physaddr;
	uint8_t			trb_used[XHCI_MAX_STREAMS];
	uint8_t			trb_index[XHCI_MAX_STREAMS];
	uint8_t			trb_halted;
	uint8_t			trb_running;
	uint8_t			trb_ep_mode;
	uint8_t			trb_ep_maxp;
};

enum {
	XHCI_ST_DISABLED,
	XHCI_ST_ENABLED,
	XHCI_ST_DEFAULT,
	XHCI_ST_ADDRESSED,
	XHCI_ST_CONFIGURED,
	XHCI_ST_MAX
};

struct xhci_hw_dev {
	struct usb_page_cache	device_pc;
	struct usb_page_cache	input_pc;
	struct usb_page_cache	endpoint_pc[XHCI_MAX_ENDPOINTS];

	struct usb_page		device_pg;
	struct usb_page		input_pg;
	struct usb_page		endpoint_pg[XHCI_MAX_ENDPOINTS];

	struct xhci_endpoint_ext endp[XHCI_MAX_ENDPOINTS];

	uint8_t			state;
	uint8_t			nports;
	uint8_t			tt;
	uint8_t			context_num;
};

struct xhci_hw_softc {
	struct usb_page_cache	root_pc;
	struct usb_page_cache	ctx_pc;
	struct usb_page_cache	scratch_pc[XHCI_MAX_SCRATCHPADS];

	struct usb_page		root_pg;
	struct usb_page		ctx_pg;
	struct usb_page		scratch_pg[XHCI_MAX_SCRATCHPADS];

	struct xhci_hw_dev	devs[XHCI_MAX_DEVICES + 1];
};

struct xhci_config_desc {
	struct usb_config_descriptor		confd;
	struct usb_interface_descriptor		ifcd;
	struct usb_endpoint_descriptor		endpd;
	struct usb_endpoint_ss_comp_descriptor	endpcd;
} __packed;

struct xhci_bos_desc {
	struct usb_bos_descriptor		bosd;
	struct usb_devcap_usb2ext_descriptor	usb2extd;
	struct usb_devcap_ss_descriptor		usbdcd;
	struct usb_devcap_container_id_descriptor cidd;
} __packed;

union xhci_hub_desc {
	struct usb_status		stat;
	struct usb_port_status		ps;
	struct usb_hub_ss_descriptor	hubd;
	uint8_t				temp[128];
};

typedef int (xhci_port_route_t)(device_t, uint32_t, uint32_t);

struct xhci_softc {
	struct xhci_hw_softc	sc_hw;
	/* base device */
	struct usb_bus		sc_bus;
	/* configure message */
	struct usb_bus_msg	sc_config_msg[2];

	struct usb_callout	sc_callout;

	xhci_port_route_t	*sc_port_route;

	union xhci_hub_desc	sc_hub_desc;

	struct cv		sc_cmd_cv;
	struct sx		sc_cmd_sx;

	struct usb_device	*sc_devices[XHCI_MAX_DEVICES];
	struct resource		*sc_io_res;
	struct resource		*sc_irq_res;
	struct resource		*sc_msix_res;

	void			*sc_intr_hdl;
	bus_size_t		sc_io_size;
	bus_space_tag_t		sc_io_tag;
	bus_space_handle_t	sc_io_hdl;
	/* last pending command address */
	uint64_t		sc_cmd_addr;
	/* result of command */
	uint32_t		sc_cmd_result[2];
 	/* copy of cmd register */
	uint32_t		sc_cmd;
	/* worst case exit latency */
	uint32_t		sc_exit_lat_max;

	/* offset to operational registers */
	uint32_t		sc_oper_off;
	/* offset to capability registers */
	uint32_t		sc_capa_off;
	/* offset to runtime registers */
	uint32_t		sc_runt_off;
	/* offset to doorbell registers */
	uint32_t		sc_door_off;

	/* chip specific */
	uint16_t		sc_erst_max;
	uint16_t		sc_event_idx;
	uint16_t		sc_command_idx;
	uint16_t		sc_imod_default;

	/* number of scratch pages */
	uint16_t		sc_noscratch;

	uint8_t			sc_event_ccs;
	uint8_t			sc_command_ccs;
	/* number of XHCI device slots */
	uint8_t			sc_noslot;
	/* number of ports on root HUB */
	uint8_t			sc_noport;
	/* root HUB device configuration */
	uint8_t			sc_conf;
	/* step status stage of all control transfers */
	uint8_t			sc_ctlstep;
	/* root HUB port event bitmap, max 256 ports */
	uint8_t			sc_hub_idata[32];

	/* size of context */
	uint8_t			sc_ctx_is_64_byte;

	/* vendor string for root HUB */
	char			sc_vendor[16];
};

#define	XHCI_CMD_LOCK(sc)	sx_xlock(&(sc)->sc_cmd_sx)
#define	XHCI_CMD_UNLOCK(sc)	sx_xunlock(&(sc)->sc_cmd_sx)
#define	XHCI_CMD_ASSERT_LOCKED(sc) sx_assert(&(sc)->sc_cmd_sx, SA_LOCKED)

/* prototypes */

uint8_t 	xhci_use_polling(void);
usb_error_t xhci_halt_controller(struct xhci_softc *);
usb_error_t xhci_reset_controller(struct xhci_softc *);
usb_error_t xhci_init(struct xhci_softc *, device_t, uint8_t);
usb_error_t xhci_start_controller(struct xhci_softc *);
void	xhci_interrupt(struct xhci_softc *);
void	xhci_uninit(struct xhci_softc *);

#endif					/* _XHCI_H_ */
