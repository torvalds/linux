#ifndef _LKL_LIB_IOMEM_H
#define _LKL_LIB_IOMEM_H

struct lkl_iomem_ops {
	int (*read)(void *data, int offset, void *res, int size);
	int (*write)(void *data, int offset, void *value, int size);
};

void* register_iomem(void *data, int size, const struct lkl_iomem_ops *ops);
void unregister_iomem(void *iomem_base);
void *lkl_ioremap(long addr, int size);
int lkl_iomem_access(const volatile void *addr, void *res, int size, int write);

#endif /* _LKL_LIB_IOMEM_H */
