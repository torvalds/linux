/*
 * include/asm-arm/arch-iop32x/uncompress.h
 */

#include <asm/types.h>
#include <asm/mach-types.h>
#include <linux/serial_reg.h>
#include <asm/hardware.h>

static volatile u8 *uart_base;

#define TX_DONE		(UART_LSR_TEMT | UART_LSR_THRE)

static inline void putc(char c)
{
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE)
		barrier();
	uart_base[UART_TX] = c;
}

static inline void flush(void)
{
}

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
	if (machine_is_iq80321())
		uart_base = (volatile u8 *)IQ80321_UART;
	else if (machine_is_iq31244() || machine_is_em7210())
		uart_base = (volatile u8 *)IQ31244_UART;
	else
		uart_base = (volatile u8 *)0xfe800000;
}

/*
 * nothing to do
 */
#define arch_decomp_setup()	__arch_decomp_setup(arch_id)
#define arch_decomp_wdog()
