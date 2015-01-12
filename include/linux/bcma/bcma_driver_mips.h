#ifndef LINUX_BCMA_DRIVER_MIPS_H_
#define LINUX_BCMA_DRIVER_MIPS_H_

#define BCMA_MIPS_IPSFLAG		0x0F08
/* which sbflags get routed to mips interrupt 1 */
#define  BCMA_MIPS_IPSFLAG_IRQ1		0x0000003F
#define  BCMA_MIPS_IPSFLAG_IRQ1_SHIFT	0
/* which sbflags get routed to mips interrupt 2 */
#define  BCMA_MIPS_IPSFLAG_IRQ2		0x00003F00
#define  BCMA_MIPS_IPSFLAG_IRQ2_SHIFT	8
/* which sbflags get routed to mips interrupt 3 */
#define  BCMA_MIPS_IPSFLAG_IRQ3		0x003F0000
#define  BCMA_MIPS_IPSFLAG_IRQ3_SHIFT	16
/* which sbflags get routed to mips interrupt 4 */
#define  BCMA_MIPS_IPSFLAG_IRQ4		0x3F000000
#define  BCMA_MIPS_IPSFLAG_IRQ4_SHIFT	24

/* MIPS 74K core registers */
#define BCMA_MIPS_MIPS74K_CORECTL	0x0000
#define BCMA_MIPS_MIPS74K_EXCEPTBASE	0x0004
#define BCMA_MIPS_MIPS74K_BIST		0x000C
#define BCMA_MIPS_MIPS74K_INTMASK_INT0	0x0014
#define BCMA_MIPS_MIPS74K_INTMASK(int) \
	((int) * 4 + BCMA_MIPS_MIPS74K_INTMASK_INT0)
#define BCMA_MIPS_MIPS74K_NMIMASK	0x002C
#define BCMA_MIPS_MIPS74K_GPIOSEL	0x0040
#define BCMA_MIPS_MIPS74K_GPIOOUT	0x0044
#define BCMA_MIPS_MIPS74K_GPIOEN	0x0048
#define BCMA_MIPS_MIPS74K_CLKCTLST	0x01E0

#define BCMA_MIPS_OOBSELINA74		0x004
#define BCMA_MIPS_OOBSELOUTA30		0x100

struct bcma_device;

struct bcma_drv_mips {
	struct bcma_device *core;
	u8 setup_done:1;
	u8 early_setup_done:1;
};

#ifdef CONFIG_BCMA_DRIVER_MIPS
extern void bcma_core_mips_init(struct bcma_drv_mips *mcore);
extern void bcma_core_mips_early_init(struct bcma_drv_mips *mcore);

extern unsigned int bcma_core_mips_irq(struct bcma_device *dev);
#else
static inline void bcma_core_mips_init(struct bcma_drv_mips *mcore) { }
static inline void bcma_core_mips_early_init(struct bcma_drv_mips *mcore) { }

static inline unsigned int bcma_core_mips_irq(struct bcma_device *dev)
{
	return 0;
}
#endif

extern u32 bcma_cpu_clock(struct bcma_drv_mips *mcore);

#endif /* LINUX_BCMA_DRIVER_MIPS_H_ */
