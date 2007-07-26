#ifndef _M68KNOMMU_MACHDEP_H
#define _M68KNOMMU_MACHDEP_H

#include <linux/seq_file.h>
#include <linux/interrupt.h>

struct pt_regs;
struct kbd_repeat;
struct mktime;
struct hwclk_time;
struct gendisk;
struct buffer_head;

extern void (*mach_sched_init) (irqreturn_t (*handler)(int, void *, struct pt_regs *));
/* machine dependent keyboard functions */
extern int (*mach_keyb_init) (void);
extern int (*mach_kbdrate) (struct kbd_repeat *);
extern void (*mach_kbd_leds) (unsigned int);
/* machine dependent irq functions */
extern void (*mach_init_IRQ) (void);
extern irq_handler_t mach_default_handler;
extern int (*mach_request_irq) (unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                                unsigned long flags, const char *devname, void *dev_id);
extern void (*mach_free_irq) (unsigned int irq, void *dev_id);
extern void (*mach_get_model) (char *model);
extern int (*mach_get_hardware_list) (char *buffer);
extern int (*mach_get_irq_list) (struct seq_file *p, void *v);
extern void (*mach_process_int) (int irq, struct pt_regs *fp);
/* machine dependent timer functions */
extern unsigned long (*mach_gettimeoffset)(void);
extern void (*mach_gettod)(int *year, int *mon, int *day, int *hour,
			   int *min, int *sec);
extern int (*mach_hwclk)(int, struct hwclk_time*);
extern int (*mach_set_clock_mmss)(unsigned long);
extern void (*mach_reset)( void );
extern void (*mach_halt)( void );
extern void (*mach_power_off)( void );
extern unsigned long (*mach_hd_init) (unsigned long, unsigned long);
extern void (*mach_hd_setup)(char *, int *);
extern long mach_max_dma_address;
extern void (*mach_floppy_eject)(void);
extern void (*mach_heartbeat) (int);
extern void (*mach_l2_flush) (int);
extern int mach_sysrq_key;
extern int mach_sysrq_shift_state;
extern int mach_sysrq_shift_mask;
extern char *mach_sysrq_xlate;

extern void config_BSP(char *command, int len);
extern void (*mach_tick)(void);

#endif /* _M68KNOMMU_MACHDEP_H */
