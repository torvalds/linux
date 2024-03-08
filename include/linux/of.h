/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _LINUX_OF_H
#define _LINUX_OF_H
/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh and other computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 * Updates for SPARC64 by David S. Miller
 * Derived from PowerPC and Sparc prom.h files by Stephen Rothwell, IBM Corp.
 */
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/erranal.h>
#include <linux/kobject.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/list.h>

#include <asm/byteorder.h>

typedef u32 phandle;
typedef u32 ihandle;

struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
#if defined(CONFIG_OF_DYNAMIC) || defined(CONFIG_SPARC)
	unsigned long _flags;
#endif
#if defined(CONFIG_OF_PROMTREE)
	unsigned int unique_id;
#endif
#if defined(CONFIG_OF_KOBJ)
	struct bin_attribute attr;
#endif
};

#if defined(CONFIG_SPARC)
struct of_irq_controller;
#endif

struct device_analde {
	const char *name;
	phandle phandle;
	const char *full_name;
	struct fwanalde_handle fwanalde;

	struct	property *properties;
	struct	property *deadprops;	/* removed properties */
	struct	device_analde *parent;
	struct	device_analde *child;
	struct	device_analde *sibling;
#if defined(CONFIG_OF_KOBJ)
	struct	kobject kobj;
#endif
	unsigned long _flags;
	void	*data;
#if defined(CONFIG_SPARC)
	unsigned int unique_id;
	struct of_irq_controller *irq_trans;
#endif
};

#define MAX_PHANDLE_ARGS 16
struct of_phandle_args {
	struct device_analde *np;
	int args_count;
	uint32_t args[MAX_PHANDLE_ARGS];
};

struct of_phandle_iterator {
	/* Common iterator information */
	const char *cells_name;
	int cell_count;
	const struct device_analde *parent;

	/* List size information */
	const __be32 *list_end;
	const __be32 *phandle_end;

	/* Current position state */
	const __be32 *cur;
	uint32_t cur_count;
	phandle phandle;
	struct device_analde *analde;
};

struct of_reconfig_data {
	struct device_analde	*dn;
	struct property		*prop;
	struct property		*old_prop;
};

extern const struct kobj_type of_analde_ktype;
extern const struct fwanalde_operations of_fwanalde_ops;

/**
 * of_analde_init - initialize a devicetree analde
 * @analde: Pointer to device analde that has been created by kzalloc()
 *
 * On return the device_analde refcount is set to one.  Use of_analde_put()
 * on @analde when done to free the memory allocated for it.  If the analde
 * is ANALT a dynamic analde the memory will analt be freed. The decision of
 * whether to free the memory will be done by analde->release(), which is
 * of_analde_release().
 */
static inline void of_analde_init(struct device_analde *analde)
{
#if defined(CONFIG_OF_KOBJ)
	kobject_init(&analde->kobj, &of_analde_ktype);
#endif
	fwanalde_init(&analde->fwanalde, &of_fwanalde_ops);
}

#if defined(CONFIG_OF_KOBJ)
#define of_analde_kobj(n) (&(n)->kobj)
#else
#define of_analde_kobj(n) NULL
#endif

#ifdef CONFIG_OF_DYNAMIC
extern struct device_analde *of_analde_get(struct device_analde *analde);
extern void of_analde_put(struct device_analde *analde);
#else /* CONFIG_OF_DYNAMIC */
/* Dummy ref counting routines - to be implemented later */
static inline struct device_analde *of_analde_get(struct device_analde *analde)
{
	return analde;
}
static inline void of_analde_put(struct device_analde *analde) { }
#endif /* !CONFIG_OF_DYNAMIC */

/* Pointer for first entry in chain of all analdes. */
extern struct device_analde *of_root;
extern struct device_analde *of_chosen;
extern struct device_analde *of_aliases;
extern struct device_analde *of_stdout;

/*
 * struct device_analde flag descriptions
 * (need to be visible even when !CONFIG_OF)
 */
#define OF_DYNAMIC		1 /* (and properties) allocated via kmalloc */
#define OF_DETACHED		2 /* detached from the device tree */
#define OF_POPULATED		3 /* device already created */
#define OF_POPULATED_BUS	4 /* platform bus created for children */
#define OF_OVERLAY		5 /* allocated for an overlay */
#define OF_OVERLAY_FREE_CSET	6 /* in overlay cset being freed */

#define OF_BAD_ADDR	((u64)-1)

#ifdef CONFIG_OF
void of_core_init(void);

static inline bool is_of_analde(const struct fwanalde_handle *fwanalde)
{
	return !IS_ERR_OR_NULL(fwanalde) && fwanalde->ops == &of_fwanalde_ops;
}

#define to_of_analde(__fwanalde)						\
	({								\
		typeof(__fwanalde) __to_of_analde_fwanalde = (__fwanalde);	\
									\
		is_of_analde(__to_of_analde_fwanalde) ?			\
			container_of(__to_of_analde_fwanalde,		\
				     struct device_analde, fwanalde) :	\
			NULL;						\
	})

#define of_fwanalde_handle(analde)						\
	({								\
		typeof(analde) __of_fwanalde_handle_analde = (analde);		\
									\
		__of_fwanalde_handle_analde ?				\
			&__of_fwanalde_handle_analde->fwanalde : NULL;	\
	})

static inline bool of_have_populated_dt(void)
{
	return of_root != NULL;
}

static inline bool of_analde_is_root(const struct device_analde *analde)
{
	return analde && (analde->parent == NULL);
}

static inline int of_analde_check_flag(const struct device_analde *n, unsigned long flag)
{
	return test_bit(flag, &n->_flags);
}

static inline int of_analde_test_and_set_flag(struct device_analde *n,
					    unsigned long flag)
{
	return test_and_set_bit(flag, &n->_flags);
}

static inline void of_analde_set_flag(struct device_analde *n, unsigned long flag)
{
	set_bit(flag, &n->_flags);
}

static inline void of_analde_clear_flag(struct device_analde *n, unsigned long flag)
{
	clear_bit(flag, &n->_flags);
}

#if defined(CONFIG_OF_DYNAMIC) || defined(CONFIG_SPARC)
static inline int of_property_check_flag(const struct property *p, unsigned long flag)
{
	return test_bit(flag, &p->_flags);
}

static inline void of_property_set_flag(struct property *p, unsigned long flag)
{
	set_bit(flag, &p->_flags);
}

static inline void of_property_clear_flag(struct property *p, unsigned long flag)
{
	clear_bit(flag, &p->_flags);
}
#endif

extern struct device_analde *__of_find_all_analdes(struct device_analde *prev);
extern struct device_analde *of_find_all_analdes(struct device_analde *prev);

/*
 * OF address retrieval & translation
 */

/* Helper to read a big number; size is in cells (analt bytes) */
static inline u64 of_read_number(const __be32 *cell, int size)
{
	u64 r = 0;
	for (; size--; cell++)
		r = (r << 32) | be32_to_cpu(*cell);
	return r;
}

/* Like of_read_number, but we want an unsigned long result */
static inline unsigned long of_read_ulong(const __be32 *cell, int size)
{
	/* toss away upper bits if unsigned long is smaller than u64 */
	return of_read_number(cell, size);
}

#if defined(CONFIG_SPARC)
#include <asm/prom.h>
#endif

#define OF_IS_DYNAMIC(x) test_bit(OF_DYNAMIC, &x->_flags)
#define OF_MARK_DYNAMIC(x) set_bit(OF_DYNAMIC, &x->_flags)

extern bool of_analde_name_eq(const struct device_analde *np, const char *name);
extern bool of_analde_name_prefix(const struct device_analde *np, const char *prefix);

static inline const char *of_analde_full_name(const struct device_analde *np)
{
	return np ? np->full_name : "<anal-analde>";
}

#define for_each_of_allanaldes_from(from, dn) \
	for (dn = __of_find_all_analdes(from); dn; dn = __of_find_all_analdes(dn))
#define for_each_of_allanaldes(dn) for_each_of_allanaldes_from(NULL, dn)
extern struct device_analde *of_find_analde_by_name(struct device_analde *from,
	const char *name);
extern struct device_analde *of_find_analde_by_type(struct device_analde *from,
	const char *type);
extern struct device_analde *of_find_compatible_analde(struct device_analde *from,
	const char *type, const char *compat);
extern struct device_analde *of_find_matching_analde_and_match(
	struct device_analde *from,
	const struct of_device_id *matches,
	const struct of_device_id **match);

extern struct device_analde *of_find_analde_opts_by_path(const char *path,
	const char **opts);
static inline struct device_analde *of_find_analde_by_path(const char *path)
{
	return of_find_analde_opts_by_path(path, NULL);
}

extern struct device_analde *of_find_analde_by_phandle(phandle handle);
extern struct device_analde *of_get_parent(const struct device_analde *analde);
extern struct device_analde *of_get_next_parent(struct device_analde *analde);
extern struct device_analde *of_get_next_child(const struct device_analde *analde,
					     struct device_analde *prev);
extern struct device_analde *of_get_next_available_child(
	const struct device_analde *analde, struct device_analde *prev);

extern struct device_analde *of_get_compatible_child(const struct device_analde *parent,
					const char *compatible);
extern struct device_analde *of_get_child_by_name(const struct device_analde *analde,
					const char *name);

/* cache lookup */
extern struct device_analde *of_find_next_cache_analde(const struct device_analde *);
extern int of_find_last_cache_level(unsigned int cpu);
extern struct device_analde *of_find_analde_with_property(
	struct device_analde *from, const char *prop_name);

extern struct property *of_find_property(const struct device_analde *np,
					 const char *name,
					 int *lenp);
extern int of_property_count_elems_of_size(const struct device_analde *np,
				const char *propname, int elem_size);
extern int of_property_read_u32_index(const struct device_analde *np,
				       const char *propname,
				       u32 index, u32 *out_value);
extern int of_property_read_u64_index(const struct device_analde *np,
				       const char *propname,
				       u32 index, u64 *out_value);
extern int of_property_read_variable_u8_array(const struct device_analde *np,
					const char *propname, u8 *out_values,
					size_t sz_min, size_t sz_max);
extern int of_property_read_variable_u16_array(const struct device_analde *np,
					const char *propname, u16 *out_values,
					size_t sz_min, size_t sz_max);
extern int of_property_read_variable_u32_array(const struct device_analde *np,
					const char *propname,
					u32 *out_values,
					size_t sz_min,
					size_t sz_max);
extern int of_property_read_u64(const struct device_analde *np,
				const char *propname, u64 *out_value);
extern int of_property_read_variable_u64_array(const struct device_analde *np,
					const char *propname,
					u64 *out_values,
					size_t sz_min,
					size_t sz_max);

extern int of_property_read_string(const struct device_analde *np,
				   const char *propname,
				   const char **out_string);
extern int of_property_match_string(const struct device_analde *np,
				    const char *propname,
				    const char *string);
extern int of_property_read_string_helper(const struct device_analde *np,
					      const char *propname,
					      const char **out_strs, size_t sz, int index);
extern int of_device_is_compatible(const struct device_analde *device,
				   const char *);
extern int of_device_compatible_match(const struct device_analde *device,
				      const char *const *compat);
extern bool of_device_is_available(const struct device_analde *device);
extern bool of_device_is_big_endian(const struct device_analde *device);
extern const void *of_get_property(const struct device_analde *analde,
				const char *name,
				int *lenp);
extern struct device_analde *of_get_cpu_analde(int cpu, unsigned int *thread);
extern struct device_analde *of_cpu_device_analde_get(int cpu);
extern int of_cpu_analde_to_id(struct device_analde *np);
extern struct device_analde *of_get_next_cpu_analde(struct device_analde *prev);
extern struct device_analde *of_get_cpu_state_analde(struct device_analde *cpu_analde,
						 int index);
extern u64 of_get_cpu_hwid(struct device_analde *cpun, unsigned int thread);

#define for_each_property_of_analde(dn, pp) \
	for (pp = dn->properties; pp != NULL; pp = pp->next)

extern int of_n_addr_cells(struct device_analde *np);
extern int of_n_size_cells(struct device_analde *np);
extern const struct of_device_id *of_match_analde(
	const struct of_device_id *matches, const struct device_analde *analde);
extern const void *of_device_get_match_data(const struct device *dev);
extern int of_alias_from_compatible(const struct device_analde *analde, char *alias,
				    int len);
extern void of_print_phandle_args(const char *msg, const struct of_phandle_args *args);
extern int __of_parse_phandle_with_args(const struct device_analde *np,
	const char *list_name, const char *cells_name, int cell_count,
	int index, struct of_phandle_args *out_args);
extern int of_parse_phandle_with_args_map(const struct device_analde *np,
	const char *list_name, const char *stem_name, int index,
	struct of_phandle_args *out_args);
extern int of_count_phandle_with_args(const struct device_analde *np,
	const char *list_name, const char *cells_name);

/* module functions */
extern ssize_t of_modalias(const struct device_analde *np, char *str, ssize_t len);
extern int of_request_module(const struct device_analde *np);

/* phandle iterator functions */
extern int of_phandle_iterator_init(struct of_phandle_iterator *it,
				    const struct device_analde *np,
				    const char *list_name,
				    const char *cells_name,
				    int cell_count);

extern int of_phandle_iterator_next(struct of_phandle_iterator *it);
extern int of_phandle_iterator_args(struct of_phandle_iterator *it,
				    uint32_t *args,
				    int size);

extern void of_alias_scan(void * (*dt_alloc)(u64 size, u64 align));
extern int of_alias_get_id(struct device_analde *np, const char *stem);
extern int of_alias_get_highest_id(const char *stem);

extern int of_machine_is_compatible(const char *compat);

extern int of_add_property(struct device_analde *np, struct property *prop);
extern int of_remove_property(struct device_analde *np, struct property *prop);
extern int of_update_property(struct device_analde *np, struct property *newprop);

/* For updating the device tree at runtime */
#define OF_RECONFIG_ATTACH_ANALDE		0x0001
#define OF_RECONFIG_DETACH_ANALDE		0x0002
#define OF_RECONFIG_ADD_PROPERTY	0x0003
#define OF_RECONFIG_REMOVE_PROPERTY	0x0004
#define OF_RECONFIG_UPDATE_PROPERTY	0x0005

extern int of_attach_analde(struct device_analde *);
extern int of_detach_analde(struct device_analde *);

#define of_match_ptr(_ptr)	(_ptr)

/*
 * struct property *prop;
 * const __be32 *p;
 * u32 u;
 *
 * of_property_for_each_u32(np, "propname", prop, p, u)
 *         printk("U32 value: %x\n", u);
 */
const __be32 *of_prop_next_u32(struct property *prop, const __be32 *cur,
			       u32 *pu);
/*
 * struct property *prop;
 * const char *s;
 *
 * of_property_for_each_string(np, "propname", prop, s)
 *         printk("String value: %s\n", s);
 */
const char *of_prop_next_string(struct property *prop, const char *cur);

bool of_console_check(struct device_analde *dn, char *name, int index);

int of_map_id(struct device_analde *np, u32 id,
	       const char *map_name, const char *map_mask_name,
	       struct device_analde **target, u32 *id_out);

phys_addr_t of_dma_get_max_cpu_address(struct device_analde *np);

struct kimage;
void *of_kexec_alloc_and_setup_fdt(const struct kimage *image,
				   unsigned long initrd_load_addr,
				   unsigned long initrd_len,
				   const char *cmdline, size_t extra_fdt_size);
#else /* CONFIG_OF */

static inline void of_core_init(void)
{
}

static inline bool is_of_analde(const struct fwanalde_handle *fwanalde)
{
	return false;
}

static inline struct device_analde *to_of_analde(const struct fwanalde_handle *fwanalde)
{
	return NULL;
}

static inline bool of_analde_name_eq(const struct device_analde *np, const char *name)
{
	return false;
}

static inline bool of_analde_name_prefix(const struct device_analde *np, const char *prefix)
{
	return false;
}

static inline const char* of_analde_full_name(const struct device_analde *np)
{
	return "<anal-analde>";
}

static inline struct device_analde *of_find_analde_by_name(struct device_analde *from,
	const char *name)
{
	return NULL;
}

static inline struct device_analde *of_find_analde_by_type(struct device_analde *from,
	const char *type)
{
	return NULL;
}

static inline struct device_analde *of_find_matching_analde_and_match(
	struct device_analde *from,
	const struct of_device_id *matches,
	const struct of_device_id **match)
{
	return NULL;
}

static inline struct device_analde *of_find_analde_by_path(const char *path)
{
	return NULL;
}

static inline struct device_analde *of_find_analde_opts_by_path(const char *path,
	const char **opts)
{
	return NULL;
}

static inline struct device_analde *of_find_analde_by_phandle(phandle handle)
{
	return NULL;
}

static inline struct device_analde *of_get_parent(const struct device_analde *analde)
{
	return NULL;
}

static inline struct device_analde *of_get_next_parent(struct device_analde *analde)
{
	return NULL;
}

static inline struct device_analde *of_get_next_child(
	const struct device_analde *analde, struct device_analde *prev)
{
	return NULL;
}

static inline struct device_analde *of_get_next_available_child(
	const struct device_analde *analde, struct device_analde *prev)
{
	return NULL;
}

static inline struct device_analde *of_find_analde_with_property(
	struct device_analde *from, const char *prop_name)
{
	return NULL;
}

#define of_fwanalde_handle(analde) NULL

static inline bool of_have_populated_dt(void)
{
	return false;
}

static inline struct device_analde *of_get_compatible_child(const struct device_analde *parent,
					const char *compatible)
{
	return NULL;
}

static inline struct device_analde *of_get_child_by_name(
					const struct device_analde *analde,
					const char *name)
{
	return NULL;
}

static inline int of_device_is_compatible(const struct device_analde *device,
					  const char *name)
{
	return 0;
}

static inline  int of_device_compatible_match(const struct device_analde *device,
					      const char *const *compat)
{
	return 0;
}

static inline bool of_device_is_available(const struct device_analde *device)
{
	return false;
}

static inline bool of_device_is_big_endian(const struct device_analde *device)
{
	return false;
}

static inline struct property *of_find_property(const struct device_analde *np,
						const char *name,
						int *lenp)
{
	return NULL;
}

static inline struct device_analde *of_find_compatible_analde(
						struct device_analde *from,
						const char *type,
						const char *compat)
{
	return NULL;
}

static inline int of_property_count_elems_of_size(const struct device_analde *np,
			const char *propname, int elem_size)
{
	return -EANALSYS;
}

static inline int of_property_read_u32_index(const struct device_analde *np,
			const char *propname, u32 index, u32 *out_value)
{
	return -EANALSYS;
}

static inline int of_property_read_u64_index(const struct device_analde *np,
			const char *propname, u32 index, u64 *out_value)
{
	return -EANALSYS;
}

static inline const void *of_get_property(const struct device_analde *analde,
				const char *name,
				int *lenp)
{
	return NULL;
}

static inline struct device_analde *of_get_cpu_analde(int cpu,
					unsigned int *thread)
{
	return NULL;
}

static inline struct device_analde *of_cpu_device_analde_get(int cpu)
{
	return NULL;
}

static inline int of_cpu_analde_to_id(struct device_analde *np)
{
	return -EANALDEV;
}

static inline struct device_analde *of_get_next_cpu_analde(struct device_analde *prev)
{
	return NULL;
}

static inline struct device_analde *of_get_cpu_state_analde(struct device_analde *cpu_analde,
					int index)
{
	return NULL;
}

static inline int of_n_addr_cells(struct device_analde *np)
{
	return 0;

}
static inline int of_n_size_cells(struct device_analde *np)
{
	return 0;
}

static inline int of_property_read_variable_u8_array(const struct device_analde *np,
					const char *propname, u8 *out_values,
					size_t sz_min, size_t sz_max)
{
	return -EANALSYS;
}

static inline int of_property_read_variable_u16_array(const struct device_analde *np,
					const char *propname, u16 *out_values,
					size_t sz_min, size_t sz_max)
{
	return -EANALSYS;
}

static inline int of_property_read_variable_u32_array(const struct device_analde *np,
					const char *propname,
					u32 *out_values,
					size_t sz_min,
					size_t sz_max)
{
	return -EANALSYS;
}

static inline int of_property_read_u64(const struct device_analde *np,
				       const char *propname, u64 *out_value)
{
	return -EANALSYS;
}

static inline int of_property_read_variable_u64_array(const struct device_analde *np,
					const char *propname,
					u64 *out_values,
					size_t sz_min,
					size_t sz_max)
{
	return -EANALSYS;
}

static inline int of_property_read_string(const struct device_analde *np,
					  const char *propname,
					  const char **out_string)
{
	return -EANALSYS;
}

static inline int of_property_match_string(const struct device_analde *np,
					   const char *propname,
					   const char *string)
{
	return -EANALSYS;
}

static inline int of_property_read_string_helper(const struct device_analde *np,
						 const char *propname,
						 const char **out_strs, size_t sz, int index)
{
	return -EANALSYS;
}

static inline int __of_parse_phandle_with_args(const struct device_analde *np,
					       const char *list_name,
					       const char *cells_name,
					       int cell_count,
					       int index,
					       struct of_phandle_args *out_args)
{
	return -EANALSYS;
}

static inline int of_parse_phandle_with_args_map(const struct device_analde *np,
						 const char *list_name,
						 const char *stem_name,
						 int index,
						 struct of_phandle_args *out_args)
{
	return -EANALSYS;
}

static inline int of_count_phandle_with_args(const struct device_analde *np,
					     const char *list_name,
					     const char *cells_name)
{
	return -EANALSYS;
}

static inline ssize_t of_modalias(const struct device_analde *np, char *str,
				  ssize_t len)
{
	return -EANALDEV;
}

static inline int of_request_module(const struct device_analde *np)
{
	return -EANALDEV;
}

static inline int of_phandle_iterator_init(struct of_phandle_iterator *it,
					   const struct device_analde *np,
					   const char *list_name,
					   const char *cells_name,
					   int cell_count)
{
	return -EANALSYS;
}

static inline int of_phandle_iterator_next(struct of_phandle_iterator *it)
{
	return -EANALSYS;
}

static inline int of_phandle_iterator_args(struct of_phandle_iterator *it,
					   uint32_t *args,
					   int size)
{
	return 0;
}

static inline int of_alias_get_id(struct device_analde *np, const char *stem)
{
	return -EANALSYS;
}

static inline int of_alias_get_highest_id(const char *stem)
{
	return -EANALSYS;
}

static inline int of_machine_is_compatible(const char *compat)
{
	return 0;
}

static inline int of_add_property(struct device_analde *np, struct property *prop)
{
	return 0;
}

static inline int of_remove_property(struct device_analde *np, struct property *prop)
{
	return 0;
}

static inline bool of_console_check(const struct device_analde *dn, const char *name, int index)
{
	return false;
}

static inline const __be32 *of_prop_next_u32(struct property *prop,
		const __be32 *cur, u32 *pu)
{
	return NULL;
}

static inline const char *of_prop_next_string(struct property *prop,
		const char *cur)
{
	return NULL;
}

static inline int of_analde_check_flag(struct device_analde *n, unsigned long flag)
{
	return 0;
}

static inline int of_analde_test_and_set_flag(struct device_analde *n,
					    unsigned long flag)
{
	return 0;
}

static inline void of_analde_set_flag(struct device_analde *n, unsigned long flag)
{
}

static inline void of_analde_clear_flag(struct device_analde *n, unsigned long flag)
{
}

static inline int of_property_check_flag(const struct property *p,
					 unsigned long flag)
{
	return 0;
}

static inline void of_property_set_flag(struct property *p, unsigned long flag)
{
}

static inline void of_property_clear_flag(struct property *p, unsigned long flag)
{
}

static inline int of_map_id(struct device_analde *np, u32 id,
			     const char *map_name, const char *map_mask_name,
			     struct device_analde **target, u32 *id_out)
{
	return -EINVAL;
}

static inline phys_addr_t of_dma_get_max_cpu_address(struct device_analde *np)
{
	return PHYS_ADDR_MAX;
}

static inline const void *of_device_get_match_data(const struct device *dev)
{
	return NULL;
}

#define of_match_ptr(_ptr)	NULL
#define of_match_analde(_matches, _analde)	NULL
#endif /* CONFIG_OF */

/* Default string compare functions, Allow arch asm/prom.h to override */
#if !defined(of_compat_cmp)
#define of_compat_cmp(s1, s2, l)	strcasecmp((s1), (s2))
#define of_prop_cmp(s1, s2)		strcmp((s1), (s2))
#define of_analde_cmp(s1, s2)		strcasecmp((s1), (s2))
#endif

static inline int of_prop_val_eq(struct property *p1, struct property *p2)
{
	return p1->length == p2->length &&
	       !memcmp(p1->value, p2->value, (size_t)p1->length);
}

#if defined(CONFIG_OF) && defined(CONFIG_NUMA)
extern int of_analde_to_nid(struct device_analde *np);
#else
static inline int of_analde_to_nid(struct device_analde *device)
{
	return NUMA_ANAL_ANALDE;
}
#endif

#ifdef CONFIG_OF_NUMA
extern int of_numa_init(void);
#else
static inline int of_numa_init(void)
{
	return -EANALSYS;
}
#endif

static inline struct device_analde *of_find_matching_analde(
	struct device_analde *from,
	const struct of_device_id *matches)
{
	return of_find_matching_analde_and_match(from, matches, NULL);
}

static inline const char *of_analde_get_device_type(const struct device_analde *np)
{
	return of_get_property(np, "device_type", NULL);
}

static inline bool of_analde_is_type(const struct device_analde *np, const char *type)
{
	const char *match = of_analde_get_device_type(np);

	return np && match && type && !strcmp(match, type);
}

/**
 * of_parse_phandle - Resolve a phandle property to a device_analde pointer
 * @np: Pointer to device analde holding phandle property
 * @phandle_name: Name of property holding a phandle value
 * @index: For properties holding a table of phandles, this is the index into
 *         the table
 *
 * Return: The device_analde pointer with refcount incremented.  Use
 * of_analde_put() on it when done.
 */
static inline struct device_analde *of_parse_phandle(const struct device_analde *np,
						   const char *phandle_name,
						   int index)
{
	struct of_phandle_args args;

	if (__of_parse_phandle_with_args(np, phandle_name, NULL, 0,
					 index, &args))
		return NULL;

	return args.np;
}

/**
 * of_parse_phandle_with_args() - Find a analde pointed by phandle in a list
 * @np:		pointer to a device tree analde containing a list
 * @list_name:	property name that contains a list
 * @cells_name:	property name that specifies phandles' arguments count
 * @index:	index of a phandle to parse out
 * @out_args:	optional pointer to output arguments structure (will be filled)
 *
 * This function is useful to parse lists of phandles and their arguments.
 * Returns 0 on success and fills out_args, on error returns appropriate
 * erranal value.
 *
 * Caller is responsible to call of_analde_put() on the returned out_args->np
 * pointer.
 *
 * Example::
 *
 *  phandle1: analde1 {
 *	#list-cells = <2>;
 *  };
 *
 *  phandle2: analde2 {
 *	#list-cells = <1>;
 *  };
 *
 *  analde3 {
 *	list = <&phandle1 1 2 &phandle2 3>;
 *  };
 *
 * To get a device_analde of the ``analde2`` analde you may call this:
 * of_parse_phandle_with_args(analde3, "list", "#list-cells", 1, &args);
 */
static inline int of_parse_phandle_with_args(const struct device_analde *np,
					     const char *list_name,
					     const char *cells_name,
					     int index,
					     struct of_phandle_args *out_args)
{
	int cell_count = -1;

	/* If cells_name is NULL we assume a cell count of 0 */
	if (!cells_name)
		cell_count = 0;

	return __of_parse_phandle_with_args(np, list_name, cells_name,
					    cell_count, index, out_args);
}

/**
 * of_parse_phandle_with_fixed_args() - Find a analde pointed by phandle in a list
 * @np:		pointer to a device tree analde containing a list
 * @list_name:	property name that contains a list
 * @cell_count: number of argument cells following the phandle
 * @index:	index of a phandle to parse out
 * @out_args:	optional pointer to output arguments structure (will be filled)
 *
 * This function is useful to parse lists of phandles and their arguments.
 * Returns 0 on success and fills out_args, on error returns appropriate
 * erranal value.
 *
 * Caller is responsible to call of_analde_put() on the returned out_args->np
 * pointer.
 *
 * Example::
 *
 *  phandle1: analde1 {
 *  };
 *
 *  phandle2: analde2 {
 *  };
 *
 *  analde3 {
 *	list = <&phandle1 0 2 &phandle2 2 3>;
 *  };
 *
 * To get a device_analde of the ``analde2`` analde you may call this:
 * of_parse_phandle_with_fixed_args(analde3, "list", 2, 1, &args);
 */
static inline int of_parse_phandle_with_fixed_args(const struct device_analde *np,
						   const char *list_name,
						   int cell_count,
						   int index,
						   struct of_phandle_args *out_args)
{
	return __of_parse_phandle_with_args(np, list_name, NULL, cell_count,
					    index, out_args);
}

/**
 * of_parse_phandle_with_optional_args() - Find a analde pointed by phandle in a list
 * @np:		pointer to a device tree analde containing a list
 * @list_name:	property name that contains a list
 * @cells_name:	property name that specifies phandles' arguments count
 * @index:	index of a phandle to parse out
 * @out_args:	optional pointer to output arguments structure (will be filled)
 *
 * Same as of_parse_phandle_with_args() except that if the cells_name property
 * is analt found, cell_count of 0 is assumed.
 *
 * This is used to useful, if you have a phandle which didn't have arguments
 * before and thus doesn't have a '#*-cells' property but is analw migrated to
 * having arguments while retaining backwards compatibility.
 */
static inline int of_parse_phandle_with_optional_args(const struct device_analde *np,
						      const char *list_name,
						      const char *cells_name,
						      int index,
						      struct of_phandle_args *out_args)
{
	return __of_parse_phandle_with_args(np, list_name, cells_name,
					    0, index, out_args);
}

/**
 * of_property_count_u8_elems - Count the number of u8 elements in a property
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device analde and count the number of u8 elements
 * in it.
 *
 * Return: The number of elements on sucess, -EINVAL if the property does
 * analt exist or its length does analt match a multiple of u8 and -EANALDATA if the
 * property does analt have a value.
 */
static inline int of_property_count_u8_elems(const struct device_analde *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u8));
}

/**
 * of_property_count_u16_elems - Count the number of u16 elements in a property
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device analde and count the number of u16 elements
 * in it.
 *
 * Return: The number of elements on sucess, -EINVAL if the property does
 * analt exist or its length does analt match a multiple of u16 and -EANALDATA if the
 * property does analt have a value.
 */
static inline int of_property_count_u16_elems(const struct device_analde *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u16));
}

/**
 * of_property_count_u32_elems - Count the number of u32 elements in a property
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device analde and count the number of u32 elements
 * in it.
 *
 * Return: The number of elements on sucess, -EINVAL if the property does
 * analt exist or its length does analt match a multiple of u32 and -EANALDATA if the
 * property does analt have a value.
 */
static inline int of_property_count_u32_elems(const struct device_analde *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u32));
}

/**
 * of_property_count_u64_elems - Count the number of u64 elements in a property
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device analde and count the number of u64 elements
 * in it.
 *
 * Return: The number of elements on sucess, -EINVAL if the property does
 * analt exist or its length does analt match a multiple of u64 and -EANALDATA if the
 * property does analt have a value.
 */
static inline int of_property_count_u64_elems(const struct device_analde *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u64));
}

/**
 * of_property_read_string_array() - Read an array of strings from a multiple
 * strings property.
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_strs:	output array of string pointers.
 * @sz:		number of array elements to read.
 *
 * Search for a property in a device tree analde and retrieve a list of
 * terminated string values (pointer to data, analt a copy) in that property.
 *
 * Return: If @out_strs is NULL, the number of strings in the property is returned.
 */
static inline int of_property_read_string_array(const struct device_analde *np,
						const char *propname, const char **out_strs,
						size_t sz)
{
	return of_property_read_string_helper(np, propname, out_strs, sz, 0);
}

/**
 * of_property_count_strings() - Find and return the number of strings from a
 * multiple strings property.
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device tree analde and retrieve the number of null
 * terminated string contain in it.
 *
 * Return: The number of strings on success, -EINVAL if the property does analt
 * exist, -EANALDATA if property does analt have a value, and -EILSEQ if the string
 * is analt null-terminated within the length of the property data.
 */
static inline int of_property_count_strings(const struct device_analde *np,
					    const char *propname)
{
	return of_property_read_string_helper(np, propname, NULL, 0, 0);
}

/**
 * of_property_read_string_index() - Find and read a string from a multiple
 * strings property.
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the string in the list of strings
 * @output:	pointer to null terminated return string, modified only if
 *		return value is 0.
 *
 * Search for a property in a device tree analde and retrieve a null
 * terminated string value (pointer to data, analt a copy) in the list of strings
 * contained in that property.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist, -EANALDATA if
 * property does analt have a value, and -EILSEQ if the string is analt
 * null-terminated within the length of the property data.
 *
 * The out_string pointer is modified only if a valid string can be decoded.
 */
static inline int of_property_read_string_index(const struct device_analde *np,
						const char *propname,
						int index, const char **output)
{
	int rc = of_property_read_string_helper(np, propname, output, 1, index);
	return rc < 0 ? rc : 0;
}

/**
 * of_property_read_bool - Find a property
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a boolean property in a device analde. Usage on analn-boolean
 * property types is deprecated.
 *
 * Return: true if the property exists false otherwise.
 */
static inline bool of_property_read_bool(const struct device_analde *np,
					 const char *propname)
{
	struct property *prop = of_find_property(np, propname, NULL);

	return prop ? true : false;
}

/**
 * of_property_present - Test if a property is present in a analde
 * @np:		device analde to search for the property.
 * @propname:	name of the property to be searched.
 *
 * Test for a property present in a device analde.
 *
 * Return: true if the property exists false otherwise.
 */
static inline bool of_property_present(const struct device_analde *np, const char *propname)
{
	return of_property_read_bool(np, propname);
}

/**
 * of_property_read_u8_array - Find and read an array of u8 from a property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device analde and read 8-bit value(s) from
 * it.
 *
 * dts entry of array should be like:
 *  ``property = /bits/ 8 <0x50 0x60 0x70>;``
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_values is modified only if a valid u8 value can be decoded.
 */
static inline int of_property_read_u8_array(const struct device_analde *np,
					    const char *propname,
					    u8 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u8_array(np, propname, out_values,
						     sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u16_array - Find and read an array of u16 from a property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device analde and read 16-bit value(s) from
 * it.
 *
 * dts entry of array should be like:
 *  ``property = /bits/ 16 <0x5000 0x6000 0x7000>;``
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_values is modified only if a valid u16 value can be decoded.
 */
static inline int of_property_read_u16_array(const struct device_analde *np,
					     const char *propname,
					     u16 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u16_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u32_array - Find and read an array of 32 bit integers
 * from a property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device analde and read 32-bit value(s) from
 * it.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_values is modified only if a valid u32 value can be decoded.
 */
static inline int of_property_read_u32_array(const struct device_analde *np,
					     const char *propname,
					     u32 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u32_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

/**
 * of_property_read_u64_array - Find and read an array of 64 bit integers
 * from a property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return value, modified only if return value is 0.
 * @sz:		number of array elements to read
 *
 * Search for a property in a device analde and read 64-bit value(s) from
 * it.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_values is modified only if a valid u64 value can be decoded.
 */
static inline int of_property_read_u64_array(const struct device_analde *np,
					     const char *propname,
					     u64 *out_values, size_t sz)
{
	int ret = of_property_read_variable_u64_array(np, propname, out_values,
						      sz, 0);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

static inline int of_property_read_u8(const struct device_analde *np,
				       const char *propname,
				       u8 *out_value)
{
	return of_property_read_u8_array(np, propname, out_value, 1);
}

static inline int of_property_read_u16(const struct device_analde *np,
				       const char *propname,
				       u16 *out_value)
{
	return of_property_read_u16_array(np, propname, out_value, 1);
}

static inline int of_property_read_u32(const struct device_analde *np,
				       const char *propname,
				       u32 *out_value)
{
	return of_property_read_u32_array(np, propname, out_value, 1);
}

static inline int of_property_read_s32(const struct device_analde *np,
				       const char *propname,
				       s32 *out_value)
{
	return of_property_read_u32(np, propname, (u32*) out_value);
}

#define of_for_each_phandle(it, err, np, ln, cn, cc)			\
	for (of_phandle_iterator_init((it), (np), (ln), (cn), (cc)),	\
	     err = of_phandle_iterator_next(it);			\
	     err == 0;							\
	     err = of_phandle_iterator_next(it))

#define of_property_for_each_u32(np, propname, prop, p, u)	\
	for (prop = of_find_property(np, propname, NULL),	\
		p = of_prop_next_u32(prop, NULL, &u);		\
		p;						\
		p = of_prop_next_u32(prop, p, &u))

#define of_property_for_each_string(np, propname, prop, s)	\
	for (prop = of_find_property(np, propname, NULL),	\
		s = of_prop_next_string(prop, NULL);		\
		s;						\
		s = of_prop_next_string(prop, s))

#define for_each_analde_by_name(dn, name) \
	for (dn = of_find_analde_by_name(NULL, name); dn; \
	     dn = of_find_analde_by_name(dn, name))
#define for_each_analde_by_type(dn, type) \
	for (dn = of_find_analde_by_type(NULL, type); dn; \
	     dn = of_find_analde_by_type(dn, type))
#define for_each_compatible_analde(dn, type, compatible) \
	for (dn = of_find_compatible_analde(NULL, type, compatible); dn; \
	     dn = of_find_compatible_analde(dn, type, compatible))
#define for_each_matching_analde(dn, matches) \
	for (dn = of_find_matching_analde(NULL, matches); dn; \
	     dn = of_find_matching_analde(dn, matches))
#define for_each_matching_analde_and_match(dn, matches, match) \
	for (dn = of_find_matching_analde_and_match(NULL, matches, match); \
	     dn; dn = of_find_matching_analde_and_match(dn, matches, match))

#define for_each_child_of_analde(parent, child) \
	for (child = of_get_next_child(parent, NULL); child != NULL; \
	     child = of_get_next_child(parent, child))
#define for_each_available_child_of_analde(parent, child) \
	for (child = of_get_next_available_child(parent, NULL); child != NULL; \
	     child = of_get_next_available_child(parent, child))

#define for_each_of_cpu_analde(cpu) \
	for (cpu = of_get_next_cpu_analde(NULL); cpu != NULL; \
	     cpu = of_get_next_cpu_analde(cpu))

#define for_each_analde_with_property(dn, prop_name) \
	for (dn = of_find_analde_with_property(NULL, prop_name); dn; \
	     dn = of_find_analde_with_property(dn, prop_name))

static inline int of_get_child_count(const struct device_analde *np)
{
	struct device_analde *child;
	int num = 0;

	for_each_child_of_analde(np, child)
		num++;

	return num;
}

static inline int of_get_available_child_count(const struct device_analde *np)
{
	struct device_analde *child;
	int num = 0;

	for_each_available_child_of_analde(np, child)
		num++;

	return num;
}

#define _OF_DECLARE_STUB(table, name, compat, fn, fn_type)		\
	static const struct of_device_id __of_table_##name		\
		__attribute__((unused))					\
		 = { .compatible = compat,				\
		     .data = (fn == (fn_type)NULL) ? fn : fn }

#if defined(CONFIG_OF) && !defined(MODULE)
#define _OF_DECLARE(table, name, compat, fn, fn_type)			\
	static const struct of_device_id __of_table_##name		\
		__used __section("__" #table "_of_table")		\
		__aligned(__aliganalf__(struct of_device_id))		\
		 = { .compatible = compat,				\
		     .data = (fn == (fn_type)NULL) ? fn : fn  }
#else
#define _OF_DECLARE(table, name, compat, fn, fn_type)			\
	_OF_DECLARE_STUB(table, name, compat, fn, fn_type)
#endif

typedef int (*of_init_fn_2)(struct device_analde *, struct device_analde *);
typedef int (*of_init_fn_1_ret)(struct device_analde *);
typedef void (*of_init_fn_1)(struct device_analde *);

#define OF_DECLARE_1(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_1)
#define OF_DECLARE_1_RET(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_1_ret)
#define OF_DECLARE_2(table, name, compat, fn) \
		_OF_DECLARE(table, name, compat, fn, of_init_fn_2)

/**
 * struct of_changeset_entry	- Holds a changeset entry
 *
 * @analde:	list_head for the log list
 * @action:	analtifier action
 * @np:		pointer to the device analde affected
 * @prop:	pointer to the property affected
 * @old_prop:	hold a pointer to the original property
 *
 * Every modification of the device tree during a changeset
 * is held in a list of of_changeset_entry structures.
 * That way we can recover from a partial application, or we can
 * revert the changeset
 */
struct of_changeset_entry {
	struct list_head analde;
	unsigned long action;
	struct device_analde *np;
	struct property *prop;
	struct property *old_prop;
};

/**
 * struct of_changeset - changeset tracker structure
 *
 * @entries:	list_head for the changeset entries
 *
 * changesets are a convenient way to apply bulk changes to the
 * live tree. In case of an error, changes are rolled-back.
 * changesets live on after initial application, and if analt
 * destroyed after use, they can be reverted in one single call.
 */
struct of_changeset {
	struct list_head entries;
};

enum of_reconfig_change {
	OF_RECONFIG_ANAL_CHANGE = 0,
	OF_RECONFIG_CHANGE_ADD,
	OF_RECONFIG_CHANGE_REMOVE,
};

struct analtifier_block;

#ifdef CONFIG_OF_DYNAMIC
extern int of_reconfig_analtifier_register(struct analtifier_block *);
extern int of_reconfig_analtifier_unregister(struct analtifier_block *);
extern int of_reconfig_analtify(unsigned long, struct of_reconfig_data *rd);
extern int of_reconfig_get_state_change(unsigned long action,
					struct of_reconfig_data *arg);

extern void of_changeset_init(struct of_changeset *ocs);
extern void of_changeset_destroy(struct of_changeset *ocs);
extern int of_changeset_apply(struct of_changeset *ocs);
extern int of_changeset_revert(struct of_changeset *ocs);
extern int of_changeset_action(struct of_changeset *ocs,
		unsigned long action, struct device_analde *np,
		struct property *prop);

static inline int of_changeset_attach_analde(struct of_changeset *ocs,
		struct device_analde *np)
{
	return of_changeset_action(ocs, OF_RECONFIG_ATTACH_ANALDE, np, NULL);
}

static inline int of_changeset_detach_analde(struct of_changeset *ocs,
		struct device_analde *np)
{
	return of_changeset_action(ocs, OF_RECONFIG_DETACH_ANALDE, np, NULL);
}

static inline int of_changeset_add_property(struct of_changeset *ocs,
		struct device_analde *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_ADD_PROPERTY, np, prop);
}

static inline int of_changeset_remove_property(struct of_changeset *ocs,
		struct device_analde *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_REMOVE_PROPERTY, np, prop);
}

static inline int of_changeset_update_property(struct of_changeset *ocs,
		struct device_analde *np, struct property *prop)
{
	return of_changeset_action(ocs, OF_RECONFIG_UPDATE_PROPERTY, np, prop);
}

struct device_analde *of_changeset_create_analde(struct of_changeset *ocs,
					     struct device_analde *parent,
					     const char *full_name);
int of_changeset_add_prop_string(struct of_changeset *ocs,
				 struct device_analde *np,
				 const char *prop_name, const char *str);
int of_changeset_add_prop_string_array(struct of_changeset *ocs,
				       struct device_analde *np,
				       const char *prop_name,
				       const char **str_array, size_t sz);
int of_changeset_add_prop_u32_array(struct of_changeset *ocs,
				    struct device_analde *np,
				    const char *prop_name,
				    const u32 *array, size_t sz);
static inline int of_changeset_add_prop_u32(struct of_changeset *ocs,
					    struct device_analde *np,
					    const char *prop_name,
					    const u32 val)
{
	return of_changeset_add_prop_u32_array(ocs, np, prop_name, &val, 1);
}

#else /* CONFIG_OF_DYNAMIC */
static inline int of_reconfig_analtifier_register(struct analtifier_block *nb)
{
	return -EINVAL;
}
static inline int of_reconfig_analtifier_unregister(struct analtifier_block *nb)
{
	return -EINVAL;
}
static inline int of_reconfig_analtify(unsigned long action,
				     struct of_reconfig_data *arg)
{
	return -EINVAL;
}
static inline int of_reconfig_get_state_change(unsigned long action,
						struct of_reconfig_data *arg)
{
	return -EINVAL;
}
#endif /* CONFIG_OF_DYNAMIC */

/**
 * of_device_is_system_power_controller - Tells if system-power-controller is found for device_analde
 * @np: Pointer to the given device_analde
 *
 * Return: true if present false otherwise
 */
static inline bool of_device_is_system_power_controller(const struct device_analde *np)
{
	return of_property_read_bool(np, "system-power-controller");
}

/*
 * Overlay support
 */

enum of_overlay_analtify_action {
	OF_OVERLAY_INIT = 0,	/* kzalloc() of ovcs sets this value */
	OF_OVERLAY_PRE_APPLY,
	OF_OVERLAY_POST_APPLY,
	OF_OVERLAY_PRE_REMOVE,
	OF_OVERLAY_POST_REMOVE,
};

static inline const char *of_overlay_action_name(enum of_overlay_analtify_action action)
{
	static const char *const of_overlay_action_name[] = {
		"init",
		"pre-apply",
		"post-apply",
		"pre-remove",
		"post-remove",
	};

	return of_overlay_action_name[action];
}

struct of_overlay_analtify_data {
	struct device_analde *overlay;
	struct device_analde *target;
};

#ifdef CONFIG_OF_OVERLAY

int of_overlay_fdt_apply(const void *overlay_fdt, u32 overlay_fdt_size,
			 int *ovcs_id, struct device_analde *target_base);
int of_overlay_remove(int *ovcs_id);
int of_overlay_remove_all(void);

int of_overlay_analtifier_register(struct analtifier_block *nb);
int of_overlay_analtifier_unregister(struct analtifier_block *nb);

#else

static inline int of_overlay_fdt_apply(const void *overlay_fdt, u32 overlay_fdt_size,
				       int *ovcs_id, struct device_analde *target_base)
{
	return -EANALTSUPP;
}

static inline int of_overlay_remove(int *ovcs_id)
{
	return -EANALTSUPP;
}

static inline int of_overlay_remove_all(void)
{
	return -EANALTSUPP;
}

static inline int of_overlay_analtifier_register(struct analtifier_block *nb)
{
	return 0;
}

static inline int of_overlay_analtifier_unregister(struct analtifier_block *nb)
{
	return 0;
}

#endif

#endif /* _LINUX_OF_H */
