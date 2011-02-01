#ifndef __LINUX_SERIAL_SCI_H
#define __LINUX_SERIAL_SCI_H

#include <linux/serial_core.h>
#include <linux/sh_dma.h>

/*
 * Generic header for SuperH SCI(F) (used by sh/sh64/h8300 and related parts)
 */

enum {
	SCBRR_ALGO_1,		/* ((clk + 16 * bps) / (16 * bps) - 1) */
	SCBRR_ALGO_2,		/* ((clk + 16 * bps) / (32 * bps) - 1) */
	SCBRR_ALGO_3,		/* (((clk * 2) + 16 * bps) / (16 * bps) - 1) */
	SCBRR_ALGO_4,		/* (((clk * 2) + 16 * bps) / (32 * bps) - 1) */
	SCBRR_ALGO_5,		/* (((clk * 1000 / 32) / bps) - 1) */
};

#define SCSCR_TIE	(1 << 7)
#define SCSCR_RIE	(1 << 6)
#define SCSCR_TE	(1 << 5)
#define SCSCR_RE	(1 << 4)
#define SCSCR_REIE	(1 << 3)	/* not supported by all parts */
#define SCSCR_TOIE	(1 << 2)	/* not supported by all parts */
#define SCSCR_CKE1	(1 << 1)
#define SCSCR_CKE0	(1 << 0)

/* Offsets into the sci_port->irqs array */
enum {
	SCIx_ERI_IRQ,
	SCIx_RXI_IRQ,
	SCIx_TXI_IRQ,
	SCIx_BRI_IRQ,
	SCIx_NR_IRQS,
};

struct device;

/*
 * Platform device specific platform_data struct
 */
struct plat_sci_port {
	void __iomem	*membase;		/* io cookie */
	unsigned long	mapbase;		/* resource base */
	unsigned int	irqs[SCIx_NR_IRQS];	/* ERI, RXI, TXI, BRI */
	unsigned int	type;			/* SCI / SCIF / IRDA */
	upf_t		flags;			/* UPF_* flags */
	char		*clk;			/* clock string */

	unsigned int	scbrr_algo_id;		/* SCBRR calculation algo */
	unsigned int	scscr;			/* SCSCR initialization */

	struct device	*dma_dev;

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	unsigned int dma_slave_tx;
	unsigned int dma_slave_rx;
#endif
};

#endif /* __LINUX_SERIAL_SCI_H */
