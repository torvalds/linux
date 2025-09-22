/* $OpenBSD: exuartreg.h,v 1.4 2021/02/22 18:32:02 kettenis Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define	EXUART_ULCON				0x00
#define		EXUART_ULCON_WORD_FIVE			(0x0 << 0)
#define		EXUART_ULCON_WORD_SIX			(0x1 << 0)
#define		EXUART_ULCON_WORD_SEVEN			(0x2 << 0)
#define		EXUART_ULCON_WORD_EIGHT			(0x3 << 0)
#define		EXUART_ULCON_WORD_MASK			(0x3 << 0)
#define		EXUART_ULCON_STOP_ONE			(0x0 << 2)
#define		EXUART_ULCON_STOP_TWO			(0x1 << 2)
#define		EXUART_ULCON_PARITY_NONE		(0x0 << 3)
#define		EXUART_ULCON_PARITY_ODD			(0x4 << 3)
#define		EXUART_ULCON_PARITY_EVEN		(0x5 << 3)
#define		EXUART_ULCON_PARITY_FORCED1		(0x6 << 3)
#define		EXUART_ULCON_PARITY_FORCED0		(0x7 << 3)
#define		EXUART_ULCON_PARITY_MASK		(0x7 << 3)
#define		EXUART_ULCON_INFRARED			(0x1 << 6)
#define	EXUART_UCON				0x04
#define		EXUART_UCON_RX_IRQORPOLL		(0x1 << 0)
#define		EXUART_UCON_RX_DMA			(0x2 << 0)
#define		EXUART_UCON_TX_IRQORPOLL		(0x1 << 2)
#define		EXUART_UCON_TX_DMA			(0x2 << 2)
#define		EXUART_UCON_SEND_BREAK			(0x1 << 4)
#define		EXUART_UCON_LOOPBACK_MODE		(0x1 << 5)
#define		EXUART_UCON_RX_ERR_STS_INT		(0x1 << 6)
#define		EXUART_UCON_RX_TIMEOUT			(0x1 << 7)
#define		EXUART_UCON_RX_INT_TYPE_PULSE		(0x0 << 8)
#define		EXUART_UCON_RX_INT_TYPE_LEVEL		(0x1 << 8)
#define		EXUART_UCON_TX_INT_TYPE_PULSE		(0x0 << 9)
#define		EXUART_UCON_TX_INT_TYPE_LEVEL		(0x1 << 9)
#define		EXUART_UCON_RX_TIMEOUT_DMA_SUSPEND	(0x1 << 10)
#define		EXUART_UCON_RX_TIMEOUT_EMPTY_FIFO	(0x1 << 11)
#define		EXUART_UCON_RX_TIMEOUT_INTERVAL(x)	(((x) & 0xf) << 12) /* 8 * (x + a) frame time */
#define		EXUART_UCON_RX_DMA_BURST_1B		(0x0 << 16)
#define		EXUART_UCON_RX_DMA_BURST_4B		(0x1 << 16)
#define		EXUART_UCON_RX_DMA_BURST_8B		(0x2 << 16)
#define		EXUART_UCON_RX_DMA_BURST_16B		(0x3 << 16)
#define		EXUART_UCON_TX_DMA_BURST_1B		(0x0 << 20)
#define		EXUART_UCON_TX_DMA_BURST_4B		(0x1 << 20)
#define		EXUART_UCON_TX_DMA_BURST_8B		(0x2 << 20)
#define		EXUART_UCON_TX_DMA_BURST_16B		(0x3 << 20)
#define		EXUART_S5L_UCON_RX_TIMEOUT		(0x1 << 9)
#define		EXUART_S5L_UCON_RXTHRESH		(0x1 << 12)
#define		EXUART_S5L_UCON_TXTHRESH		(0x1 << 13)
#define	EXUART_UFCON				0x08
#define		EXUART_UFCON_FIFO_ENABLE		(0x1 << 0)
#define		EXUART_UFCON_RX_FIFO_RESET		(0x1 << 1)
#define		EXUART_UFCON_TX_FIFO_RESET		(0x1 << 2)
#define		EXUART_UFCON_RX_FIFO_TRIGGER_LEVEL(x)	(((x) & 0x7) << 4)
#define		EXUART_UFCON_TX_FIFO_TRIGGER_LEVEL(x)	(((x) & 0x7) << 8)
#define	EXUART_UMCON				0x0c
#define		EXUART_UMCON_RTS			(0x1 << 0)
#define		EXUART_UMCON_MODEM_INT_EN		(0x1 << 3)
#define		EXUART_UMCON_AUTO_FLOW_CONTROL		(0x1 << 4)
#define		EXUART_UMCON_RTS_TRIGGER_LEVEL		(((x) & 0x7) << 5)
#define	EXUART_UTRSTAT				0x10
#define		EXUART_UTRSTAT_RXBREADY			(0x1 << 0)
#define		EXUART_UTRSTAT_TXBEMPTY			(0x1 << 1)
#define		EXUART_UTRSTAT_TXEMPTY			(0x1 << 2)
#define		EXUART_UTRSTAT_RX_TIMEOUT_STSCLR	(0x1 << 3)
#define		EXUART_UTRSTAT_RX_DMA_FSM_STS(x)	(((x) >> 8) & 0xf)
#define		EXUART_UTRSTAT_TX_DMA_FSM_STS(x)	(((x) >> 12) & 0xf)
#define		EXUART_UTRSTAT_RX_FIFO_CNT_TIMEOUT(x)	(((x) >> 16) & 0xff)
#define		EXUART_S5L_UTRSTAT_RXTHRESH		(0x1 << 4)
#define		EXUART_S5L_UTRSTAT_TXTHRESH		(0x1 << 5)
#define		EXUART_S5L_UTRSTAT_RX_TIMEOUT		(0x1 << 9)
#define	EXUART_UERSTAT				0x14
#define		EXUART_UERSTAT_OVERRUN			(0x1 << 0)
#define		EXUART_UERSTAT_PARITY			(0x1 << 1)
#define		EXUART_UERSTAT_FRAME			(0x1 << 2)
#define		EXUART_UERSTAT_BREAK			(0x1 << 3)
#define	EXUART_UFSTAT				0x18
#define		EXUART_UFSTAT_RX_FIFO_CNT(x)		(((x) >> 0) & 0xff) /* 0 when full */
#define		EXUART_UFSTAT_RX_FIFO_CNT_MASK		(0xff << 0) /* 0 when full */
#define		EXUART_UFSTAT_RX_FIFO_FULL		(0x1 << 8)
#define		EXUART_UFSTAT_RX_FIFO_ERROR		(0x1 << 9)
#define		EXUART_UFSTAT_TX_FIFO_CNT(x)		(((x) >> 16) & 0xff) /* 0 when full */
#define		EXUART_UFSTAT_TX_FIFO_CNT_MASK		(0xff << 16) /* 0 when full */
#define		EXUART_UFSTAT_TX_FIFO_FULL		(0x1 << 24)
#define		EXUART_S5L_UFSTAT_RX_FIFO_CNT(x)	(((x) >> 0) & 0xf) /* 0 when full */
#define		EXUART_S5L_UFSTAT_RX_FIFO_CNT_MASK	(0xf << 0) /* 0 when full */
#define		EXUART_S5L_UFSTAT_RX_FIFO_FULL		(0x1 << 8)
#define		EXUART_S5L_UFSTAT_TX_FIFO_CNT(x)	(((x) >> 4) & 0xf) /* 0 when full */
#define		EXUART_S5L_UFSTAT_TX_FIFO_CNT_MASK	(0xf << 4) /* 0 when full */
#define		EXUART_S5L_UFSTAT_TX_FIFO_FULL		(0x1 << 9)
#define	EXUART_UMSTAT				0x1c
#define		EXUART_UMSTAT_CTS			(0x1 << 0)
#define		EXUART_UMSTAT_DELTA_CTS			(0x1 << 4)
#define	EXUART_UTXH				0x20
#define	EXUART_URXH				0x24
#define	EXUART_UBRDIV				0x28
#define		EXUART_UBRDIV_SEL(x)			(((x) & 0xffff) << 0)
#define	EXUART_UFRACVAL				0x2c
#define		EXUART_UFRACVAL_SEL(x)			(((x) & 0xf) << 0)
#define	EXUART_UINTP				0x30
#define		EXUART_UINTP_RXD			(0x1 << 0)
#define		EXUART_UINTP_ERROR			(0x1 << 1)
#define		EXUART_UINTP_TXD			(0x1 << 2)
#define		EXUART_UINTP_MODEM			(0x1 << 3)
#define	EXUART_UINTS				0x34
#define		EXUART_UINTS_RXD			(0x1 << 0)
#define		EXUART_UINTS_ERROR			(0x1 << 1)
#define		EXUART_UINTS_TXD			(0x1 << 2)
#define		EXUART_UINTS_MODEM			(0x1 << 3)
#define	EXUART_UINTM				0x38
#define		EXUART_UINTM_RXD			(0x1 << 0)
#define		EXUART_UINTM_ERROR			(0x1 << 1)
#define		EXUART_UINTM_TXD			(0x1 << 2)
#define		EXUART_UINTM_MODEM			(0x1 << 3)
