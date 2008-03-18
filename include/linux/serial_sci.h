#ifndef __LINUX_SERIAL_SCI_H
#define __LINUX_SERIAL_SCI_H

#include <linux/serial_core.h>

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

/*
 * Platform device specific platform_data struct
 */
struct plat_sci_port {
	void __iomem	*membase;		/* io cookie */
	unsigned long	mapbase;		/* resource base */
	unsigned int	irqs[SCIx_NR_IRQS];	/* ERI, RXI, TXI, BRI */
	unsigned int	type;			/* SCI / SCIF / IRDA */
	upf_t		flags;			/* UPF_* flags */
};

int early_sci_setup(struct uart_port *port);

#endif /* __LINUX_SERIAL_SCI_H */
