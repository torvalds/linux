#ifndef __CPM_H
#define __CPM_H

#include <linux/compiler.h>
#include <linux/types.h>

int cpm_muram_init(void);
unsigned long cpm_muram_alloc(unsigned long size, unsigned long align);
int cpm_muram_free(unsigned long offset);
unsigned long cpm_muram_alloc_fixed(unsigned long offset, unsigned long size);
void __iomem *cpm_muram_addr(unsigned long offset);
dma_addr_t cpm_muram_dma(void __iomem *addr);

#endif
