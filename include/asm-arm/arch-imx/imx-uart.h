#ifndef ASMARM_ARCH_UART_H
#define ASMARM_ARCH_UART_H

#define IMXUART_HAVE_RTSCTS (1<<0)

struct imxuart_platform_data {
	unsigned int flags;
};

#endif
