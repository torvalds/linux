#ifndef __ASM_ARCH_ZYLONITE_H
#define __ASM_ARCH_ZYLONITE_H

#define ZYLONITE_ETH_PHYS	0x14000000

#define EXT_GPIO(x)		(128 + (x))

/* the following variables are processor specific and initialized
 * by the corresponding zylonite_pxa3xx_init()
 */
struct platform_mmc_slot {
	int gpio_cd;
	int gpio_wp;
};

extern struct platform_mmc_slot zylonite_mmc_slot[];

extern int gpio_backlight;
extern int gpio_eth_irq;

extern int wm9713_irq;

extern int lcd_id;
extern int lcd_orientation;

#ifdef CONFIG_CPU_PXA300
extern void zylonite_pxa300_init(void);
#else
static inline void zylonite_pxa300_init(void)
{
	if (cpu_is_pxa300() || cpu_is_pxa310())
		panic("%s: PXA300/PXA310 not supported\n", __FUNCTION__);
}
#endif

#ifdef CONFIG_CPU_PXA320
extern void zylonite_pxa320_init(void);
#else
static inline void zylonite_pxa320_init(void)
{
	if (cpu_is_pxa320())
		panic("%s: PXA320 not supported\n", __FUNCTION__);
}
#endif

#endif /* __ASM_ARCH_ZYLONITE_H */
