#ifndef _IOP13XX_TIME_H_
#define _IOP13XX_TIME_H_
#define IRQ_IOP_TIMER0 IRQ_IOP13XX_TIMER0

#define IOP_TMR_EN	    0x02
#define IOP_TMR_RELOAD	    0x04
#define IOP_TMR_PRIVILEGED 0x08
#define IOP_TMR_RATIO_1_1  0x00

void iop_init_time(unsigned long tickrate);
unsigned long iop_gettimeoffset(void);

static inline void write_tmr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c9, 0" : : "r" (val));
}

static inline void write_tmr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c1, c9, 0" : : "r" (val));
}

static inline u32 read_tcr0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c2, c9, 0" : "=r" (val));
	return val;
}

static inline u32 read_tcr1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c9, 0" : "=r" (val));
	return val;
}

static inline void write_trr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c4, c9, 0" : : "r" (val));
}

static inline void write_trr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c5, c9, 0" : : "r" (val));
}

static inline void write_tisr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c6, c9, 0" : : "r" (val));
}
#endif
