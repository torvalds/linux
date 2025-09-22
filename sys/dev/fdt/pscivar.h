/* Public Domain */

#ifndef _SYS_DEV_FDT_PSCIVAR_H_
#define _SYS_DEV_FDT_PSCIVAR_H_

#define PSCI_SUCCESS		0
#define PSCI_NOT_SUPPORTED	-1

#define PSCI_METHOD_NONE	0
#define PSCI_METHOD_HVC		1
#define PSCI_METHOD_SMC		2

#define PSCI_VERSION		0x84000000
#ifdef __LP64__
#define CPU_SUSPEND		0xc4000001
#else
#define CPU_SUSPEND		0x84000001
#endif
#define CPU_OFF			0x84000002
#ifdef __LP64__
#define CPU_ON			0xc4000003
#else
#define CPU_ON			0x84000003
#endif
#define SYSTEM_OFF		0x84000008
#define SYSTEM_RESET		0x84000009
#define PSCI_FEATURES		0x8400000a
#ifdef __LP64__
#define SYSTEM_SUSPEND		0xc400000e
#else
#define SYSTEM_SUSPEND		0x8400000e
#endif

#define PSCI_FEATURE_POWER_STATE_EXT		(1 << 1)
#define  PSCI_POWER_STATE_POWERDOWN		(1 << 16)
#define  PSCI_POWER_STATE_EXT_POWERDOWN		(1 << 30)

int	psci_can_suspend(void);

int32_t	psci_system_suspend(register_t, register_t);
int32_t	psci_cpu_on(register_t, register_t, register_t);
int32_t	psci_cpu_off(void);
int32_t	psci_cpu_suspend(register_t, register_t, register_t);
int32_t	psci_features(uint32_t);
void	psci_flush_bp(void);
int	psci_method(void);

int32_t	smccc(uint32_t, register_t, register_t, register_t);

void	smccc_enable_arch_workaround_2(void);
int	smccc_needs_arch_workaround_3(void);

#endif /* _SYS_DEV_FDT_PSCIVAR_H_ */
