#ifndef _ASM_POWERPC_MACHDEP_H
#define _ASM_POWERPC_MACHDEP_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/setup.h>

/* We export this macro for external modules like Alsa to know if
 * ppc_md.feature_call is implemented or not
 */
#define CONFIG_PPC_HAS_FEATURE_CALLS

struct pt_regs;
struct pci_bus;	
struct device_node;
struct iommu_table;
struct rtc_time;
struct file;
#ifdef CONFIG_KEXEC
struct kimage;
#endif

#ifdef CONFIG_SMP
struct smp_ops_t {
	void  (*message_pass)(int target, int msg);
	int   (*probe)(void);
	void  (*kick_cpu)(int nr);
	void  (*setup_cpu)(int nr);
	void  (*take_timebase)(void);
	void  (*give_timebase)(void);
	int   (*cpu_enable)(unsigned int nr);
	int   (*cpu_disable)(void);
	void  (*cpu_die)(unsigned int nr);
	int   (*cpu_bootable)(unsigned int nr);
};
#endif

struct machdep_calls {
	char		*name;
#ifdef CONFIG_PPC64
	void            (*hpte_invalidate)(unsigned long slot,
					   unsigned long va,
					   int psize,
					   int local);
	long		(*hpte_updatepp)(unsigned long slot, 
					 unsigned long newpp, 
					 unsigned long va,
					 int pize,
					 int local);
	void            (*hpte_updateboltedpp)(unsigned long newpp, 
					       unsigned long ea,
					       int psize);
	long		(*hpte_insert)(unsigned long hpte_group,
				       unsigned long va,
				       unsigned long prpn,
				       unsigned long rflags,
				       unsigned long vflags,
				       int psize);
	long		(*hpte_remove)(unsigned long hpte_group);
	void		(*flush_hash_range)(unsigned long number, int local);

	/* special for kexec, to be called in real mode, linar mapping is
	 * destroyed as well */
	void		(*hpte_clear_all)(void);

	void		(*tce_build)(struct iommu_table * tbl,
				     long index,
				     long npages,
				     unsigned long uaddr,
				     enum dma_data_direction direction);
	void		(*tce_free)(struct iommu_table *tbl,
				    long index,
				    long npages);
	unsigned long	(*tce_get)(struct iommu_table *tbl,
				    long index);
	void		(*tce_flush)(struct iommu_table *tbl);
	void		(*iommu_dev_setup)(struct pci_dev *dev);
	void		(*iommu_bus_setup)(struct pci_bus *bus);
#endif /* CONFIG_PPC64 */

	int		(*probe)(void);
	void		(*setup_arch)(void);
	void		(*init_early)(void);
	/* Optional, may be NULL. */
	void		(*show_cpuinfo)(struct seq_file *m);
	void		(*show_percpuinfo)(struct seq_file *m, int i);

	void		(*init_IRQ)(void);
	unsigned int	(*get_irq)(void);
#ifdef CONFIG_KEXEC
	void		(*kexec_cpu_down)(int crash_shutdown, int secondary);
#endif

	/* PCI stuff */
	/* Called after scanning the bus, before allocating resources */
	void		(*pcibios_fixup)(void);
	int		(*pci_probe_mode)(struct pci_bus *);
	void		(*pci_irq_fixup)(struct pci_dev *dev);

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);
	void		(*panic)(char *str);
	void		(*cpu_die)(void);

	long		(*time_init)(void); /* Optional, may be NULL */

	int		(*set_rtc_time)(struct rtc_time *);
	void		(*get_rtc_time)(struct rtc_time *);
	unsigned long	(*get_boot_time)(void);
	unsigned char 	(*rtc_read_val)(int addr);
	void		(*rtc_write_val)(int addr, unsigned char val);

	void		(*calibrate_decr)(void);

	void		(*progress)(char *, unsigned short);

	/* Interface for platform error logging */
	void 		(*log_error)(char *buf, unsigned int err_type, int fatal);

	unsigned char 	(*nvram_read_val)(int addr);
	void		(*nvram_write_val)(int addr, unsigned char val);
	ssize_t		(*nvram_write)(char *buf, size_t count, loff_t *index);
	ssize_t		(*nvram_read)(char *buf, size_t count, loff_t *index);	
	ssize_t		(*nvram_size)(void);		
	void		(*nvram_sync)(void);

	/* Exception handlers */
	int		(*system_reset_exception)(struct pt_regs *regs);
	int 		(*machine_check_exception)(struct pt_regs *regs);

	/* Motherboard/chipset features. This is a kind of general purpose
	 * hook used to control some machine specific features (like reset
	 * lines, chip power control, etc...).
	 */
	long	 	(*feature_call)(unsigned int feature, ...);

	/* Check availability of legacy devices like i8042 */
	int 		(*check_legacy_ioport)(unsigned int baseport);

	/* Get legacy PCI/IDE interrupt mapping */ 
	int		(*pci_get_legacy_ide_irq)(struct pci_dev *dev, int channel);
	
	/* Get access protection for /dev/mem */
	pgprot_t	(*phys_mem_access_prot)(struct file *file,
						unsigned long pfn,
						unsigned long size,
						pgprot_t vma_prot);

	/* Idle loop for this platform, leave empty for default idle loop */
	void		(*idle_loop)(void);

	/*
	 * Function for waiting for work with reduced power in idle loop;
	 * called with interrupts disabled.
	 */
	void		(*power_save)(void);

	/* Function to enable performance monitor counters for this
	   platform, called once per cpu. */
	void		(*enable_pmcs)(void);

	/* Set DABR for this platform, leave empty for default implemenation */
	int		(*set_dabr)(unsigned long dabr);

#ifdef CONFIG_PPC32	/* XXX for now */
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. */
	void		(*init)(void);

	void		(*setup_io_mappings)(void);

	void		(*early_serial_map)(void);
	void		(*kgdb_map_scc)(void);

	/*
	 * optional PCI "hooks"
	 */

	/* Called after PPC generic resource fixup to perform
	   machine specific fixups */
	void (*pcibios_fixup_resources)(struct pci_dev *);

	/* Called for each PCI bus in the system when it's probed */
	void (*pcibios_fixup_bus)(struct pci_bus *);

	/* Called when pci_enable_device() is called (initial=0) or
	 * when a device with no assigned resource is found (initial=1).
	 * Returns 0 to allow assignment/enabling of the device. */
	int  (*pcibios_enable_device_hook)(struct pci_dev *, int initial);

	/* Called in indirect_* to avoid touching devices */
	int (*pci_exclude_device)(unsigned char, unsigned char);

	/* Called at then very end of pcibios_init() */
	void (*pcibios_after_init)(void);

#endif /* CONFIG_PPC32 */

	/* Called to shutdown machine specific hardware not already controlled
	 * by other drivers.
	 */
	void (*machine_shutdown)(void);

#ifdef CONFIG_KEXEC
	/* Called to do the minimal shutdown needed to run a kexec'd kernel
	 * to run successfully.
	 * XXX Should we move this one out of kexec scope?
	 */
	void (*machine_crash_shutdown)(struct pt_regs *regs);

	/* Called to do what every setup is needed on image and the
	 * reboot code buffer. Returns 0 on success.
	 * Provide your own (maybe dummy) implementation if your platform
	 * claims to support kexec.
	 */
	int (*machine_kexec_prepare)(struct kimage *image);

	/* Called to handle any machine specific cleanup on image */
	void (*machine_kexec_cleanup)(struct kimage *image);

	/* Called to perform the _real_ kexec.
	 * Do NOT allocate memory or fail here. We are past the point of
	 * no return.
	 */
	void (*machine_kexec)(struct kimage *image);
#endif /* CONFIG_KEXEC */

#ifdef CONFIG_PCI_MSI
	int (*enable_msi)(struct pci_dev *pdev);
	void (*disable_msi)(struct pci_dev *pdev);
#endif /* CONFIG_PCI_MSI */
};

extern void power4_idle(void);
extern void ppc6xx_idle(void);

/*
 * ppc_md contains a copy of the machine description structure for the
 * current platform. machine_id contains the initial address where the
 * description was found during boot.
 */
extern struct machdep_calls ppc_md;
extern struct machdep_calls *machine_id;

#define __machine_desc __attribute__ ((__section__ (".machine.desc")))

#define define_machine(name)					\
	extern struct machdep_calls mach_##name;		\
	EXPORT_SYMBOL(mach_##name);				\
	struct machdep_calls mach_##name __machine_desc =

#define machine_is(name) \
	({ \
		extern struct machdep_calls mach_##name \
			__attribute__((weak));		 \
		machine_id == &mach_##name; \
	})

extern void probe_machine(void);

extern char cmd_line[COMMAND_LINE_SIZE];

#ifdef CONFIG_PPC_PMAC
/*
 * Power macintoshes have either a CUDA, PMU or SMU controlling
 * system reset, power, NVRAM, RTC.
 */
typedef enum sys_ctrler_kind {
	SYS_CTRLER_UNKNOWN = 0,
	SYS_CTRLER_CUDA = 1,
	SYS_CTRLER_PMU = 2,
	SYS_CTRLER_SMU = 3,
} sys_ctrler_t;
extern sys_ctrler_t sys_ctrler;

#endif /* CONFIG_PPC_PMAC */

extern void setup_pci_ptrs(void);

#ifdef CONFIG_SMP
/* Poor default implementations */
extern void __devinit smp_generic_give_timebase(void);
extern void __devinit smp_generic_take_timebase(void);
#endif /* CONFIG_SMP */


/* Functions to produce codes on the leds.
 * The SRC code should be unique for the message category and should
 * be limited to the lower 24 bits (the upper 8 are set by these funcs),
 * and (for boot & dump) should be sorted numerically in the order
 * the events occur.
 */
/* Print a boot progress message. */
void ppc64_boot_msg(unsigned int src, const char *msg);
/* Print a termination message (print only -- does not stop the kernel) */
void ppc64_terminate_msg(unsigned int src, const char *msg);

static inline void log_error(char *buf, unsigned int err_type, int fatal)
{
	if (ppc_md.log_error)
		ppc_md.log_error(buf, err_type, fatal);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_MACHDEP_H */
