#ifndef HAYESESP_H
#define HAYESESP_H

struct hayes_esp_config {
	short flow_on;
	short flow_off;
	short rx_trigger;
	short tx_trigger;
	short pio_threshold;
	unsigned char rx_timeout;
	char dma_channel;
};

#ifdef __KERNEL__

#define ESP_DMA_CHANNEL   0
#define ESP_RX_TRIGGER    768
#define ESP_TX_TRIGGER    768
#define ESP_FLOW_OFF      1016
#define ESP_FLOW_ON       944
#define ESP_RX_TMOUT      128
#define ESP_PIO_THRESHOLD 32

#define ESP_IN_MAJOR	57	/* major dev # for dial in */
#define ESP_OUT_MAJOR	58	/* major dev # for dial out */
#define ESPC_SCALE 	3
#define UART_ESI_BASE	0x00
#define UART_ESI_SID	0x01
#define UART_ESI_RX	0x02
#define UART_ESI_TX	0x02
#define UART_ESI_CMD1	0x04
#define UART_ESI_CMD2	0x05
#define UART_ESI_STAT1	0x04
#define UART_ESI_STAT2	0x05
#define UART_ESI_RWS	0x07

#define UART_IER_DMA_TMOUT	0x80
#define UART_IER_DMA_TC		0x08

#define ESI_SET_IRQ		0x04
#define ESI_SET_DMA_TMOUT	0x05
#define ESI_SET_SRV_MASK	0x06
#define ESI_SET_ERR_MASK	0x07
#define ESI_SET_FLOW_CNTL	0x08
#define ESI_SET_FLOW_CHARS	0x09
#define ESI_SET_FLOW_LVL	0x0a
#define ESI_SET_TRIGGER		0x0b
#define ESI_SET_RX_TIMEOUT	0x0c
#define ESI_SET_FLOW_TMOUT	0x0d
#define ESI_WRITE_UART		0x0e
#define ESI_READ_UART		0x0f
#define ESI_SET_MODE		0x10
#define ESI_GET_ERR_STAT	0x12
#define ESI_GET_UART_STAT	0x13
#define ESI_GET_RX_AVAIL	0x14
#define ESI_GET_TX_AVAIL	0x15
#define ESI_START_DMA_RX	0x16
#define ESI_START_DMA_TX	0x17
#define ESI_ISSUE_BREAK		0x1a
#define ESI_FLUSH_RX		0x1b
#define ESI_FLUSH_TX		0x1c
#define ESI_SET_BAUD		0x1d
#define ESI_SET_ENH_IRQ		0x1f
#define ESI_SET_REINTR		0x20
#define ESI_SET_PRESCALAR	0x23
#define ESI_NO_COMMAND		0xff

#define ESP_STAT_RX_TIMEOUT	0x01
#define ESP_STAT_DMA_RX		0x02
#define ESP_STAT_DMA_TX		0x04
#define ESP_STAT_NEVER_DMA      0x08
#define ESP_STAT_USE_PIO        0x10

#define ESP_MAGIC		0x53ee
#define ESP_XMIT_SIZE		4096

struct esp_struct {
	int			magic;
	struct tty_port		port;
	spinlock_t		lock;
	int			io_port;
	int			irq;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			stat_flags;
	int			custom_divisor;
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	unsigned long		last_active;
	int			line;
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	wait_queue_head_t	break_wait;
	struct async_icount	icount;	/* kernel counters for the 4 input interrupts */
	struct hayes_esp_config config; /* port configuration */
	struct esp_struct	*next_port; /* For the linked list */
};

struct esp_pio_buffer {
	unsigned char data[1024];
	struct esp_pio_buffer *next;
};

#endif /* __KERNEL__ */


#endif /* ESP_H */

