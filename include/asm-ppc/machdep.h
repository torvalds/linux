#ifdef __KERNEL__
#ifndef _PPC_MACHDEP_H
#define _PPC_MACHDEP_H

#include <linux/init.h>
#include <linux/kexec.h>

#include <asm/setup.h>
#include <asm/page.h>

struct pt_regs;
struct pci_bus;	
struct pci_dev;
struct seq_file;
struct file;

/*
 * This is for compatibility with ARCH=powerpc.
 */
#define machine_is(x)	__MACHINE_IS_##x
#define __MACHINE_IS_powermac	0
#define __MACHINE_IS_chrp	0
#ifdef CONFIG_PPC_PREP
#define __MACHINE_IS_prep	1
#else
#define __MACHINE_IS_prep	0
#endif

/* We export this macro for external modules like Alsa to know if
 * ppc_md.feature_call is implemented or not
 */
#define CONFIG_PPC_HAS_FEATURE_CALLS

struct machdep_calls {
	void		(*setup_arch)(void);
	/* Optional, may be NULL. */
	int		(*show_cpuinfo)(struct seq_file *m);
	int		(*show_percpuinfo)(struct seq_file *m, int i);
	/* Optional, may be NULL. */
	unsigned int	(*irq_canonicalize)(unsigned int irq);
	void		(*init_IRQ)(void);
	int		(*get_irq)(void);
	
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. DEPRECATED ! */
	void		(*init)(void);
	/* For compatibility with merged platforms */
	void		(*init_early)(void);

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);

	void		(*idle_loop)(void);
	void		(*power_save)(void);

	long		(*time_init)(void); /* Optional, may be NULL */
	int		(*set_rtc_time)(unsigned long nowtime);
	unsigned long	(*get_rtc_time)(void);
	unsigned char 	(*rtc_read_val)(int addr);
	void		(*rtc_write_val)(int addr, unsigned char val);
	void		(*calibrate_decr)(void);

	void		(*heartbeat)(void);
	unsigned long	heartbeat_reset;
	unsigned long	heartbeat_count;

	unsigned long	(*find_end_of_memory)(void);
	void		(*setup_io_mappings)(void);

	void		(*early_serial_map)(void);
  	void		(*progress)(char *, unsigned short);
	void		(*kgdb_map_scc)(void);

	unsigned char 	(*nvram_read_val)(int addr);
	void		(*nvram_write_val)(int addr, unsigned char val);
	void		(*nvram_sync)(void);

	/*
	 * optional PCI "hooks"
	 */

	/* Called after scanning the bus, before allocating resources */
	void (*pcibios_fixup)(void);

	/* Called after PPC generic resource fixup to perform
	   machine specific fixups */
	void (*pcibios_fixup_resources)(struct pci_dev *);

	/* Called for each PCI bus in the system when it's probed */
	void (*pcibios_fixup_bus)(struct pci_bus *);

	/* Called when pci_enable_device() is called (initial=0) or
	 * when a device with no assigned resource is found (initial=1).
	 * Returns 0 to allow assignment/enabling of the device. */
	int  (*pcibios_enable_device_hook)(struct pci_dev *, int initial);

	/* For interrupt routing */
	unsigned char (*pci_swizzle)(struct pci_dev *, unsigned char *);
	int (*pci_map_irq)(struct pci_dev *, unsigned char, unsigned char);

	/* Called in indirect_* to avoid touching devices */
	int (*pci_exclude_device)(unsigned char, unsigned char);

	/* Called at then very end of pcibios_init() */
	void (*pcibios_after_init)(void);

	/* Get access protection for /dev/mem */
	pgprot_t	(*phys_mem_access_prot)(struct file *file,
						unsigned long pfn,
						unsigned long size,
						pgprot_t vma_prot);

	/* Motherboard/chipset features. This is a kind of general purpose
	 * hook used to control some machine specific features (like reset
	 * lines, chip power control, etc...).
	 */
	long (*feature_call)(unsigned int feature, ...);

#ifdef CONFIG_SMP
	/* functions for dealing with other cpus */
	struct smp_ops_t *smp_ops;
#endif /* CONFIG_SMP */

#ifdef CONFIG_KEXEC
	/* Called to shutdown machine specific hardware not already controlled
	 * by other drivers.
	 * XXX Should we move this one out of kexec scope?
	 */
	void (*machine_shutdown)(void);

	/* Called to do the minimal shutdown needed to run a kexec'd kernel
	 * to run successfully.
	 * XXX Should we move this one out of kexec scope?
	 */
	void (*machine_crash_shutdown)(void);

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
};

extern struct machdep_calls ppc_md;
extern char cmd_line[COMMAND_LINE_SIZE];

extern void setup_pci_ptrs(void);

#ifdef CONFIG_SMP
struct smp_ops_t {
	void  (*message_pass)(int target, int msg);
	int   (*probe)(void);
	void  (*kick_cpu)(int nr);
	void  (*setup_cpu)(int nr);
	void  (*space_timers)(int nr);
	void  (*take_timebase)(void);
	void  (*give_timebase)(void);
};

/* Poor default implementations */
extern void __devinit smp_generic_give_timebase(void);
extern void __devinit smp_generic_take_timebase(void);
#endif /* CONFIG_SMP */

#endif /* _PPC_MACHDEP_H */
#endif /* __KERNEL__ */
