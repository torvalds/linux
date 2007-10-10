#ifndef __LINUX_KVM_H
#define __LINUX_KVM_H

/*
 * Userspace interface for /dev/kvm - kernel based virtual machine
 *
 * Note: you must update KVM_API_VERSION if you change this interface.
 */

#include <asm/types.h>
#include <linux/ioctl.h>

#define KVM_API_VERSION 12

/* Architectural interrupt line count. */
#define KVM_NR_INTERRUPTS 256

/* for KVM_CREATE_MEMORY_REGION */
struct kvm_memory_region {
	__u32 slot;
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size; /* bytes */
};

/* for kvm_memory_region::flags */
#define KVM_MEM_LOG_DIRTY_PAGES  1UL

struct kvm_memory_alias {
	__u32 slot;  /* this has a different namespace than memory slots */
	__u32 flags;
	__u64 guest_phys_addr;
	__u64 memory_size;
	__u64 target_phys_addr;
};

/* for KVM_IRQ_LINE */
struct kvm_irq_level {
	/*
	 * ACPI gsi notion of irq.
	 * For IA-64 (APIC model) IOAPIC0: irq 0-23; IOAPIC1: irq 24-47..
	 * For X86 (standard AT mode) PIC0/1: irq 0-15. IOAPIC0: 0-23..
	 */
	__u32 irq;
	__u32 level;
};

/* for KVM_GET_IRQCHIP and KVM_SET_IRQCHIP */
struct kvm_pic_state {
	__u8 last_irr;	/* edge detection */
	__u8 irr;		/* interrupt request register */
	__u8 imr;		/* interrupt mask register */
	__u8 isr;		/* interrupt service register */
	__u8 priority_add;	/* highest irq priority */
	__u8 irq_base;
	__u8 read_reg_select;
	__u8 poll;
	__u8 special_mask;
	__u8 init_state;
	__u8 auto_eoi;
	__u8 rotate_on_auto_eoi;
	__u8 special_fully_nested_mode;
	__u8 init4;		/* true if 4 byte init */
	__u8 elcr;		/* PIIX edge/trigger selection */
	__u8 elcr_mask;
};

#define KVM_IOAPIC_NUM_PINS  24
struct kvm_ioapic_state {
	__u64 base_address;
	__u32 ioregsel;
	__u32 id;
	__u32 irr;
	__u32 pad;
	union {
		__u64 bits;
		struct {
			__u8 vector;
			__u8 delivery_mode:3;
			__u8 dest_mode:1;
			__u8 delivery_status:1;
			__u8 polarity:1;
			__u8 remote_irr:1;
			__u8 trig_mode:1;
			__u8 mask:1;
			__u8 reserve:7;
			__u8 reserved[4];
			__u8 dest_id;
		} fields;
	} redirtbl[KVM_IOAPIC_NUM_PINS];
};

#define KVM_IRQCHIP_PIC_MASTER   0
#define KVM_IRQCHIP_PIC_SLAVE    1
#define KVM_IRQCHIP_IOAPIC       2

struct kvm_irqchip {
	__u32 chip_id;
	__u32 pad;
        union {
		char dummy[512];  /* reserving space */
		struct kvm_pic_state pic;
		struct kvm_ioapic_state ioapic;
	} chip;
};

#define KVM_EXIT_UNKNOWN          0
#define KVM_EXIT_EXCEPTION        1
#define KVM_EXIT_IO               2
#define KVM_EXIT_HYPERCALL        3
#define KVM_EXIT_DEBUG            4
#define KVM_EXIT_HLT              5
#define KVM_EXIT_MMIO             6
#define KVM_EXIT_IRQ_WINDOW_OPEN  7
#define KVM_EXIT_SHUTDOWN         8
#define KVM_EXIT_FAIL_ENTRY       9
#define KVM_EXIT_INTR             10
#define KVM_EXIT_SET_TPR          11

/* for KVM_RUN, returned by mmap(vcpu_fd, offset=0) */
struct kvm_run {
	/* in */
	__u8 request_interrupt_window;
	__u8 padding1[7];

	/* out */
	__u32 exit_reason;
	__u8 ready_for_interrupt_injection;
	__u8 if_flag;
	__u8 padding2[2];

	/* in (pre_kvm_run), out (post_kvm_run) */
	__u64 cr8;
	__u64 apic_base;

	union {
		/* KVM_EXIT_UNKNOWN */
		struct {
			__u64 hardware_exit_reason;
		} hw;
		/* KVM_EXIT_FAIL_ENTRY */
		struct {
			__u64 hardware_entry_failure_reason;
		} fail_entry;
		/* KVM_EXIT_EXCEPTION */
		struct {
			__u32 exception;
			__u32 error_code;
		} ex;
		/* KVM_EXIT_IO */
		struct kvm_io {
#define KVM_EXIT_IO_IN  0
#define KVM_EXIT_IO_OUT 1
			__u8 direction;
			__u8 size; /* bytes */
			__u16 port;
			__u32 count;
			__u64 data_offset; /* relative to kvm_run start */
		} io;
		struct {
		} debug;
		/* KVM_EXIT_MMIO */
		struct {
			__u64 phys_addr;
			__u8  data[8];
			__u32 len;
			__u8  is_write;
		} mmio;
		/* KVM_EXIT_HYPERCALL */
		struct {
			__u64 nr;
			__u64 args[6];
			__u64 ret;
			__u32 longmode;
			__u32 pad;
		} hypercall;
		/* Fix the size of the union. */
		char padding[256];
	};
};

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
	/* out (KVM_GET_REGS) / in (KVM_SET_REGS) */
	__u64 rax, rbx, rcx, rdx;
	__u64 rsi, rdi, rsp, rbp;
	__u64 r8,  r9,  r10, r11;
	__u64 r12, r13, r14, r15;
	__u64 rip, rflags;
};

/* for KVM_GET_FPU and KVM_SET_FPU */
struct kvm_fpu {
	__u8  fpr[8][16];
	__u16 fcw;
	__u16 fsw;
	__u8  ftwx;  /* in fxsave format */
	__u8  pad1;
	__u16 last_opcode;
	__u64 last_ip;
	__u64 last_dp;
	__u8  xmm[16][16];
	__u32 mxcsr;
	__u32 pad2;
};

/* for KVM_GET_LAPIC and KVM_SET_LAPIC */
#define KVM_APIC_REG_SIZE 0x400
struct kvm_lapic_state {
	char regs[KVM_APIC_REG_SIZE];
};

struct kvm_segment {
	__u64 base;
	__u32 limit;
	__u16 selector;
	__u8  type;
	__u8  present, dpl, db, s, l, g, avl;
	__u8  unusable;
	__u8  padding;
};

struct kvm_dtable {
	__u64 base;
	__u16 limit;
	__u16 padding[3];
};

/* for KVM_GET_SREGS and KVM_SET_SREGS */
struct kvm_sregs {
	/* out (KVM_GET_SREGS) / in (KVM_SET_SREGS) */
	struct kvm_segment cs, ds, es, fs, gs, ss;
	struct kvm_segment tr, ldt;
	struct kvm_dtable gdt, idt;
	__u64 cr0, cr2, cr3, cr4, cr8;
	__u64 efer;
	__u64 apic_base;
	__u64 interrupt_bitmap[(KVM_NR_INTERRUPTS + 63) / 64];
};

struct kvm_msr_entry {
	__u32 index;
	__u32 reserved;
	__u64 data;
};

/* for KVM_GET_MSRS and KVM_SET_MSRS */
struct kvm_msrs {
	__u32 nmsrs; /* number of msrs in entries */
	__u32 pad;

	struct kvm_msr_entry entries[0];
};

/* for KVM_GET_MSR_INDEX_LIST */
struct kvm_msr_list {
	__u32 nmsrs; /* number of msrs in entries */
	__u32 indices[0];
};

/* for KVM_TRANSLATE */
struct kvm_translation {
	/* in */
	__u64 linear_address;

	/* out */
	__u64 physical_address;
	__u8  valid;
	__u8  writeable;
	__u8  usermode;
	__u8  pad[5];
};

/* for KVM_INTERRUPT */
struct kvm_interrupt {
	/* in */
	__u32 irq;
};

struct kvm_breakpoint {
	__u32 enabled;
	__u32 padding;
	__u64 address;
};

/* for KVM_DEBUG_GUEST */
struct kvm_debug_guest {
	/* int */
	__u32 enabled;
	__u32 pad;
	struct kvm_breakpoint breakpoints[4];
	__u32 singlestep;
};

/* for KVM_GET_DIRTY_LOG */
struct kvm_dirty_log {
	__u32 slot;
	__u32 padding;
	union {
		void __user *dirty_bitmap; /* one bit per page */
		__u64 padding;
	};
};

struct kvm_cpuid_entry {
	__u32 function;
	__u32 eax;
	__u32 ebx;
	__u32 ecx;
	__u32 edx;
	__u32 padding;
};

/* for KVM_SET_CPUID */
struct kvm_cpuid {
	__u32 nent;
	__u32 padding;
	struct kvm_cpuid_entry entries[0];
};

/* for KVM_SET_SIGNAL_MASK */
struct kvm_signal_mask {
	__u32 len;
	__u8  sigset[0];
};

#define KVMIO 0xAE

/*
 * ioctls for /dev/kvm fds:
 */
#define KVM_GET_API_VERSION       _IO(KVMIO,   0x00)
#define KVM_CREATE_VM             _IO(KVMIO,   0x01) /* returns a VM fd */
#define KVM_GET_MSR_INDEX_LIST    _IOWR(KVMIO, 0x02, struct kvm_msr_list)
/*
 * Check if a kvm extension is available.  Argument is extension number,
 * return is 1 (yes) or 0 (no, sorry).
 */
#define KVM_CHECK_EXTENSION       _IO(KVMIO,   0x03)
/*
 * Get size for mmap(vcpu_fd)
 */
#define KVM_GET_VCPU_MMAP_SIZE    _IO(KVMIO,   0x04) /* in bytes */

/*
 * Extension capability list.
 */
#define KVM_CAP_IRQCHIP	  0
#define KVM_CAP_HLT	  1

/*
 * ioctls for VM fds
 */
#define KVM_SET_MEMORY_REGION     _IOW(KVMIO, 0x40, struct kvm_memory_region)
/*
 * KVM_CREATE_VCPU receives as a parameter the vcpu slot, and returns
 * a vcpu fd.
 */
#define KVM_CREATE_VCPU           _IO(KVMIO,  0x41)
#define KVM_GET_DIRTY_LOG         _IOW(KVMIO, 0x42, struct kvm_dirty_log)
#define KVM_SET_MEMORY_ALIAS      _IOW(KVMIO, 0x43, struct kvm_memory_alias)
/* Device model IOC */
#define KVM_CREATE_IRQCHIP	  _IO(KVMIO,  0x60)
#define KVM_IRQ_LINE		  _IOW(KVMIO, 0x61, struct kvm_irq_level)
#define KVM_GET_IRQCHIP		  _IOWR(KVMIO, 0x62, struct kvm_irqchip)
#define KVM_SET_IRQCHIP		  _IOR(KVMIO,  0x63, struct kvm_irqchip)

/*
 * ioctls for vcpu fds
 */
#define KVM_RUN                   _IO(KVMIO,   0x80)
#define KVM_GET_REGS              _IOR(KVMIO,  0x81, struct kvm_regs)
#define KVM_SET_REGS              _IOW(KVMIO,  0x82, struct kvm_regs)
#define KVM_GET_SREGS             _IOR(KVMIO,  0x83, struct kvm_sregs)
#define KVM_SET_SREGS             _IOW(KVMIO,  0x84, struct kvm_sregs)
#define KVM_TRANSLATE             _IOWR(KVMIO, 0x85, struct kvm_translation)
#define KVM_INTERRUPT             _IOW(KVMIO,  0x86, struct kvm_interrupt)
#define KVM_DEBUG_GUEST           _IOW(KVMIO,  0x87, struct kvm_debug_guest)
#define KVM_GET_MSRS              _IOWR(KVMIO, 0x88, struct kvm_msrs)
#define KVM_SET_MSRS              _IOW(KVMIO,  0x89, struct kvm_msrs)
#define KVM_SET_CPUID             _IOW(KVMIO,  0x8a, struct kvm_cpuid)
#define KVM_SET_SIGNAL_MASK       _IOW(KVMIO,  0x8b, struct kvm_signal_mask)
#define KVM_GET_FPU               _IOR(KVMIO,  0x8c, struct kvm_fpu)
#define KVM_SET_FPU               _IOW(KVMIO,  0x8d, struct kvm_fpu)
#define KVM_GET_LAPIC             _IOR(KVMIO,  0x8e, struct kvm_lapic_state)
#define KVM_SET_LAPIC             _IOW(KVMIO,  0x8f, struct kvm_lapic_state)

#endif
