#ifndef _ASM_X8664_PROTO_H
#define _ASM_X8664_PROTO_H 1

#include <asm/ldt.h>

/* misc architecture specific prototypes */

struct cpuinfo_x86; 
struct pt_regs;

extern void start_kernel(void);
extern void pda_init(int); 

extern void early_idt_handler(void);

extern void mcheck_init(struct cpuinfo_x86 *c);
extern void init_memory_mapping(unsigned long start, unsigned long end);

extern void system_call(void); 
extern int kernel_syscall(void);
extern void syscall_init(void);

extern void ia32_syscall(void);
extern void ia32_cstar_target(void); 
extern void ia32_sysenter_target(void); 

extern void config_acpi_tables(void);
extern void ia32_syscall(void);

extern int pmtimer_mark_offset(void);
extern void pmtimer_resume(void);
extern void pmtimer_wait(unsigned);
extern unsigned int do_gettimeoffset_pm(void);
#ifdef CONFIG_X86_PM_TIMER
extern u32 pmtmr_ioport;
#else
#define pmtmr_ioport 0
#endif
extern int nohpet;

extern void early_printk(const char *fmt, ...) __attribute__((format(printf,1,2)));

extern void early_identify_cpu(struct cpuinfo_x86 *c);

extern int k8_scan_nodes(unsigned long start, unsigned long end);

extern void numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn);
extern unsigned long numa_free_all_bootmem(void);

extern void reserve_bootmem_generic(unsigned long phys, unsigned len);

extern void load_gs_index(unsigned gs);

extern void stop_timer_interrupt(void);
extern void main_timer_handler(void);

extern unsigned long end_pfn_map; 

extern void show_trace(struct task_struct *, struct pt_regs *, unsigned long * rsp);
extern void show_registers(struct pt_regs *regs);

extern void exception_table_check(void);

extern void acpi_reserve_bootmem(void);

extern void swap_low_mappings(void);

extern void __show_regs(struct pt_regs * regs);
extern void show_regs(struct pt_regs * regs);

extern void syscall32_cpu_init(void);

extern void setup_node_bootmem(int nodeid, unsigned long start, unsigned long end);

extern void early_quirks(void);
extern void check_efer(void);

extern void select_idle_routine(const struct cpuinfo_x86 *c);

extern unsigned long table_start, table_end;

extern int exception_trace;
extern unsigned cpu_khz;
extern unsigned tsc_khz;

extern int reboot_force;
extern int notsc_setup(char *);

extern int timer_over_8254;

extern int gsi_irq_sharing(int gsi);

extern void smp_local_timer_interrupt(void);

extern int force_mwait;

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr);

void i8254_timer_resume(void);

#define round_up(x,y) (((x) + (y) - 1) & ~((y)-1))
#define round_down(x,y) ((x) & ~((y)-1))

#endif
