#ifndef ASM_X86__MICROCODE_H
#define ASM_X86__MICROCODE_H

struct cpu_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int rev;
};

struct device;

struct microcode_ops {
	int  (*request_microcode_user) (int cpu, const void __user *buf, size_t size);
	int  (*request_microcode_fw) (int cpu, struct device *device);

	void (*apply_microcode) (int cpu);

	int  (*collect_cpu_info) (int cpu, struct cpu_signature *csig);
	void (*microcode_fini_cpu) (int cpu);
};

struct ucode_cpu_info {
	struct cpu_signature cpu_sig;
	int valid;
	void *mc;
};
extern struct ucode_cpu_info ucode_cpu_info[];

#ifdef CONFIG_MICROCODE_INTEL
extern struct microcode_ops * __init init_intel_microcode(void);
#else
static inline struct microcode_ops * __init init_intel_microcode(void)
{
	return NULL;
}
#endif /* CONFIG_MICROCODE_INTEL */

#ifdef CONFIG_MICROCODE_AMD
extern struct microcode_ops * __init init_amd_microcode(void);
#else
static inline struct microcode_ops * __init init_amd_microcode(void)
{
	return NULL;
}
#endif

#endif /* ASM_X86__MICROCODE_H */
