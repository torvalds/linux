#ifndef STARFIVE_VIC7100_H
#define STARFIVE_VIC7100_H
#include <asm/io.h>
#include <soc/sifive/sifive_l2_cache.h>

/*cache.c*/
#define starfive_flush_dcache(start, len) \
	sifive_l2_flush64_range(start, len)

void *dw_phys_to_virt(dma_addr_t phys);
dma_addr_t dw_virt_to_phys(void *vaddr);

int async_memcpy_single(dma_addr_t dst_dma, dma_addr_t src_dma, size_t size);
int async_memcpy_single_virt(void *dst, void *src, size_t size);
int async_memcpy_test(size_t size);

#endif /*STARFIVE_VIC7100_H*/
