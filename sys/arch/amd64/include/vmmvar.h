/*	$OpenBSD: vmmvar.h,v 1.117 2025/09/17 18:37:44 sf Exp $	*/
/*
 * Copyright (c) 2014 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * CPU capabilities for VMM operation
 */
#ifndef _MACHINE_VMMVAR_H_
#define _MACHINE_VMMVAR_H_

#ifndef _LOCORE

#define VMM_HV_SIGNATURE 	"OpenBSDVMM58"

/* VMX: Basic Exit Reasons */
#define VMX_EXIT_NMI				0
#define VMX_EXIT_EXTINT				1
#define VMX_EXIT_TRIPLE_FAULT			2
#define VMX_EXIT_INIT				3
#define VMX_EXIT_SIPI				4
#define VMX_EXIT_IO_SMI				5
#define VMX_EXIT_OTHER_SMI			6
#define VMX_EXIT_INT_WINDOW			7
#define VMX_EXIT_NMI_WINDOW			8
#define VMX_EXIT_TASK_SWITCH			9
#define VMX_EXIT_CPUID				10
#define VMX_EXIT_GETSEC				11
#define VMX_EXIT_HLT				12
#define VMX_EXIT_INVD				13
#define VMX_EXIT_INVLPG				14
#define VMX_EXIT_RDPMC				15
#define VMX_EXIT_RDTSC				16
#define VMX_EXIT_RSM				17
#define VMX_EXIT_VMCALL				18
#define VMX_EXIT_VMCLEAR			19
#define VMX_EXIT_VMLAUNCH			20
#define VMX_EXIT_VMPTRLD			21
#define VMX_EXIT_VMPTRST			22
#define VMX_EXIT_VMREAD				23
#define VMX_EXIT_VMRESUME			24
#define VMX_EXIT_VMWRITE			25
#define VMX_EXIT_VMXOFF				26
#define VMX_EXIT_VMXON				27
#define VMX_EXIT_CR_ACCESS			28
#define VMX_EXIT_MOV_DR				29
#define VMX_EXIT_IO				30
#define VMX_EXIT_RDMSR				31
#define VMX_EXIT_WRMSR				32
#define VMX_EXIT_ENTRY_FAILED_GUEST_STATE	33
#define VMX_EXIT_ENTRY_FAILED_MSR_LOAD		34
#define VMX_EXIT_MWAIT				36
#define VMX_EXIT_MTF				37
#define VMX_EXIT_MONITOR			39
#define VMX_EXIT_PAUSE				40
#define VMX_EXIT_ENTRY_FAILED_MCE		41
#define VMX_EXIT_TPR_BELOW_THRESHOLD		43
#define VMX_EXIT_APIC_ACCESS			44
#define VMX_EXIT_VIRTUALIZED_EOI		45
#define VMX_EXIT_GDTR_IDTR			46
#define	VMX_EXIT_LDTR_TR			47
#define VMX_EXIT_EPT_VIOLATION			48
#define VMX_EXIT_EPT_MISCONFIGURATION		49
#define VMX_EXIT_INVEPT				50
#define VMX_EXIT_RDTSCP				51
#define VMX_EXIT_VMX_PREEMPTION_TIMER_EXPIRED	52
#define VMX_EXIT_INVVPID			53
#define VMX_EXIT_WBINVD				54
#define VMX_EXIT_XSETBV				55
#define VMX_EXIT_APIC_WRITE			56
#define VMX_EXIT_RDRAND				57
#define VMX_EXIT_INVPCID			58
#define VMX_EXIT_VMFUNC				59
#define VMX_EXIT_RDSEED				61
#define VMX_EXIT_XSAVES				63
#define VMX_EXIT_XRSTORS			64

#define VM_EXIT_TERMINATED			0xFFFE
#define VM_EXIT_NONE				0xFFFF

/*
 * VMX: Misc defines
 */
#define VMX_MAX_CR3_TARGETS			256
#define VMX_VMCS_PA_CLEAR			0xFFFFFFFFFFFFFFFFUL

#endif	/* ! _LOCORE */

/*
 * SVM: Intercept codes (exit reasons)
 */
#define SVM_VMEXIT_CR0_READ			0x00
#define SVM_VMEXIT_CR1_READ			0x01
#define SVM_VMEXIT_CR2_READ			0x02
#define SVM_VMEXIT_CR3_READ			0x03
#define SVM_VMEXIT_CR4_READ			0x04
#define SVM_VMEXIT_CR5_READ			0x05
#define SVM_VMEXIT_CR6_READ			0x06
#define SVM_VMEXIT_CR7_READ			0x07
#define SVM_VMEXIT_CR8_READ			0x08
#define SVM_VMEXIT_CR9_READ			0x09
#define SVM_VMEXIT_CR10_READ			0x0A
#define SVM_VMEXIT_CR11_READ			0x0B
#define SVM_VMEXIT_CR12_READ			0x0C
#define SVM_VMEXIT_CR13_READ			0x0D
#define SVM_VMEXIT_CR14_READ			0x0E
#define SVM_VMEXIT_CR15_READ			0x0F
#define SVM_VMEXIT_CR0_WRITE			0x10
#define SVM_VMEXIT_CR1_WRITE			0x11
#define SVM_VMEXIT_CR2_WRITE			0x12
#define SVM_VMEXIT_CR3_WRITE			0x13
#define SVM_VMEXIT_CR4_WRITE			0x14
#define SVM_VMEXIT_CR5_WRITE			0x15
#define SVM_VMEXIT_CR6_WRITE			0x16
#define SVM_VMEXIT_CR7_WRITE			0x17
#define SVM_VMEXIT_CR8_WRITE			0x18
#define SVM_VMEXIT_CR9_WRITE			0x19
#define SVM_VMEXIT_CR10_WRITE			0x1A
#define SVM_VMEXIT_CR11_WRITE			0x1B
#define SVM_VMEXIT_CR12_WRITE			0x1C
#define SVM_VMEXIT_CR13_WRITE			0x1D
#define SVM_VMEXIT_CR14_WRITE			0x1E
#define SVM_VMEXIT_CR15_WRITE			0x1F
#define SVM_VMEXIT_DR0_READ			0x20
#define SVM_VMEXIT_DR1_READ			0x21
#define SVM_VMEXIT_DR2_READ			0x22
#define SVM_VMEXIT_DR3_READ			0x23
#define SVM_VMEXIT_DR4_READ			0x24
#define SVM_VMEXIT_DR5_READ			0x25
#define SVM_VMEXIT_DR6_READ			0x26
#define SVM_VMEXIT_DR7_READ			0x27
#define SVM_VMEXIT_DR8_READ			0x28
#define SVM_VMEXIT_DR9_READ			0x29
#define SVM_VMEXIT_DR10_READ			0x2A
#define SVM_VMEXIT_DR11_READ			0x2B
#define SVM_VMEXIT_DR12_READ			0x2C
#define SVM_VMEXIT_DR13_READ			0x2D
#define SVM_VMEXIT_DR14_READ			0x2E
#define SVM_VMEXIT_DR15_READ			0x2F
#define SVM_VMEXIT_DR0_WRITE			0x30
#define SVM_VMEXIT_DR1_WRITE			0x31
#define SVM_VMEXIT_DR2_WRITE			0x32
#define SVM_VMEXIT_DR3_WRITE			0x33
#define SVM_VMEXIT_DR4_WRITE			0x34
#define SVM_VMEXIT_DR5_WRITE			0x35
#define SVM_VMEXIT_DR6_WRITE			0x36
#define SVM_VMEXIT_DR7_WRITE			0x37
#define SVM_VMEXIT_DR8_WRITE			0x38
#define SVM_VMEXIT_DR9_WRITE			0x39
#define SVM_VMEXIT_DR10_WRITE			0x3A
#define SVM_VMEXIT_DR11_WRITE			0x3B
#define SVM_VMEXIT_DR12_WRITE			0x3C
#define SVM_VMEXIT_DR13_WRITE			0x3D
#define SVM_VMEXIT_DR14_WRITE			0x3E
#define SVM_VMEXIT_DR15_WRITE			0x3F
#define SVM_VMEXIT_EXCP0			0x40
#define SVM_VMEXIT_EXCP1			0x41
#define SVM_VMEXIT_EXCP2			0x42
#define SVM_VMEXIT_EXCP3			0x43
#define SVM_VMEXIT_EXCP4			0x44
#define SVM_VMEXIT_EXCP5			0x45
#define SVM_VMEXIT_EXCP6			0x46
#define SVM_VMEXIT_EXCP7			0x47
#define SVM_VMEXIT_EXCP8			0x48
#define SVM_VMEXIT_EXCP9			0x49
#define SVM_VMEXIT_EXCP10			0x4A
#define SVM_VMEXIT_EXCP11			0x4B
#define SVM_VMEXIT_EXCP12			0x4C
#define SVM_VMEXIT_EXCP13			0x4D
#define SVM_VMEXIT_EXCP14			0x4E
#define SVM_VMEXIT_EXCP15			0x4F
#define SVM_VMEXIT_EXCP16			0x50
#define SVM_VMEXIT_EXCP17			0x51
#define SVM_VMEXIT_EXCP18			0x52
#define SVM_VMEXIT_EXCP19			0x53
#define SVM_VMEXIT_EXCP20			0x54
#define SVM_VMEXIT_EXCP21			0x55
#define SVM_VMEXIT_EXCP22			0x56
#define SVM_VMEXIT_EXCP23			0x57
#define SVM_VMEXIT_EXCP24			0x58
#define SVM_VMEXIT_EXCP25			0x59
#define SVM_VMEXIT_EXCP26			0x5A
#define SVM_VMEXIT_EXCP27			0x5B
#define SVM_VMEXIT_EXCP28			0x5C
#define SVM_VMEXIT_EXCP29			0x5D
#define SVM_VMEXIT_EXCP30			0x5E
#define SVM_VMEXIT_EXCP31			0x5F
#define SVM_VMEXIT_INTR				0x60
#define SVM_VMEXIT_NMI				0x61
#define SVM_VMEXIT_SMI				0x62
#define SVM_VMEXIT_INIT				0x63
#define SVM_VMEXIT_VINTR			0x64
#define SVM_VMEXIT_CR0_SEL_WRITE		0x65
#define SVM_VMEXIT_IDTR_READ			0x66
#define SVM_VMEXIT_GDTR_READ			0x67
#define SVM_VMEXIT_LDTR_READ			0x68
#define SVM_VMEXIT_TR_READ			0x69
#define SVM_VMEXIT_IDTR_WRITE			0x6A
#define SVM_VMEXIT_GDTR_WRITE			0x6B
#define SVM_VMEXIT_LDTR_WRITE			0x6C
#define SVM_VMEXIT_TR_WRITE			0x6D
#define SVM_VMEXIT_RDTSC			0x6E
#define SVM_VMEXIT_RDPMC			0x6F
#define SVM_VMEXIT_PUSHF			0x70
#define SVM_VMEXIT_POPF				0x71
#define SVM_VMEXIT_CPUID			0x72
#define SVM_VMEXIT_RSM				0x73
#define SVM_VMEXIT_IRET				0x74
#define SVM_VMEXIT_SWINT			0x75
#define SVM_VMEXIT_INVD				0x76
#define SVM_VMEXIT_PAUSE			0x77
#define SVM_VMEXIT_HLT				0x78
#define SVM_VMEXIT_INVLPG			0x79
#define SVM_VMEXIT_INVLPGA			0x7A
#define SVM_VMEXIT_IOIO				0x7B
#define SVM_VMEXIT_MSR				0x7C
#define SVM_VMEXIT_TASK_SWITCH			0x7D
#define SVM_VMEXIT_FERR_FREEZE			0x7E
#define SVM_VMEXIT_SHUTDOWN			0x7F
#define SVM_VMEXIT_VMRUN			0x80
#define SVM_VMEXIT_VMMCALL			0x81
#define SVM_VMEXIT_VMLOAD			0x82
#define SVM_VMEXIT_VMSAVE			0x83
#define SVM_VMEXIT_STGI				0x84
#define SVM_VMEXIT_CLGI				0x85
#define SVM_VMEXIT_SKINIT			0x86
#define SVM_VMEXIT_RDTSCP			0x87
#define SVM_VMEXIT_ICEBP			0x88
#define SVM_VMEXIT_WBINVD			0x89
#define SVM_VMEXIT_MONITOR			0x8A
#define SVM_VMEXIT_MWAIT			0x8B
#define SVM_VMEXIT_MWAIT_CONDITIONAL		0x8C
#define SVM_VMEXIT_XSETBV			0x8D
#define SVM_VMEXIT_EFER_WRITE_TRAP		0x8F
#define SVM_VMEXIT_CR0_WRITE_TRAP		0x90
#define SVM_VMEXIT_CR1_WRITE_TRAP		0x91
#define SVM_VMEXIT_CR2_WRITE_TRAP		0x92
#define SVM_VMEXIT_CR3_WRITE_TRAP		0x93
#define SVM_VMEXIT_CR4_WRITE_TRAP		0x94
#define SVM_VMEXIT_CR5_WRITE_TRAP		0x95
#define SVM_VMEXIT_CR6_WRITE_TRAP		0x96
#define SVM_VMEXIT_CR7_WRITE_TRAP		0x97
#define SVM_VMEXIT_CR8_WRITE_TRAP		0x98
#define SVM_VMEXIT_CR9_WRITE_TRAP		0x99
#define SVM_VMEXIT_CR10_WRITE_TRAP		0x9A
#define SVM_VMEXIT_CR11_WRITE_TRAP		0x9B
#define SVM_VMEXIT_CR12_WRITE_TRAP		0x9C
#define SVM_VMEXIT_CR13_WRITE_TRAP		0x9D
#define SVM_VMEXIT_CR14_WRITE_TRAP		0x9E
#define SVM_VMEXIT_CR15_WRITE_TRAP		0x9F
#define SVM_VMEXIT_NPF				0x400
#define SVM_AVIC_INCOMPLETE_IPI			0x401
#define SVM_AVIC_NOACCEL			0x402
#define SVM_VMEXIT_VMGEXIT			0x403
#define SVM_VMEXIT_INVALID			-1

/*
 *  Additional VMEXIT codes used in SEV-ES/SNP in the GHCB
 */
#define SEV_VMGEXIT_MMIO_READ			0x80000001
#define SEV_VMGEXIT_MMIO_WRITE			0x80000002

#ifndef _LOCORE

/*
 * Exception injection vectors (these correspond to the CPU exception types
 * defined in the SDM.)
 */
#define VMM_EX_DE	0	/* Divide Error #DE */
#define VMM_EX_DB	1	/* Debug Exception #DB */
#define VMM_EX_NMI	2	/* NMI */
#define VMM_EX_BP	3	/* Breakpoint #BP */
#define VMM_EX_OF	4	/* Overflow #OF */
#define VMM_EX_BR	5	/* Bound range exceeded #BR */
#define VMM_EX_UD	6	/* Undefined opcode #UD */
#define VMM_EX_NM	7	/* Device not available #NM */
#define VMM_EX_DF	8	/* Double fault #DF */
#define VMM_EX_CP	9	/* Coprocessor segment overrun (unused) */
#define VMM_EX_TS	10	/* Invalid TSS #TS */
#define VMM_EX_NP	11	/* Segment not present #NP */
#define VMM_EX_SS	12	/* Stack segment fault #SS */
#define VMM_EX_GP	13	/* General protection #GP */
#define VMM_EX_PF	14	/* Page fault #PF */
#define VMM_EX_MF	16	/* x87 FPU floating point error #MF */
#define VMM_EX_AC	17	/* Alignment check #AC */
#define VMM_EX_MC	18	/* Machine check #MC */
#define VMM_EX_XM	19	/* SIMD floating point exception #XM */
#define VMM_EX_VE	20	/* Virtualization exception #VE */

enum {
	VEI_DIR_OUT,
	VEI_DIR_IN
};

enum {
	VEE_FAULT_INVALID = 0,
	VEE_FAULT_HANDLED,
	VEE_FAULT_MMIO_ASSIST,
	VEE_FAULT_PROTECT,
};

enum {
	VMM_CPU_MODE_REAL,
	VMM_CPU_MODE_PROT,
	VMM_CPU_MODE_PROT32,
	VMM_CPU_MODE_COMPAT,
	VMM_CPU_MODE_LONG,
	VMM_CPU_MODE_UNKNOWN,
};

struct vmm_softc_md {
	/* Capabilities */
	uint32_t		nr_rvi_cpus;	/* [I] */
	uint32_t		nr_ept_cpus;	/* [I] */
	uint8_t			pkru_enabled;	/* [I] */
};

/*
 * vm exit data
 *  vm_exit_inout		: describes an IN/OUT exit
 */
struct vm_exit_inout {
	uint8_t			vei_size;	/* Size of access */
	uint8_t			vei_dir;	/* Direction */
	uint8_t			vei_rep;	/* REP prefix? */
	uint8_t			vei_string;	/* string variety? */
	uint8_t			vei_encoding;	/* operand encoding */
	uint16_t		vei_port;	/* port */
	uint32_t		vei_data;	/* data */
	uint8_t			vei_insn_len;	/* Count of instruction bytes */
};

/*
 *  vm_exit_eptviolation	: describes an EPT VIOLATION exit
 */
struct vm_exit_eptviolation {
	uint8_t		vee_fault_type;		/* type of vm exit */
	uint8_t		vee_insn_info;		/* bitfield */
#define VEE_LEN_VALID		0x1		/* vee_insn_len is valid */
#define VEE_BYTES_VALID		0x2		/* vee_insn_bytes is valid */
	uint8_t		vee_insn_len;		/* [VMX] instruction length */
	uint8_t		vee_insn_bytes[15];	/* [SVM] bytes at {R,E,}IP */
};

/*
 * struct vcpu_inject_event	: describes an exception or interrupt to inject.
 */
struct vcpu_inject_event {
	uint8_t		vie_vector;	/* Exception or interrupt vector. */
	uint32_t	vie_errorcode;	/* Optional error code. */
	uint8_t		vie_type;
#define VCPU_INJECT_NONE	0
#define VCPU_INJECT_INTR	1	/* External hardware interrupt. */
#define VCPU_INJECT_EX		2	/* HW or SW Exception */
#define VCPU_INJECT_NMI		3	/* Non-maskable Interrupt */
};

/*
 * struct vcpu_segment_info
 *
 * Describes a segment + selector set, used in constructing the initial vcpu
 * register content
 */
struct vcpu_segment_info {
	uint16_t	vsi_sel;
	uint32_t	vsi_limit;
	uint32_t	vsi_ar;
	uint64_t	vsi_base;
};

/* The GPRS are ordered to assist instruction decode. */
#define VCPU_REGS_RAX		0
#define VCPU_REGS_RCX		1
#define VCPU_REGS_RDX		2
#define VCPU_REGS_RBX		3
#define VCPU_REGS_RSP		4
#define VCPU_REGS_RBP		5
#define VCPU_REGS_RSI		6
#define VCPU_REGS_RDI		7
#define VCPU_REGS_R8		8
#define VCPU_REGS_R9		9
#define VCPU_REGS_R10		10
#define VCPU_REGS_R11		11
#define VCPU_REGS_R12		12
#define VCPU_REGS_R13		13
#define VCPU_REGS_R14		14
#define VCPU_REGS_R15		15
#define VCPU_REGS_RIP		16
#define VCPU_REGS_RFLAGS	17
#define VCPU_REGS_NGPRS		(VCPU_REGS_RFLAGS + 1)

#define VCPU_REGS_CR0		0
#define VCPU_REGS_CR2		1
#define VCPU_REGS_CR3		2
#define VCPU_REGS_CR4		3
#define VCPU_REGS_CR8		4
#define VCPU_REGS_XCR0		5
#define VCPU_REGS_PDPTE0 	6
#define VCPU_REGS_PDPTE1 	7
#define VCPU_REGS_PDPTE2 	8
#define VCPU_REGS_PDPTE3 	9
#define VCPU_REGS_NCRS		(VCPU_REGS_PDPTE3 + 1)

#define VCPU_REGS_ES		0
#define VCPU_REGS_CS		1
#define VCPU_REGS_SS		2
#define VCPU_REGS_DS		3
#define VCPU_REGS_FS		4
#define VCPU_REGS_GS		5
#define VCPU_REGS_LDTR		6
#define VCPU_REGS_TR		7
#define VCPU_REGS_NSREGS	(VCPU_REGS_TR + 1)

#define VCPU_REGS_EFER   	0
#define VCPU_REGS_STAR   	1
#define VCPU_REGS_LSTAR  	2
#define VCPU_REGS_CSTAR  	3
#define VCPU_REGS_SFMASK 	4
#define VCPU_REGS_KGSBASE	5
#define VCPU_REGS_MISC_ENABLE	6
#define VCPU_REGS_NMSRS		(VCPU_REGS_MISC_ENABLE + 1)

#define VCPU_REGS_DR0		0
#define VCPU_REGS_DR1		1
#define VCPU_REGS_DR2		2
#define VCPU_REGS_DR3		3
#define VCPU_REGS_DR6		4
#define VCPU_REGS_DR7		5
#define VCPU_REGS_NDRS		(VCPU_REGS_DR7 + 1)

struct vcpu_reg_state {
	uint64_t			vrs_gprs[VCPU_REGS_NGPRS];
	uint64_t			vrs_crs[VCPU_REGS_NCRS];
	uint64_t			vrs_msrs[VCPU_REGS_NMSRS];
	uint64_t			vrs_drs[VCPU_REGS_NDRS];
	struct vcpu_segment_info	vrs_sregs[VCPU_REGS_NSREGS];
	struct vcpu_segment_info	vrs_gdtr;
	struct vcpu_segment_info	vrs_idtr;
};

#define VCPU_HOST_REGS_EFER   		0
#define VCPU_HOST_REGS_STAR   		1
#define VCPU_HOST_REGS_LSTAR  		2
#define VCPU_HOST_REGS_CSTAR  		3
#define VCPU_HOST_REGS_SFMASK 		4
#define VCPU_HOST_REGS_KGSBASE		5
#define VCPU_HOST_REGS_MISC_ENABLE	6
#define VCPU_HOST_REGS_NMSRS		(VCPU_HOST_REGS_MISC_ENABLE + 1)

/*
 * struct vm_exit
 *
 * Contains VM exit information communicated to vmd(8). This information is
 * gathered by vmm(4) from the CPU on each exit that requires help from vmd.
 */
struct vm_exit {
	union {
		struct vm_exit_inout		vei;	/* IN/OUT exit */
		struct vm_exit_eptviolation	vee;	/* EPT VIOLATION exit*/
	};

	struct vcpu_reg_state		vrs;
	int				cpl;
};

struct vm_intr_params {
	/* Input parameters to VMM_IOC_INTR */
	uint32_t		vip_vm_id;
	uint32_t		vip_vcpu_id;
	uint16_t		vip_intr;
};

#define VM_RWREGS_GPRS	0x1	/* read/write GPRs */
#define VM_RWREGS_SREGS	0x2	/* read/write segment registers */
#define VM_RWREGS_CRS	0x4	/* read/write CRs */
#define VM_RWREGS_MSRS	0x8	/* read/write MSRs */
#define VM_RWREGS_DRS	0x10	/* read/write DRs */
#define VM_RWREGS_ALL	(VM_RWREGS_GPRS | VM_RWREGS_SREGS | VM_RWREGS_CRS | \
    VM_RWREGS_MSRS | VM_RWREGS_DRS)

struct vm_rwregs_params {
	/*
	 * Input/output parameters to VMM_IOC_READREGS /
	 * VMM_IOC_WRITEREGS
	 */
	uint32_t		vrwp_vm_id;
	uint32_t		vrwp_vcpu_id;
	uint64_t		vrwp_mask;
	struct vcpu_reg_state	vrwp_regs;
};

/* IOCTL definitions */
#define VMM_IOC_INTR _IOW('V', 6, struct vm_intr_params) /* Intr pending */

/* CPUID masks */
/*
 * clone host capabilities minus:
 *  debug store (CPUIDECX_DTES64, CPUIDECX_DSCPL, CPUID_DS)
 *  monitor/mwait (CPUIDECX_MWAIT, CPUIDECX_MWAITX)
 *  vmx/svm (CPUIDECX_VMX, CPUIDECX_SVM)
 *  smx (CPUIDECX_SMX)
 *  speedstep (CPUIDECX_EST)
 *  thermal (CPUIDECX_TM2, CPUID_ACPI, CPUID_TM)
 *  context id (CPUIDECX_CNXTID)
 *  machine check (CPUID_MCE, CPUID_MCA)
 *  silicon debug (CPUIDECX_SDBG)
 *  xTPR (CPUIDECX_XTPR)
 *  perf/debug (CPUIDECX_PDCM)
 *  pcid (CPUIDECX_PCID)
 *  direct cache access (CPUIDECX_DCA)
 *  x2APIC (CPUIDECX_X2APIC)
 *  apic deadline (CPUIDECX_DEADLINE)
 *  apic (CPUID_APIC)
 *  psn (CPUID_PSN)
 *  self snoop (CPUID_SS)
 *  hyperthreading (CPUID_HTT)
 *  pending break enabled (CPUID_PBE)
 *  MTRR (CPUID_MTRR)
 *  Speculative execution control features (AMD)
 */
#define VMM_CPUIDECX_MASK ~(CPUIDECX_EST | CPUIDECX_TM2 | CPUIDECX_MWAIT | \
    CPUIDECX_PDCM | CPUIDECX_VMX | CPUIDECX_DTES64 | \
    CPUIDECX_DSCPL | CPUIDECX_SMX | CPUIDECX_CNXTID | \
    CPUIDECX_SDBG | CPUIDECX_XTPR | CPUIDECX_PCID | \
    CPUIDECX_DCA | CPUIDECX_X2APIC | CPUIDECX_DEADLINE)
#define VMM_ECPUIDECX_MASK ~(CPUIDECX_SVM | CPUIDECX_MWAITX)
#define VMM_CPUIDEDX_MASK ~(CPUID_ACPI | CPUID_TM | \
    CPUID_HTT | CPUID_DS | CPUID_APIC | \
    CPUID_PSN | CPUID_SS | CPUID_PBE | \
    CPUID_MTRR | CPUID_MCE | CPUID_MCA)
#define VMM_AMDSPEC_EBX_MASK ~(CPUIDEBX_IBPB | CPUIDEBX_IBRS | \
    CPUIDEBX_STIBP | CPUIDEBX_IBRS_ALWAYSON | CPUIDEBX_STIBP_ALWAYSON | \
    CPUIDEBX_IBRS_PREF | CPUIDEBX_SSBD | CPUIDEBX_VIRT_SSBD | \
    CPUIDEBX_SSBD_NOTREQ)

/* This mask is an include list for bits we want to expose */
#define VMM_APMI_EDX_INCLUDE_MASK (CPUIDEDX_ITSC)

/*
 * SEFF flags - copy from host minus:
 *  TSC_ADJUST (SEFF0EBX_TSC_ADJUST)
 *  SGX (SEFF0EBX_SGX)
 *  HLE (SEFF0EBX_HLE)
 *  INVPCID (SEFF0EBX_INVPCID)
 *  RTM (SEFF0EBX_RTM)
 *  PQM (SEFF0EBX_PQM)
 *  AVX512F (SEFF0EBX_AVX512F)
 *  AVX512DQ (SEFF0EBX_AVX512DQ)
 *  AVX512IFMA (SEFF0EBX_AVX512IFMA)
 *  AVX512PF (SEFF0EBX_AVX512PF)
 *  AVX512ER (SEFF0EBX_AVX512ER)
 *  AVX512CD (SEFF0EBX_AVX512CD)
 *  AVX512BW (SEFF0EBX_AVX512BW)
 *  AVX512VL (SEFF0EBX_AVX512VL)
 *  MPX (SEFF0EBX_MPX)
 *  PCOMMIT (SEFF0EBX_PCOMMIT)
 *  PT (SEFF0EBX_PT)
 */
#define VMM_SEFF0EBX_MASK ~(SEFF0EBX_TSC_ADJUST | SEFF0EBX_SGX | \
    SEFF0EBX_HLE | SEFF0EBX_INVPCID | \
    SEFF0EBX_RTM | SEFF0EBX_PQM | SEFF0EBX_MPX | \
    SEFF0EBX_PCOMMIT | SEFF0EBX_PT | \
    SEFF0EBX_AVX512F | SEFF0EBX_AVX512DQ | \
    SEFF0EBX_AVX512IFMA | SEFF0EBX_AVX512PF | \
    SEFF0EBX_AVX512ER | SEFF0EBX_AVX512CD | \
    SEFF0EBX_AVX512BW | SEFF0EBX_AVX512VL)

/* ECX mask contains the bits to include */
#define VMM_SEFF0ECX_MASK (SEFF0ECX_UMIP)

/* EDX mask contains the bits to include */
#define VMM_SEFF0EDX_MASK (SEFF0EDX_MD_CLEAR)

/*
 * Extended function flags - copy from host minus:
 * 0x80000001  EDX:RDTSCP Support
 */
#define VMM_FEAT_EFLAGS_MASK ~(CPUID_RDTSCP)

/*
 * CPUID[0x4] deterministic cache info
 */
#define VMM_CPUID4_CACHE_TOPOLOGY_MASK	0x3FF

#ifdef _KERNEL

#define VMX_FAIL_LAUNCH_UNKNOWN 	1
#define VMX_FAIL_LAUNCH_INVALID_VMCS	2
#define VMX_FAIL_LAUNCH_VALID_VMCS	3

/* MSR bitmap manipulation macros */
#define VMX_MSRIDX(m)			((m) / 8)
#define VMX_MSRBIT(m)			(1 << (m) % 8)

#define SVM_MSRIDX(m)			((m) / 4)
#define SVM_MSRBIT_R(m)			(1 << (((m) % 4) * 2))
#define SVM_MSRBIT_W(m)			(1 << (((m) % 4) * 2 + 1))

enum {
	VMM_MODE_UNKNOWN,
	VMM_MODE_EPT,
	VMM_MODE_RVI
};

enum {
	VMM_MEM_TYPE_REGULAR,
	VMM_MEM_TYPE_MMIO,
	VMM_MEM_TYPE_UNKNOWN
};

/* Forward declarations */
struct vm;
struct vm_create_params;

/*
 * Implementation-specific cpu state
 */

struct vmcb_segment {
	uint16_t 			vs_sel;			/* 000h */
	uint16_t 			vs_attr;		/* 002h */
	uint32_t			vs_lim;			/* 004h */
	uint64_t			vs_base;		/* 008h */
};

#define SVM_ENABLE_NP		(1ULL << 0)
#define SVM_ENABLE_SEV		(1ULL << 1)
#define SVM_SEVES_ENABLE	(1ULL << 2)

#define SMV_GUEST_INTR_MASK	(1ULL << 1)

#define SVM_LBRVIRT_ENABLE	(1ULL << 0)

struct vmcb {
	union {
		struct {
			uint32_t	v_cr_rw;		/* 000h */
			uint32_t	v_dr_rw;		/* 004h */
			uint32_t	v_excp;			/* 008h */
			uint32_t	v_intercept1;		/* 00Ch */
			uint32_t	v_intercept2;		/* 010h */
			uint8_t		v_pad1[0x28];		/* 014h-03Bh */
			uint16_t	v_pause_thr;		/* 03Ch */
			uint16_t	v_pause_ct;		/* 03Eh */
			uint64_t	v_iopm_pa;		/* 040h */
			uint64_t	v_msrpm_pa;		/* 048h */
			uint64_t	v_tsc_offset;		/* 050h */
			uint32_t	v_asid;			/* 058h */
			uint8_t		v_tlb_control;		/* 05Ch */
			uint8_t		v_pad2[0x3];		/* 05Dh-05Fh */
			uint8_t		v_tpr;			/* 060h */
			uint8_t		v_irq;			/* 061h */
			uint8_t		v_intr_misc;		/* 062h */
			uint8_t		v_intr_masking;		/* 063h */
			uint8_t		v_intr_vector;		/* 064h */
			uint8_t		v_pad3[0x3];		/* 065h-067h */
			uint64_t	v_intr_shadow;		/* 068h */
			uint64_t	v_exitcode;		/* 070h */
			uint64_t	v_exitinfo1;		/* 078h */
			uint64_t	v_exitinfo2;		/* 080h */
			uint64_t	v_exitintinfo;		/* 088h */
			uint64_t	v_np_enable;		/* 090h */
			uint64_t	v_avic_apic_bar;	/* 098h */
			uint64_t	v_ghcb_gpa;		/* 0A0h */
			uint64_t	v_eventinj;		/* 0A8h */
			uint64_t	v_n_cr3;		/* 0B0h */
			uint64_t	v_lbr_virt_enable;	/* 0B8h */
			uint64_t	v_vmcb_clean_bits;	/* 0C0h */
			uint64_t	v_nrip;			/* 0C8h */
			uint8_t		v_n_bytes_fetched;	/* 0D0h */
			uint8_t		v_guest_ins_bytes[0xf];	/* 0D1h-0DFh */
			uint64_t	v_avic_apic_back_page;	/* 0E0h */
			uint64_t	v_pad5;			/* 0E8h-0EFh */
			uint64_t	v_avic_logical_table;	/* 0F0h */
			uint64_t	v_avic_phys;		/* 0F8h */
			uint64_t	v_pad12;		/* 100h */
			uint64_t	v_vmsa_pa;		/* 108h */

		};
		uint8_t			vmcb_control[0x400];
	};

	union {
		struct {
			/* Offsets here are relative to start of VMCB SSA */
			struct vmcb_segment	v_es;		/* 000h */
			struct vmcb_segment	v_cs;		/* 010h */
			struct vmcb_segment	v_ss;		/* 020h */
			struct vmcb_segment	v_ds;		/* 030h */
			struct vmcb_segment	v_fs;		/* 040h */
			struct vmcb_segment	v_gs;		/* 050h */
			struct vmcb_segment	v_gdtr;		/* 060h */
			struct vmcb_segment	v_ldtr;		/* 070h */
			struct vmcb_segment	v_idtr;		/* 080h */
			struct vmcb_segment	v_tr;		/* 090h */
			uint8_t 		v_pad6[0x2B];	/* 0A0h-0CAh */
			uint8_t			v_cpl;		/* 0CBh */
			uint32_t		v_pad7;		/* 0CCh-0CFh */
			uint64_t		v_efer;		/* 0D0h */
			uint8_t			v_pad8[0x70];	/* 0D8h-147h */
			uint64_t		v_cr4;		/* 148h */
			uint64_t		v_cr3;		/* 150h */
			uint64_t		v_cr0;		/* 158h */
			uint64_t		v_dr7;		/* 160h */
			uint64_t		v_dr6;		/* 168h */
			uint64_t		v_rflags;	/* 170h */
			uint64_t		v_rip;		/* 178h */
			uint64_t		v_pad9[0xB];	/* 180h-1D7h */
			uint64_t		v_rsp;		/* 1D8h */
			uint64_t		v_pad10[0x3];	/* 1E0h-1F7h */
			uint64_t		v_rax;		/* 1F8h */
			uint64_t		v_star;		/* 200h */
			uint64_t		v_lstar;	/* 208h */
			uint64_t		v_cstar;	/* 210h */
			uint64_t		v_sfmask;	/* 218h */
			uint64_t		v_kgsbase;	/* 220h */
			uint64_t		v_sysenter_cs;	/* 228h */
			uint64_t		v_sysenter_esp;	/* 230h */
			uint64_t		v_sysenter_eip;	/* 238h */
			uint64_t		v_cr2;		/* 240h */
			uint64_t		v_pad11[0x4];	/* 248h-267h */
			uint64_t		v_g_pat;	/* 268h */
			uint64_t		v_dbgctl;	/* 270h */
			uint64_t		v_br_from;	/* 278h */
			uint64_t		v_br_to;	/* 280h */
			uint64_t		v_lastexcpfrom;	/* 288h */
			uint64_t		v_lastexcpto;	/* 290h */
		};
		uint8_t				vmcb_layout[PAGE_SIZE - 0x400];
	};
};

struct vmsa {
		struct vmcb_segment	v_es;		/* 000h */
		struct vmcb_segment	v_cs;		/* 010h */
		struct vmcb_segment	v_ss;		/* 020h */
		struct vmcb_segment	v_ds;		/* 030h */
		struct vmcb_segment	v_fs;		/* 040h */
		struct vmcb_segment	v_gs;		/* 050h */
		struct vmcb_segment	v_gdtr;		/* 060h */
		struct vmcb_segment	v_ldtr;		/* 070h */
		struct vmcb_segment	v_idtr;		/* 080h */
		struct vmcb_segment	v_tr;		/* 090h */
		uint64_t		v_pl0_ssp;	/* 0A0h */
		uint64_t		v_pl1_ssp;	/* 0A8h */
		uint64_t		v_pl2_ssp;	/* 0B0h */
		uint64_t		v_pl3_ssp;	/* 0B8h */
		uint64_t		v_u_cet;	/* 0C0h */
		uint8_t			v_pad1[0x2];	/* 0C8h-0C9h */
		uint8_t			v_vmpl;		/* 0CAh */
		uint8_t			v_cpl;		/* 0CBh */
		uint8_t			v_pad2[0x4];	/* 0CCh-0CFh */
		uint64_t		v_efer;		/* 0D0h */
		uint8_t			v_pad3[0x68];	/* 0D8h-13Fh */
		uint64_t		v_xss;		/* 140h */
		uint64_t		v_cr4;		/* 148h */
		uint64_t		v_cr3;		/* 150h */
		uint64_t		v_cr0;		/* 158h */
		uint64_t		v_dr7;		/* 160h */
		uint64_t		v_dr6;		/* 168h */
		uint64_t		v_rflags;	/* 170h */
		uint64_t		v_rip;		/* 178h */
		uint64_t		v_dr0;		/* 180h */
		uint64_t		v_dr1;		/* 188h */
		uint64_t		v_dr2;		/* 190h */
		uint64_t		v_dr3;		/* 198h */
		uint64_t		v_dr0_addr_msk;	/* 1A0h */
		uint64_t		v_dr1_addr_msk;	/* 1A8h */
		uint64_t		v_dr2_addr_msk; /* 1B0h */
		uint64_t		v_dr3_addr_msk; /* 1B8h */
		uint8_t			v_pad4[0x18];	/* 1C0h-1D7h */
		uint64_t		v_rsp;		/* 1D8h */
		uint64_t		v_s_cet;	/* 1E0h */
		uint64_t		v_ssp;		/* 1E8h */
		uint64_t		v_isst_addr;	/* 1F0h */
		uint64_t		v_rax;		/* 1F8h */
		uint64_t		v_star;		/* 200h */
		uint64_t		v_lstar;	/* 208h */
		uint64_t		v_cstar;	/* 210h */
		uint64_t		v_sfmask;	/* 218h */
		uint64_t		v_kgsbase;	/* 220h */
		uint64_t		v_sysenter_cs;	/* 228h */
		uint64_t		v_sysenter_esp;	/* 230h */
		uint64_t		v_sysenter_eip;	/* 238h */
		uint64_t		v_cr2;		/* 240h */
		uint8_t			v_pad5[0x20];	/* 248h-267h */
		uint64_t		v_g_pat;	/* 268h */
		uint64_t		v_dbgctl;	/* 270h */
		uint64_t		v_br_from;	/* 278h */
		uint64_t		v_br_to;	/* 280h */
		uint64_t		v_lastexcpfrom;	/* 288h */
		uint64_t		v_lastexcpto;	/* 290h */
		uint8_t			v_pad6[0x48];	/* 298h-2DFh */
		uint8_t			v_pad7[0x8];	/* 2E0h-2E7h */
		uint32_t		v_pkru;		/* 2E8h */
		uint32_t		v_tsc_aux;	/* 2ECh */
		uint64_t		v_gst_tsc_scale;/* 2F0h */
		uint64_t		v_gst_tsc_off;	/* 2F8h */
		uint64_t		v_reg_prot_nce;	/* 300h */
		uint64_t		v_rcx;		/* 308h */
		uint64_t		v_rdx;		/* 310h */
		uint64_t		v_rbx;		/* 318h */
		uint64_t		v_pad8;		/* 320h */
		uint64_t		v_rbp;		/* 328h */
		uint64_t		v_rsi;		/* 330h */
		uint64_t		v_rdi;		/* 338h */
		uint64_t		v_r8;		/* 340h */
		uint64_t		v_r9;		/* 348h */
		uint64_t		v_r10;		/* 350h */
		uint64_t		v_r11;		/* 358h */
		uint64_t		v_r12;		/* 360h */
		uint64_t		v_r13;		/* 368h */
		uint64_t		v_r14;		/* 370h */
		uint64_t		v_r15;		/* 378h */
		uint8_t			v_pad9[0x10];	/* 380h-38Fh */
		uint64_t		v_gst_exitinfo1;/* 390h */
		uint64_t		v_gst_exitinfo2;/* 398h */
		uint64_t		v_gst_exitiinfo;/* 3A0h */
		uint64_t		v_gst_nrip;	/* 3A8h */
		uint64_t		v_sev_features;	/* 3B0h */
		uint64_t		v_intr_ctrl;	/* 3B8h */
		uint64_t		v_gst_exitcode;	/* 3C0h */
		uint64_t		v_virtual_tom;	/* 3C8h */
		uint64_t		v_tlb_id;	/* 3D0h */
		uint64_t		v_pcup_id;	/* 3D8h */
		uint64_t		v_eventinj;	/* 3E0h */
		uint64_t		v_xcr0;		/* 3E8h */
		uint8_t			v_pad10[0x10];	/* 3F0h-3FFh */
		uint64_t		v_x87_dp;	/* 400h */
		uint32_t		v_mxcsr;	/* 408h */
		uint16_t		v_x87_ftw;	/* 40Ch */
		uint16_t		v_x87_fsw;	/* 40Eh */
		uint16_t		v_x87_fcw;	/* 410h */
		uint16_t		v_x87_fop;	/* 412h */
		uint16_t		v_x87_ds;	/* 414h */
		uint16_t		v_x87_cs;	/* 416h */
		uint64_t		v_x87_rip;	/* 418h */
		uint8_t			v_fp_x87[0x50];	/* 420h-46Fh */
		uint8_t			v_fp_xmm[0x100];/* 470h-56Fh */
		uint8_t			v_fp_ymm[0x100];/* 570h-66fh */
		uint8_t			v_lbr_st[0x100];/* 670h-76Fh */
		uint64_t		v_lbr_select;	/* 770h */
		uint64_t		v_ibs_fetch_ctl;/* 778h */
		uint64_t		v_ibs_fetch_la;	/* 780h */
		uint64_t		v_ibs_op_ctl;	/* 788h */
		uint64_t		v_ibs_op_rip;	/* 790h */
		uint64_t		v_ibs_op_data;	/* 798h */
		uint64_t		v_ibs_op_data2;	/* 7A0h */
		uint64_t		v_ibs_op_data3;	/* 7A8h */
		uint64_t		v_ibs_dc_la;	/* 7B0h */
		uint64_t		v_ibstgt_rip;	/* 7B8h */
		uint64_t		v_ic_ibs_xtd_ct;/* 7C0h */
};

/*
 * With SEV-ES the host save area (HSA) has the same layout as the
 * VMSA.  However, it has the offset 0x400 into the HSA page.
 * See AMD APM Vol 2, Appendix B.
 */
#define SVM_HSA_OFFSET			0x400

struct vmcs {
	uint32_t	vmcs_revision;
};

struct vmx_invvpid_descriptor {
	uint64_t	vid_vpid;
	uint64_t	vid_addr;
};

struct vmx_invept_descriptor {
	uint64_t	vid_eptp;
	uint64_t	vid_reserved;
};

struct vmx_msr_store {
	uint64_t	vms_index;
	uint64_t	vms_data;
};

/*
 * Storage for guest registers not preserved in VMCS and various exit
 * information.
 *
 * Note that vmx/svm_enter_guest depend on the layout of this struct for
 * field access.
 */
struct vcpu_gueststate {
	/* %rsi should be first */
	uint64_t	vg_rsi;			/* 0x00 */
	uint64_t	vg_rax;			/* 0x08 */
	uint64_t	vg_rbx;			/* 0x10 */
	uint64_t	vg_rcx;			/* 0x18 */
	uint64_t	vg_rdx;			/* 0x20 */
	uint64_t	vg_rdi;			/* 0x28 */
	uint64_t	vg_rbp;			/* 0x30 */
	uint64_t	vg_r8;			/* 0x38 */
	uint64_t	vg_r9;			/* 0x40 */
	uint64_t	vg_r10;			/* 0x48 */
	uint64_t	vg_r11;			/* 0x50 */
	uint64_t	vg_r12;			/* 0x58 */
	uint64_t	vg_r13;			/* 0x60 */
	uint64_t	vg_r14;			/* 0x68 */
	uint64_t	vg_r15;			/* 0x70 */
	uint64_t	vg_cr2;			/* 0x78 */
	uint64_t	vg_rip;			/* 0x80 */
	uint32_t	vg_exit_reason;		/* 0x88 */
	uint64_t	vg_rflags;		/* 0x90 */
	uint64_t	vg_xcr0;		/* 0x98 */
	/*
	 * Debug registers
	 * - %dr4/%dr5 are aliased to %dr6/%dr7 (or cause #DE)
	 * - %dr7 is saved automatically in the VMCS
	 */
	uint64_t	vg_dr0;			/* 0xa0 */
	uint64_t	vg_dr1;			/* 0xa8 */
	uint64_t	vg_dr2;			/* 0xb0 */
	uint64_t	vg_dr3;			/* 0xb8 */
	uint64_t	vg_dr6;			/* 0xc0 */
};

/*
 * Virtual CPU
 *
 * Methods used to vcpu struct members:
 *	a	atomic operations
 *	I	immutable operations
 *	K	kernel lock
 *	r	reference count
 *	v	vcpu rwlock
 *	V	vm struct's vcpu list lock (vm_vcpu_lock)
 */
struct vcpu {
	/*
	 * Guest FPU state - this must remain as the first member of the struct
	 * to ensure 64-byte alignment (set up during vcpu_pool init)
	 */
	struct savefpu vc_g_fpu;		/* [v] */

	/* VMCS / VMCB pointer */
	vaddr_t vc_control_va;			/* [I] */
	paddr_t vc_control_pa;			/* [I] */

	/* VLAPIC pointer */
	vaddr_t vc_vlapic_va;			/* [I] */
	uint64_t vc_vlapic_pa;			/* [I] */

	/* MSR bitmap address */
	vaddr_t vc_msr_bitmap_va;		/* [I] */
	uint64_t vc_msr_bitmap_pa;		/* [I] */

	struct vm *vc_parent;			/* [I] */
	uint32_t vc_id;				/* [I] */
	uint16_t vc_vpid;			/* [I] */
	u_int vc_state;				/* [a] */
	SLIST_ENTRY(vcpu) vc_vcpu_link;		/* [V] */

	uint8_t vc_virt_mode;			/* [I] */

	struct rwlock vc_lock;

	struct cpu_info *vc_curcpu;		/* [a] */
	struct cpu_info *vc_last_pcpu;		/* [v] */
	struct vm_exit vc_exit;			/* [v] */

	uint16_t vc_intr;			/* [v] */
	uint8_t vc_irqready;			/* [v] */

	uint8_t vc_fpuinited;			/* [v] */

	uint64_t vc_h_xcr0;			/* [v] */

	struct vcpu_gueststate vc_gueststate;	/* [v] */
	struct vcpu_inject_event vc_inject;	/* [v] */

	uint32_t vc_pvclock_version;		/* [v] */
	paddr_t vc_pvclock_system_gpa;		/* [v] */
	uint32_t vc_pvclock_system_tsc_mul;	/* [v] */

	/* Shadowed MSRs */
	uint64_t vc_shadow_pat;			/* [v] */

	/* Userland Protection Keys */
	uint32_t vc_pkru;			/* [v] */

	/* VMX only (all requiring [v]) */
	uint64_t vc_vmx_basic;
	uint64_t vc_vmx_entry_ctls;
	uint64_t vc_vmx_true_entry_ctls;
	uint64_t vc_vmx_exit_ctls;
	uint64_t vc_vmx_true_exit_ctls;
	uint64_t vc_vmx_pinbased_ctls;
	uint64_t vc_vmx_true_pinbased_ctls;
	uint64_t vc_vmx_procbased_ctls;
	uint64_t vc_vmx_true_procbased_ctls;
	uint64_t vc_vmx_procbased2_ctls;
	vaddr_t vc_vmx_msr_exit_save_va;
	paddr_t vc_vmx_msr_exit_save_pa;
	vaddr_t vc_vmx_msr_exit_load_va;
	paddr_t vc_vmx_msr_exit_load_pa;
#if 0	/* XXX currently use msr_exit_save for msr_entry_load too */
	vaddr_t vc_vmx_msr_entry_load_va;
	paddr_t vc_vmx_msr_entry_load_pa;
#endif
	uint8_t vc_vmx_vpid_enabled;
	uint64_t vc_vmx_cr0_fixed1;
	uint64_t vc_vmx_cr0_fixed0;
	uint32_t vc_vmx_vmcs_state;		/* [a] */
#define VMCS_CLEARED	0
#define VMCS_LAUNCHED	1

	/* SVM only (all requiring [v]) */
	vaddr_t vc_svm_hsa_va;
	paddr_t vc_svm_hsa_pa;
	vaddr_t vc_svm_vmsa_va;
	paddr_t vc_svm_vmsa_pa;
	vaddr_t vc_svm_ghcb_va;
	paddr_t vc_svm_ghcb_pa;
	vaddr_t vc_svm_ioio_va;
	paddr_t vc_svm_ioio_pa;
	int vc_sev;				/* [I] */
	int vc_seves;				/* [I] */
};

SLIST_HEAD(vcpu_head, vcpu);

void	vmm_dispatch_intr(vaddr_t);
int	vmxon(uint64_t *);
int	vmxoff(void);
int	vmclear(paddr_t *);
int	vmptrld(paddr_t *);
int	vmptrst(paddr_t *);
int	vmwrite(uint64_t, uint64_t);
int	vmread(uint64_t, uint64_t *);
int	invvpid(uint64_t, struct vmx_invvpid_descriptor *);
int	invept(uint64_t, struct vmx_invept_descriptor *);
int	vmx_enter_guest(paddr_t *, struct vcpu_gueststate *, int, uint8_t);
int	svm_enter_guest(uint64_t, struct vcpu_gueststate *,
    struct region_descriptor *);
int	svm_seves_enter_guest(uint64_t, vaddr_t, struct region_descriptor *);
void	start_vmm_on_cpu(struct cpu_info *);
void	stop_vmm_on_cpu(struct cpu_info *);
void	vmclear_on_cpu(struct cpu_info *);
int	vmm_probe_machdep(struct device *, void *, void *);
void	vmm_attach_machdep(struct device *, struct device *, void *);
void	vmm_activate_machdep(struct device *, int);
int	vmmioctl_machdep(dev_t, u_long, caddr_t, int, struct proc *);
int	pledge_ioctl_vmm_machdep(struct proc *, long);
int	vmm_start(void);
int	vmm_stop(void);
int	vm_impl_init(struct vm *, struct proc *);
void	vm_impl_deinit(struct vm *);
int	vcpu_init(struct vcpu *, struct vm_create_params *);
void	vcpu_deinit(struct vcpu *);
int	vm_rwregs(struct vm_rwregs_params *, int);
int	vcpu_reset_regs(struct vcpu *, struct vcpu_reg_state *);
int	svm_get_vmsa_pa(uint32_t, uint32_t, uint64_t *);

#endif /* _KERNEL */

#endif	/* ! _LOCORE */

#endif /* ! _MACHINE_VMMVAR_H_ */
