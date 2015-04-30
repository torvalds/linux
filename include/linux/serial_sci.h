#ifndef __LINUX_SERIAL_SCI_H
#define __LINUX_SERIAL_SCI_H

#include <linux/serial_core.h>
#include <linux/sh_dma.h>

/*
 * Generic header for SuperH (H)SCI(F) (used by sh/sh64 and related parts)
 */

#define SCIx_NOT_SUPPORTED	(-1)

/* Serial Control Register (@ = not supported by all parts) */
#define SCSCR_TIE	(1 << 7)	/* Transmit Interrupt Enable */
#define SCSCR_RIE	(1 << 6)	/* Receive Interrupt Enable */
#define SCSCR_TE	(1 << 5)	/* Transmit Enable */
#define SCSCR_RE	(1 << 4)	/* Receive Enable */
#define SCSCR_REIE	(1 << 3)	/* Receive Error Interrupt Enable @ */
#define SCSCR_TOIE	(1 << 2)	/* Timeout Interrupt Enable @ */
#define SCSCR_CKE1	(1 << 1)	/* Clock Enable 1 */
#define SCSCR_CKE0	(1 << 0)	/* Clock Enable 0 */


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
