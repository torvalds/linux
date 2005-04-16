/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef _ASM_MC146818RTC_H
#define _ASM_MC146818RTC_H

#ifdef CONFIG_SH_MPC1211
#undef  _ASM_MC146818RTC_H
#undef  RTC_IRQ
#include <asm/mpc1211/mc146818rtc.h>
#else

#include <asm/rtc.h>

#define RTC_ALWAYS_BCD	1

/* FIXME:RTC Interrupt feature is not implemented yet. */
#undef  RTC_IRQ
#define RTC_IRQ		0

#if defined(CONFIG_CPU_SH3)
#define RTC_PORT(n)		(R64CNT+(n)*2)
#define CMOS_READ(addr)		__CMOS_READ(addr,b)
#define CMOS_WRITE(val,addr)	__CMOS_WRITE(val,addr,b)

#elif defined(CONFIG_SH_SECUREEDGE5410)
#include <asm/snapgear/io.h>

#define RTC_PORT(n)             SECUREEDGE_IOPORT_ADDR
#define CMOS_READ(addr)         secureedge5410_cmos_read(addr)
#define CMOS_WRITE(val,addr)    secureedge5410_cmos_write(val,addr)
extern unsigned char secureedge5410_cmos_read(int addr);
extern void secureedge5410_cmos_write(unsigned char val, int addr);

#elif defined(CONFIG_CPU_SH4)
#define RTC_PORT(n)		(R64CNT+(n)*4)
#define CMOS_READ(addr)		__CMOS_READ(addr,w)
#define CMOS_WRITE(val,addr)	__CMOS_WRITE(val,addr,w)
#endif

#define __CMOS_READ(addr, s) ({						\
	unsigned char val=0, rcr1, rcr2, r64cnt, retry;			\
	switch(addr) {							\
		case RTC_SECONDS:					\
			val = ctrl_inb(RSECCNT);			\
			break;						\
		case RTC_SECONDS_ALARM:					\
			val = ctrl_inb(RSECAR);				\
			break;						\
		case RTC_MINUTES:					\
			val = ctrl_inb(RMINCNT);			\
			break;						\
		case RTC_MINUTES_ALARM:					\
			val = ctrl_inb(RMINAR);				\
			break;						\
		case RTC_HOURS:						\
			val = ctrl_inb(RHRCNT);				\
			break;						\
		case RTC_HOURS_ALARM:					\
			val = ctrl_inb(RHRAR);				\
			break;						\
		case RTC_DAY_OF_WEEK:					\
			val = ctrl_inb(RWKCNT);				\
			break;						\
		case RTC_DAY_OF_MONTH:					\
			val = ctrl_inb(RDAYCNT);			\
			break;						\
		case RTC_MONTH:						\
			val = ctrl_inb(RMONCNT);			\
			break;						\
		case RTC_YEAR:						\
			val = ctrl_in##s(RYRCNT);			\
			break;						\
		case RTC_REG_A: /* RTC_FREQ_SELECT */			\
			rcr2 = ctrl_inb(RCR2);				\
			val = (rcr2 & RCR2_PESMASK) >> 4;		\
			rcr1 = ctrl_inb(RCR1);				\
			rcr1 = (rcr1 & (RCR1_CIE | RCR1_AIE)) | RCR1_AF;\
			retry = 0;					\
			do {						\
				ctrl_outb(rcr1, RCR1); /* clear CF */	\
				r64cnt = ctrl_inb(R64CNT);		\
			} while((ctrl_inb(RCR1) & RCR1_CF) && retry++ < 1000);\
			r64cnt ^= RTC_BIT_INVERTED;			\
			if(r64cnt == 0x7f || r64cnt == 0)		\
				val |= RTC_UIP;				\
			break;						\
		case RTC_REG_B:	/* RTC_CONTROL */			\
			rcr1 = ctrl_inb(RCR1);				\
			rcr2 = ctrl_inb(RCR2);				\
			if(rcr1 & RCR1_CIE)	val |= RTC_UIE;		\
			if(rcr1 & RCR1_AIE)	val |= RTC_AIE;		\
			if(rcr2 & RCR2_PESMASK)	val |= RTC_PIE;		\
			if(!(rcr2 & RCR2_START))val |= RTC_SET;		\
			val |= RTC_24H;					\
			break;						\
		case RTC_REG_C:	/* RTC_INTR_FLAGS */			\
			rcr1 = ctrl_inb(RCR1);				\
			rcr1 &= ~(RCR1_CF | RCR1_AF);			\
			ctrl_outb(rcr1, RCR1);				\
			rcr2 = ctrl_inb(RCR2);				\
			rcr2 &= ~RCR2_PEF;				\
			ctrl_outb(rcr2, RCR2);				\
			break;						\
		case RTC_REG_D:	/* RTC_VALID */				\
			/* Always valid ... */				\
			val = RTC_VRT;					\
			break;						\
		default:						\
			break;						\
	}								\
	val;								\
})

#define __CMOS_WRITE(val, addr, s) ({					\
	unsigned char rcr1,rcr2;					\
	switch(addr) {							\
		case RTC_SECONDS:					\
			ctrl_outb(val, RSECCNT);			\
			break;						\
		case RTC_SECONDS_ALARM:					\
			ctrl_outb(val, RSECAR);				\
			break;						\
		case RTC_MINUTES:					\
			ctrl_outb(val, RMINCNT);			\
			break;						\
		case RTC_MINUTES_ALARM:					\
			ctrl_outb(val, RMINAR);				\
			break;						\
		case RTC_HOURS:						\
			ctrl_outb(val, RHRCNT);				\
			break;						\
		case RTC_HOURS_ALARM:					\
			ctrl_outb(val, RHRAR);				\
			break;						\
		case RTC_DAY_OF_WEEK:					\
			ctrl_outb(val, RWKCNT);				\
			break;						\
		case RTC_DAY_OF_MONTH:					\
			ctrl_outb(val, RDAYCNT);			\
			break;						\
		case RTC_MONTH:						\
			ctrl_outb(val, RMONCNT);			\
			break;						\
		case RTC_YEAR:						\
			ctrl_out##s((ctrl_in##s(RYRCNT) & 0xff00) | (val & 0xff), RYRCNT);\
			break;						\
		case RTC_REG_A: /* RTC_FREQ_SELECT */			\
			rcr2 = ctrl_inb(RCR2);				\
			if((val & RTC_DIV_CTL) == RTC_DIV_RESET2)	\
				rcr2 |= RCR2_RESET;			\
			ctrl_outb(rcr2, RCR2);				\
			break;						\
		case RTC_REG_B:	/* RTC_CONTROL */			\
			rcr1 = (ctrl_inb(RCR1) & 0x99) | RCR1_AF;	\
			if(val & RTC_AIE) rcr1 |= RCR1_AIE;		\
			else              rcr1 &= ~RCR1_AIE;		\
			if(val & RTC_UIE) rcr1 |= RCR1_CIE;		\
			else              rcr1 &= ~RCR1_CIE;		\
			ctrl_outb(rcr1, RCR1);				\
			rcr2 = ctrl_inb(RCR2);				\
			if(val & RTC_SET) rcr2 &= ~RCR2_START;		\
			else              rcr2 |= RCR2_START;		\
			ctrl_outb(rcr2, RCR2);				\
			break;						\
		case RTC_REG_C:	/* RTC_INTR_FLAGS */			\
			break;						\
		case RTC_REG_D:	/* RTC_VALID */				\
			break;						\
		default:						\
			break;						\
	}								\
})

#endif /* CONFIG_SH_MPC1211 */
#endif /* _ASM_MC146818RTC_H */
