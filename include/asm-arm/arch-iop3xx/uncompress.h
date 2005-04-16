/*
 *  linux/include/asm-arm/arch-iop3xx/uncompress.h
 */
#include <linux/config.h>
#include <asm/types.h>
#include <asm/mach-types.h>
#include <linux/serial_reg.h>
#include <asm/hardware.h>

#ifdef CONFIG_ARCH_IOP321
#define UTYPE unsigned char *
#elif defined(CONFIG_ARCH_IOP331)
#define UTYPE u32 *
#else
#error "Missing IOP3xx arch type def"
#endif

static volatile UTYPE uart_base;

#define TX_DONE (UART_LSR_TEMT|UART_LSR_THRE)

static __inline__ void putc(char c)
{
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE);
	*uart_base = c;
}

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
        if(machine_is_iq80321())
			uart_base = (volatile UTYPE)IQ80321_UART;
		else if(machine_is_iq31244())
			uart_base = (volatile UTYPE)IQ31244_UART;
		else if(machine_is_iq80331() || machine_is_iq80332())
			uart_base = (volatile UTYPE)IOP331_UART0_PHYS;
		else
			uart_base = (volatile UTYPE)0xfe800000;
}

/*
 * nothing to do
 */
#define arch_decomp_setup()	__arch_decomp_setup(arch_id)
#define arch_decomp_wdog()
