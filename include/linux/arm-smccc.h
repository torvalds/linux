/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, Linaro Limited
 */
#ifndef __LINUX_ARM_SMCCC_H
#define __LINUX_ARM_SMCCC_H

#include <linux/init.h>
#include <uapi/linux/const.h>

/*
 * This file provides common defines for ARM SMC Calling Convention as
 * specified in
 * https://developer.arm.com/docs/den0028/latest
 *
 * This code is up-to-date with version DEN 0028 C
 */

#define ARM_SMCCC_STD_CALL	        _AC(0,U)
#define ARM_SMCCC_FAST_CALL	        _AC(1,U)
#define ARM_SMCCC_TYPE_SHIFT		31

#define ARM_SMCCC_SMC_32		0
#define ARM_SMCCC_SMC_64		1
#define ARM_SMCCC_CALL_CONV_SHIFT	30

#define ARM_SMCCC_OWNER_MASK		0x3F
#define ARM_SMCCC_OWNER_SHIFT		24

#define ARM_SMCCC_FUNC_MASK		0xFFFF

#define ARM_SMCCC_IS_FAST_CALL(smc_val)	\
	((smc_val) & (ARM_SMCCC_FAST_CALL << ARM_SMCCC_TYPE_SHIFT))
#define ARM_SMCCC_IS_64(smc_val) \
	((smc_val) & (ARM_SMCCC_SMC_64 << ARM_SMCCC_CALL_CONV_SHIFT))
#define ARM_SMCCC_FUNC_NUM(smc_val)	((smc_val) & ARM_SMCCC_FUNC_MASK)
#define ARM_SMCCC_OWNER_NUM(smc_val) \
	(((smc_val) >> ARM_SMCCC_OWNER_SHIFT) & ARM_SMCCC_OWNER_MASK)

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num) \
	(((type) << ARM_SMCCC_TYPE_SHIFT) | \
	((calling_convention) << ARM_SMCCC_CALL_CONV_SHIFT) | \
	(((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) | \
	((func_num) & ARM_SMCCC_FUNC_MASK))

#define ARM_SMCCC_OWNER_ARCH		0
#define ARM_SMCCC_OWNER_CPU		1
#define ARM_SMCCC_OWNER_SIP		2
#define ARM_SMCCC_OWNER_OEM		3
#define ARM_SMCCC_OWNER_STANDARD	4
#define ARM_SMCCC_OWNER_STANDARD_HYP	5
#define ARM_SMCCC_OWNER_VENDOR_HYP	6
#define ARM_SMCCC_OWNER_TRUSTED_APP	48
#define ARM_SMCCC_OWNER_TRUSTED_APP_END	49
#define ARM_SMCCC_OWNER_TRUSTED_OS	50
#define ARM_SMCCC_OWNER_TRUSTED_OS_END	63

#define ARM_SMCCC_FUNC_QUERY_CALL_UID  0xff01

#define ARM_SMCCC_QUIRK_NONE		0
#define ARM_SMCCC_QUIRK_QCOM_A6		1 /* Save/restore register a6 */

#define ARM_SMCCC_VERSION_1_0		0x10000
#define ARM_SMCCC_VERSION_1_1		0x10001
#define ARM_SMCCC_VERSION_1_2		0x10002
#define ARM_SMCCC_VERSION_1_3		0x10003

#define ARM_SMCCC_1_3_SVE_HINT		0x10000

#define ARM_SMCCC_VERSION_FUNC_ID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0)

#define ARM_SMCCC_ARCH_FEATURES_FUNC_ID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 1)

#define ARM_SMCCC_ARCH_SOC_ID						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 2)

#define ARM_SMCCC_ARCH_WORKAROUND_1					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0x8000)

#define ARM_SMCCC_ARCH_WORKAROUND_2					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0x7fff)

#define ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_FUNC_QUERY_CALL_UID)

/* KVM UID value: 28b46fb6-2ec5-11e9-a9ca-4b564d003a74 */
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0	0xb66fb428U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1	0xe911c52eU
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2	0x564bcaa9U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3	0x743a004dU

/* KVM "vendor specific" services */
#define ARM_SMCCC_KVM_FUNC_FEATURES		0
#define ARM_SMCCC_KVM_FUNC_PTP			1
#define ARM_SMCCC_KVM_FUNC_FEATURES_2		127
#define ARM_SMCCC_KVM_NUM_FUNCS			128

#define ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_FEATURES)

#define SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED	1

/*
 * ptp_kvm is a feature used for time sync between vm and host.
 * ptp_kvm module in guest kernel will get service from host using
 * this hypercall ID.
 */
#define ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_PTP)

/* ptp_kvm counter type ID */
#define KVM_PTP_VIRT_COUNTER			0
#define KVM_PTP_PHYS_COUNTER			1

/* Paravirtualised time calls (defined by ARM DEN0057A) */
#define ARM_SMCCC_HV_PV_TIME_FEATURES				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD_HYP,	\
			   0x20)

#define ARM_SMCCC_HV_PV_TIME_ST					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD_HYP,	\
			   0x21)

/* TRNG entropy source calls (defined by ARM DEN0098) */
#define ARM_SMCCC_TRNG_VERSION					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x50)

#define ARM_SMCCC_TRNG_FEATURES					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x51)

#define ARM_SMCCC_TRNG_GET_UUID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x52)

#define ARM_SMCCC_TRNG_RND32					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x53)

#define ARM_SMCCC_TRNG_RND64					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x53)

/*
 * Return codes defined in ARM DEN 0070A
 * ARM DEN 0070A is now merged/consolidated into ARM DEN 0028 C
 */
#define SMCCC_RET_SUCCESS			0
#define SMCCC_RET_NOT_SUPPORTED			-1
#define SMCCC_RET_NOT_REQUIRED			-2
#define SMCCC_RET_INVALID_PARAMETER		-3

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/types.h>

enum arm_smccc_conduit {
	SMCCC_CONDUIT_NONE,
	SMCCC_CONDUIT_SMC,
	SMCCC_CONDUIT_HVC,
};

/**
 * arm_smccc_1_1_get_conduit()
 *
 * Returns the conduit to be used for SMCCCv1.1 or later.
 *
 * When SMCCCv1.1 is not present, returns SMCCC_CONDUIT_NONE.
 */
enum arm_smccc_conduit arm_smccc_1_1_get_conduit(void);

/**
 * arm_smccc_get_version()
 *
 * Returns the version to be used for SMCCCv1.1 or later.
 *
 * When SMCCCv1.1 or above is not present, returns SMCCCv1.0, but this
 * does not imply the presence of firmware or a valid conduit. Caller
 * handling SMCCCv1.0 must determine the conduit by other means.
 */
u32 arm_smccc_get_version(void);

void __init arm_smccc_version_init(u32 version, enum arm_smccc_conduit conduit);

extern u64 smccc_has_sve_hint;

/**
 * struct arm_smccc_res - Result from SMC/HVC call
 * @a0-a3 result values from registers 0 to 3
 */
struct arm_smccc_res {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
};

#ifdef CONFIG_ARM64
/**
 * struct arm_smccc_1_2_regs - Arguments for or Results from SMC/HVC call
 * @a0-a17 argument values from registers 0 to 17
 */
struct arm_smccc_1_2_regs {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
	unsigned long a8;
	unsigned long a9;
	unsigned long a10;
	unsigned long a11;
	unsigned long a12;
	unsigned long a13;
	unsigned long a14;
	unsigned long a15;
	unsigned long a16;
	unsigned long a17;
};

/**
 * arm_smccc_1_2_hvc() - make HVC calls
 * @args: arguments passed via struct arm_smccc_1_2_regs
 * @res: result values via struct arm_smccc_1_2_regs
 *
 * This function is used to make HVC calls following SMC Calling Convention
 * v1.2 or above. The content of the supplied param are copied from the
 * structure to registers prior to the HVC instruction. The return values
 * are updated with the content from registers on return from the HVC
 * instruction.
 */
asmlinkage void arm_smccc_1_2_hvc(const struct arm_smccc_1_2_regs *args,
				  struct arm_smccc_1_2_regs *res);

/**
 * arm_smccc_1_2_smc() - make SMC calls
 * @args: arguments passed via struct arm_smccc_1_2_regs
 * @res: result values via struct arm_smccc_1_2_regs
 *
 * This function is used to make SMC calls following SMC Calling Convention
 * v1.2 or above. The content of the supplied param are copied from the
 * structure to registers prior to the SMC instruction. The return values
 * are updated with the content from registers on return from the SMC
 * instruction.
 */
asmlinkage void arm_smccc_1_2_smc(const struct arm_smccc_1_2_regs *args,
				  struct arm_smccc_1_2_regs *res);
#endif

/**
 * struct arm_smccc_quirk - Contains quirk information
 * @id: quirk identification
 * @state: quirk specific information
 * @a6: Qualcomm quirk entry for returning post-smc call contents of a6
 */
struct arm_smccc_quirk {
	int	id;
	union {
		unsigned long a6;
	} state;
};

/**
 * __arm_smccc_sve_check() - Set the SVE hint bit when doing SMC calls
 *
 * Sets the SMCCC hint bit to indicate if there is live state in the SVE
 * registers, this modifies x0 in place and should never be called from C
 * code.
 */
asmlinkage unsigned long __arm_smccc_sve_check(unsigned long x0);

/**
 * __arm_smccc_smc() - make SMC calls
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 * @quirk: points to an arm_smccc_quirk, or NULL when no quirks are required.
 *
 * This function is used to make SMC calls following SMC Calling Convention.
 * The content of the supplied param are copied to registers 0 to 7 prior
 * to the SMC instruction. The return values are updated with the content
 * from register 0 to 3 on return from the SMC instruction.  An optional
 * quirk structure provides vendor specific behavior.
 */
asmlinkage void __arm_smccc_smc(unsigned long a0, unsigned long a1,
			unsigned long a2, unsigned long a3, unsigned long a4,
			unsigned long a5, unsigned long a6, unsigned long a7,
			struct arm_smccc_res *res, struct arm_smccc_quirk *quirk);

/**
 * __arm_smccc_hvc() - make HVC calls
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 * @quirk: points to an arm_smccc_quirk, or NULL when no quirks are required.
 *
 * This function is used to make HVC calls following SMC Calling
 * Convention.  The content of the supplied param are copied to registers 0
 * to 7 prior to the HVC instruction. The return values are updated with
 * the content from register 0 to 3 on return from the HVC instruction.  An
 * optional quirk structure provides vendor specific behavior.
 */
asmlinkage void __arm_smccc_hvc(unsigned long a0, unsigned long a1,
			unsigned long a2, unsigned long a3, unsigned long a4,
			unsigned long a5, unsigned long a6, unsigned long a7,
			struct arm_smccc_res *res, struct arm_smccc_quirk *quirk);

#define arm_smccc_smc(...) __arm_smccc_smc(__VA_ARGS__, NULL)

#define arm_smccc_smc_quirk(...) __arm_smccc_smc(__VA_ARGS__)

#define arm_smccc_hvc(...) __arm_smccc_hvc(__VA_ARGS__, NULL)

#define arm_smccc_hvc_quirk(...) __arm_smccc_hvc(__VA_ARGS__)

/* SMCCC v1.1 implementation madness follows */
#ifdef CONFIG_ARM64

#define SMCCC_SMC_INST	"smc	#0"
#define SMCCC_HVC_INST	"hvc	#0"

#elif defined(CONFIG_ARM)
#include <asm/opcodes-sec.h>
#include <asm/opcodes-virt.h>

#define SMCCC_SMC_INST	__SMC(0)
#define SMCCC_HVC_INST	__HVC(0)

#endif

/* nVHE hypervisor doesn't have a current thread so needs separate checks */
#if defined(CONFIG_ARM64_SVE) && !defined(__KVM_NVHE_HYPERVISOR__)

#define SMCCC_SVE_CHECK ALTERNATIVE("nop \n",  "bl __arm_smccc_sve_check \n", \
				    ARM64_SVE)
#define smccc_sve_clobbers "x16", "x30", "cc",

#else

#define SMCCC_SVE_CHECK
#define smccc_sve_clobbers

#endif

#define ___count_args(_0, _1, _2, _3, _4, _5, _6, _7, _8, x, ...) x

#define __count_args(...)						\
	___count_args(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0)

#define __constraint_read_0	"r" (arg0)
#define __constraint_read_1	__constraint_read_0, "r" (arg1)
#define __constraint_read_2	__constraint_read_1, "r" (arg2)
#define __constraint_read_3	__constraint_read_2, "r" (arg3)
#define __constraint_read_4	__constraint_read_3, "r" (arg4)
#define __constraint_read_5	__constraint_read_4, "r" (arg5)
#define __constraint_read_6	__constraint_read_5, "r" (arg6)
#define __constraint_read_7	__constraint_read_6, "r" (arg7)

#define __declare_arg_0(a0, res)					\
	struct arm_smccc_res   *___res = res;				\
	register unsigned long arg0 asm("r0") = (u32)a0

#define __declare_arg_1(a0, a1, res)					\
	typeof(a1) __a1 = a1;						\
	struct arm_smccc_res   *___res = res;				\
	register unsigned long arg0 asm("r0") = (u32)a0;			\
	register typeof(a1) arg1 asm("r1") = __a1

#define __declare_arg_2(a0, a1, a2, res)				\
	typeof(a1) __a1 = a1;						\
	typeof(a2) __a2 = a2;						\
	struct arm_smccc_res   *___res = res;				\
	register unsigned long arg0 asm("r0") = (u32)a0;			\
	register typeof(a1) arg1 asm("r1") = __a1;			\
	register typeof(a2) arg2 asm("r2") = __a2

#define __declare_arg_3(a0, a1, a2, a3, res)				\
	typeof(a1) __a1 = a1;						\
	typeof(a2) __a2 = a2;						\
	typeof(a3) __a3 = a3;						\
	struct arm_smccc_res   *___res = res;				\
	register unsigned long arg0 asm("r0") = (u32)a0;			\
	register typeof(a1) arg1 asm("r1") = __a1;			\
	register typeof(a2) arg2 asm("r2") = __a2;			\
	register typeof(a3) arg3 asm("r3") = __a3

#define __declare_arg_4(a0, a1, a2, a3, a4, res)			\
	typeof(a4) __a4 = a4;						\
	__declare_arg_3(a0, a1, a2, a3, res);				\
	register typeof(a4) arg4 asm("r4") = __a4

#define __declare_arg_5(a0, a1, a2, a3, a4, a5, res)			\
	typeof(a5) __a5 = a5;						\
	__declare_arg_4(a0, a1, a2, a3, a4, res);			\
	register typeof(a5) arg5 asm("r5") = __a5

#define __declare_arg_6(a0, a1, a2, a3, a4, a5, a6, res)		\
	typeof(a6) __a6 = a6;						\
	__declare_arg_5(a0, a1, a2, a3, a4, a5, res);			\
	register typeof(a6) arg6 asm("r6") = __a6

#define __declare_arg_7(a0, a1, a2, a3, a4, a5, a6, a7, res)		\
	typeof(a7) __a7 = a7;						\
	__declare_arg_6(a0, a1, a2, a3, a4, a5, a6, res);		\
	register typeof(a7) arg7 asm("r7") = __a7

#define ___declare_args(count, ...) __declare_arg_ ## count(__VA_ARGS__)
#define __declare_args(count, ...)  ___declare_args(count, __VA_ARGS__)

#define ___constraints(count)						\
	: __constraint_read_ ## count					\
	: smccc_sve_clobbers "memory"
#define __constraints(count)	___constraints(count)

/*
 * We have an output list that is not necessarily used, and GCC feels
 * entitled to optimise the whole sequence away. "volatile" is what
 * makes it stick.
 */
#define __arm_smccc_1_1(inst, ...)					\
	do {								\
		register unsigned long r0 asm("r0");			\
		register unsigned long r1 asm("r1");			\
		register unsigned long r2 asm("r2");			\
		register unsigned long r3 asm("r3"); 			\
		__declare_args(__count_args(__VA_ARGS__), __VA_ARGS__);	\
		asm volatile(SMCCC_SVE_CHECK				\
			     inst "\n" :				\
			     "=r" (r0), "=r" (r1), "=r" (r2), "=r" (r3)	\
			     __constraints(__count_args(__VA_ARGS__)));	\
		if (___res)						\
			*___res = (typeof(*___res)){r0, r1, r2, r3};	\
	} while (0)

/*
 * arm_smccc_1_1_smc() - make an SMCCC v1.1 compliant SMC call
 *
 * This is a variadic macro taking one to eight source arguments, and
 * an optional return structure.
 *
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 *
 * This macro is used to make SMC calls following SMC Calling Convention v1.1.
 * The content of the supplied param are copied to registers 0 to 7 prior
 * to the SMC instruction. The return values are updated with the content
 * from register 0 to 3 on return from the SMC instruction if not NULL.
 */
#define arm_smccc_1_1_smc(...)	__arm_smccc_1_1(SMCCC_SMC_INST, __VA_ARGS__)

/*
 * arm_smccc_1_1_hvc() - make an SMCCC v1.1 compliant HVC call
 *
 * This is a variadic macro taking one to eight source arguments, and
 * an optional return structure.
 *
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 *
 * This macro is used to make HVC calls following SMC Calling Convention v1.1.
 * The content of the supplied param are copied to registers 0 to 7 prior
 * to the HVC instruction. The return values are updated with the content
 * from register 0 to 3 on return from the HVC instruction if not NULL.
 */
#define arm_smccc_1_1_hvc(...)	__arm_smccc_1_1(SMCCC_HVC_INST, __VA_ARGS__)

/*
 * Like arm_smccc_1_1* but always returns SMCCC_RET_NOT_SUPPORTED.
 * Used when the SMCCC conduit is not defined. The empty asm statement
 * avoids compiler warnings about unused variables.
 */
#define __fail_smccc_1_1(...)						\
	do {								\
		__declare_args(__count_args(__VA_ARGS__), __VA_ARGS__);	\
		asm ("" : __constraints(__count_args(__VA_ARGS__)));	\
		if (___res)						\
			___res->a0 = SMCCC_RET_NOT_SUPPORTED;		\
	} while (0)

/*
 * arm_smccc_1_1_invoke() - make an SMCCC v1.1 compliant call
 *
 * This is a variadic macro taking one to eight source arguments, and
 * an optional return structure.
 *
 * @a0-a7: arguments passed in registers 0 to 7
 * @res: result values from registers 0 to 3
 *
 * This macro will make either an HVC call or an SMC call depending on the
 * current SMCCC conduit. If no valid conduit is available then -1
 * (SMCCC_RET_NOT_SUPPORTED) is returned in @res.a0 (if supplied).
 *
 * The return value also provides the conduit that was used.
 */
#define arm_smccc_1_1_invoke(...) ({					\
		int method = arm_smccc_1_1_get_conduit();		\
		switch (method) {					\
		case SMCCC_CONDUIT_HVC:					\
			arm_smccc_1_1_hvc(__VA_ARGS__);			\
			break;						\
		case SMCCC_CONDUIT_SMC:					\
			arm_smccc_1_1_smc(__VA_ARGS__);			\
			break;						\
		default:						\
			__fail_smccc_1_1(__VA_ARGS__);			\
			method = SMCCC_CONDUIT_NONE;			\
			break;						\
		}							\
		method;							\
	})

#endif /*__ASSEMBLY__*/
#endif /*__LINUX_ARM_SMCCC_H*/
