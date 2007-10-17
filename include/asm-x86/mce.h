#ifndef _ASM_X86_MCE_H
#define _ASM_X86_MCE_H

#ifdef __x86_64__

#include <asm/ioctls.h>
#include <asm/types.h>

/*
 * Machine Check support for x86
 */

#define MCG_CTL_P	 (1UL<<8)   /* MCG_CAP register available */

#define MCG_STATUS_RIPV  (1UL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV  (1UL<<1)   /* eip points to correct instruction */
#define MCG_STATUS_MCIP  (1UL<<2)   /* machine check in progress */

#define MCI_STATUS_VAL   (1UL<<63)  /* valid error */
#define MCI_STATUS_OVER  (1UL<<62)  /* previous errors lost */
#define MCI_STATUS_UC    (1UL<<61)  /* uncorrected error */
#define MCI_STATUS_EN    (1UL<<60)  /* error enabled */
#define MCI_STATUS_MISCV (1UL<<59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1UL<<58)  /* addr reg. valid */
#define MCI_STATUS_PCC   (1UL<<57)  /* processor context corrupt */

/* Fields are zero when not available */
struct mce {
	__u64 status;
	__u64 misc;
	__u64 addr;
	__u64 mcgstatus;
	__u64 rip;
	__u64 tsc;	/* cpu time stamp counter */
	__u64 res1;	/* for future extension */
	__u64 res2;	/* dito. */
	__u8  cs;		/* code segment */
	__u8  bank;	/* machine check bank */
	__u8  cpu;	/* cpu that raised the error */
	__u8  finished;   /* entry is valid */
	__u32 pad;
};

/*
 * This structure contains all data related to the MCE log.  Also
 * carries a signature to make it easier to find from external
 * debugging tools.  Each entry is only valid when its finished flag
 * is set.
 */

#define MCE_LOG_LEN 32

struct mce_log {
	char signature[12]; /* "MACHINECHECK" */
	unsigned len;	    /* = MCE_LOG_LEN */
	unsigned next;
	unsigned flags;
	unsigned pad0;
	struct mce entry[MCE_LOG_LEN];
};

#define MCE_OVERFLOW 0		/* bit 0 in flags means overflow */

#define MCE_LOG_SIGNATURE	"MACHINECHECK"

#define MCE_GET_RECORD_LEN   _IOR('M', 1, int)
#define MCE_GET_LOG_LEN      _IOR('M', 2, int)
#define MCE_GETCLEAR_FLAGS   _IOR('M', 3, int)

/* Software defined banks */
#define MCE_EXTENDED_BANK	128
#define MCE_THERMAL_BANK	MCE_EXTENDED_BANK + 0

#define K8_MCE_THRESHOLD_BASE      (MCE_EXTENDED_BANK + 1)      /* MCE_AMD */
#define K8_MCE_THRESHOLD_BANK_0    (MCE_THRESHOLD_BASE + 0 * 9)
#define K8_MCE_THRESHOLD_BANK_1    (MCE_THRESHOLD_BASE + 1 * 9)
#define K8_MCE_THRESHOLD_BANK_2    (MCE_THRESHOLD_BASE + 2 * 9)
#define K8_MCE_THRESHOLD_BANK_3    (MCE_THRESHOLD_BASE + 3 * 9)
#define K8_MCE_THRESHOLD_BANK_4    (MCE_THRESHOLD_BASE + 4 * 9)
#define K8_MCE_THRESHOLD_BANK_5    (MCE_THRESHOLD_BASE + 5 * 9)
#define K8_MCE_THRESHOLD_DRAM_ECC  (MCE_THRESHOLD_BANK_4 + 0)

#endif /* __x86_64__ */

#ifdef __KERNEL__

#ifdef CONFIG_X86_32
#ifdef CONFIG_X86_MCE
extern void mcheck_init(struct cpuinfo_x86 *c);
#else
#define mcheck_init(c) do {} while(0)
#endif

extern int mce_disabled;

#else /* CONFIG_X86_32 */

#include <asm/atomic.h>

void mce_log(struct mce *m);
DECLARE_PER_CPU(struct sys_device, device_mce);

#ifdef CONFIG_X86_MCE_INTEL
void mce_intel_feature_init(struct cpuinfo_x86 *c);
#else
static inline void mce_intel_feature_init(struct cpuinfo_x86 *c) { }
#endif

#ifdef CONFIG_X86_MCE_AMD
void mce_amd_feature_init(struct cpuinfo_x86 *c);
#else
static inline void mce_amd_feature_init(struct cpuinfo_x86 *c) { }
#endif

void mce_log_therm_throt_event(unsigned int cpu, __u64 status);

extern atomic_t mce_entry;

extern void do_machine_check(struct pt_regs *, long);
extern int mce_notify_user(void);

#endif /* !CONFIG_X86_32 */

extern void stop_mce(void);
extern void restart_mce(void);

#endif /* __KERNEL__ */

#endif
