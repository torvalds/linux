#ifndef _ASM_X8664_PROTO_H
#define _ASM_X8664_PROTO_H 1

#include <asm/ldt.h>

/* misc architecture specific prototypes */

struct cpuinfo_x86; 
struct pt_regs;

extern void start_kernel(void);
extern void pda_init(int); 

extern void zap_low_mappings(int cpu);

extern void early_idt_handler(void);

extern void mcheck_init(struct cpuinfo_x86 *c);
#ifdef CONFIG_MTRR
extern void mtrr_ap_init(void);
extern void mtrr_bp_init(void);
#else
#define mtrr_ap_init() do {} while (0)
#define mtrr_bp_init() do {} while (0)
#endif
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
extern unsigned long long monotonic_base;
extern int sysctl_vsyscall;
extern int nohpet;
extern unsigned long vxtime_hz;
extern void time_init_gtod(void);

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

extern char *syscall32_page;
extern void syscall32_cpu_init(void);

extern void setup_node_bootmem(int nodeid, unsigned long start, unsigned long end);

extern void early_quirks(void);
extern void quirk_intel_irqbalance(void);
extern void check_efer(void);

extern int unhandled_signal(struct task_struct *tsk, int sig);

extern int unsynchronized_tsc(void);

extern void select_idle_routine(const struct cpuinfo_x86 *c);

extern unsigned long table_start, table_end;

extern int exception_trace;
extern unsigned cpu_khz;

extern void no_iommu_init(void);
extern int force_iommu, no_iommu;
extern int iommu_detected;
#ifdef CONFIG_IOMMU
extern void gart_iommu_init(void);
extern void gart_parse_options(char *);
extern void iommu_hole_init(void);
extern int fallback_aper_order;
extern int fallback_aper_force;
extern int iommu_aperture;
extern int iommu_aperture_allowed;
extern int iommu_aperture_disabled;
extern int fix_aperture;
#else
#define iommu_aperture 0
#define iommu_aperture_allowed 0
#endif

extern int reboot_force;
extern int notsc_setup(char *);

extern int timer_over_8254;

extern int gsi_irq_sharing(int gsi);

extern void smp_local_timer_interrupt(void);

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr);

#define round_up(x,y) (((x) + (y) - 1) & ~((y)-1))
#define round_down(x,y) ((x) & ~((y)-1))

#endif
