/*
 * Copyright (C) 2015, 2016 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __KVM_ARM_VGIC_MMIO_H__
#define __KVM_ARM_VGIC_MMIO_H__

struct vgic_register_region {
	unsigned int reg_offset;
	unsigned int len;
	unsigned int bits_per_irq;
	unsigned int access_flags;
	unsigned long (*read)(struct kvm_vcpu *vcpu, gpa_t addr,
			      unsigned int len);
	void (*write)(struct kvm_vcpu *vcpu, gpa_t addr, unsigned int len,
		      unsigned long val);
};

extern struct kvm_io_device_ops kvm_io_gic_ops;

#define VGIC_ACCESS_8bit	1
#define VGIC_ACCESS_32bit	2
#define VGIC_ACCESS_64bit	4

/*
 * Generate a mask that covers the number of bytes required to address
 * up to 1024 interrupts, each represented by <bits> bits. This assumes
 * that <bits> is a power of two.
 */
#define VGIC_ADDR_IRQ_MASK(bits) (((bits) * 1024 / 8) - 1)

/*
 * (addr & mask) gives us the byte offset for the INT ID, so we want to
 * divide this with 'bytes per irq' to get the INT ID, which is given
 * by '(bits) / 8'.  But we do this with fixed-point-arithmetic and
 * take advantage of the fact that division by a fraction equals
 * multiplication with the inverted fraction, and scale up both the
 * numerator and denominator with 8 to support at most 64 bits per IRQ:
 */
#define VGIC_ADDR_TO_INTID(addr, bits)  (((addr) & VGIC_ADDR_IRQ_MASK(bits)) * \
					64 / (bits) / 8)

/*
 * Some VGIC registers store per-IRQ information, with a different number
 * of bits per IRQ. For those registers this macro is used.
 * The _WITH_LENGTH version instantiates registers with a fixed length
 * and is mutually exclusive with the _PER_IRQ version.
 */
#define REGISTER_DESC_WITH_BITS_PER_IRQ(off, rd, wr, bpi, acc)		\
	{								\
		.reg_offset = off,					\
		.bits_per_irq = bpi,					\
		.len = bpi * 1024 / 8,					\
		.access_flags = acc,					\
		.read = rd,						\
		.write = wr,						\
	}

#define REGISTER_DESC_WITH_LENGTH(off, rd, wr, length, acc)		\
	{								\
		.reg_offset = off,					\
		.bits_per_irq = 0,					\
		.len = length,						\
		.access_flags = acc,					\
		.read = rd,						\
		.write = wr,						\
	}

int kvm_vgic_register_mmio_region(struct kvm *kvm, struct kvm_vcpu *vcpu,
				  struct vgic_register_region *reg_desc,
				  struct vgic_io_device *region,
				  int nr_irqs, bool offset_private);

unsigned long vgic_data_mmio_bus_to_host(const void *val, unsigned int len);

void vgic_data_host_to_mmio_bus(void *buf, unsigned int len,
				unsigned long data);

unsigned long vgic_mmio_read_raz(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len);

unsigned long vgic_mmio_read_rao(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len);

void vgic_mmio_write_wi(struct kvm_vcpu *vcpu, gpa_t addr,
			unsigned int len, unsigned long val);

unsigned long vgic_mmio_read_enable(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len);

void vgic_mmio_write_senable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val);

void vgic_mmio_write_cenable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val);

unsigned long vgic_mmio_read_pending(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len);

void vgic_mmio_write_spending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val);

void vgic_mmio_write_cpending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val);

unsigned long vgic_mmio_read_active(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len);

void vgic_mmio_write_cactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val);

void vgic_mmio_write_sactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val);

unsigned int vgic_v2_init_dist_iodev(struct vgic_io_device *dev);

#endif
