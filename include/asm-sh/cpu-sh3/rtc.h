#ifndef __ASM_CPU_SH3_RTC_H
#define __ASM_CPU_SH3_RTC_H

/* SH-3 RTC */
#define R64CNT  	0xfffffec0
#define RSECCNT 	0xfffffec2
#define RMINCNT 	0xfffffec4
#define RHRCNT  	0xfffffec6
#define RWKCNT  	0xfffffec8
#define RDAYCNT 	0xfffffeca
#define RMONCNT 	0xfffffecc
#define RYRCNT  	0xfffffece
#define RSECAR  	0xfffffed0
#define RMINAR  	0xfffffed2
#define RHRAR   	0xfffffed4
#define RWKAR   	0xfffffed6
#define RDAYAR  	0xfffffed8
#define RMONAR  	0xfffffeda
#define RCR1    	0xfffffedc
#define RCR2    	0xfffffede

#define RTC_BIT_INVERTED	0	/* No bug on SH7708, SH7709A */

#endif /* __ASM_CPU_SH3_RTC_H */

