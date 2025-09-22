/*	$OpenBSD: uftdireg.h,v 1.14 2022/12/30 00:54:09 kevlo Exp $ 	*/
/*	$NetBSD: uftdireg.h,v 1.6 2002/07/11 21:14:28 augustss Exp $ */

/*
 * Definitions for the FTDI USB Single Port Serial Converter -
 * known as FTDI_SIO (Serial Input/Output application of the chipset)
 *
 * The device is based on the FTDI FT8U100AX chip. It has a DB25 on one side,
 * USB on the other.
 *
 * Thanks to FTDI (http://www.ftdi.co.uk) for so kindly providing details
 * of the protocol required to talk to the device and ongoing assistance
 * during development.
 *
 * Bill Ryder - bryder@sgi.com of Silicon Graphics, Inc. is the original
 * author of this file.
 */
/* Modified by Lennart Augustsson */

/* Vendor Request Interface */
#define FTDI_SIO_RESET 		0 /* Reset the port */
#define FTDI_SIO_MODEM_CTRL 	1 /* Set the modem control register */
#define FTDI_SIO_SET_FLOW_CTRL	2 /* Set flow control register */
#define FTDI_SIO_SET_BAUD_RATE	3 /* Set baud rate */
#define FTDI_SIO_SET_DATA	4 /* Set the data characteristics of the port */
#define FTDI_SIO_GET_STATUS	5 /* Retrieve current value of status reg */
#define FTDI_SIO_SET_EVENT_CHAR	6 /* Set the event character */
#define FTDI_SIO_SET_ERROR_CHAR	7 /* Set the error character */

/* Port Identifier Table */
#define FTDI_PIT_DEFAULT 	0 /* SIOA */
#define FTDI_PIT_SIOA		1 /* SIOA */
#define FTDI_PIT_SIOB		2 /* SIOB */
#define FTDI_PIT_PARALLEL	3 /* Parallel */

enum uftdi_type {
	UFTDI_TYPE_SIO,
	UFTDI_TYPE_8U232AM,
	UFTDI_TYPE_2232H,
	UFTDI_TYPE_232R
};

/*
 * BmRequestType:  0100 0000B
 * bRequest:       FTDI_SIO_RESET
 * wValue:         Control Value
 *                   0 = Reset SIO
 *                   1 = Purge RX buffer
 *                   2 = Purge TX buffer
 * wIndex:         Port
 * wLength:        0
 * Data:           None
 *
 * The Reset SIO command has this effect:
 *
 *    Sets flow control set to 'none'
 *    Event char = 0x0d
 *    Event trigger = disabled
 *    Purge RX buffer
 *    Purge TX buffer
 *    Clear DTR
 *    Clear RTS
 *    baud and data format not reset
 *
 * The Purge RX and TX buffer commands affect nothing except the buffers
 *
 */
/* FTDI_SIO_RESET */
#define FTDI_SIO_RESET_SIO 0
#define FTDI_SIO_RESET_PURGE_RX 1
#define FTDI_SIO_RESET_PURGE_TX 2


/*
 * BmRequestType:  0100 0000B
 * bRequest:       FTDI_SIO_SET_BAUDRATE
 * wValue:         BaudRate value - see below
 * wIndex:         Port
 * wLength:        0
 * Data:           None
 */
/* FTDI_SIO_SET_BAUDRATE */
enum {
	ftdi_sio_b300 = 0,
	ftdi_sio_b600 = 1,
	ftdi_sio_b1200 = 2,
	ftdi_sio_b2400 = 3,
	ftdi_sio_b4800 = 4,
	ftdi_sio_b9600 = 5,
	ftdi_sio_b19200 = 6,
	ftdi_sio_b38400 = 7,
	ftdi_sio_b57600 = 8,
	ftdi_sio_b115200 = 9
};

#define FTDI_8U232AM_FREQ 3000000 /* (48MHz / 16) */
#define FTDI_2232H_FREQ 12000000 /* (120MHz / 10) */

/* Bounds for normal divisors as 4-bit fixed precision ints. */
#define FTDI_8U232AM_MIN_DIV 0x20
#define FTDI_8U232AM_MAX_DIV 0x3fff8

/*
 * BmRequestType:  0100 0000B
 * bRequest:       FTDI_SIO_SET_DATA
 * wValue:         Data characteristics (see below)
 * wIndex:         Port
 * wLength:        0
 * Data:           No
 *
 * Data characteristics
 *
 *   B0..7   Number of data bits
 *   B8..10  Parity
 *           0 = None
 *           1 = Odd
 *           2 = Even
 *           3 = Mark
 *           4 = Space
 *   B11..13 Stop Bits
 *           0 = 1
 *           1 = 1.5
 *           2 = 2
 *   B14..15 Reserved
 *
 */
/* FTDI_SIO_SET_DATA */
#define FTDI_SIO_SET_DATA_BITS(n) (n)
#define FTDI_SIO_SET_DATA_PARITY_NONE (0x0 << 8)
#define FTDI_SIO_SET_DATA_PARITY_ODD (0x1 << 8)
#define FTDI_SIO_SET_DATA_PARITY_EVEN (0x2 << 8)
#define FTDI_SIO_SET_DATA_PARITY_MARK (0x3 << 8)
#define FTDI_SIO_SET_DATA_PARITY_SPACE (0x4 << 8)
#define FTDI_SIO_SET_DATA_STOP_BITS_1 (0x0 << 11)
#define FTDI_SIO_SET_DATA_STOP_BITS_15 (0x1 << 11)
#define FTDI_SIO_SET_DATA_STOP_BITS_2 (0x2 << 11)
#define FTDI_SIO_SET_BREAK (0x1 << 14)


/*
 * BmRequestType:   0100 0000B
 * bRequest:        FTDI_SIO_MODEM_CTRL
 * wValue:          ControlValue (see below)
 * wIndex:          Port
 * wLength:         0
 * Data:            None
 *
 * NOTE: If the device is in RTS/CTS flow control, the RTS set by this
 * command will be IGNORED without an error being returned
 * Also - you can not set DTR and RTS with one control message
 *
 * ControlValue
 * B0    DTR state
 *          0 = reset
 *          1 = set
 * B1    RTS state
 *          0 = reset
 *          1 = set
 * B2..7 Reserved
 * B8    DTR state enable
 *          0 = ignore
 *          1 = use DTR state
 * B9    RTS state enable
 *          0 = ignore
 *          1 = use RTS state
 * B10..15 Reserved
 */
/* FTDI_SIO_MODEM_CTRL */
#define FTDI_SIO_SET_DTR_MASK 0x1
#define FTDI_SIO_SET_DTR_HIGH (1 | ( FTDI_SIO_SET_DTR_MASK  << 8))
#define FTDI_SIO_SET_DTR_LOW  (0 | ( FTDI_SIO_SET_DTR_MASK  << 8))
#define FTDI_SIO_SET_RTS_MASK 0x2
#define FTDI_SIO_SET_RTS_HIGH (2 | ( FTDI_SIO_SET_RTS_MASK << 8))
#define FTDI_SIO_SET_RTS_LOW (0 | ( FTDI_SIO_SET_RTS_MASK << 8))


/*
 *   BmRequestType:  0100 0000b
 *   bRequest:       FTDI_SIO_SET_FLOW_CTRL
 *   wValue:         Xoff/Xon
 *   wIndex:         Protocol/Port - hIndex is protocol / lIndex is port
 *   wLength:        0
 *   Data:           None
 *
 * hIndex protocol is:
 *   B0 Output handshaking using RTS/CTS
 *       0 = disabled
 *       1 = enabled
 *   B1 Output handshaking using DTR/DSR
 *       0 = disabled
 *       1 = enabled
 *   B2 Xon/Xoff handshaking
 *       0 = disabled
 *       1 = enabled
 *
 * A value of zero in the hIndex field disables handshaking
 *
 * If Xon/Xoff handshaking is specified, the hValue field should contain the
 * XOFF character and the lValue field contains the XON character.
 */
/* FTDI_SIO_SET_FLOW_CTRL */
#define FTDI_SIO_DISABLE_FLOW_CTRL 0x0
#define FTDI_SIO_RTS_CTS_HS 0x1
#define FTDI_SIO_DTR_DSR_HS 0x2
#define FTDI_SIO_XON_XOFF_HS 0x4


/*
 *  BmRequestType:   0100 0000b
 *  bRequest:        FTDI_SIO_SET_EVENT_CHAR
 *  wValue:          Event Char
 *  wIndex:          Port
 *  wLength:         0
 *  Data:            None
 *
 * wValue:
 *   B0..7   Event Character
 *   B8      Event Character Processing
 *             0 = disabled
 *             1 = enabled
 *   B9..15  Reserved
 *
 * FTDI_SIO_SET_EVENT_CHAR
 *
 * Set the special event character for the specified communications port.
 * If the device sees this character it will immediately return the
 * data read so far - rather than wait 40ms or until 62 bytes are read
 * which is what normally happens.
 */



/*
 *  BmRequestType:  0100 0000b
 *  bRequest:       FTDI_SIO_SET_ERROR_CHAR
 *  wValue:         Error Char
 *  wIndex:         Port
 *  wLength:        0
 *  Data:           None
 *
 *  Error Char
 *  B0..7  Error Character
 *  B8     Error Character Processing
 *           0 = disabled
 *           1 = enabled
 *  B9..15 Reserved
 *
 *
 * FTDI_SIO_SET_ERROR_CHAR
 * Set the parity error replacement character for the specified communications
 * port.
 */


/*
 *   BmRequestType:   1100 0000b
 *   bRequest:        FTDI_SIO_GET_STATUS
 *   wValue:          zero
 *   wIndex:          Port
 *   wLength:         1		(2 for newer devices like the FT232R)
 *   Data:            Status
 *
 * One byte of data is returned
 * B0..3 0
 * B4    CTS
 *         0 = inactive
 *         1 = active
 * B5    DSR
 *         0 = inactive
 *         1 = active
 * B6    Ring Indicator (RI)
 *         0 = inactive
 *         1 = active
 * B7    Receive Line Signal Detect (RLSD)
 *         0 = inactive
 *         1 = active
 *
 * FTDI_SIO_GET_STATUS
 * Retrieve the current value of the modem status register.
 */
#define FTDI_SIO_CTS_MASK 0x10
#define FTDI_SIO_DSR_MASK 0x20
#define FTDI_SIO_RI_MASK  0x40
#define FTDI_SIO_RLSD_MASK 0x80



/*
 *
 * DATA FORMAT
 *
 * IN Endpoint
 *
 * The device reserves the first two bytes of data on this endpoint to contain
 * the current values of the modem and line status registers. In the absence of
 * data, the device generates a message consisting of these two status bytes
 * every 40 ms.
 *
 * Byte 0: Modem Status
 *   NOTE: 4 upper bits have same layout as the MSR register in a 16550
 *
 * Offset	Description
 * B0..3	Port
 * B4		Clear to Send (CTS)
 * B5		Data Set Ready (DSR)
 * B6		Ring Indicator (RI)
 * B7		Receive Line Signal Detect (RLSD)
 *
 * Byte 1: Line Status
 *   NOTE: same layout as the LSR register in a 16550
 *
 * Offset	Description
 * B0	Data Ready (DR)
 * B1	Overrun Error (OE)
 * B2	Parity Error (PE)
 * B3	Framing Error (FE)
 * B4	Break Interrupt (BI)
 * B5	Transmitter Holding Register (THRE)
 * B6	Transmitter Empty (TEMT)
 * B7	Error in RCVR FIFO
 *
 *
 * OUT Endpoint
 *
 * This device reserves the first bytes of data on this endpoint contain the
 * length and port identifier of the message. For the FTDI USB Serial converter
 * the port identifier is always 1.
 *
 * Byte 0: Port & length
 *
 * Offset	Description
 * B0..1	Port
 * B2..7	Length of message - (not including Byte 0)
 *
 */
#define FTDI_PORT_MASK 0x0f
#define FTDI_MSR_MASK 0xf0
#define FTDI_GET_MSR(p) (((p)[0]) & FTDI_MSR_MASK)
#define FTDI_GET_LSR(p) ((p)[1])
#define FTDI_LSR_MASK (~0x60) /* interesting bits */
#define FTDI_OUT_TAG(len, port) (((len) << 2) | (port))
