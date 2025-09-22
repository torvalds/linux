/* Public domain. */

#ifndef _LINUX_OF_H
#define _LINUX_OF_H

#ifdef __macppc__
static inline int
of_machine_is_compatible(const char *model)
{
	extern char *hw_prod;
	return (strcmp(model, hw_prod) == 0);
}
#endif

struct device_node {
	const char *full_name;
};

#define of_node	dv_cfdata

struct device_node *__of_get_compatible_child(void *, const char *);
#define of_get_compatible_child(d, n) \
	__of_get_compatible_child(&(d), (n))

struct device_node *__of_get_child_by_name(void *, const char *);
#define of_get_child_by_name(d, n) \
	__of_get_child_by_name(&(d), (n))

#define of_node_put(p)

struct device_node *__of_devnode(void *);
#define __of_node(arg) \
	__builtin_types_compatible_p(typeof(arg), struct device_node *) ? \
		(struct device_node *)arg : __of_devnode(&arg)

int	__of_property_present(struct device_node *, const char *);
#define of_property_present(n, p) \
	__of_property_present(__of_node(n), (p))

int	__of_property_read_variable_u32_array(struct device_node *,
	    const char *, uint32_t *, size_t, size_t);
#define of_property_read_u32(n, p, o) \
	__of_property_read_variable_u32_array(__of_node(n), (p), (o), 1, 1)
#define of_property_read_variable_u32_array(n, p, o, l, h) \
	__of_property_read_variable_u32_array(__of_node(n), (p), (o), (l), (h))

int	__of_property_read_variable_u64_array(struct device_node *,
	    const char *, uint64_t *, size_t, size_t);
#define of_property_read_u64(n, p, o) \
	__of_property_read_variable_u64_array(__of_node(n), (p), (o), 1, 1)

int	__of_property_match_string(struct device_node *,
	    const char *, const char *);
#define of_property_match_string(n, a, b) \
	__of_property_match_string(__of_node(n), (a), (b))

struct device_node *__of_parse_phandle(struct device_node *,
	    const char *, int);
#define of_parse_phandle(n, a, b) \
	__of_parse_phandle(__of_node(n), (a), (b))

struct of_phandle_args {
	struct device_node *np;
	int args_count;
	uint32_t args[5];
};

int	__of_parse_phandle_with_args(struct device_node *,
	    const char *, const char *, int, struct of_phandle_args *);
#define of_parse_phandle_with_args(n, a, b, c, d)			\
	__of_parse_phandle_with_args(__of_node(n), (a), (b), (c), (d))

int	of_device_is_available(struct device_node *);

struct of_device_id {
	const char *compatible;
	const void *data;
};

struct device_node *__matching_node(struct device_node *,
	    const struct of_device_id *);
#define for_each_matching_node(a, b) \
	for (a = __matching_node(NULL, b); a; a = __matching_node(a, b))

#endif
