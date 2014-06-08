#ifndef __LINUX_GOLDFISH_H
#define __LINUX_GOLDFISH_H

/* Helpers for Goldfish virtual platform */

static inline void gf_write64(unsigned long data,
		void __iomem *portl, void __iomem *porth)
{
	writel((u32)data, portl);
#ifdef CONFIG_64BIT
	writel(data>>32, porth);
#endif
}

#endif /* __LINUX_GOLDFISH_H */
