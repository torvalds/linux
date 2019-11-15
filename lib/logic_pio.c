// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 HiSilicon Limited, All Rights Reserved.
 * Author: Gabriele Paoloni <gabriele.paoloni@huawei.com>
 * Author: Zhichang Yuan <yuanzhichang@hisilicon.com>
 */

#define pr_fmt(fmt)	"LOGIC PIO: " fmt

#include <linux/of.h>
#include <linux/io.h>
#include <linux/logic_pio.h>
#include <linux/mm.h>
#include <linux/rculist.h>
#include <linux/sizes.h>
#include <linux/slab.h>

/* The unique hardware address list */
static LIST_HEAD(io_range_list);
static DEFINE_MUTEX(io_range_mutex);

/* Consider a kernel general helper for this */
#define in_range(b, first, len)        ((b) >= (first) && (b) < (first) + (len))

/**
 * logic_pio_register_range - register logical PIO range for a host
 * @new_range: pointer to the IO range to be registered.
 *
 * Returns 0 on success, the error code in case of failure.
 *
 * Register a new IO range node in the IO range list.
 */
int logic_pio_register_range(struct logic_pio_hwaddr *new_range)
{
	struct logic_pio_hwaddr *range;
	resource_size_t start;
	resource_size_t end;
	resource_size_t mmio_end = 0;
	resource_size_t iio_sz = MMIO_UPPER_LIMIT;
	int ret = 0;

	if (!new_range || !new_range->fwnode || !new_range->size)
		return -EINVAL;

	start = new_range->hw_start;
	end = new_range->hw_start + new_range->size;

	mutex_lock(&io_range_mutex);
	list_for_each_entry(range, &io_range_list, list) {
		if (range->fwnode == new_range->fwnode) {
			/* range already there */
			goto end_register;
		}
		if (range->flags == LOGIC_PIO_CPU_MMIO &&
		    new_range->flags == LOGIC_PIO_CPU_MMIO) {
			/* for MMIO ranges we need to check for overlap */
			if (start >= range->hw_start + range->size ||
			    end < range->hw_start) {
				mmio_end = range->io_start + range->size;
			} else {
				ret = -EFAULT;
				goto end_register;
			}
		} else if (range->flags == LOGIC_PIO_INDIRECT &&
			   new_range->flags == LOGIC_PIO_INDIRECT) {
			iio_sz += range->size;
		}
	}

	/* range not registered yet, check for available space */
	if (new_range->flags == LOGIC_PIO_CPU_MMIO) {
		if (mmio_end + new_range->size - 1 > MMIO_UPPER_LIMIT) {
			/* if it's too big check if 64K space can be reserved */
			if (mmio_end + SZ_64K - 1 > MMIO_UPPER_LIMIT) {
				ret = -E2BIG;
				goto end_register;
			}
			new_range->size = SZ_64K;
			pr_warn("Requested IO range too big, new size set to 64K\n");
		}
		new_range->io_start = mmio_end;
	} else if (new_range->flags == LOGIC_PIO_INDIRECT) {
		if (iio_sz + new_range->size - 1 > IO_SPACE_LIMIT) {
			ret = -E2BIG;
			goto end_register;
		}
		new_range->io_start = iio_sz;
	} else {
		/* invalid flag */
		ret = -EINVAL;
		goto end_register;
	}

	list_add_tail_rcu(&new_range->list, &io_range_list);

end_register:
	mutex_unlock(&io_range_mutex);
	return ret;
}

/**
 * logic_pio_unregister_range - unregister a logical PIO range for a host
 * @range: pointer to the IO range which has been already registered.
 *
 * Unregister a previously-registered IO range node.
 */
void logic_pio_unregister_range(struct logic_pio_hwaddr *range)
{
	mutex_lock(&io_range_mutex);
	list_del_rcu(&range->list);
	mutex_unlock(&io_range_mutex);
	synchronize_rcu();
}

/**
 * find_io_range_by_fwnode - find logical PIO range for given FW node
 * @fwnode: FW node handle associated with logical PIO range
 *
 * Returns pointer to node on success, NULL otherwise.
 *
 * Traverse the io_range_list to find the registered node for @fwnode.
 */
struct logic_pio_hwaddr *find_io_range_by_fwnode(struct fwnode_handle *fwnode)
{
	struct logic_pio_hwaddr *range, *found_range = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(range, &io_range_list, list) {
		if (range->fwnode == fwnode) {
			found_range = range;
			break;
		}
	}
	rcu_read_unlock();

	return found_range;
}

/* Return a registered range given an input PIO token */
static struct logic_pio_hwaddr *find_io_range(unsigned long pio)
{
	struct logic_pio_hwaddr *range, *found_range = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(range, &io_range_list, list) {
		if (in_range(pio, range->io_start, range->size)) {
			found_range = range;
			break;
		}
	}
	rcu_read_unlock();

	if (!found_range)
		pr_err("PIO entry token 0x%lx invalid\n", pio);

	return found_range;
}

/**
 * logic_pio_to_hwaddr - translate logical PIO to HW address
 * @pio: logical PIO value
 *
 * Returns HW address if valid, ~0 otherwise.
 *
 * Translate the input logical PIO to the corresponding hardware address.
 * The input PIO should be unique in the whole logical PIO space.
 */
resource_size_t logic_pio_to_hwaddr(unsigned long pio)
{
	struct logic_pio_hwaddr *range;

	range = find_io_range(pio);
	if (range)
		return range->hw_start + pio - range->io_start;

	return (resource_size_t)~0;
}

/**
 * logic_pio_trans_hwaddr - translate HW address to logical PIO
 * @fwnode: FW node reference for the host
 * @addr: Host-relative HW address
 * @size: size to translate
 *
 * Returns Logical PIO value if successful, ~0UL otherwise
 */
unsigned long logic_pio_trans_hwaddr(struct fwnode_handle *fwnode,
				     resource_size_t addr, resource_size_t size)
{
	struct logic_pio_hwaddr *range;

	range = find_io_range_by_fwnode(fwnode);
	if (!range || range->flags == LOGIC_PIO_CPU_MMIO) {
		pr_err("IO range not found or invalid\n");
		return ~0UL;
	}
	if (range->size < size) {
		pr_err("resource size %pa cannot fit in IO range size %pa\n",
		       &size, &range->size);
		return ~0UL;
	}
	return addr - range->hw_start + range->io_start;
}

unsigned long logic_pio_trans_cpuaddr(resource_size_t addr)
{
	struct logic_pio_hwaddr *range;

	rcu_read_lock();
	list_for_each_entry_rcu(range, &io_range_list, list) {
		if (range->flags != LOGIC_PIO_CPU_MMIO)
			continue;
		if (in_range(addr, range->hw_start, range->size)) {
			unsigned long cpuaddr;

			cpuaddr = addr - range->hw_start + range->io_start;

			rcu_read_unlock();
			return cpuaddr;
		}
	}
	rcu_read_unlock();

	pr_err("addr %pa not registered in io_range_list\n", &addr);

	return ~0UL;
}

#if defined(CONFIG_INDIRECT_PIO) && defined(PCI_IOBASE)
#define BUILD_LOGIC_IO(bw, type)					\
type logic_in##bw(unsigned long addr)					\
{									\
	type ret = (type)~0;						\
									\
	if (addr < MMIO_UPPER_LIMIT) {					\
		ret = read##bw(PCI_IOBASE + addr);			\
	} else if (addr >= MMIO_UPPER_LIMIT && addr < IO_SPACE_LIMIT) { \
		struct logic_pio_hwaddr *entry = find_io_range(addr);	\
									\
		if (entry && entry->ops)				\
			ret = entry->ops->in(entry->hostdata,		\
					addr, sizeof(type));		\
		else							\
			WARN_ON_ONCE(1);				\
	}								\
	return ret;							\
}									\
									\
void logic_out##bw(type value, unsigned long addr)			\
{									\
	if (addr < MMIO_UPPER_LIMIT) {					\
		write##bw(value, PCI_IOBASE + addr);			\
	} else if (addr >= MMIO_UPPER_LIMIT && addr < IO_SPACE_LIMIT) {	\
		struct logic_pio_hwaddr *entry = find_io_range(addr);	\
									\
		if (entry && entry->ops)				\
			entry->ops->out(entry->hostdata,		\
					addr, value, sizeof(type));	\
		else							\
			WARN_ON_ONCE(1);				\
	}								\
}									\
									\
void logic_ins##bw(unsigned long addr, void *buffer,		\
		   unsigned int count)					\
{									\
	if (addr < MMIO_UPPER_LIMIT) {					\
		reads##bw(PCI_IOBASE + addr, buffer, count);		\
	} else if (addr >= MMIO_UPPER_LIMIT && addr < IO_SPACE_LIMIT) {	\
		struct logic_pio_hwaddr *entry = find_io_range(addr);	\
									\
		if (entry && entry->ops)				\
			entry->ops->ins(entry->hostdata,		\
				addr, buffer, sizeof(type), count);	\
		else							\
			WARN_ON_ONCE(1);				\
	}								\
									\
}									\
									\
void logic_outs##bw(unsigned long addr, const void *buffer,		\
		    unsigned int count)					\
{									\
	if (addr < MMIO_UPPER_LIMIT) {					\
		writes##bw(PCI_IOBASE + addr, buffer, count);		\
	} else if (addr >= MMIO_UPPER_LIMIT && addr < IO_SPACE_LIMIT) {	\
		struct logic_pio_hwaddr *entry = find_io_range(addr);	\
									\
		if (entry && entry->ops)				\
			entry->ops->outs(entry->hostdata,		\
				addr, buffer, sizeof(type), count);	\
		else							\
			WARN_ON_ONCE(1);				\
	}								\
}

BUILD_LOGIC_IO(b, u8)
EXPORT_SYMBOL(logic_inb);
EXPORT_SYMBOL(logic_insb);
EXPORT_SYMBOL(logic_outb);
EXPORT_SYMBOL(logic_outsb);

BUILD_LOGIC_IO(w, u16)
EXPORT_SYMBOL(logic_inw);
EXPORT_SYMBOL(logic_insw);
EXPORT_SYMBOL(logic_outw);
EXPORT_SYMBOL(logic_outsw);

BUILD_LOGIC_IO(l, u32)
EXPORT_SYMBOL(logic_inl);
EXPORT_SYMBOL(logic_insl);
EXPORT_SYMBOL(logic_outl);
EXPORT_SYMBOL(logic_outsl);

#endif /* CONFIG_INDIRECT_PIO && PCI_IOBASE */
