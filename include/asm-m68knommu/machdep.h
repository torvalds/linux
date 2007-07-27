#ifndef _M68KNOMMU_MACHDEP_H
#define _M68KNOMMU_MACHDEP_H

#include <linux/interrupt.h>

extern void (*mach_sched_init) (irq_handler_t handler);
/* machine dependent timer functions */
extern unsigned long (*mach_gettimeoffset)(void);
extern void (*mach_gettod)(int *year, int *mon, int *day, int *hour,
			   int *min, int *sec);
extern int (*mach_set_clock_mmss)(unsigned long);

/* machine dependent power off functions */
extern void (*mach_reset)( void );
extern void (*mach_halt)( void );
extern void (*mach_power_off)( void );

extern void config_BSP(char *command, int len);
extern void (*mach_tick)(void);

#endif /* _M68KNOMMU_MACHDEP_H */
