/*
 *  linux/include/asm-arm/mach/serial_sa1100.h
 *
 *  Author: Nicolas Pitre
 *
 * Moved to include/asm-arm/mach and changed lots, Russell King
 *
 * Low level machine dependent UART functions.
 */

struct uart_port;
struct uart_info;

/*
 * This is a temporary structure for registering these
 * functions; it is intended to be discarded after boot.
 */
struct sa1100_port_fns {
	void	(*set_mctrl)(struct uart_port *, u_int);
	u_int	(*get_mctrl)(struct uart_port *);
	void	(*pm)(struct uart_port *, u_int, u_int);
	int	(*set_wake)(struct uart_port *, u_int);
};

#ifdef CONFIG_SERIAL_SA1100
void sa1100_register_uart_fns(struct sa1100_port_fns *fns);
void sa1100_register_uart(int idx, int port);
#else
#define sa1100_register_uart_fns(fns) do { } while (0)
#define sa1100_register_uart(idx,port) do { } while (0)
#endif
