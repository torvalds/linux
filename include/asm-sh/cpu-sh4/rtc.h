#ifndef __ASM_CPU_SH4_RTC_H
#define __ASM_CPU_SH4_RTC_H

/* SH-4 RTC */
#define R64CNT  	0xffc80000
#define RSECCNT 	0xffc80004
#define RMINCNT 	0xffc80008
#define RHRCNT  	0xffc8000c
#define RWKCNT  	0xffc80010
#define RDAYCNT 	0xffc80014
#define RMONCNT 	0xffc80018
#define RYRCNT  	0xffc8001c  /* 16bit */
#define RSECAR  	0xffc80020
#define RMINAR  	0xffc80024
#define RHRAR   	0xffc80028
#define RWKAR   	0xffc8002c
#define RDAYAR  	0xffc80030
#define RMONAR  	0xffc80034
#define RCR1    	0xffc80038
#define RCR2    	0xffc8003c

#define RTC_BIT_INVERTED	0x40	/* bug on SH7750, SH7750S */

#endif /* __ASM_CPU_SH4_RTC_H */

