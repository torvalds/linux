/*
 * Blackfin Infra-red Driver
 *
 * Copyright 2006-2008 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 *
 */

#include <linux/serial.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#define SIR_UART_GET_CHAR(port)   bfin_read16((port)->membase + OFFSET_RBR)
#define SIR_UART_GET_DLL(port)    bfin_read16((port)->membase + OFFSET_DLL)
#define SIR_UART_GET_IER(port)    bfin_read16((port)->membase + OFFSET_IER_SET)
#define SIR_UART_GET_DLH(port)    bfin_read16((port)->membase + OFFSET_DLH)
#define SIR_UART_GET_LCR(port)    bfin_read16((port)->membase + OFFSET_LCR)
#define SIR_UART_GET_LSR(port)    bfin_read16((port)->membase + OFFSET_LSR)
#define SIR_UART_GET_GCTL(port)   bfin_read16((port)->membase + OFFSET_GCTL)

#define SIR_UART_PUT_CHAR(port, v) bfin_write16(((port)->membase + OFFSET_THR), v)
#define SIR_UART_PUT_DLL(port, v)  bfin_write16(((port)->membase + OFFSET_DLL), v)
#define SIR_UART_SET_IER(port, v)  bfin_write16(((port)->membase + OFFSET_IER_SET), v)
#define SIR_UART_CLEAR_IER(port, v)  bfin_write16(((port)->membase + OFFSET_IER_CLEAR), v)
#define SIR_UART_PUT_DLH(port, v)  bfin_write16(((port)->membase + OFFSET_DLH), v)
#define SIR_UART_PUT_LSR(port, v)  bfin_write16(((port)->membase + OFFSET_LSR), v)
#define SIR_UART_PUT_LCR(port, v)  bfin_write16(((port)->membase + OFFSET_LCR), v)
#define SIR_UART_CLEAR_LSR(port)  bfin_write16(((port)->membase + OFFSET_LSR), -1)
#define SIR_UART_PUT_GCTL(port, v) bfin_write16(((port)->membase + OFFSET_GCTL), v)

#ifdef CONFIG_SIR_BFIN_DMA
struct dma_rx_buf {
	char *buf;
	int head;
	int tail;
	};
#endif /* CONFIG_SIR_BFIN_DMA */

struct bfin_sir_port {
	unsigned char __iomem   *membase;
	unsigned int            irq;
	unsigned int            lsr;
	unsigned long           clk;
	struct net_device       *dev;
#ifdef CONFIG_SIR_BFIN_DMA
	int                     tx_done;
	struct dma_rx_buf       rx_dma_buf;
	struct timer_list       rx_dma_timer;
	int                     rx_dma_nrows;
#endif /* CONFIG_SIR_BFIN_DMA */
	unsigned int            tx_dma_channel;
	unsigned int            rx_dma_channel;
};

struct bfin_sir_port sir_ports[BFIN_UART_NR_PORTS];

struct bfin_sir_port_res {
	unsigned long   base_addr;
	int             irq;
	unsigned int    rx_dma_channel;
	unsigned int    tx_dma_channel;
};

struct bfin_sir_port_res bfin_sir_port_resource[] = {
#ifdef CONFIG_BFIN_SIR0
	{
	0xFFC00400,
	IRQ_UART0_RX,
	CH_UART0_RX,
	CH_UART0_TX,
	},
#endif
#ifdef CONFIG_BFIN_SIR1
	{
	0xFFC02000,
	IRQ_UART1_RX,
	CH_UART1_RX,
	CH_UART1_TX,
	},
#endif
#ifdef CONFIG_BFIN_SIR2
	{
	0xFFC02100,
	IRQ_UART2_RX,
	CH_UART2_RX,
	CH_UART2_TX,
	},
#endif
#ifdef CONFIG_BFIN_SIR3
	{
	0xFFC03100,
	IRQ_UART3_RX,
	CH_UART3_RX,
	CH_UART3_TX,
	},
#endif
};

int nr_sirs = ARRAY_SIZE(bfin_sir_port_resource);

struct bfin_sir_self {
	struct bfin_sir_port    *sir_port;
	spinlock_t              lock;
	unsigned int            open;
	int                     speed;
	int                     newspeed;

	struct sk_buff          *txskb;
	struct sk_buff          *rxskb;
	struct net_device_stats stats;
	struct device           *dev;
	struct irlap_cb         *irlap;
	struct qos_info         qos;

	iobuff_t                tx_buff;
	iobuff_t                rx_buff;

	struct work_struct      work;
	int                     mtt;
};

#define DRIVER_NAME "bfin_sir"

static void bfin_sir_hw_init(void)
{
#ifdef CONFIG_BFIN_SIR0
	peripheral_request(P_UART0_TX, DRIVER_NAME);
	peripheral_request(P_UART0_RX, DRIVER_NAME);
#endif

#ifdef CONFIG_BFIN_SIR1
	peripheral_request(P_UART1_TX, DRIVER_NAME);
	peripheral_request(P_UART1_RX, DRIVER_NAME);
#endif

#ifdef CONFIG_BFIN_SIR2
	peripheral_request(P_UART2_TX, DRIVER_NAME);
	peripheral_request(P_UART2_RX, DRIVER_NAME);
#endif

#ifdef CONFIG_BFIN_SIR3
	peripheral_request(P_UART3_TX, DRIVER_NAME);
	peripheral_request(P_UART3_RX, DRIVER_NAME);
#endif
	SSYNC();
}
