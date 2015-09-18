#include <string.h>
#include <stdint.h>
#include <lkl_host.h>

#include "iomem.h"

#define IOMEM_OFFSET_BITS		24
#define IOMEM_ADDR_MARK			0x8000000
#define MAX_IOMEM_REGIONS		(IOMEM_ADDR_MARK >> IOMEM_OFFSET_BITS)

#define IOMEM_ADDR_TO_INDEX(addr) \
	((((uintptr_t)addr & ~IOMEM_ADDR_MARK) >> IOMEM_OFFSET_BITS))
#define IOMEM_ADDR_TO_OFFSET(addr) \
	(((uintptr_t)addr) & ((1 << IOMEM_OFFSET_BITS) - 1))
#define IOMEM_INDEX_TO_ADDR(i) \
	(void *)(uintptr_t)((i << IOMEM_OFFSET_BITS) | IOMEM_ADDR_MARK)

static struct iomem_region {
	void *base;
	void *iomem_addr;
	int size;
	const struct lkl_iomem_ops *ops;
} *iomem_regions[MAX_IOMEM_REGIONS];

static struct iomem_region *find_iomem_reg(void *base)
{
	int i;

	for (i = 0; i < MAX_IOMEM_REGIONS; i++)
		if (iomem_regions[i] && iomem_regions[i]->base == base)
			return iomem_regions[i];

	return NULL;
}

int register_iomem(void *base, int size, const struct lkl_iomem_ops *ops)
{
	struct iomem_region *iomem_reg;
	int i;

	if (size > (1 << IOMEM_OFFSET_BITS) - 1)
		return -1;

	if (find_iomem_reg(base))
		return -1;

	for (i = 0; i < MAX_IOMEM_REGIONS; i++)
		if (!iomem_regions[i])
			break;

	if (i >= MAX_IOMEM_REGIONS)
		return -1;

	iomem_reg = lkl_host_ops.mem_alloc(sizeof(*iomem_reg));
	if (!iomem_reg)
		return -1;

	iomem_reg->base = base;
	iomem_reg->size = size;
	iomem_reg->ops = ops;
	iomem_reg->iomem_addr = IOMEM_INDEX_TO_ADDR(i);

	iomem_regions[i] = iomem_reg;

	return 0;
}

void unregister_iomem(void *iomem_base)
{
	struct iomem_region *iomem_reg = find_iomem_reg(iomem_base);
	unsigned int index;

	if (!iomem_reg) {
		lkl_printf("%s: invalid iomem base %p\n", __func__, iomem_base);
		return;
	}

	index = IOMEM_ADDR_TO_INDEX(iomem_reg->iomem_addr);
	if (index >= MAX_IOMEM_REGIONS) {
		lkl_printf("%s: invalid iomem_addr %p\n", __func__,
			   iomem_reg->iomem_addr);
		return;
	}

	iomem_regions[index] = NULL;
	lkl_host_ops.mem_free(iomem_reg->base);
	lkl_host_ops.mem_free(iomem_reg);
}

void *lkl_ioremap(long addr, int size)
{
	struct iomem_region *iomem_reg = find_iomem_reg((void *)addr);

	if (iomem_reg && size <= iomem_reg->size)
		return iomem_reg->iomem_addr;

	return NULL;
}

int lkl_iomem_access(const volatile void *addr, void *res, int size, int write)
{
	struct iomem_region *iomem_reg;
	int index = IOMEM_ADDR_TO_INDEX(addr);
	int offset = IOMEM_ADDR_TO_OFFSET(addr);
	int ret;

	if (index > MAX_IOMEM_REGIONS || !iomem_regions[index] ||
	    offset + size > iomem_regions[index]->size)
		return -1;

	iomem_reg = iomem_regions[index];

	if (write)
		ret = iomem_reg->ops->write(iomem_reg->base, offset, res, size);
	else
		ret = iomem_reg->ops->read(iomem_reg->base, offset, res, size);

	return ret;
}
