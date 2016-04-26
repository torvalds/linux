/*
 * VGIC MMIO handling functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/bsearch.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/iodev.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"
#include "vgic-mmio.h"

unsigned long vgic_mmio_read_raz(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return 0;
}

unsigned long vgic_mmio_read_rao(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return -1UL;
}

void vgic_mmio_write_wi(struct kvm_vcpu *vcpu, gpa_t addr,
			unsigned int len, unsigned long val)
{
	/* Ignore */
}

static int match_region(const void *key, const void *elt)
{
	const unsigned int offset = (unsigned long)key;
	const struct vgic_register_region *region = elt;

	if (offset < region->reg_offset)
		return -1;

	if (offset >= region->reg_offset + region->len)
		return 1;

	return 0;
}

/* Find the proper register handler entry given a certain address offset. */
static const struct vgic_register_region *
vgic_find_mmio_region(const struct vgic_register_region *region, int nr_regions,
		      unsigned int offset)
{
	return bsearch((void *)(uintptr_t)offset, region, nr_regions,
		       sizeof(region[0]), match_region);
}

/*
 * kvm_mmio_read_buf() returns a value in a format where it can be converted
 * to a byte array and be directly observed as the guest wanted it to appear
 * in memory if it had done the store itself, which is LE for the GIC, as the
 * guest knows the GIC is always LE.
 *
 * We convert this value to the CPUs native format to deal with it as a data
 * value.
 */
unsigned long vgic_data_mmio_bus_to_host(const void *val, unsigned int len)
{
	unsigned long data = kvm_mmio_read_buf(val, len);

	switch (len) {
	case 1:
		return data;
	case 2:
		return le16_to_cpu(data);
	case 4:
		return le32_to_cpu(data);
	default:
		return le64_to_cpu(data);
	}
}

/*
 * kvm_mmio_write_buf() expects a value in a format such that if converted to
 * a byte array it is observed as the guest would see it if it could perform
 * the load directly.  Since the GIC is LE, and the guest knows this, the
 * guest expects a value in little endian format.
 *
 * We convert the data value from the CPUs native format to LE so that the
 * value is returned in the proper format.
 */
void vgic_data_host_to_mmio_bus(void *buf, unsigned int len,
				unsigned long data)
{
	switch (len) {
	case 1:
		break;
	case 2:
		data = cpu_to_le16(data);
		break;
	case 4:
		data = cpu_to_le32(data);
		break;
	default:
		data = cpu_to_le64(data);
	}

	kvm_mmio_write_buf(buf, len, data);
}

static
struct vgic_io_device *kvm_to_vgic_iodev(const struct kvm_io_device *dev)
{
	return container_of(dev, struct vgic_io_device, dev);
}

static bool check_region(const struct vgic_register_region *region,
			 gpa_t addr, int len)
{
	if ((region->access_flags & VGIC_ACCESS_8bit) && len == 1)
		return true;
	if ((region->access_flags & VGIC_ACCESS_32bit) &&
	    len == sizeof(u32) && !(addr & 3))
		return true;
	if ((region->access_flags & VGIC_ACCESS_64bit) &&
	    len == sizeof(u64) && !(addr & 7))
		return true;

	return false;
}

static int dispatch_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			      gpa_t addr, int len, void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;
	unsigned long data;

	region = vgic_find_mmio_region(iodev->regions, iodev->nr_regions,
				       addr - iodev->base_addr);
	if (!region || !check_region(region, addr, len)) {
		memset(val, 0, len);
		return 0;
	}

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	data = region->read(r_vcpu, addr, len);
	vgic_data_host_to_mmio_bus(val, len, data);
	return 0;
}

static int dispatch_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			       gpa_t addr, int len, const void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;
	unsigned long data = vgic_data_mmio_bus_to_host(val, len);

	region = vgic_find_mmio_region(iodev->regions, iodev->nr_regions,
				       addr - iodev->base_addr);
	if (!region)
		return 0;

	if (!check_region(region, addr, len))
		return 0;

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	region->write(r_vcpu, addr, len, data);
	return 0;
}

struct kvm_io_device_ops kvm_io_gic_ops = {
	.read = dispatch_mmio_read,
	.write = dispatch_mmio_write,
};
