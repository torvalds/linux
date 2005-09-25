#ifdef __KERNEL__
#ifndef _PPC64_MACHDEP_H
#define _PPC64_MACHDEP_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/setup.h>

struct pt_regs;
struct pci_bus;	
struct device_node;
struct iommu_table;
struct rtc_time;
struct file;

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
	void            (*hpte_invalidate)(unsigned long slot,
					   unsigned long va,
					   int large,
					   int local);
	long		(*hpte_updatepp)(unsigned long slot, 
					 unsigned long newpp, 
					 unsigned long va,
					 int large,
					 int local);
	void            (*hpte_updateboltedpp)(unsigned long newpp, 
					       unsigned long ea);
	long		(*hpte_insert)(unsigned long hpte_group,
				       unsigned long va,
				       unsigned long prpn,
				       unsigned long vflags,
				       unsigned long rflags);
	long		(*hpte_remove)(unsigned long hpte_group);
	void		(*flush_hash_range)(unsigned long context,
					    unsigned long number,
					    int local);
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
	void		(*tce_flush)(struct iommu_table *tbl);
	void		(*iommu_dev_setup)(struct pci_dev *dev);
	void		(*iommu_bus_setup)(struct pci_bus *bus);
	void		(*irq_bus_setup)(struct pci_bus *bus);

	int		(*probe)(int platform);
	void		(*setup_arch)(void);
	void		(*init_early)(void);
	/* Optional, may be NULL. */
	void		(*get_cpuinfo)(struct seq_file *m);

	void		(*init_IRQ)(void);
	int		(*get_irq)(struct pt_regs *);
	void		(*cpu_irq_down)(int secondary);

	/* PCI stuff */
	void		(*pcibios_fixup)(void);
	int		(*pci_probe_mode)(struct pci_bus *);

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);
	void		(*panic)(char *str);
	void		(*cpu_die)(void);

	int		(*set_rtc_time)(struct rtc_time *);
	void		(*get_rtc_time)(struct rtc_time *);
	void		(*get_boot_time)(struct rtc_time *);

	void		(*calibrate_decr)(void);

	void		(*progress)(char *, unsigned short);

	/* Interface for platform error logging */
	void 		(*log_error)(char *buf, unsigned int err_type, int fatal);

	ssize_t		(*nvram_write)(char *buf, size_t count, loff_t *index);
	ssize_t		(*nvram_read)(char *buf, size_t count, loff_t *index);	
	ssize_t		(*nvram_size)(void);		
	int		(*nvram_sync)(void);

	/* Exception handlers */
	void		(*system_reset_exception)(struct pt_regs *regs);
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
						unsigned long offset,
						unsigned long size,
						pgprot_t vma_prot);

	/* Idle loop for this platform, leave empty for default idle loop */
	int		(*idle_loop)(void);

	/* Function to enable pmcs for this platform, called once per cpu. */
	void		(*enable_pmcs)(void);
};

extern int default_idle(void);
extern int native_idle(void);

extern struct machdep_calls ppc_md;
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

#endif /* _PPC64_MACHDEP_H */
#endif /* __KERNEL__ */
