/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  linux/include/asm-arm/hardware/serial_amba.h
 *
 *  Internal header file for AMBA serial ports
 *
 *  Copyright (C) ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 */
#ifndef ASM_ARM_HARDWARE_SERIAL_AMBA_H
#define ASM_ARM_HARDWARE_SERIAL_AMBA_H

#ifndef __ASSEMBLY__
#include <linux/bitfield.h>
#include <linux/bits.h>
#endif

#include <linux/types.h>

/* -------------------------------------------------------------------------------
 *  From AMBA UART (PL010) Block Specification
 * -------------------------------------------------------------------------------
 *  UART Register Offsets.
 */
#define UART01x_DR		0x00	/* Data read or written from the interface. */
#define UART01x_RSR		0x04	/* Receive status register (Read). */
#define UART01x_ECR		0x04	/* Error clear register (Write). */
#define UART010_LCRH		0x08	/* Line control register, high byte. */
#define ST_UART011_DMAWM	0x08    /* DMA watermark configure register. */
#define UART010_LCRM		0x0C	/* Line control register, middle byte. */
#define ST_UART011_TIMEOUT	0x0C    /* Timeout period register. */
#define UART010_LCRL		0x10	/* Line control register, low byte. */
#define UART010_CR		0x14	/* Control register. */
#define UART01x_FR		0x18	/* Flag register (Read only). */
#define UART010_IIR		0x1C	/* Interrupt identification register (Read). */
#define UART010_ICR		0x1C	/* Interrupt clear register (Write). */
#define ST_UART011_LCRH_RX	0x1C    /* Rx line control register. */
#define UART01x_ILPR		0x20	/* IrDA low power counter register. */
#define UART011_IBRD		0x24	/* Integer baud rate divisor register. */
#define UART011_FBRD		0x28	/* Fractional baud rate divisor register. */
#define UART011_LCRH		0x2c	/* Line control register. */
#define ST_UART011_LCRH_TX	0x2c    /* Tx Line control register. */
#define UART011_CR		0x30	/* Control register. */
#define UART011_IFLS		0x34	/* Interrupt fifo level select. */
#define UART011_IMSC		0x38	/* Interrupt mask. */
#define UART011_RIS		0x3c	/* Raw interrupt status. */
#define UART011_MIS		0x40	/* Masked interrupt status. */
#define UART011_ICR		0x44	/* Interrupt clear register. */
#define UART011_DMACR		0x48	/* DMA control register. */
#define ST_UART011_XFCR		0x50	/* XON/XOFF control register. */
#define ST_UART011_XON1		0x54	/* XON1 register. */
#define ST_UART011_XON2		0x58	/* XON2 register. */
#define ST_UART011_XOFF1	0x5C	/* XON1 register. */
#define ST_UART011_XOFF2	0x60	/* XON2 register. */
#define ST_UART011_ITCR		0x80	/* Integration test control register. */
#define ST_UART011_ITIP		0x84	/* Integration test input register. */
#define ST_UART011_ABCR		0x100	/* Autobaud control register. */
#define ST_UART011_ABIMSC	0x15C	/* Autobaud interrupt mask/clear register. */

/*
 * ZTE UART register offsets.  This UART has a radically different address
 * allocation from the ARM and ST variants, so we list all registers here.
 * We assume unlisted registers do not exist.
 */
#define ZX_UART011_DR		0x04
#define ZX_UART011_FR		0x14
#define ZX_UART011_IBRD		0x24
#define ZX_UART011_FBRD		0x28
#define ZX_UART011_LCRH		0x30
#define ZX_UART011_CR		0x34
#define ZX_UART011_IFLS		0x38
#define ZX_UART011_IMSC		0x40
#define ZX_UART011_RIS		0x44
#define ZX_UART011_MIS		0x48
#define ZX_UART011_ICR		0x4c
#define ZX_UART011_DMACR	0x50

#define UART011_DR_OE		BIT(11)
#define UART011_DR_BE		BIT(10)
#define UART011_DR_PE		BIT(9)
#define UART011_DR_FE		BIT(8)

#define UART01x_RSR_OE		BIT(3)
#define UART01x_RSR_BE		BIT(2)
#define UART01x_RSR_PE		BIT(1)
#define UART01x_RSR_FE		BIT(0)

#define UART011_FR_RI		BIT(8)
#define UART011_FR_TXFE		BIT(7)
#define UART011_FR_RXFF		BIT(6)
#define UART01x_FR_TXFF		(1 << 5)	/* used in ASM */
#define UART01x_FR_RXFE		BIT(4)
#define UART01x_FR_BUSY		(1 << 3)	/* used in ASM */
#define UART01x_FR_DCD		BIT(2)
#define UART01x_FR_DSR		BIT(1)
#define UART01x_FR_CTS		BIT(0)
#define UART01x_FR_TMSK		(UART01x_FR_TXFF + UART01x_FR_BUSY)

/*
 * Some bits of Flag Register on ZTE device have different position from
 * standard ones.
 */
#define ZX_UART01x_FR_BUSY	BIT(8)
#define ZX_UART01x_FR_DSR	BIT(3)
#define ZX_UART01x_FR_CTS	BIT(1)
#define ZX_UART011_FR_RI	BIT(0)

#define UART011_CR_CTSEN	BIT(15)	/* CTS hardware flow control */
#define UART011_CR_RTSEN	BIT(14)	/* RTS hardware flow control */
#define UART011_CR_OUT2		BIT(13)	/* OUT2 */
#define UART011_CR_OUT1		BIT(12)	/* OUT1 */
#define UART011_CR_RTS		BIT(11)	/* RTS */
#define UART011_CR_DTR		BIT(10)	/* DTR */
#define UART011_CR_RXE		BIT(9)	/* receive enable */
#define UART011_CR_TXE		BIT(8)	/* transmit enable */
#define UART011_CR_LBE		BIT(7)	/* loopback enable */
#define UART010_CR_RTIE		BIT(6)
#define UART010_CR_TIE		BIT(5)
#define UART010_CR_RIE		BIT(4)
#define UART010_CR_MSIE		BIT(3)
#define ST_UART011_CR_OVSFACT	BIT(3)	/* Oversampling factor */
#define UART01x_CR_IIRLP	BIT(2)	/* SIR low power mode */
#define UART01x_CR_SIREN	BIT(1)	/* SIR enable */
#define UART01x_CR_UARTEN	BIT(0)	/* UART enable */

#define UART011_LCRH_SPS	BIT(7)
#define UART01x_LCRH_WLEN_8	0x60
#define UART01x_LCRH_WLEN_7	0x40
#define UART01x_LCRH_WLEN_6	0x20
#define UART01x_LCRH_WLEN_5	0x00
#define UART01x_LCRH_FEN	BIT(4)
#define UART01x_LCRH_STP2	BIT(3)
#define UART01x_LCRH_EPS	BIT(2)
#define UART01x_LCRH_PEN	BIT(1)
#define UART01x_LCRH_BRK	BIT(0)

#define ST_UART011_DMAWM_RX	GENMASK(5, 3)
#define ST_UART011_DMAWM_RX_1	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 0)
#define ST_UART011_DMAWM_RX_2	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 1)
#define ST_UART011_DMAWM_RX_4	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 2)
#define ST_UART011_DMAWM_RX_8	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 3)
#define ST_UART011_DMAWM_RX_16	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 4)
#define ST_UART011_DMAWM_RX_32	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 5)
#define ST_UART011_DMAWM_RX_48	FIELD_PREP_CONST(ST_UART011_DMAWM_RX, 6)
#define ST_UART011_DMAWM_TX	GENMASK(2, 0)
#define ST_UART011_DMAWM_TX_1	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 0)
#define ST_UART011_DMAWM_TX_2	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 1)
#define ST_UART011_DMAWM_TX_4	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 2)
#define ST_UART011_DMAWM_TX_8	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 3)
#define ST_UART011_DMAWM_TX_16	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 4)
#define ST_UART011_DMAWM_TX_32	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 5)
#define ST_UART011_DMAWM_TX_48	FIELD_PREP_CONST(ST_UART011_DMAWM_TX, 6)

#define UART010_IIR_RTIS	BIT(3)
#define UART010_IIR_TIS		BIT(2)
#define UART010_IIR_RIS		BIT(1)
#define UART010_IIR_MIS		BIT(0)

#define UART011_IFLS_RXIFLSEL	GENMASK(5, 3)
#define UART011_IFLS_RX1_8	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 0)
#define UART011_IFLS_RX2_8	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 1)
#define UART011_IFLS_RX4_8	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 2)
#define UART011_IFLS_RX6_8	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 3)
#define UART011_IFLS_RX7_8	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 4)
#define UART011_IFLS_TXIFLSEL	GENMASK(2, 0)
#define UART011_IFLS_TX1_8	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 0)
#define UART011_IFLS_TX2_8	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 1)
#define UART011_IFLS_TX4_8	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 2)
#define UART011_IFLS_TX6_8	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 3)
#define UART011_IFLS_TX7_8	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 4)
/* special values for ST vendor with deeper fifo */
#define UART011_IFLS_RX_HALF	FIELD_PREP_CONST(UART011_IFLS_RXIFLSEL, 5)
#define UART011_IFLS_TX_HALF	FIELD_PREP_CONST(UART011_IFLS_TXIFLSEL, 5)

#define UART011_OEIM		BIT(10)	/* overrun error interrupt mask */
#define UART011_BEIM		BIT(9)	/* break error interrupt mask */
#define UART011_PEIM		BIT(8)	/* parity error interrupt mask */
#define UART011_FEIM		BIT(7)	/* framing error interrupt mask */
#define UART011_RTIM		BIT(6)	/* receive timeout interrupt mask */
#define UART011_TXIM		BIT(5)	/* transmit interrupt mask */
#define UART011_RXIM		BIT(4)	/* receive interrupt mask */
#define UART011_DSRMIM		BIT(3)	/* DSR interrupt mask */
#define UART011_DCDMIM		BIT(2)	/* DCD interrupt mask */
#define UART011_CTSMIM		BIT(1)	/* CTS interrupt mask */
#define UART011_RIMIM		BIT(0)	/* RI interrupt mask */

#define UART011_OEIS		BIT(10)	/* overrun error interrupt status */
#define UART011_BEIS		BIT(9)	/* break error interrupt status */
#define UART011_PEIS		BIT(8)	/* parity error interrupt status */
#define UART011_FEIS		BIT(7)	/* framing error interrupt status */
#define UART011_RTIS		BIT(6)	/* receive timeout interrupt status */
#define UART011_TXIS		BIT(5)	/* transmit interrupt status */
#define UART011_RXIS		BIT(4)	/* receive interrupt status */
#define UART011_DSRMIS		BIT(3)	/* DSR interrupt status */
#define UART011_DCDMIS		BIT(2)	/* DCD interrupt status */
#define UART011_CTSMIS		BIT(1)	/* CTS interrupt status */
#define UART011_RIMIS		BIT(0)	/* RI interrupt status */

#define UART011_OEIC		BIT(10)	/* overrun error interrupt clear */
#define UART011_BEIC		BIT(9)	/* break error interrupt clear */
#define UART011_PEIC		BIT(8)	/* parity error interrupt clear */
#define UART011_FEIC		BIT(7)	/* framing error interrupt clear */
#define UART011_RTIC		BIT(6)	/* receive timeout interrupt clear */
#define UART011_TXIC		BIT(5)	/* transmit interrupt clear */
#define UART011_RXIC		BIT(4)	/* receive interrupt clear */
#define UART011_DSRMIC		BIT(3)	/* DSR interrupt clear */
#define UART011_DCDMIC		BIT(2)	/* DCD interrupt clear */
#define UART011_CTSMIC		BIT(1)	/* CTS interrupt clear */
#define UART011_RIMIC		BIT(0)	/* RI interrupt clear */

#define UART011_DMAONERR	BIT(2)	/* disable dma on error */
#define UART011_TXDMAE		BIT(1)	/* enable transmit dma */
#define UART011_RXDMAE		BIT(0)	/* enable receive dma */

#define UART01x_RSR_ANY		(UART01x_RSR_OE | UART01x_RSR_BE | UART01x_RSR_PE | UART01x_RSR_FE)
#define UART01x_FR_MODEM_ANY	(UART01x_FR_DCD | UART01x_FR_DSR | UART01x_FR_CTS)

#ifndef __ASSEMBLY__
struct amba_device; /* in uncompress this is included but amba/bus.h is not */
struct amba_pl010_data {
	void (*set_mctrl)(struct amba_device *dev, void __iomem *base, unsigned int mctrl);
};

struct dma_chan;
struct amba_pl011_data {
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
	void *dma_rx_param;
	void *dma_tx_param;
	bool dma_rx_poll_enable;
	unsigned int dma_rx_poll_rate;
	unsigned int dma_rx_poll_timeout;
	void (*init)(void);
	void (*exit)(void);
};
#endif

#endif
