/*
 * Copyright 2014-2016 by Open Source Security, Inc., Brad Spengler <spender@grsecurity.net>
 *                   and PaX Team <pageexec@freemail.hu>
 * Licensed under the GPL v2
 *
 * Note: the choice of the license means that the compilation process is
 *       NOT 'eligible' as defined by gcc's library exception to the GPL v3,
 *       but for the kernel it doesn't matter since it doesn't link against
 *       any of the gcc libraries
 *
 * Usage:
 * $ # for 4.5/4.6/C based 4.7
 * $ gcc -I`gcc -print-file-name=plugin`/include -I`gcc -print-file-name=plugin`/include/c-family -fPIC -shared -O2 -o randomize_layout_plugin.so randomize_layout_plugin.c
 * $ # for C++ based 4.7/4.8+
 * $ g++ -I`g++ -print-file-name=plugin`/include -I`g++ -print-file-name=plugin`/include/c-family -fPIC -shared -O2 -o randomize_layout_plugin.so randomize_layout_plugin.c
 * $ gcc -fplugin=./randomize_layout_plugin.so test.c -O2
 */

#include "gcc-common.h"
#include "randomize_layout_seed.h"

#if BUILDING_GCC_MAJOR < 4 || (BUILDING_GCC_MAJOR == 4 && BUILDING_GCC_MINOR < 7)
#error "The RANDSTRUCT plugin requires GCC 4.7 or newer."
#endif

#define ORIG_TYPE_NAME(node) \
	(TYPE_NAME(TYPE_MAIN_VARIANT(node)) != NULL_TREE ? ((const unsigned char *)IDENTIFIER_POINTER(TYPE_NAME(TYPE_MAIN_VARIANT(node)))) : (const unsigned char *)"anonymous")

#define INFORM(loc, msg, ...)	inform(loc, "randstruct: " msg, ##__VA_ARGS__)
#define MISMATCH(loc, how, ...)	INFORM(loc, "casting between randomized structure pointer types (" how "): %qT and %qT\n", __VA_ARGS__)

__visible int plugin_is_GPL_compatible;

static int performance_mode;

static struct plugin_info randomize_layout_plugin_info = {
	.version	= "201402201816vanilla",
	.help		= "disable\t\t\tdo not activate plugin\n"
			  "performance-mode\tenable cacheline-aware layout randomization\n"
};

struct whitelist_entry {
	const char *pathname;
	const char *lhs;
	const char *rhs;
};

static const struct whitelist_entry whitelist[] = {
	/* NIU overloads mapping with page struct */
	{ "drivers/net/ethernet/sun/niu.c", "page", "address_space" },
	/* unix_skb_parms via UNIXCB() buffer */
	{ "net/unix/af_unix.c", "unix_skb_parms", "char" },
	/* big_key payload.data struct splashing */
	{ "security/keys/big_key.c", "path", "void *" },
	/* walk struct security_hook_heads as an array of struct hlist_head */
	{ "security/security.c", "hlist_head", "security_hook_heads" },
	{ }
};

/* from old Linux dcache.h */
static inline unsigned long
partial_name_hash(unsigned long c, unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}
static inline unsigned int
name_hash(const unsigned char *name)
{
	unsigned long hash = 0;
	unsigned int len = strlen((const char *)name);
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return (unsigned int)hash;
}

static tree handle_randomize_layout_attr(tree *node, tree name, tree args, int flags, bool *no_add_attrs)
{
	tree type;

	*no_add_attrs = true;
	if (TREE_CODE(*node) == FUNCTION_DECL) {
		error("%qE attribute does not apply to functions (%qF)", name, *node);
		return NULL_TREE;
	}

	if (TREE_CODE(*node) == PARM_DECL) {
		error("%qE attribute does not apply to function parameters (%qD)", name, *node);
		return NULL_TREE;
	}

	if (TREE_CODE(*node) == VAR_DECL) {
		error("%qE attribute does not apply to variables (%qD)", name, *node);
		return NULL_TREE;
	}

	if (TYPE_P(*node)) {
		type = *node;
	} else {
		gcc_assert(TREE_CODE(*node) == TYPE_DECL);
		type = TREE_TYPE(*node);
	}

	if (TREE_CODE(type) != RECORD_TYPE) {
		error("%qE attribute used on %qT applies to struct types only", name, type);
		return NULL_TREE;
	}

	if (lookup_attribute(IDENTIFIER_POINTER(name), TYPE_ATTRIBUTES(type))) {
		error("%qE attribute is already applied to the type %qT", name, type);
		return NULL_TREE;
	}

	*no_add_attrs = false;

	return NULL_TREE;
}

/* set on complete types that we don't need to inspect further at all */
static tree handle_randomize_considered_attr(tree *node, tree name, tree args, int flags, bool *no_add_attrs)
{
	*no_add_attrs = false;
	return NULL_TREE;
}

/*
 * set on types that we've performed a shuffle on, to prevent re-shuffling
 * this does not preclude us from inspecting its fields for potential shuffles
 */
static tree handle_randomize_performed_attr(tree *node, tree name, tree args, int flags, bool *no_add_attrs)
{
	*no_add_attrs = false;
	return NULL_TREE;
}

/*
 * 64bit variant of Bob Jenkins' public domain PRNG
 * 256 bits of internal state
 */

typedef unsigned long long u64;

typedef struct ranctx { u64 a; u64 b; u64 c; u64 d; } ranctx;

#define rot(x,k) (((x)<<(k))|((x)>>(64-(k))))
static u64 ranval(ranctx *x) {
	u64 e = x->a - rot(x->b, 7);
	x->a = x->b ^ rot(x->c, 13);
	x->b = x->c + rot(x->d, 37);
	x->c = x->d + e;
	x->d = e + x->a;
	return x->d;
}

static void raninit(ranctx *x, u64 *seed) {
	int i;

	x->a = seed[0];
	x->b = seed[1];
	x->c = seed[2];
	x->d = seed[3];

	for (i=0; i < 30; ++i)
		(void)ranval(x);
}

static u64 shuffle_seed[4];

struct partition_group {
	tree tree_start;
	unsigned long start;
	unsigned long length;
};

static void partition_struct(tree *fields, unsigned long length, struct partition_group *size_groups, unsigned long *num_groups)
{
	unsigned long i;
	unsigned long accum_size = 0;
	unsigned long accum_length = 0;
	unsigned long group_idx = 0;

	gcc_assert(length < INT_MAX);

	memset(size_groups, 0, sizeof(struct partition_group) * length);

	for (i = 0; i < length; i++) {
		if (size_groups[group_idx].tree_start == NULL_TREE) {
			size_groups[group_idx].tree_start = fields[i];
			size_groups[group_idx].start = i;
			accum_length = 0;
			accum_size = 0;
		}
		accum_size += (unsigned long)int_size_in_bytes(TREE_TYPE(fields[i]));
		accum_length++;
		if (accum_size >= 64) {
			size_groups[group_idx].length = accum_length;
			accum_length = 0;
			group_idx++;
		}
	}

	if (size_groups[group_idx].tree_start != NULL_TREE &&
	    !size_groups[group_idx].length) {
		size_groups[group_idx].length = accum_length;
		group_idx++;
	}

	*num_groups = group_idx;
}

static void performance_shuffle(tree *newtree, unsigned long length, ranctx *prng_state)
{
	unsigned long i, x;
	struct partition_group size_group[length];
	unsigned long num_groups = 0;
	unsigned long randnum;

	partition_struct(newtree, length, (struct partition_group *)&size_group, &num_groups);
	for (i = num_groups - 1; i > 0; i--) {
		struct partition_group tmp;
		randnum = ranval(prng_state) % (i + 1);
		tmp = size_group[i];
		size_group[i] = size_group[randnum];
		size_group[randnum] = tmp;
	}

	for (x = 0; x < num_groups; x++) {
		for (i = size_group[x].start + size_group[x].length - 1; i > size_group[x].start; i--) {
			tree tmp;
			if (DECL_BIT_FIELD_TYPE(newtree[i]))
				continue;
			randnum = ranval(prng_state) % (i + 1);
			// we could handle this case differently if desired
			if (DECL_BIT_FIELD_TYPE(newtree[randnum]))
				continue;
			tmp = newtree[i];
			newtree[i] = newtree[randnum];
			newtree[randnum] = tmp;
		}
	}
}

static void full_shuffle(tree *newtree, unsigned long length, ranctx *prng_state)
{
	unsigned long i, randnum;

	for (i = length - 1; i > 0; i--) {
		tree tmp;
		randnum = ranval(prng_state) % (i + 1);
		tmp = newtree[i];
		newtree[i] = newtree[randnum];
		newtree[randnum] = tmp;
	}
}

/* modern in-place Fisher-Yates shuffle */
static void shuffle(const_tree type, tree *newtree, unsigned long length)
{
	unsigned long i;
	u64 seed[4];
	ranctx prng_state;
	const unsigned char *structname;

	if (length == 0)
		return;

	gcc_assert(TREE_CODE(type) == RECORD_TYPE);

	structname = ORIG_TYPE_NAME(type);

#ifdef __DEBUG_PLUGIN
	fprintf(stderr, "Shuffling struct %s %p\n", (const char *)structname, type);
#ifdef __DEBUG_VERBOSE
	debug_tree((tree)type);
#endif
#endif

	for (i = 0; i < 4; i++) {
		seed[i] = shuffle_seed[i];
		seed[i] ^= name_hash(structname);
	}

	raninit(&prng_state, (u64 *)&seed);

	if (performance_mode)
		performance_shuffle(newtree, length, &prng_state);
	else
		full_shuffle(newtree, length, &prng_state);
}

static bool is_flexible_array(const_tree field)
{
	const_tree fieldtype;
	const_tree typesize;
	const_tree elemtype;
	const_tree elemsize;

	fieldtype = TREE_TYPE(field);
	typesize = TYPE_SIZE(fieldtype);

	if (TREE_CODE(fieldtype) != ARRAY_TYPE)
		return false;

	elemtype = TREE_TYPE(fieldtype);
	elemsize = TYPE_SIZE(elemtype);

	/* size of type is represented in bits */

	if (typesize == NULL_TREE && TYPE_DOMAIN(fieldtype) != NULL_TREE &&
	    TYPE_MAX_VALUE(TYPE_DOMAIN(fieldtype)) == NULL_TREE)
		return true;

	if (typesize != NULL_TREE &&
	    (TREE_CONSTANT(typesize) && (!tree_to_uhwi(typesize) ||
	     tree_to_uhwi(typesize) == tree_to_uhwi(elemsize))))
		return true;

	return false;
}

static int relayout_struct(tree type)
{
	unsigned long num_fields = (unsigned long)list_length(TYPE_FIELDS(type));
	unsigned long shuffle_length = num_fields;
	tree field;
	tree newtree[num_fields];
	unsigned long i;
	tree list;
	tree variant;
	tree main_variant;
	expanded_location xloc;
	bool has_flexarray = false;

	if (TYPE_FIELDS(type) == NULL_TREE)
		return 0;

	if (num_fields < 2)
		return 0;

	gcc_assert(TREE_CODE(type) == RECORD_TYPE);

	gcc_assert(num_fields < INT_MAX);

	if (lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(type)) ||
	    lookup_attribute("no_randomize_layout", TYPE_ATTRIBUTES(TYPE_MAIN_VARIANT(type))))
		return 0;

	/* Workaround for 3rd-party VirtualBox source that we can't modify ourselves */
	if (!strcmp((const char *)ORIG_TYPE_NAME(type), "INTNETTRUNKFACTORY") ||
	    !strcmp((const char *)ORIG_TYPE_NAME(type), "RAWPCIFACTORY"))
		return 0;

	/* throw out any structs in uapi */
	xloc = expand_location(DECL_SOURCE_LOCATION(TYPE_FIELDS(type)));

	if (strstr(xloc.file, "/uapi/"))
		error(G_("attempted to randomize userland API struct %s"), ORIG_TYPE_NAME(type));

	for (field = TYPE_FIELDS(type), i = 0; field; field = TREE_CHAIN(field), i++) {
		gcc_assert(TREE_CODE(field) == FIELD_DECL);
		newtree[i] = field;
	}

	/*
	 * enforce that we don't randomize the layout of the last
	 * element of a struct if it's a 0 or 1-length array
	 * or a proper flexible array
	 */
	if (is_flexible_array(newtree[num_fields - 1])) {
		has_flexarray = true;
		shuffle_length--;
	}

	shuffle(type, (tree *)newtree, shuffle_length);

	/*
	 * set up a bogus anonymous struct field designed to error out on unnamed struct initializers
	 * as gcc provides no other way to detect such code
	 */
	list = make_node(FIELD_DECL);
	TREE_CHAIN(list) = newtree[0];
	TREE_TYPE(list) = void_type_node;
	DECL_SIZE(list) = bitsize_zero_node;
	DECL_NONADDRESSABLE_P(list) = 1;
	DECL_FIELD_BIT_OFFSET(list) = bitsize_zero_node;
	DECL_SIZE_UNIT(list) = size_zero_node;
	DECL_FIELD_OFFSET(list) = size_zero_node;
	DECL_CONTEXT(list) = type;
	// to satisfy the constify plugin
	TREE_READONLY(list) = 1;

	for (i = 0; i < num_fields - 1; i++)
		TREE_CHAIN(newtree[i]) = newtree[i+1];
	TREE_CHAIN(newtree[num_fields - 1]) = NULL_TREE;

	main_variant = TYPE_MAIN_VARIANT(type);
	for (variant = main_variant; variant; variant = TYPE_NEXT_VARIANT(variant)) {
		TYPE_FIELDS(variant) = list;
		TYPE_ATTRIBUTES(variant) = copy_list(TYPE_ATTRIBUTES(variant));
		TYPE_ATTRIBUTES(variant) = tree_cons(get_identifier("randomize_performed"), NULL_TREE, TYPE_ATTRIBUTES(variant));
		TYPE_ATTRIBUTES(variant) = tree_cons(get_identifier("designated_init"), NULL_TREE, TYPE_ATTRIBUTES(variant));
		if (has_flexarray)
			TYPE_ATTRIBUTES(type) = tree_cons(get_identifier("has_flexarray"), NULL_TREE, TYPE_ATTRIBUTES(type));
	}

	/*
	 * force a re-layout of the main variant
	 * the TYPE_SIZE for all variants will be recomputed
	 * by finalize_type_size()
	 */
	TYPE_SIZE(main_variant) = NULL_TREE;
	layout_type(main_variant);
	gcc_assert(TYPE_SIZE(main_variant) != NULL_TREE);

	return 1;
}

/* from constify plugin */
static const_tree get_field_type(const_tree field)
{
	return strip_array_types(TREE_TYPE(field));
}

/* from constify plugin */
static bool is_fptr(const_tree fieldtype)
{
	if (TREE_CODE(fieldtype) != POINTER_TYPE)
		return false;

	return TREE_CODE(TREE_TYPE(fieldtype)) == FUNCTION_TYPE;
}

/* derived from constify plugin */
static int is_pure_ops_struct(const_tree node)
{
	const_tree field;

	gcc_assert(TREE_CODE(node) == RECORD_TYPE || TREE_CODE(node) == UNION_TYPE);

	for (field = TYPE_FIELDS(node); field; field = TREE_CHAIN(field)) {
		const_tree fieldtype = get_field_type(field);
		enum tree_code code = TREE_CODE(fieldtype);

		if (node == fieldtype)
			continue;

		if (!is_fptr(fieldtype))
			return 0;

		if (code != RECORD_TYPE && code != UNION_TYPE)
			continue;

		if (!is_pure_ops_struct(fieldtype))
			return 0;
	}

	return 1;
}

static void randomize_type(tree type)
{
	tree variant;

	gcc_assert(TREE_CODE(type) == RECORD_TYPE);

	if (lookup_attribute("randomize_considered", TYPE_ATTRIBUTES(type)))
		return;

	if (lookup_attribute("randomize_layout", TYPE_ATTRIBUTES(TYPE_MAIN_VARIANT(type))) || is_pure_ops_struct(type))
		relayout_struct(type);

	for (variant = TYPE_MAIN_VARIANT(type); variant; variant = TYPE_NEXT_VARIANT(variant)) {
		TYPE_ATTRIBUTES(type) = copy_list(TYPE_ATTRIBUTES(type));
		TYPE_ATTRIBUTES(type) = tree_cons(get_identifier("randomize_considered"), NULL_TREE, TYPE_ATTRIBUTES(type));
	}
#ifdef __DEBUG_PLUGIN
	fprintf(stderr, "Marking randomize_considered on struct %s\n", ORIG_TYPE_NAME(type));
#ifdef __DEBUG_VERBOSE
	debug_tree(type);
#endif
#endif
}

static void update_decl_size(tree decl)
{
	tree lastval, lastidx, field, init, type, flexsize;
	unsigned HOST_WIDE_INT len;

	type = TREE_TYPE(decl);

	if (!lookup_attribute("has_flexarray", TYPE_ATTRIBUTES(type)))
		return;

	init = DECL_INITIAL(decl);
	if (init == NULL_TREE || init == error_mark_node)
		return;

	if (TREE_CODE(init) != CONSTRUCTOR)
		return;

	len = CONSTRUCTOR_NELTS(init);
        if (!len)
		return;

	lastval = CONSTRUCTOR_ELT(init, CONSTRUCTOR_NELTS(init) - 1)->value;
	lastidx = CONSTRUCTOR_ELT(init, CONSTRUCTOR_NELTS(init) - 1)->index;

	for (field = TYPE_FIELDS(TREE_TYPE(decl)); TREE_CHAIN(field); field = TREE_CHAIN(field))
		;

	if (lastidx != field)
		return;

	if (TREE_CODE(lastval) != STRING_CST) {
		error("Only string constants are supported as initializers "
		      "for randomized structures with flexible arrays");
		return;
	}

	flexsize = bitsize_int(TREE_STRING_LENGTH(lastval) *
		tree_to_uhwi(TYPE_SIZE(TREE_TYPE(TREE_TYPE(lastval)))));

	DECL_SIZE(decl) = size_binop(PLUS_EXPR, TYPE_SIZE(type), flexsize);

	return;
}


static void randomize_layout_finish_decl(void *event_data, void *data)
{
	tree decl = (tree)event_data;
	tree type;

	if (decl == NULL_TREE || decl == error_mark_node)
		return;

	type = TREE_TYPE(decl);

	if (TREE_CODE(decl) != VAR_DECL)
		return;

	if (TREE_CODE(type) != RECORD_TYPE && TREE_CODE(type) != UNION_TYPE)
		return;

	if (!lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(type)))
		return;

	DECL_SIZE(decl) = 0;
	DECL_SIZE_UNIT(decl) = 0;
	SET_DECL_ALIGN(decl, 0);
	SET_DECL_MODE (decl, VOIDmode);
	SET_DECL_RTL(decl, 0);
	update_decl_size(decl);
	layout_decl(decl, 0);
}

static void finish_type(void *event_data, void *data)
{
	tree type = (tree)event_data;

	if (type == NULL_TREE || type == error_mark_node)
		return;

	if (TREE_CODE(type) != RECORD_TYPE)
		return;

	if (TYPE_FIELDS(type) == NULL_TREE)
		return;

	if (lookup_attribute("randomize_considered", TYPE_ATTRIBUTES(type)))
		return;

#ifdef __DEBUG_PLUGIN
	fprintf(stderr, "Calling randomize_type on %s\n", ORIG_TYPE_NAME(type));
#endif
#ifdef __DEBUG_VERBOSE
	debug_tree(type);
#endif
	randomize_type(type);

	return;
}

static struct attribute_spec randomize_layout_attr = { };
static struct attribute_spec no_randomize_layout_attr = { };
static struct attribute_spec randomize_considered_attr = { };
static struct attribute_spec randomize_performed_attr = { };

static void register_attributes(void *event_data, void *data)
{
	randomize_layout_attr.name		= "randomize_layout";
	randomize_layout_attr.type_required	= true;
	randomize_layout_attr.handler		= handle_randomize_layout_attr;
#if BUILDING_GCC_VERSION >= 4007
	randomize_layout_attr.affects_type_identity = true;
#endif

	no_randomize_layout_attr.name		= "no_randomize_layout";
	no_randomize_layout_attr.type_required	= true;
	no_randomize_layout_attr.handler	= handle_randomize_layout_attr;
#if BUILDING_GCC_VERSION >= 4007
	no_randomize_layout_attr.affects_type_identity = true;
#endif

	randomize_considered_attr.name		= "randomize_considered";
	randomize_considered_attr.type_required	= true;
	randomize_considered_attr.handler	= handle_randomize_considered_attr;

	randomize_performed_attr.name		= "randomize_performed";
	randomize_performed_attr.type_required	= true;
	randomize_performed_attr.handler	= handle_randomize_performed_attr;

	register_attribute(&randomize_layout_attr);
	register_attribute(&no_randomize_layout_attr);
	register_attribute(&randomize_considered_attr);
	register_attribute(&randomize_performed_attr);
}

static void check_bad_casts_in_constructor(tree var, tree init)
{
	unsigned HOST_WIDE_INT idx;
	tree field, val;
	tree field_type, val_type;

	FOR_EACH_CONSTRUCTOR_ELT(CONSTRUCTOR_ELTS(init), idx, field, val) {
		if (TREE_CODE(val) == CONSTRUCTOR) {
			check_bad_casts_in_constructor(var, val);
			continue;
		}

		/* pipacs' plugin creates franken-arrays that differ from those produced by
		   normal code which all have valid 'field' trees. work around this */
		if (field == NULL_TREE)
			continue;
		field_type = TREE_TYPE(field);
		val_type = TREE_TYPE(val);

		if (TREE_CODE(field_type) != POINTER_TYPE || TREE_CODE(val_type) != POINTER_TYPE)
			continue;

		if (field_type == val_type)
			continue;

		field_type = TYPE_MAIN_VARIANT(strip_array_types(TYPE_MAIN_VARIANT(TREE_TYPE(field_type))));
		val_type = TYPE_MAIN_VARIANT(strip_array_types(TYPE_MAIN_VARIANT(TREE_TYPE(val_type))));

		if (field_type == void_type_node)
			continue;
		if (field_type == val_type)
			continue;
		if (TREE_CODE(val_type) != RECORD_TYPE)
			continue;

		if (!lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(val_type)))
			continue;
		MISMATCH(DECL_SOURCE_LOCATION(var), "constructor\n", TYPE_MAIN_VARIANT(field_type), TYPE_MAIN_VARIANT(val_type));
	}
}

/* derived from the constify plugin */
static void check_global_variables(void *event_data, void *data)
{
	struct varpool_node *node;
	tree init;

	FOR_EACH_VARIABLE(node) {
		tree var = NODE_DECL(node);
		init = DECL_INITIAL(var);
		if (init == NULL_TREE)
			continue;

		if (TREE_CODE(init) != CONSTRUCTOR)
			continue;

		check_bad_casts_in_constructor(var, init);
	}
}

static bool dominated_by_is_err(const_tree rhs, basic_block bb)
{
	basic_block dom;
	gimple dom_stmt;
	gimple call_stmt;
	const_tree dom_lhs;
	const_tree poss_is_err_cond;
	const_tree poss_is_err_func;
	const_tree is_err_arg;

	dom = get_immediate_dominator(CDI_DOMINATORS, bb);
	if (!dom)
		return false;

	dom_stmt = last_stmt(dom);
	if (!dom_stmt)
		return false;

	if (gimple_code(dom_stmt) != GIMPLE_COND)
		return false;

	if (gimple_cond_code(dom_stmt) != NE_EXPR)
		return false;

	if (!integer_zerop(gimple_cond_rhs(dom_stmt)))
		return false;

	poss_is_err_cond = gimple_cond_lhs(dom_stmt);

	if (TREE_CODE(poss_is_err_cond) != SSA_NAME)
		return false;

	call_stmt = SSA_NAME_DEF_STMT(poss_is_err_cond);

	if (gimple_code(call_stmt) != GIMPLE_CALL)
		return false;

	dom_lhs = gimple_get_lhs(call_stmt);
	poss_is_err_func = gimple_call_fndecl(call_stmt);
	if (!poss_is_err_func)
		return false;
	if (dom_lhs != poss_is_err_cond)
		return false;
	if (strcmp(DECL_NAME_POINTER(poss_is_err_func), "IS_ERR"))
		return false;

	is_err_arg = gimple_call_arg(call_stmt, 0);
	if (!is_err_arg)
		return false;

	if (is_err_arg != rhs)
		return false;

	return true;
}

static void handle_local_var_initializers(void)
{
	tree var;
	unsigned int i;

	FOR_EACH_LOCAL_DECL(cfun, i, var) {
		tree init = DECL_INITIAL(var);
		if (!init)
			continue;
		if (TREE_CODE(init) != CONSTRUCTOR)
			continue;
		check_bad_casts_in_constructor(var, init);
	}
}

static bool type_name_eq(gimple stmt, const_tree type_tree, const char *wanted_name)
{
	const char *type_name;

	if (type_tree == NULL_TREE)
		return false;

	switch (TREE_CODE(type_tree)) {
	case RECORD_TYPE:
		type_name = TYPE_NAME_POINTER(type_tree);
		break;
	case INTEGER_TYPE:
		if (TYPE_PRECISION(type_tree) == CHAR_TYPE_SIZE)
			type_name = "char";
		else {
			INFORM(gimple_location(stmt), "found non-char INTEGER_TYPE cast comparison: %qT\n", type_tree);
			debug_tree(type_tree);
			return false;
		}
		break;
	case POINTER_TYPE:
		if (TREE_CODE(TREE_TYPE(type_tree)) == VOID_TYPE) {
			type_name = "void *";
			break;
		} else {
			INFORM(gimple_location(stmt), "found non-void POINTER_TYPE cast comparison %qT\n", type_tree);
			debug_tree(type_tree);
			return false;
		}
	default:
		INFORM(gimple_location(stmt), "unhandled cast comparison: %qT\n", type_tree);
		debug_tree(type_tree);
		return false;
	}

	return strcmp(type_name, wanted_name) == 0;
}

static bool whitelisted_cast(gimple stmt, const_tree lhs_tree, const_tree rhs_tree)
{
	const struct whitelist_entry *entry;
	expanded_location xloc = expand_location(gimple_location(stmt));

	for (entry = whitelist; entry->pathname; entry++) {
		if (!strstr(xloc.file, entry->pathname))
			continue;

		if (type_name_eq(stmt, lhs_tree, entry->lhs) && type_name_eq(stmt, rhs_tree, entry->rhs))
			return true;
	}

	return false;
}

/*
 * iterate over all statements to find "bad" casts:
 * those where the address of the start of a structure is cast
 * to a pointer of a structure of a different type, or a
 * structure pointer type is cast to a different structure pointer type
 */
static unsigned int find_bad_casts_execute(void)
{
	basic_block bb;

	handle_local_var_initializers();

	FOR_EACH_BB_FN(bb, cfun) {
		gimple_stmt_iterator gsi;

		for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
			gimple stmt;
			const_tree lhs;
			const_tree lhs_type;
			const_tree rhs1;
			const_tree rhs_type;
			const_tree ptr_lhs_type;
			const_tree ptr_rhs_type;
			const_tree op0;
			const_tree op0_type;
			enum tree_code rhs_code;

			stmt = gsi_stmt(gsi);

#ifdef __DEBUG_PLUGIN
#ifdef __DEBUG_VERBOSE
			debug_gimple_stmt(stmt);
			debug_tree(gimple_get_lhs(stmt));
#endif
#endif

			if (gimple_code(stmt) != GIMPLE_ASSIGN)
				continue;

#ifdef __DEBUG_PLUGIN
#ifdef __DEBUG_VERBOSE
			debug_tree(gimple_assign_rhs1(stmt));
#endif
#endif


			rhs_code = gimple_assign_rhs_code(stmt);

			if (rhs_code != ADDR_EXPR && rhs_code != SSA_NAME)
				continue;

			lhs = gimple_get_lhs(stmt);
			lhs_type = TREE_TYPE(lhs);
			rhs1 = gimple_assign_rhs1(stmt);
			rhs_type = TREE_TYPE(rhs1);

			if (TREE_CODE(rhs_type) != POINTER_TYPE ||
			    TREE_CODE(lhs_type) != POINTER_TYPE)
				continue;

			ptr_lhs_type = TYPE_MAIN_VARIANT(strip_array_types(TYPE_MAIN_VARIANT(TREE_TYPE(lhs_type))));
			ptr_rhs_type = TYPE_MAIN_VARIANT(strip_array_types(TYPE_MAIN_VARIANT(TREE_TYPE(rhs_type))));

			if (ptr_rhs_type == void_type_node)
				continue;

			if (ptr_lhs_type == void_type_node)
				continue;

			if (dominated_by_is_err(rhs1, bb))
				continue;

			if (TREE_CODE(ptr_rhs_type) != RECORD_TYPE) {
#ifndef __DEBUG_PLUGIN
				if (lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(ptr_lhs_type)))
#endif
				{
					if (!whitelisted_cast(stmt, ptr_lhs_type, ptr_rhs_type))
						MISMATCH(gimple_location(stmt), "rhs", ptr_lhs_type, ptr_rhs_type);
				}
				continue;
			}

			if (rhs_code == SSA_NAME && ptr_lhs_type == ptr_rhs_type)
				continue;

			if (rhs_code == ADDR_EXPR) {
				op0 = TREE_OPERAND(rhs1, 0);

				if (op0 == NULL_TREE)
					continue;

				if (TREE_CODE(op0) != VAR_DECL)
					continue;

				op0_type = TYPE_MAIN_VARIANT(strip_array_types(TYPE_MAIN_VARIANT(TREE_TYPE(op0))));
				if (op0_type == ptr_lhs_type)
					continue;

#ifndef __DEBUG_PLUGIN
				if (lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(op0_type)))
#endif
				{
					if (!whitelisted_cast(stmt, ptr_lhs_type, op0_type))
						MISMATCH(gimple_location(stmt), "op0", ptr_lhs_type, op0_type);
				}
			} else {
				const_tree ssa_name_var = SSA_NAME_VAR(rhs1);
				/* skip bogus type casts introduced by container_of */
				if (ssa_name_var != NULL_TREE && DECL_NAME(ssa_name_var) && 
				    !strcmp((const char *)DECL_NAME_POINTER(ssa_name_var), "__mptr"))
					continue;
#ifndef __DEBUG_PLUGIN
				if (lookup_attribute("randomize_performed", TYPE_ATTRIBUTES(ptr_rhs_type)))
#endif
				{
					if (!whitelisted_cast(stmt, ptr_lhs_type, ptr_rhs_type))
						MISMATCH(gimple_location(stmt), "ssa", ptr_lhs_type, ptr_rhs_type);
				}
			}

		}
	}
	return 0;
}

#define PASS_NAME find_bad_casts
#define NO_GATE
#define TODO_FLAGS_FINISH TODO_dump_func
#include "gcc-generate-gimple-pass.h"

__visible int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version)
{
	int i;
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
	bool enable = true;
	int obtained_seed = 0;
	struct register_pass_info find_bad_casts_pass_info;

	find_bad_casts_pass_info.pass			= make_find_bad_casts_pass();
	find_bad_casts_pass_info.reference_pass_name	= "ssa";
	find_bad_casts_pass_info.ref_pass_instance_number	= 1;
	find_bad_casts_pass_info.pos_op			= PASS_POS_INSERT_AFTER;

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	if (strncmp(lang_hooks.name, "GNU C", 5) && !strncmp(lang_hooks.name, "GNU C+", 6)) {
		inform(UNKNOWN_LOCATION, G_("%s supports C only, not %s"), plugin_name, lang_hooks.name);
		enable = false;
	}

	for (i = 0; i < argc; ++i) {
		if (!strcmp(argv[i].key, "disable")) {
			enable = false;
			continue;
		}
		if (!strcmp(argv[i].key, "performance-mode")) {
			performance_mode = 1;
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	if (strlen(randstruct_seed) != 64) {
		error(G_("invalid seed value supplied for %s plugin"), plugin_name);
		return 1;
	}
	obtained_seed = sscanf(randstruct_seed, "%016llx%016llx%016llx%016llx",
		&shuffle_seed[0], &shuffle_seed[1], &shuffle_seed[2], &shuffle_seed[3]);
	if (obtained_seed != 4) {
		error(G_("Invalid seed supplied for %s plugin"), plugin_name);
		return 1;
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL, &randomize_layout_plugin_info);
	if (enable) {
		register_callback(plugin_name, PLUGIN_ALL_IPA_PASSES_START, check_global_variables, NULL);
		register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &find_bad_casts_pass_info);
		register_callback(plugin_name, PLUGIN_FINISH_TYPE, finish_type, NULL);
		register_callback(plugin_name, PLUGIN_FINISH_DECL, randomize_layout_finish_decl, NULL);
	}
	register_callback(plugin_name, PLUGIN_ATTRIBUTES, register_attributes, NULL);

	return 0;
}
