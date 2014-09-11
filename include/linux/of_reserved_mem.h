#ifndef __OF_RESERVED_MEM_H
#define __OF_RESERVED_MEM_H

struct device;
struct of_phandle_args;
struct reserved_mem_ops;

struct reserved_mem {
	const char			*name;
	unsigned long			fdt_node;
	unsigned long			phandle;
	const struct reserved_mem_ops	*ops;
	phys_addr_t			base;
	phys_addr_t			size;
	void				*priv;
};

struct reserved_mem_ops {
	void	(*device_init)(struct reserved_mem *rmem,
			       struct device *dev);
	void	(*device_release)(struct reserved_mem *rmem,
				  struct device *dev);
};

typedef int (*reservedmem_of_init_fn)(struct reserved_mem *rmem);

#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	_OF_DECLARE(reservedmem, name, compat, init, reservedmem_of_init_fn)

#ifdef CONFIG_OF_RESERVED_MEM
void of_reserved_mem_device_init(struct device *dev);
void of_reserved_mem_device_release(struct device *dev);

void fdt_init_reserved_mem(void);
void fdt_reserved_mem_save_node(unsigned long node, const char *uname,
			       phys_addr_t base, phys_addr_t size);
#else
static inline void of_reserved_mem_device_init(struct device *dev) { }
static inline void of_reserved_mem_device_release(struct device *pdev) { }

static inline void fdt_init_reserved_mem(void) { }
static inline void fdt_reserved_mem_save_node(unsigned long node,
		const char *uname, phys_addr_t base, phys_addr_t size) { }
#endif

#endif /* __OF_RESERVED_MEM_H */
