#include <linux/serial.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#define NR_PORTS		2

#define OFFSET_THR              0x00	/* Transmit Holding register            */
#define OFFSET_RBR              0x00	/* Receive Buffer register              */
#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_IER              0x04	/* Interrupt Enable Register            */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_IIR              0x08	/* Interrupt Identification Register    */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_GCTL             0x24	/* Global Control Register              */

#define UART_GET_CHAR(uart)     bfin_read16(((uart)->port.membase + OFFSET_RBR))
#define UART_GET_DLL(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLL))
#define UART_GET_IER(uart)      bfin_read16(((uart)->port.membase + OFFSET_IER))
#define UART_GET_DLH(uart)	bfin_read16(((uart)->port.membase + OFFSET_DLH))
#define UART_GET_IIR(uart)      bfin_read16(((uart)->port.membase + OFFSET_IIR))
#define UART_GET_LCR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LCR))
#define UART_GET_LSR(uart)      bfin_read16(((uart)->port.membase + OFFSET_LSR))
#define UART_GET_GCTL(uart)     bfin_read16(((uart)->port.membase + OFFSET_GCTL))

#define UART_PUT_CHAR(uart, v)   bfin_write16(((uart)->port.membase + OFFSET_THR), v)
#define UART_PUT_DLL(uart, v)    bfin_write16(((uart)->port.membase + OFFSET_DLL), v)
#define UART_PUT_IER(uart, v)    bfin_write16(((uart)->port.membase + OFFSET_IER), v)
#define UART_PUT_DLH(uart, v)    bfin_write16(((uart)->port.membase + OFFSET_DLH), v)
#define UART_PUT_LCR(uart, v)    bfin_write16(((uart)->port.membase + OFFSET_LCR), v)
#define UART_PUT_GCTL(uart, v)   bfin_write16(((uart)->port.membase + OFFSET_GCTL), v)

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
	struct uart_port port;
	unsigned int old_status;
#ifdef CONFIG_SERIAL_BFIN_DMA
	int tx_done;
	int tx_count;
	struct circ_buf rx_dma_buf;
	struct timer_list rx_dma_timer;
	int rx_dma_nrows;
	unsigned int tx_dma_channel;
	unsigned int rx_dma_channel;
	struct work_struct tx_dma_workqueue;
#else
	struct work_struct cts_workqueue;
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	int cts_pin;
	int rts_pin;
#endif
};

struct bfin_serial_port bfin_serial_ports[NR_PORTS];
struct bfin_serial_res {
	unsigned long uart_base_addr;
	int uart_irq;
#ifdef CONFIG_SERIAL_BFIN_DMA
	unsigned int uart_tx_dma_channel;
	unsigned int uart_rx_dma_channel;
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	int uart_cts_pin;
	int uart_rts_pin;
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
#ifdef CONFIG_BFIN_UART1_CTSRTS
	 CONFIG_UART1_CTS_PIN,
	 CONFIG_UART1_RTS_PIN,
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
#endif

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
