/*
 * file:        include/asm-blackfin/mach-bf548/bfin_serial_5xx.h
 * based on:
 * author:
 *
 * created:
 * description:
 *	blackfin serial driver head file
 * rev:
 *
 * modified:
 *
 *
 * bugs:         enter bugs at http://blackfin.uclinux.org/
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2, or (at your option)
 * any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; see the file copying.
 * if not, write to the free software foundation,
 * 59 temple place - suite 330, boston, ma 02111-1307, usa.
 */

#include <linux/serial.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#define UART_GET_CHAR(uart)     bfin_read16(((uart)->port.membase + OFFSET_RBR))
#define UART_GET_DLL(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLL))
#define UART_GET_DLH(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLH))
#define UART_GET_IER(uart)      bfin_read16(((uart)->port.membase + OFFSET_IER_SET))
#define UART_GET_LCR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LCR))
#define UART_GET_LSR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LSR))
#define UART_GET_GCTL(uart)     bfin_read16(((uart)->port.membase + OFFSET_GCTL))
#define UART_GET_MSR(uart)      bfin_read16(((uart)->port.membase + OFFSET_MSR))
#define UART_GET_MCR(uart)      bfin_read16(((uart)->port.membase + OFFSET_MCR))

#define UART_PUT_CHAR(uart,v)   bfin_write16(((uart)->port.membase + OFFSET_THR),v)
#define UART_PUT_DLL(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_DLL),v)
#define UART_SET_IER(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_IER_SET),v)
#define UART_CLEAR_IER(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_IER_CLEAR),v)
#define UART_PUT_DLH(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_DLH),v)
#define UART_PUT_LSR(uart,v)	bfin_write16(((uart)->port.membase + OFFSET_LSR),v)
#define UART_PUT_LCR(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_LCR),v)
#define UART_CLEAR_LSR(uart)    bfin_write16(((uart)->port.membase + OFFSET_LSR), -1)
#define UART_PUT_GCTL(uart,v)   bfin_write16(((uart)->port.membase + OFFSET_GCTL),v)
#define UART_PUT_MCR(uart,v)    bfin_write16(((uart)->port.membase + OFFSET_MCR),v)

#define UART_SET_DLAB(uart)     /* MMRs not muxed on BF54x */
#define UART_CLEAR_DLAB(uart)   /* MMRs not muxed on BF54x */

#if defined(CONFIG_BFIN_UART0_CTSRTS) || defined(CONFIG_BFIN_UART1_CTSRTS)
# define CONFIG_SERIAL_BFIN_CTSRTS

# ifndef CONFIG_UART0_CTS_PIN
#  define CONFIG_UART0_CTS_PIN -1
# endif

# ifndef CONFIG_UART0_RTS_PIN
#  define CONFIG_UART0_RTS_PIN -1
# endif

# ifndef CONFIG_UART1_CTS_PIN
#  define CONFIG_UART1_CTS_PIN -1
# endif

# ifndef CONFIG_UART1_RTS_PIN
#  define CONFIG_UART1_RTS_PIN -1
# endif
#endif
/*
 * The pin configuration is different from schematic
 */
struct bfin_serial_port {
        struct uart_port        port;
        unsigned int            old_status;
#ifdef CONFIG_SERIAL_BFIN_DMA
	int			tx_done;
	int			tx_count;
	struct circ_buf		rx_dma_buf;
	struct timer_list       rx_dma_timer;
	int			rx_dma_nrows;
	unsigned int		tx_dma_channel;
	unsigned int		rx_dma_channel;
	struct work_struct	tx_dma_workqueue;
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	struct work_struct 	cts_workqueue;
	int		cts_pin;
	int 		rts_pin;
#endif
};

struct bfin_serial_port bfin_serial_ports[BFIN_UART_NR_PORTS];
struct bfin_serial_res {
	unsigned long	uart_base_addr;
	int		uart_irq;
#ifdef CONFIG_SERIAL_BFIN_DMA
	unsigned int	uart_tx_dma_channel;
	unsigned int	uart_rx_dma_channel;
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	int	uart_cts_pin;
	int	uart_rts_pin;
#endif
};

struct bfin_serial_res bfin_serial_resource[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
	0xFFC00400,
	IRQ_UART0_RX,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART0_TX,
	CH_UART0_RX,
#endif
#ifdef CONFIG_BFIN_UART0_CTSRTS
	CONFIG_UART0_CTS_PIN,
	CONFIG_UART0_RTS_PIN,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
	0xFFC02000,
	IRQ_UART1_RX,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART1_TX,
	CH_UART1_RX,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART2
	{
	0xFFC02100,
	IRQ_UART2_RX,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART2_TX,
	CH_UART2_RX,
#endif
#ifdef CONFIG_BFIN_UART2_CTSRTS
	CONFIG_UART2_CTS_PIN,
	CONFIG_UART2_RTS_PIN,
#endif
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
	{
	0xFFC03100,
	IRQ_UART3_RX,
#ifdef CONFIG_SERIAL_BFIN_DMA
	CH_UART3_TX,
	CH_UART3_RX,
#endif
	},
#endif
};

int nr_ports = ARRAY_SIZE(bfin_serial_resource);

#define DRIVER_NAME "bfin-uart"

static void bfin_serial_hw_init(struct bfin_serial_port *uart)
{
#ifdef CONFIG_SERIAL_BFIN_UART0
	peripheral_request(P_UART0_TX, DRIVER_NAME);
	peripheral_request(P_UART0_RX, DRIVER_NAME);
#endif

#ifdef CONFIG_SERIAL_BFIN_UART1
	peripheral_request(P_UART1_TX, DRIVER_NAME);
	peripheral_request(P_UART1_RX, DRIVER_NAME);

#ifdef CONFIG_BFIN_UART1_CTSRTS
	peripheral_request(P_UART1_RTS, DRIVER_NAME);
	peripheral_request(P_UART1_CTS DRIVER_NAME);
#endif
#endif

#ifdef CONFIG_SERIAL_BFIN_UART2
	peripheral_request(P_UART2_TX, DRIVER_NAME);
	peripheral_request(P_UART2_RX, DRIVER_NAME);
#endif

#ifdef CONFIG_SERIAL_BFIN_UART3
	peripheral_request(P_UART3_TX, DRIVER_NAME);
	peripheral_request(P_UART3_RX, DRIVER_NAME);

#ifdef CONFIG_BFIN_UART3_CTSRTS
	peripheral_request(P_UART3_RTS, DRIVER_NAME);
	peripheral_request(P_UART3_CTS DRIVER_NAME);
#endif
#endif
	SSYNC();
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	if (uart->cts_pin >= 0) {
		gpio_request(uart->cts_pin, DRIVER_NAME);
		gpio_direction_input(uart->cts_pin);
	}

	if (uart->rts_pin >= 0) {
		gpio_request(uart->rts_pin, DRIVER_NAME);
		gpio_direction_output(uart->rts_pin, 0);
	}
#endif
}
