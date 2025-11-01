/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) host specific defines
 */

#ifndef SELFTEST_KVM_VGIC_H
#define SELFTEST_KVM_VGIC_H

#include <linux/kvm.h>

#include "kvm_util.h"

#define REDIST_REGION_ATTR_ADDR(count, base, flags, index) \
	(((uint64_t)(count) << 52) | \
	((uint64_t)((base) >> 16) << 16) | \
	((uint64_t)(flags) << 12) | \
	index)

bool kvm_supports_vgic_v3(void);
int __vgic_v3_setup(struct kvm_vm *vm, unsigned int nr_vcpus, uint32_t nr_irqs);
void __vgic_v3_init(int fd);
int vgic_v3_setup(struct kvm_vm *vm, unsigned int nr_vcpus, uint32_t nr_irqs);

#define VGIC_MAX_RESERVED	1023

void kvm_irq_set_level_info(int gic_fd, uint32_t intid, int level);
int _kvm_irq_set_level_info(int gic_fd, uint32_t intid, int level);

void kvm_arm_irq_line(struct kvm_vm *vm, uint32_t intid, int level);
int _kvm_arm_irq_line(struct kvm_vm *vm, uint32_t intid, int level);

/* The vcpu arg only applies to private interrupts. */
void kvm_irq_write_ispendr(int gic_fd, uint32_t intid, struct kvm_vcpu *vcpu);
void kvm_irq_write_isactiver(int gic_fd, uint32_t intid, struct kvm_vcpu *vcpu);

#define KVM_IRQCHIP_NUM_PINS	(1020 - 32)

int vgic_its_setup(struct kvm_vm *vm);

#endif // SELFTEST_KVM_VGIC_H
