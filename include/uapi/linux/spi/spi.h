/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_SPI_H
#define _UAPI_SPI_H

#include <linux/const.h>

#define	SPI_CPHA		_BITUL(0)	/* clock phase */
#define	SPI_CPOL		_BITUL(1)	/* clock polarity */

#define	SPI_MODE_0		(0|0)		/* (original MicroWire) */
#define	SPI_MODE_1		(0|SPI_CPHA)
#define	SPI_MODE_2		(SPI_CPOL|0)
#define	SPI_MODE_3		(SPI_CPOL|SPI_CPHA)
#define	SPI_MODE_X_MASK		(SPI_CPOL|SPI_CPHA)

#define	SPI_CS_HIGH		_BITUL(2)	/* chipselect active high? */
#define	SPI_LSB_FIRST		_BITUL(3)	/* per-word bits-on-wire */
#define	SPI_3WIRE		_BITUL(4)	/* SI/SO signals shared */
#define	SPI_LOOP		_BITUL(5)	/* loopback mode */
#define	SPI_NO_CS		_BITUL(6)	/* 1 dev/bus, no chipselect */
#define	SPI_READY		_BITUL(7)	/* slave pulls low to pause */
#define	SPI_TX_DUAL		_BITUL(8)	/* transmit with 2 wires */
#define	SPI_TX_QUAD		_BITUL(9)	/* transmit with 4 wires */
#define	SPI_RX_DUAL		_BITUL(10)	/* receive with 2 wires */
#define	SPI_RX_QUAD		_BITUL(11)	/* receive with 4 wires */
#define	SPI_CS_WORD		_BITUL(12)	/* toggle cs after each word */
#define	SPI_TX_OCTAL		_BITUL(13)	/* transmit with 8 wires */
#define	SPI_RX_OCTAL		_BITUL(14)	/* receive with 8 wires */
#define	SPI_3WIRE_HIZ		_BITUL(15)	/* high impedance turnaround */

#endif /* _UAPI_SPI_H */
