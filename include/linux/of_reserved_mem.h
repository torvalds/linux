#ifndef __OF_RESERVED_MEM_H
#define __OF_RESERVED_MEM_H

struct reserved_mem {
	const char			*name;
	unsigned long			fdt_node;
	phys_addr_t			base;
	phys_addr_t			size;
};

#ifdef CONFIG_OF_RESERVED_MEM
void fdt_init_reserved_mem(void);
void fdt_reserved_mem_save_node(unsigned long node, const char *uname,
			       phys_addr_t base, phys_addr_t size);
#else
static inline void fdt_init_reserved_mem(void) { }
static inline void fdt_reserved_mem_save_node(unsigned long node,
		const char *uname, phys_addr_t base, phys_addr_t size) { }
#endif

#endif /* __OF_RESERVED_MEM_H */
