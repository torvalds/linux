#ifndef __LINUX_SERIAL_SCI_H
#define __LINUX_SERIAL_SCI_H

#include <linux/serial_core.h>
#include <linux/sh_dma.h>

/*
 * Generic header for SuperH (H)SCI(F) (used by sh/sh64 and related parts)
 */

#define SCIx_NOT_SUPPORTED	(-1)

/* SCSMR (Serial Mode Register) */
#define SCSMR_CHR	(1 << 6)	/* 7-bit Character Length */
#define SCSMR_PE	(1 << 5)	/* Parity Enable */
#define SCSMR_ODD	(1 << 4)	/* Odd Parity */
#define SCSMR_STOP	(1 << 3)	/* Stop Bit Length */
#define SCSMR_CKS	0x0003		/* Clock Select */

/* Serial Control Register (@ = not supported by all parts) */
#define SCSCR_TIE	(1 << 7)	/* Transmit Interrupt Enable */
#define SCSCR_RIE	(1 << 6)	/* Receive Interrupt Enable */
#define SCSCR_TE	(1 << 5)	/* Transmit Enable */
#define SCSCR_RE	(1 << 4)	/* Receive Enable */
#define SCSCR_REIE	(1 << 3)	/* Receive Error Interrupt Enable @ */
#define SCSCR_TOIE	(1 << 2)	/* Timeout Interrupt Enable @ */
#define SCSCR_CKE1	(1 << 1)	/* Clock Enable 1 */
#define SCSCR_CKE0	(1 << 0)	/* Clock Enable 0 */
/* SCIFA/SCIFB only */
#define SCSCR_TDRQE	(1 << 15)	/* Tx Data Transfer Request Enable */
#define SCSCR_RDRQE	(1 << 14)	/* Rx Data Transfer Request Enable */

/* SCxSR (Serial Status Register) on SCI */
#define SCI_TDRE  0x80			/* Transmit Data Register Empty */
#define SCI_RDRF  0x40			/* Receive Data Register Full */
#define SCI_ORER  0x20			/* Overrun Error */
#define SCI_FER   0x10			/* Framing Error */
#define SCI_PER   0x08			/* Parity Error */
#define SCI_TEND  0x04			/* Transmit End */

#define SCI_DEFAULT_ERROR_MASK (SCI_PER | SCI_FER)

/* SCxSR (Serial Status Register) on SCIF, HSCIF */
#define SCIF_ER    0x0080		/* Receive Error */
#define SCIF_TEND  0x0040		/* Transmission End */
#define SCIF_TDFE  0x0020		/* Transmit FIFO Data Empty */
#define SCIF_BRK   0x0010		/* Break Detect */
#define SCIF_FER   0x0008		/* Framing Error */
#define SCIF_PER   0x0004		/* Parity Error */
#define SCIF_RDF   0x0002		/* Receive FIFO Data Full */
#define SCIF_DR    0x0001		/* Receive Data Ready */

#define SCIF_DEFAULT_ERROR_MASK (SCIF_PER | SCIF_FER | SCIF_ER | SCIF_BRK)

/* SCFCR (FIFO Control Register) */
#define SCFCR_LOOP	(1 << 0)	/* Loopback Test */

/* SCSPTR (Serial Port Register), optional */
#define SCSPTR_RTSIO	(1 << 7)	/* Serial Port RTS Pin Input/Output */
#define SCSPTR_CTSIO	(1 << 5)	/* Serial Port CTS Pin Input/Output */
#define SCSPTR_SPB2IO	(1 << 1)	/* Serial Port Break Input/Output */
#define SCSPTR_SPB2DT	(1 << 0)	/* Serial Port Break Data */

/* HSSRR HSCIF */
#define HSCIF_SRE	0x8000		/* Sampling Rate Register Enable */

enum {
	SCIx_PROBE_REGTYPE,

	SCIx_SCI_REGTYPE,
	SCIx_IRDA_REGTYPE,
	SCIx_SCIFA_REGTYPE,
	SCIx_SCIFB_REGTYPE,
	SCIx_SH2_SCIF_FIFODATA_REGTYPE,
	SCIx_SH3_SCIF_REGTYPE,
	SCIx_SH4_SCIF_REGTYPE,
	SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE,
	SCIx_SH4_SCIF_FIFODATA_REGTYPE,
	SCIx_SH7705_SCIF_REGTYPE,
	SCIx_HSCIF_REGTYPE,

	SCIx_NR_REGTYPES,
};

/*
 * SCI register subset common for all port types.
 * Not all registers will exist on all parts.
 */
enum {
	SCSMR,				/* Serial Mode Register */
	SCBRR,				/* Bit Rate Register */
	SCSCR,				/* Serial Control Register */
	SCxSR,				/* Serial Status Register */
	SCFCR,				/* FIFO Control Register */
	SCFDR,				/* FIFO Data Count Register */
	SCxTDR,				/* Transmit (FIFO) Data Register */
	SCxRDR,				/* Receive (FIFO) Data Register */
	SCLSR,				/* Line Status Register */
	SCTFDR,				/* Transmit FIFO Data Count Register */
	SCRFDR,				/* Receive FIFO Data Count Register */
	SCSPTR,				/* Serial Port Register */
	HSSRR,				/* Sampling Rate Register */

	SCIx_NR_REGS,
};

struct device;

struct plat_sci_port_ops {
	void (*init_pins)(struct uart_port *, unsigned int cflag);
};

/*
 * Port-specific capabilities
 */
#define SCIx_HAVE_RTSCTS	(1 << 0)

/*
 * Platform device specific platform_data struct
 */
struct plat_sci_port {
	unsigned int	type;			/* SCI / SCIF / IRDA / HSCIF */
	upf_t		flags;			/* UPF_* flags */
	unsigned long	capabilities;		/* Port features/capabilities */

	unsigned int	sampling_rate;
	unsigned int	scscr;			/* SCSCR initialization */

	/*
	 * Platform overrides if necessary, defaults otherwise.
	 */
	int		port_reg;
	unsigned char	regshift;
	unsigned char	regtype;

	struct plat_sci_port_ops	*ops;

	unsigned int	dma_slave_tx;
	unsigned int	dma_slave_rx;
};

#endif /* __LINUX_SERIAL_SCI_H */
