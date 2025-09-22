/*	$OpenBSD: uslhcomreg.h,v 1.2 2015/01/22 14:33:01 krw Exp $	*/

/*
 * Copyright (c) 2015 SASANO Takayoshi <uaa@openbsd.org>
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

#define	USLHCOM_TX_HEADER_SIZE		sizeof(u_char)

#define	SET_TRANSMIT_DATA(x)		(x)
#define	GET_RECEIVE_DATA(x)		(x)

#define	SET_DEVICE_RESET		0x40
#define	GET_SET_UART_ENABLE		0x41
#define	GET_UART_STATUS			0x42
#define	SET_CLEAR_FIFOS			0x43
#define	GET_GPIO_STATE			0x44
#define	SET_GPIO_STATE			0x45
#define	GET_VERSION			0x46
#define	GET_SET_OTP_LOCK_BYTE		0x47

#define	GET_SET_UART_CONFIG		0x50
#define	SET_TRANSMIT_LINE_BREAK		0x51
#define	SET_STOP_LINE_BREAK		0x52


/* SET_DEVICE_RESET */
#define	DEVICE_RESET_VALUE		0x00

/* GET_SET_UART_ENABLE */
#define	UART_DISABLE			0x00
#define	UART_ENABLE			0x01

/* GET_UART_STATUS */
struct uslhcom_uart_status {
	u_char	tx_fifo[2];	/* (big endian) */
	u_char	rx_fifo[2];	/* (big endian) */
	u_char	error_status;
	u_char	break_status;
} __packed;

#define	ERROR_STATUS_PARITY		0x01
#define	ERROR_STATUS_OVERRUN		0x02
#define	BREAK_STATUS			0x01

/* SET_CLEAR_FIFO */
#define	CLEAR_TX_FIFO			0x01
#define	CLEAR_RX_FIFO			0x02

/* GET_VERSION */
struct uslhcom_version_info {
	u_char	product_id;
	u_char	product_revision;
} __packed;

/* GET_SET_UART_CONFIG */
struct uslhcom_uart_config {
	u_char	baud_rate[4];	/* (big endian) */
	u_char	parity;
	u_char	data_control;
	u_char	data_bits;
	u_char	stop_bits;
} __packed;

/*
 * Silicon Labs CP2110/4 Application Note (AN434) Rev 0.4 says that
 * valid baud rate is 300bps to 500,000bps.
 * But HidUartSample of CP2110 SDK accepts 50bps to 2,000,000bps.
 */
#define	UART_CONFIG_BAUD_RATE_MIN	50
#define	UART_CONFIG_BAUD_RATE_MAX	2000000

#define	UART_CONFIG_PARITY_NONE		0x00
#define	UART_CONFIG_PARITY_EVEN		0x01
#define	UART_CONFIG_PARITY_ODD		0x02
#define	UART_CONFIG_PARITY_MARK		0x03
#define	UART_CONFIG_PARITY_SPACE	0x04

#define	UART_CONFIG_DATA_CONTROL_NONE	0x00
#define	UART_CONFIG_DATA_CONTROL_HARD	0x01

/*
 * AN434 Rev 0.4 describes setting 0x05 ... 0x08 to configure data bits.
 * But actually it requires different values.
 */
#define	UART_CONFIG_DATA_BITS_5		0x00
#define	UART_CONFIG_DATA_BITS_6		0x01
#define	UART_CONFIG_DATA_BITS_7		0x02
#define	UART_CONFIG_DATA_BITS_8		0x03

#define	UART_CONFIG_STOP_BITS_1		0x00
#define	UART_CONFIG_STOP_BITS_2		0x01
