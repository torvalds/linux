#ifndef __OF_RESERVED_MEM_H
#define __OF_RESERVED_MEM_H

struct device;
struct of_phandle_args;
struct reserved_mem_ops;

struct reserved_mem {
	const char			*name;
	unsigned long			fdt_node;
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

typedef int (*reservedmem_of_init_fn)(struct reserved_mem *rmem,
				      unsigned long node, const char *uname);

#ifdef CONFIG_OF_RESERVED_MEM
void fdt_init_reserved_mem(void);
void fdt_reserved_mem_save_node(unsigned long node, const char *uname,
			       phys_addr_t base, phys_addr_t size);

#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	static const struct of_device_id __reservedmem_of_table_##name	\
		__used __section(__reservedmem_of_table)		\
		 = { .compatible = compat,				\
		     .data = (init == (reservedmem_of_init_fn)NULL) ?	\
				init : init }

#else
static inline void fdt_init_reserved_mem(void) { }
static inline void fdt_reserved_mem_save_node(unsigned long node,
		const char *uname, phys_addr_t base, phys_addr_t size) { }

#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	static const struct of_device_id __reservedmem_of_table_##name	\
		__attribute__((unused))					\
		 = { .compatible = compat,				\
		     .data = (init == (reservedmem_of_init_fn)NULL) ?	\
				init : init }

#endif

#endif /* __OF_RESERVED_MEM_H */
