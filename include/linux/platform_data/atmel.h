/*
 * atmel platform data
 *
 * GPL v2 Only
 */

#ifndef __ATMEL_H__
#define __ATMEL_H__

 /* Compact Flash */
struct at91_cf_data {
	int	irq_pin;		/* I/O IRQ */
	int	det_pin;		/* Card detect */
	int	vcc_pin;		/* power switching */
	int	rst_pin;		/* card reset */
	u8	chipselect;		/* EBI Chip Select number */
	u8	flags;
#define AT91_CF_TRUE_IDE	0x01
#define AT91_IDE_SWAP_A0_A2	0x02
};

/* FIXME: this needs a better location, but gets stuff building again */
#ifdef CONFIG_ATMEL_PM
extern int at91_suspend_entering_slow_clock(void);
#else
static inline int at91_suspend_entering_slow_clock(void)
{
	return 0;
}
#endif

#endif /* __ATMEL_H__ */
