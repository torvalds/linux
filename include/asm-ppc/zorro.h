#ifndef _ASM_PPC_ZORRO_H
#define _ASM_PPC_ZORRO_H

#include <asm/io.h>

#define z_readb in_8
#define z_readw in_be16
#define z_readl in_be32

#define z_writeb(val, port) out_8((port), (val))
#define z_writew(val, port) out_be16((port), (val))
#define z_writel(val, port) out_be32((port), (val))

#define z_memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define z_memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define z_memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

extern void *__ioremap(unsigned long address, unsigned long size,
		       unsigned long flags);

extern void *ioremap(unsigned long address, unsigned long size);
extern void iounmap(void *addr);

extern void *__ioremap(unsigned long address, unsigned long size,
                       unsigned long flags);

#define z_ioremap ioremap
#define z_iounmap iounmap

#endif /* _ASM_PPC_ZORRO_H */
