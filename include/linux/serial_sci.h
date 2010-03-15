#ifndef __LINUX_SERIAL_SCI_H
#define __LINUX_SERIAL_SCI_H

#include <linux/serial_core.h>
#include <asm/dmaengine.h>

/*
 * Generic header for SuperH SCI(F) (used by sh/sh64/h8300 and related parts)
 */

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
	struct device	*dma_dev;
	enum sh_dmae_slave_chan_id dma_slave_tx;
	enum sh_dmae_slave_chan_id dma_slave_rx;
};

#endif /* __LINUX_SERIAL_SCI_H */
