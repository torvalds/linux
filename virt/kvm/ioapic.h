#ifndef __KVM_IO_APIC_H
#define __KVM_IO_APIC_H

#include <linux/kvm_host.h>

#include "iodev.h"

struct kvm;
struct kvm_vcpu;

#define IOAPIC_NUM_PINS  KVM_IOAPIC_NUM_PINS
#define IOAPIC_VERSION_ID 0x11	/* IOAPIC version */
#define IOAPIC_EDGE_TRIG  0
#define IOAPIC_LEVEL_TRIG 1

#define IOAPIC_DEFAULT_BASE_ADDRESS  0xfec00000
#define IOAPIC_MEM_LENGTH            0x100

/* Direct registers. */
#define IOAPIC_REG_SELECT  0x00
#define IOAPIC_REG_WINDOW  0x10
#define IOAPIC_REG_EOI     0x40	/* IA64 IOSAPIC only */

/* Indirect registers. */
#define IOAPIC_REG_APIC_ID 0x00	/* x86 IOAPIC only */
#define IOAPIC_REG_VERSION 0x01
#define IOAPIC_REG_ARB_ID  0x02	/* x86 IOAPIC only */

/*ioapic delivery mode*/
#define	IOAPIC_FIXED			0x0
#define	IOAPIC_LOWEST_PRIORITY		0x1
#define	IOAPIC_PMI			0x2
#define	IOAPIC_NMI			0x4
#define	IOAPIC_INIT			0x5
#define	IOAPIC_EXTINT			0x7

struct kvm_ioapic {
	u64 base_address;
	u32 ioregsel;
	u32 id;
	u32 irr;
	u32 pad;
	union ioapic_redir_entry {
		u64 bits;
		struct {
			u8 vector;
			u8 delivery_mode:3;
			u8 dest_mode:1;
			u8 delivery_status:1;
			u8 polarity:1;
			u8 remote_irr:1;
			u8 trig_mode:1;
			u8 mask:1;
			u8 reserve:7;
			u8 reserved[4];
			u8 dest_id;
		} fields;
	} redirtbl[IOAPIC_NUM_PINS];
	struct kvm_io_device dev;
	struct kvm *kvm;
};

#ifdef DEBUG
#define ASSERT(x)  							\
do {									\
	if (!(x)) {							\
		printk(KERN_EMERG "assertion failed %s: %d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
		BUG();							\
	}								\
} while (0)
#else
#define ASSERT(x) do { } while (0)
#endif

static inline struct kvm_ioapic *ioapic_irqchip(struct kvm *kvm)
{
	return kvm->arch.vioapic;
}

#ifdef CONFIG_IA64
static inline int irqchip_in_kernel(struct kvm *kvm)
{
	return 1;
}
#endif

struct kvm_vcpu *kvm_get_lowest_prio_vcpu(struct kvm *kvm, u8 vector,
				       unsigned long bitmap);
void kvm_ioapic_update_eoi(struct kvm *kvm, int vector);
int kvm_ioapic_init(struct kvm *kvm);
void kvm_ioapic_set_irq(struct kvm_ioapic *ioapic, int irq, int level);
void kvm_ioapic_reset(struct kvm_ioapic *ioapic);

#endif
