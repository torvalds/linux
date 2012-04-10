#ifndef __LINUX_OF_SERIAL_H
#define __LINUX_OF_SERIAL_H

/*
 * FIXME remove this file when tegra finishes conversion to open firmware,
 * expectation is that all quirks will then be self-contained in
 * drivers/tty/serial/of_serial.c.
 */
#ifdef CONFIG_ARCH_TEGRA
extern void tegra_serial_handle_break(struct uart_port *port);
#else
static inline void tegra_serial_handle_break(struct uart_port *port)
{
}
#endif

#endif /* __LINUX_OF_SERIAL */
