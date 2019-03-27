/******************************************************************************
 * arch-arm.h
 *
 * Guest OS interface to ARM Xen.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright 2011 (C) Citrix Systems
 */

#ifndef __XEN_PUBLIC_ARCH_ARM_H__
#define __XEN_PUBLIC_ARCH_ARM_H__

/*
 * `incontents 50 arm_abi Hypercall Calling Convention
 *
 * A hypercall is issued using the ARM HVC instruction.
 *
 * A hypercall can take up to 5 arguments. These are passed in
 * registers, the first argument in x0/r0 (for arm64/arm32 guests
 * respectively irrespective of whether the underlying hypervisor is
 * 32- or 64-bit), the second argument in x1/r1, the third in x2/r2,
 * the forth in x3/r3 and the fifth in x4/r4.
 *
 * The hypercall number is passed in r12 (arm) or x16 (arm64). In both
 * cases the relevant ARM procedure calling convention specifies this
 * is an inter-procedure-call scratch register (e.g. for use in linker
 * stubs). This use does not conflict with use during a hypercall.
 *
 * The HVC ISS must contain a Xen specific TAG: XEN_HYPERCALL_TAG.
 *
 * The return value is in x0/r0.
 *
 * The hypercall will clobber x16/r12 and the argument registers used
 * by that hypercall (except r0 which is the return value) i.e. in
 * addition to x16/r12 a 2 argument hypercall will clobber x1/r1 and a
 * 4 argument hypercall will clobber x1/r1, x2/r2 and x3/r3.
 *
 * Parameter structs passed to hypercalls are laid out according to
 * the Procedure Call Standard for the ARM Architecture (AAPCS, AKA
 * EABI) and Procedure Call Standard for the ARM 64-bit Architecture
 * (AAPCS64). Where there is a conflict the 64-bit standard should be
 * used regardless of guest type. Structures which are passed as
 * hypercall arguments are always little endian.
 *
 * All memory which is shared with other entities in the system
 * (including the hypervisor and other guests) must reside in memory
 * which is mapped as Normal Inner-cacheable. This applies to:
 *  - hypercall arguments passed via a pointer to guest memory.
 *  - memory shared via the grant table mechanism (including PV I/O
 *    rings etc).
 *  - memory shared with the hypervisor (struct shared_info, struct
 *    vcpu_info, the grant table, etc).
 *
 * Any Inner cache allocation strategy (Write-Back, Write-Through etc)
 * is acceptable. There is no restriction on the Outer-cacheability.
 */

/*
 * `incontents 55 arm_hcall Supported Hypercalls
 *
 * Xen on ARM makes extensive use of hardware facilities and therefore
 * only a subset of the potential hypercalls are required.
 *
 * Since ARM uses second stage paging any machine/physical addresses
 * passed to hypercalls are Guest Physical Addresses (Intermediate
 * Physical Addresses) unless otherwise noted.
 *
 * The following hypercalls (and sub operations) are supported on the
 * ARM platform. Other hypercalls should be considered
 * unavailable/unsupported.
 *
 *  HYPERVISOR_memory_op
 *   All generic sub-operations
 *
 *  HYPERVISOR_domctl
 *   All generic sub-operations, with the exception of:
 *    * XEN_DOMCTL_irq_permission (not yet implemented)
 *
 *  HYPERVISOR_sched_op
 *   All generic sub-operations, with the exception of:
 *    * SCHEDOP_block -- prefer wfi hardware instruction
 *
 *  HYPERVISOR_console_io
 *   All generic sub-operations
 *
 *  HYPERVISOR_xen_version
 *   All generic sub-operations
 *
 *  HYPERVISOR_event_channel_op
 *   All generic sub-operations
 *
 *  HYPERVISOR_physdev_op
 *   No sub-operations are currenty supported
 *
 *  HYPERVISOR_sysctl
 *   All generic sub-operations, with the exception of:
 *    * XEN_SYSCTL_page_offline_op
 *    * XEN_SYSCTL_get_pmstat
 *    * XEN_SYSCTL_pm_op
 *
 *  HYPERVISOR_hvm_op
 *   Exactly these sub-operations are supported:
 *    * HVMOP_set_param
 *    * HVMOP_get_param
 *
 *  HYPERVISOR_grant_table_op
 *   All generic sub-operations
 *
 *  HYPERVISOR_vcpu_op
 *   Exactly these sub-operations are supported:
 *    * VCPUOP_register_vcpu_info
 *    * VCPUOP_register_runstate_memory_area
 *
 *
 * Other notes on the ARM ABI:
 *
 * - struct start_info is not exported to ARM guests.
 *
 * - struct shared_info is mapped by ARM guests using the
 *   HYPERVISOR_memory_op sub-op XENMEM_add_to_physmap, passing
 *   XENMAPSPACE_shared_info as space parameter.
 *
 * - All the per-cpu struct vcpu_info are mapped by ARM guests using the
 *   HYPERVISOR_vcpu_op sub-op VCPUOP_register_vcpu_info, including cpu0
 *   struct vcpu_info.
 *
 * - The grant table is mapped using the HYPERVISOR_memory_op sub-op
 *   XENMEM_add_to_physmap, passing XENMAPSPACE_grant_table as space
 *   parameter. The memory range specified under the Xen compatible
 *   hypervisor node on device tree can be used as target gpfn for the
 *   mapping.
 *
 * - Xenstore is initialized by using the two hvm_params
 *   HVM_PARAM_STORE_PFN and HVM_PARAM_STORE_EVTCHN. They can be read
 *   with the HYPERVISOR_hvm_op sub-op HVMOP_get_param.
 *
 * - The paravirtualized console is initialized by using the two
 *   hvm_params HVM_PARAM_CONSOLE_PFN and HVM_PARAM_CONSOLE_EVTCHN. They
 *   can be read with the HYPERVISOR_hvm_op sub-op HVMOP_get_param.
 *
 * - Event channel notifications are delivered using the percpu GIC
 *   interrupt specified under the Xen compatible hypervisor node on
 *   device tree.
 *
 * - The device tree Xen compatible node is fully described under Linux
 *   at Documentation/devicetree/bindings/arm/xen.txt.
 */

#define XEN_HYPERCALL_TAG   0XEA1

#define  int64_aligned_t  int64_t __attribute__((aligned(8)))
#define uint64_aligned_t uint64_t __attribute__((aligned(8)))

#ifndef __ASSEMBLY__
#define ___DEFINE_XEN_GUEST_HANDLE(name, type)                  \
    typedef union { type *p; unsigned long q; }                 \
        __guest_handle_ ## name;                                \
    typedef union { type *p; uint64_aligned_t q; }              \
        __guest_handle_64_ ## name;

/*
 * XEN_GUEST_HANDLE represents a guest pointer, when passed as a field
 * in a struct in memory. On ARM is always 8 bytes sizes and 8 bytes
 * aligned.
 * XEN_GUEST_HANDLE_PARAM represents a guest pointer, when passed as an
 * hypercall argument. It is 4 bytes on aarch32 and 8 bytes on aarch64.
 */
#define __DEFINE_XEN_GUEST_HANDLE(name, type) \
    ___DEFINE_XEN_GUEST_HANDLE(name, type);   \
    ___DEFINE_XEN_GUEST_HANDLE(const_##name, const type)
#define DEFINE_XEN_GUEST_HANDLE(name)   __DEFINE_XEN_GUEST_HANDLE(name, name)
#define __XEN_GUEST_HANDLE(name)        __guest_handle_64_ ## name
#define XEN_GUEST_HANDLE(name)          __XEN_GUEST_HANDLE(name)
#define XEN_GUEST_HANDLE_PARAM(name)    __guest_handle_ ## name
#define set_xen_guest_handle_raw(hnd, val)                  \
    do {                                                    \
        typeof(&(hnd)) _sxghr_tmp = &(hnd);                 \
        _sxghr_tmp->q = 0;                                  \
        _sxghr_tmp->p = val;                                \
    } while ( 0 )
#ifdef __XEN_TOOLS__
#define get_xen_guest_handle(val, hnd)  do { val = (hnd).p; } while (0)
#endif
#define set_xen_guest_handle(hnd, val) set_xen_guest_handle_raw(hnd, val)

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
/* Anonymous union includes both 32- and 64-bit names (e.g., r0/x0). */
# define __DECL_REG(n64, n32) union {          \
        uint64_t n64;                          \
        uint32_t n32;                          \
    }
#else
/* Non-gcc sources must always use the proper 64-bit name (e.g., x0). */
#define __DECL_REG(n64, n32) uint64_t n64
#endif

struct vcpu_guest_core_regs
{
    /*         Aarch64       Aarch32 */
    __DECL_REG(x0,           r0_usr);
    __DECL_REG(x1,           r1_usr);
    __DECL_REG(x2,           r2_usr);
    __DECL_REG(x3,           r3_usr);
    __DECL_REG(x4,           r4_usr);
    __DECL_REG(x5,           r5_usr);
    __DECL_REG(x6,           r6_usr);
    __DECL_REG(x7,           r7_usr);
    __DECL_REG(x8,           r8_usr);
    __DECL_REG(x9,           r9_usr);
    __DECL_REG(x10,          r10_usr);
    __DECL_REG(x11,          r11_usr);
    __DECL_REG(x12,          r12_usr);

    __DECL_REG(x13,          sp_usr);
    __DECL_REG(x14,          lr_usr);

    __DECL_REG(x15,          __unused_sp_hyp);

    __DECL_REG(x16,          lr_irq);
    __DECL_REG(x17,          sp_irq);

    __DECL_REG(x18,          lr_svc);
    __DECL_REG(x19,          sp_svc);

    __DECL_REG(x20,          lr_abt);
    __DECL_REG(x21,          sp_abt);

    __DECL_REG(x22,          lr_und);
    __DECL_REG(x23,          sp_und);

    __DECL_REG(x24,          r8_fiq);
    __DECL_REG(x25,          r9_fiq);
    __DECL_REG(x26,          r10_fiq);
    __DECL_REG(x27,          r11_fiq);
    __DECL_REG(x28,          r12_fiq);

    __DECL_REG(x29,          sp_fiq);
    __DECL_REG(x30,          lr_fiq);

    /* Return address and mode */
    __DECL_REG(pc64,         pc32);             /* ELR_EL2 */
    uint32_t cpsr;                              /* SPSR_EL2 */

    union {
        uint32_t spsr_el1;       /* AArch64 */
        uint32_t spsr_svc;       /* AArch32 */
    };

    /* AArch32 guests only */
    uint32_t spsr_fiq, spsr_irq, spsr_und, spsr_abt;

    /* AArch64 guests only */
    uint64_t sp_el0;
    uint64_t sp_el1, elr_el1;
};
typedef struct vcpu_guest_core_regs vcpu_guest_core_regs_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_guest_core_regs_t);

#undef __DECL_REG

typedef uint64_t xen_pfn_t;
#define PRI_xen_pfn PRIx64

/* Maximum number of virtual CPUs in legacy multi-processor guests. */
/* Only one. All other VCPUS must use VCPUOP_register_vcpu_info */
#define XEN_LEGACY_MAX_VCPUS 1

typedef uint64_t xen_ulong_t;
#define PRI_xen_ulong PRIx64

#if defined(__XEN__) || defined(__XEN_TOOLS__)
struct vcpu_guest_context {
#define _VGCF_online                   0
#define VGCF_online                    (1<<_VGCF_online)
    uint32_t flags;                         /* VGCF_* */

    struct vcpu_guest_core_regs user_regs;  /* Core CPU registers */

    uint32_t sctlr;
    uint64_t ttbcr, ttbr0, ttbr1;
};
typedef struct vcpu_guest_context vcpu_guest_context_t;
DEFINE_XEN_GUEST_HANDLE(vcpu_guest_context_t);

/*
 * struct xen_arch_domainconfig's ABI is covered by
 * XEN_DOMCTL_INTERFACE_VERSION.
 */
#define XEN_DOMCTL_CONFIG_GIC_NATIVE    0
#define XEN_DOMCTL_CONFIG_GIC_V2        1
#define XEN_DOMCTL_CONFIG_GIC_V3        2
struct xen_arch_domainconfig {
    /* IN/OUT */
    uint8_t gic_version;
    /* IN */
    uint32_t nr_spis;
    /*
     * OUT
     * Based on the property clock-frequency in the DT timer node.
     * The property may be present when the bootloader/firmware doesn't
     * set correctly CNTFRQ which hold the timer frequency.
     *
     * As it's not possible to trap this register, we have to replicate
     * the value in the guest DT.
     *
     * = 0 => property not present
     * > 0 => Value of the property
     *
     */
    uint32_t clock_frequency;
};
#endif /* __XEN__ || __XEN_TOOLS__ */

struct arch_vcpu_info {
};
typedef struct arch_vcpu_info arch_vcpu_info_t;

struct arch_shared_info {
};
typedef struct arch_shared_info arch_shared_info_t;
typedef uint64_t xen_callback_t;

#endif

#if defined(__XEN__) || defined(__XEN_TOOLS__)

/* PSR bits (CPSR, SPSR) */

#define PSR_THUMB       (1<<5)        /* Thumb Mode enable */
#define PSR_FIQ_MASK    (1<<6)        /* Fast Interrupt mask */
#define PSR_IRQ_MASK    (1<<7)        /* Interrupt mask */
#define PSR_ABT_MASK    (1<<8)        /* Asynchronous Abort mask */
#define PSR_BIG_ENDIAN  (1<<9)        /* arm32: Big Endian Mode */
#define PSR_DBG_MASK    (1<<9)        /* arm64: Debug Exception mask */
#define PSR_IT_MASK     (0x0600fc00)  /* Thumb If-Then Mask */
#define PSR_JAZELLE     (1<<24)       /* Jazelle Mode */

/* 32 bit modes */
#define PSR_MODE_USR 0x10
#define PSR_MODE_FIQ 0x11
#define PSR_MODE_IRQ 0x12
#define PSR_MODE_SVC 0x13
#define PSR_MODE_MON 0x16
#define PSR_MODE_ABT 0x17
#define PSR_MODE_HYP 0x1a
#define PSR_MODE_UND 0x1b
#define PSR_MODE_SYS 0x1f

/* 64 bit modes */
#define PSR_MODE_BIT  0x10 /* Set iff AArch32 */
#define PSR_MODE_EL3h 0x0d
#define PSR_MODE_EL3t 0x0c
#define PSR_MODE_EL2h 0x09
#define PSR_MODE_EL2t 0x08
#define PSR_MODE_EL1h 0x05
#define PSR_MODE_EL1t 0x04
#define PSR_MODE_EL0t 0x00

#define PSR_GUEST32_INIT  (PSR_ABT_MASK|PSR_FIQ_MASK|PSR_IRQ_MASK|PSR_MODE_SVC)
#define PSR_GUEST64_INIT (PSR_ABT_MASK|PSR_FIQ_MASK|PSR_IRQ_MASK|PSR_MODE_EL1h)

#define SCTLR_GUEST_INIT    0x00c50078

/*
 * Virtual machine platform (memory layout, interrupts)
 *
 * These are defined for consistency between the tools and the
 * hypervisor. Guests must not rely on these hardcoded values but
 * should instead use the FDT.
 */

/* Physical Address Space */

/*
 * vGIC mappings: Only one set of mapping is used by the guest.
 * Therefore they can overlap.
 */

/* vGIC v2 mappings */
#define GUEST_GICD_BASE   0x03001000ULL
#define GUEST_GICD_SIZE   0x00001000ULL
#define GUEST_GICC_BASE   0x03002000ULL
#define GUEST_GICC_SIZE   0x00000100ULL

/* vGIC v3 mappings */
#define GUEST_GICV3_GICD_BASE      0x03001000ULL
#define GUEST_GICV3_GICD_SIZE      0x00010000ULL

#define GUEST_GICV3_RDIST_STRIDE   0x20000ULL
#define GUEST_GICV3_RDIST_REGIONS  1

#define GUEST_GICV3_GICR0_BASE     0x03020000ULL    /* vCPU0 - vCPU127 */
#define GUEST_GICV3_GICR0_SIZE     0x01000000ULL

/*
 * 16MB == 4096 pages reserved for guest to use as a region to map its
 * grant table in.
 */
#define GUEST_GNTTAB_BASE 0x38000000ULL
#define GUEST_GNTTAB_SIZE 0x01000000ULL

#define GUEST_MAGIC_BASE  0x39000000ULL
#define GUEST_MAGIC_SIZE  0x01000000ULL

#define GUEST_RAM_BANKS   2

#define GUEST_RAM0_BASE   0x40000000ULL /* 3GB of low RAM @ 1GB */
#define GUEST_RAM0_SIZE   0xc0000000ULL

#define GUEST_RAM1_BASE   0x0200000000ULL /* 1016GB of RAM @ 8GB */
#define GUEST_RAM1_SIZE   0xfe00000000ULL

#define GUEST_RAM_BASE    GUEST_RAM0_BASE /* Lowest RAM address */
/* Largest amount of actual RAM, not including holes */
#define GUEST_RAM_MAX     (GUEST_RAM0_SIZE + GUEST_RAM1_SIZE)
/* Suitable for e.g. const uint64_t ramfoo[] = GUEST_RAM_BANK_FOOS; */
#define GUEST_RAM_BANK_BASES   { GUEST_RAM0_BASE, GUEST_RAM1_BASE }
#define GUEST_RAM_BANK_SIZES   { GUEST_RAM0_SIZE, GUEST_RAM1_SIZE }

/* Interrupts */
#define GUEST_TIMER_VIRT_PPI    27
#define GUEST_TIMER_PHYS_S_PPI  29
#define GUEST_TIMER_PHYS_NS_PPI 30
#define GUEST_EVTCHN_PPI        31

/* PSCI functions */
#define PSCI_cpu_suspend 0
#define PSCI_cpu_off     1
#define PSCI_cpu_on      2
#define PSCI_migrate     3

#endif

#ifndef __ASSEMBLY__
/* Stub definition of PMU structure */
typedef struct xen_pmu_arch { uint8_t dummy; } xen_pmu_arch_t;
#endif

#endif /*  __XEN_PUBLIC_ARCH_ARM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
