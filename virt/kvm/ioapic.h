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

#ifdef CONFIG_X86
#define RTC_GSI 8
#else
#define RTC_GSI -1U
#endif

struct rtc_status {
	int pending_eoi;
	DECLARE_BITMAP(dest_map, KVM_MAX_VCPUS);
};

struct kvm_ioapic {
	u64 base_address;
	u32 ioregsel;
	u32 id;
	u32 irr;
	u32 pad;
	union kvm_ioapic_redirect_entry redirtbl[IOAPIC_NUM_PINS];
	unsigned long irq_states[IOAPIC_NUM_PINS];
	struct kvm_io_device dev;
	struct kvm *kvm;
	void (*ack_notifier)(void *opaque, int irq);
	spinlock_t lock;
	DECLARE_BITMAP(handled_vectors, 256);
	struct rtc_status rtc_status;
	struct delayed_work eoi_inject;
	u32 irq_eoi[IOAPIC_NUM_PINS];
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

void kvm_rtc_eoi_tracking_restore_one(struct kvm_vcpu *vcpu);
int kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
		int short_hand, int dest, int dest_mode);
int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2);
void kvm_ioapic_update_eoi(struct kvm_vcpu *vcpu, int vector,
			int trigger_mode);
bool kvm_ioapic_handles_vector(struct kvm *kvm, int vector);
int kvm_ioapic_init(struct kvm *kvm);
void kvm_ioapic_destroy(struct kvm *kvm);
int kvm_ioapic_set_irq(struct kvm_ioapic *ioapic, int irq, int irq_source_id,
		       int level, bool line_status);
void kvm_ioapic_clear_all(struct kvm_ioapic *ioapic, int irq_source_id);
int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, unsigned long *dest_map);
int kvm_get_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state);
int kvm_set_ioapic(struct kvm *kvm, struct kvm_ioapic_state *state);
void kvm_vcpu_request_scan_ioapic(struct kvm *kvm);
void kvm_ioapic_scan_entry(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap,
			u32 *tmr);

#endif
