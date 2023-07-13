// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */

#include <uapi/linux/btf.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/bpf_perf_event.h>
#include <uapi/linux/types.h>
#include <linux/seq_file.h>
#include <linux/compiler.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/sort.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/skmsg.h>
#include <linux/perf_event.h>
#include <linux/bsearch.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <net/sock.h>
#include "../tools/lib/bpf/relo_core.h"

/* BTF (BPF Type Format) is the meta data format which describes
 * the data types of BPF program/map.  Hence, it basically focus
 * on the C programming language which the modern BPF is primary
 * using.
 *
 * ELF Section:
 * ~~~~~~~~~~~
 * The BTF data is stored under the ".BTF" ELF section
 *
 * struct btf_type:
 * ~~~~~~~~~~~~~~~
 * Each 'struct btf_type' object describes a C data type.
 * Depending on the type it is describing, a 'struct btf_type'
 * object may be followed by more data.  F.e.
 * To describe an array, 'struct btf_type' is followed by
 * 'struct btf_array'.
 *
 * 'struct btf_type' and any extra data following it are
 * 4 bytes aligned.
 *
 * Type section:
 * ~~~~~~~~~~~~~
 * The BTF type section contains a list of 'struct btf_type' objects.
 * Each one describes a C type.  Recall from the above section
 * that a 'struct btf_type' object could be immediately followed by extra
 * data in order to describe some particular C types.
 *
 * type_id:
 * ~~~~~~~
 * Each btf_type object is identified by a type_id.  The type_id
 * is implicitly implied by the location of the btf_type object in
 * the BTF type section.  The first one has type_id 1.  The second
 * one has type_id 2...etc.  Hence, an earlier btf_type has
 * a smaller type_id.
 *
 * A btf_type object may refer to another btf_type object by using
 * type_id (i.e. the "type" in the "struct btf_type").
 *
 * NOTE that we cannot assume any reference-order.
 * A btf_type object can refer to an earlier btf_type object
 * but it can also refer to a later btf_type object.
 *
 * For example, to describe "const void *".  A btf_type
 * object describing "const" may refer to another btf_type
 * object describing "void *".  This type-reference is done
 * by specifying type_id:
 *
 * [1] CONST (anon) type_id=2
 * [2] PTR (anon) type_id=0
 *
 * The above is the btf_verifier debug log:
 *   - Each line started with "[?]" is a btf_type object
 *   - [?] is the type_id of the btf_type object.
 *   - CONST/PTR is the BTF_KIND_XXX
 *   - "(anon)" is the name of the type.  It just
 *     happens that CONST and PTR has no name.
 *   - type_id=XXX is the 'u32 type' in btf_type
 *
 * NOTE: "void" has type_id 0
 *
 * String section:
 * ~~~~~~~~~~~~~~
 * The BTF string section contains the names used by the type section.
 * Each string is referred by an "offset" from the beginning of the
 * string section.
 *
 * Each string is '\0' terminated.
 *
 * The first character in the string section must be '\0'
 * which is used to mean 'anonymous'. Some btf_type may not
 * have a name.
 */

/* BTF verification:
 *
 * To verify BTF data, two passes are needed.
 *
 * Pass #1
 * ~~~~~~~
 * The first pass is to collect all btf_type objects to
 * an array: "btf->types".
 *
 * Depending on the C type that a btf_type is describing,
 * a btf_type may be followed by extra data.  We don't know
 * how many btf_type is there, and more importantly we don't
 * know where each btf_type is located in the type section.
 *
 * Without knowing the location of each type_id, most verifications
 * cannot be done.  e.g. an earlier btf_type may refer to a later
 * btf_type (recall the "const void *" above), so we cannot
 * check this type-reference in the first pass.
 *
 * In the first pass, it still does some verifications (e.g.
 * checking the name is a valid offset to the string section).
 *
 * Pass #2
 * ~~~~~~~
 * The main focus is to resolve a btf_type that is referring
 * to another type.
 *
 * We have to ensure the referring type:
 * 1) does exist in the BTF (i.e. in btf->types[])
 * 2) does not cause a loop:
 *	struct A {
 *		struct B b;
 *	};
 *
 *	struct B {
 *		struct A a;
 *	};
 *
 * btf_type_needs_resolve() decides if a btf_type needs
 * to be resolved.
 *
 * The needs_resolve type implements the "resolve()" ops which
 * essentially does a DFS and detects backedge.
 *
 * During resolve (or DFS), different C types have different
 * "RESOLVED" conditions.
 *
 * When resolving a BTF_KIND_STRUCT, we need to resolve all its
 * members because a member is always referring to another
 * type.  A struct's member can be treated as "RESOLVED" if
 * it is referring to a BTF_KIND_PTR.  Otherwise, the
 * following valid C struct would be rejected:
 *
 *	struct A {
 *		int m;
 *		struct A *a;
 *	};
 *
 * When resolving a BTF_KIND_PTR, it needs to keep resolving if
 * it is referring to another BTF_KIND_PTR.  Otherwise, we cannot
 * detect a pointer loop, e.g.:
 * BTF_KIND_CONST -> BTF_KIND_PTR -> BTF_KIND_CONST -> BTF_KIND_PTR +
 *                        ^                                         |
 *                        +-----------------------------------------+
 *
 */

#define BITS_PER_U128 (sizeof(u64) * BITS_PER_BYTE * 2)
#define BITS_PER_BYTE_MASK (BITS_PER_BYTE - 1)
#define BITS_PER_BYTE_MASKED(bits) ((bits) & BITS_PER_BYTE_MASK)
#define BITS_ROUNDDOWN_BYTES(bits) ((bits) >> 3)
#define BITS_ROUNDUP_BYTES(bits) \
	(BITS_ROUNDDOWN_BYTES(bits) + !!BITS_PER_BYTE_MASKED(bits))

#define BTF_INFO_MASK 0x9f00ffff
#define BTF_INT_MASK 0x0fffffff
#define BTF_TYPE_ID_VALID(type_id) ((type_id) <= BTF_MAX_TYPE)
#define BTF_STR_OFFSET_VALID(name_off) ((name_off) <= BTF_MAX_NAME_OFFSET)

/* 16MB for 64k structs and each has 16 members and
 * a few MB spaces for the string section.
 * The hard limit is S32_MAX.
 */
#define BTF_MAX_SIZE (16 * 1024 * 1024)

#define for_each_member_from(i, from, struct_type, member)		\
	for (i = from, member = btf_type_member(struct_type) + from;	\
	     i < btf_type_vlen(struct_type);				\
	     i++, member++)

#define for_each_vsi_from(i, from, struct_type, member)				\
	for (i = from, member = btf_type_var_secinfo(struct_type) + from;	\
	     i < btf_type_vlen(struct_type);					\
	     i++, member++)

DEFINE_IDR(btf_idr);
DEFINE_SPINLOCK(btf_idr_lock);

enum btf_kfunc_hook {
	BTF_KFUNC_HOOK_XDP,
	BTF_KFUNC_HOOK_TC,
	BTF_KFUNC_HOOK_STRUCT_OPS,
	BTF_KFUNC_HOOK_TRACING,
	BTF_KFUNC_HOOK_SYSCALL,
	BTF_KFUNC_HOOK_MAX,
};

enum {
	BTF_KFUNC_SET_MAX_CNT = 256,
	BTF_DTOR_KFUNC_MAX_CNT = 256,
};

struct btf_kfunc_set_tab {
	struct btf_id_set8 *sets[BTF_KFUNC_HOOK_MAX];
};

struct btf_id_dtor_kfunc_tab {
	u32 cnt;
	struct btf_id_dtor_kfunc dtors[];
};

struct btf {
	void *data;
	struct btf_type **types;
	u32 *resolved_ids;
	u32 *resolved_sizes;
	const char *strings;
	void *nohdr_data;
	struct btf_header hdr;
	u32 nr_types; /* includes VOID for base BTF */
	u32 types_size;
	u32 data_size;
	refcount_t refcnt;
	u32 id;
	struct rcu_head rcu;
	struct btf_kfunc_set_tab *kfunc_set_tab;
	struct btf_id_dtor_kfunc_tab *dtor_kfunc_tab;

	/* split BTF support */
	struct btf *base_btf;
	u32 start_id; /* first type ID in this BTF (0 for base BTF) */
	u32 start_str_off; /* first string offset (0 for base BTF) */
	char name[MODULE_NAME_LEN];
	bool kernel_btf;
};

enum verifier_phase {
	CHECK_META,
	CHECK_TYPE,
};

struct resolve_vertex {
	const struct btf_type *t;
	u32 type_id;
	u16 next_member;
};

enum visit_state {
	NOT_VISITED,
	VISITED,
	RESOLVED,
};

enum resolve_mode {
	RESOLVE_TBD,	/* To Be Determined */
	RESOLVE_PTR,	/* Resolving for Pointer */
	RESOLVE_STRUCT_OR_ARRAY,	/* Resolving for struct/union
					 * or array
					 */
};

#define MAX_RESOLVE_DEPTH 32

struct btf_sec_info {
	u32 off;
	u32 len;
};

struct btf_verifier_env {
	struct btf *btf;
	u8 *visit_states;
	struct resolve_vertex stack[MAX_RESOLVE_DEPTH];
	struct bpf_verifier_log log;
	u32 log_type_id;
	u32 top_stack;
	enum verifier_phase phase;
	enum resolve_mode resolve_mode;
};

static const char * const btf_kind_str[NR_BTF_KINDS] = {
	[BTF_KIND_UNKN]		= "UNKNOWN",
	[BTF_KIND_INT]		= "INT",
	[BTF_KIND_PTR]		= "PTR",
	[BTF_KIND_ARRAY]	= "ARRAY",
	[BTF_KIND_STRUCT]	= "STRUCT",
	[BTF_KIND_UNION]	= "UNION",
	[BTF_KIND_ENUM]		= "ENUM",
	[BTF_KIND_FWD]		= "FWD",
	[BTF_KIND_TYPEDEF]	= "TYPEDEF",
	[BTF_KIND_VOLATILE]	= "VOLATILE",
	[BTF_KIND_CONST]	= "CONST",
	[BTF_KIND_RESTRICT]	= "RESTRICT",
	[BTF_KIND_FUNC]		= "FUNC",
	[BTF_KIND_FUNC_PROTO]	= "FUNC_PROTO",
	[BTF_KIND_VAR]		= "VAR",
	[BTF_KIND_DATASEC]	= "DATASEC",
	[BTF_KIND_FLOAT]	= "FLOAT",
	[BTF_KIND_DECL_TAG]	= "DECL_TAG",
	[BTF_KIND_TYPE_TAG]	= "TYPE_TAG",
	[BTF_KIND_ENUM64]	= "ENUM64",
};

const char *btf_type_str(const struct btf_type *t)
{
	return btf_kind_str[BTF_INFO_KIND(t->info)];
}

/* Chunk size we use in safe copy of data to be shown. */
#define BTF_SHOW_OBJ_SAFE_SIZE		32

/*
 * This is the maximum size of a base type value (equivalent to a
 * 128-bit int); if we are at the end of our safe buffer and have
 * less than 16 bytes space we can't be assured of being able
 * to copy the next type safely, so in such cases we will initiate
 * a new copy.
 */
#define BTF_SHOW_OBJ_BASE_TYPE_SIZE	16

/* Type name size */
#define BTF_SHOW_NAME_SIZE		80

/*
 * Common data to all BTF show operations. Private show functions can add
 * their own data to a structure containing a struct btf_show and consult it
 * in the show callback.  See btf_type_show() below.
 *
 * One challenge with showing nested data is we want to skip 0-valued
 * data, but in order to figure out whether a nested object is all zeros
 * we need to walk through it.  As a result, we need to make two passes
 * when handling structs, unions and arrays; the first path simply looks
 * for nonzero data, while the second actually does the display.  The first
 * pass is signalled by show->state.depth_check being set, and if we
 * encounter a non-zero value we set show->state.depth_to_show to
 * the depth at which we encountered it.  When we have completed the
 * first pass, we will know if anything needs to be displayed if
 * depth_to_show > depth.  See btf_[struct,array]_show() for the
 * implementation of this.
 *
 * Another problem is we want to ensure the data for display is safe to
 * access.  To support this, the anonymous "struct {} obj" tracks the data
 * object and our safe copy of it.  We copy portions of the data needed
 * to the object "copy" buffer, but because its size is limited to
 * BTF_SHOW_OBJ_COPY_LEN bytes, multiple copies may be required as we
 * traverse larger objects for display.
 *
 * The various data type show functions all start with a call to
 * btf_show_start_type() which returns a pointer to the safe copy
 * of the data needed (or if BTF_SHOW_UNSAFE is specified, to the
 * raw data itself).  btf_show_obj_safe() is responsible for
 * using copy_from_kernel_nofault() to update the safe data if necessary
 * as we traverse the object's data.  skbuff-like semantics are
 * used:
 *
 * - obj.head points to the start of the toplevel object for display
 * - obj.size is the size of the toplevel object
 * - obj.data points to the current point in the original data at
 *   which our safe data starts.  obj.data will advance as we copy
 *   portions of the data.
 *
 * In most cases a single copy will suffice, but larger data structures
 * such as "struct task_struct" will require many copies.  The logic in
 * btf_show_obj_safe() handles the logic that determines if a new
 * copy_from_kernel_nofault() is needed.
 */
struct btf_show {
	u64 flags;
	void *target;	/* target of show operation (seq file, buffer) */
	void (*showfn)(struct btf_show *show, const char *fmt, va_list args);
	const struct btf *btf;
	/* below are used during iteration */
	struct {
		u8 depth;
		u8 depth_to_show;
		u8 depth_check;
		u8 array_member:1,
		   array_terminated:1;
		u16 array_encoding;
		u32 type_id;
		int status;			/* non-zero for error */
		const struct btf_type *type;
		const struct btf_member *member;
		char name[BTF_SHOW_NAME_SIZE];	/* space for member name/type */
	} state;
	struct {
		u32 size;
		void *head;
		void *data;
		u8 safe[BTF_SHOW_OBJ_SAFE_SIZE];
	} obj;
};

struct btf_kind_operations {
	s32 (*check_meta)(struct btf_verifier_env *env,
			  const struct btf_type *t,
			  u32 meta_left);
	int (*resolve)(struct btf_verifier_env *env,
		       const struct resolve_vertex *v);
	int (*check_member)(struct btf_verifier_env *env,
			    const struct btf_type *struct_type,
			    const struct btf_member *member,
			    const struct btf_type *member_type);
	int (*check_kflag_member)(struct btf_verifier_env *env,
				  const struct btf_type *struct_type,
				  const struct btf_member *member,
				  const struct btf_type *member_type);
	void (*log_details)(struct btf_verifier_env *env,
			    const struct btf_type *t);
	void (*show)(const struct btf *btf, const struct btf_type *t,
			 u32 type_id, void *data, u8 bits_offsets,
			 struct btf_show *show);
};

static const struct btf_kind_operations * const kind_ops[NR_BTF_KINDS];
static struct btf_type btf_void;

static int btf_resolve(struct btf_verifier_env *env,
		       const struct btf_type *t, u32 type_id);

static int btf_func_check(struct btf_verifier_env *env,
			  const struct btf_type *t);

static bool btf_type_is_modifier(const struct btf_type *t)
{
	/* Some of them is not strictly a C modifier
	 * but they are grouped into the same bucket
	 * for BTF concern:
	 *   A type (t) that refers to another
	 *   type through t->type AND its size cannot
	 *   be determined without following the t->type.
	 *
	 * ptr does not fall into this bucket
	 * because its size is always sizeof(void *).
	 */
	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPE_TAG:
		return true;
	}

	return false;
}

bool btf_type_is_void(const struct btf_type *t)
{
	return t == &btf_void;
}

static bool btf_type_is_fwd(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_FWD;
}

static bool btf_type_nosize(const struct btf_type *t)
{
	return btf_type_is_void(t) || btf_type_is_fwd(t) ||
	       btf_type_is_func(t) || btf_type_is_func_proto(t);
}

static bool btf_type_nosize_or_null(const struct btf_type *t)
{
	return !t || btf_type_nosize(t);
}

static bool __btf_type_is_struct(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_STRUCT;
}

static bool btf_type_is_array(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_ARRAY;
}

static bool btf_type_is_datasec(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_DATASEC;
}

static bool btf_type_is_decl_tag(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_DECL_TAG;
}

static bool btf_type_is_decl_tag_target(const struct btf_type *t)
{
	return btf_type_is_func(t) || btf_type_is_struct(t) ||
	       btf_type_is_var(t) || btf_type_is_typedef(t);
}

u32 btf_nr_types(const struct btf *btf)
{
	u32 total = 0;

	while (btf) {
		total += btf->nr_types;
		btf = btf->base_btf;
	}

	return total;
}

s32 btf_find_by_name_kind(const struct btf *btf, const char *name, u8 kind)
{
	const struct btf_type *t;
	const char *tname;
	u32 i, total;

	total = btf_nr_types(btf);
	for (i = 1; i < total; i++) {
		t = btf_type_by_id(btf, i);
		if (BTF_INFO_KIND(t->info) != kind)
			continue;

		tname = btf_name_by_offset(btf, t->name_off);
		if (!strcmp(tname, name))
			return i;
	}

	return -ENOENT;
}

static s32 bpf_find_btf_id(const char *name, u32 kind, struct btf **btf_p)
{
	struct btf *btf;
	s32 ret;
	int id;

	btf = bpf_get_btf_vmlinux();
	if (IS_ERR(btf))
		return PTR_ERR(btf);
	if (!btf)
		return -EINVAL;

	ret = btf_find_by_name_kind(btf, name, kind);
	/* ret is never zero, since btf_find_by_name_kind returns
	 * positive btf_id or negative error.
	 */
	if (ret > 0) {
		btf_get(btf);
		*btf_p = btf;
		return ret;
	}

	/* If name is not found in vmlinux's BTF then search in module's BTFs */
	spin_lock_bh(&btf_idr_lock);
	idr_for_each_entry(&btf_idr, btf, id) {
		if (!btf_is_module(btf))
			continue;
		/* linear search could be slow hence unlock/lock
		 * the IDR to avoiding holding it for too long
		 */
		btf_get(btf);
		spin_unlock_bh(&btf_idr_lock);
		ret = btf_find_by_name_kind(btf, name, kind);
		if (ret > 0) {
			*btf_p = btf;
			return ret;
		}
		btf_put(btf);
		spin_lock_bh(&btf_idr_lock);
	}
	spin_unlock_bh(&btf_idr_lock);
	return ret;
}

const struct btf_type *btf_type_skip_modifiers(const struct btf *btf,
					       u32 id, u32 *res_id)
{
	const struct btf_type *t = btf_type_by_id(btf, id);

	while (btf_type_is_modifier(t)) {
		id = t->type;
		t = btf_type_by_id(btf, t->type);
	}

	if (res_id)
		*res_id = id;

	return t;
}

const struct btf_type *btf_type_resolve_ptr(const struct btf *btf,
					    u32 id, u32 *res_id)
{
	const struct btf_type *t;

	t = btf_type_skip_modifiers(btf, id, NULL);
	if (!btf_type_is_ptr(t))
		return NULL;

	return btf_type_skip_modifiers(btf, t->type, res_id);
}

const struct btf_type *btf_type_resolve_func_ptr(const struct btf *btf,
						 u32 id, u32 *res_id)
{
	const struct btf_type *ptype;

	ptype = btf_type_resolve_ptr(btf, id, res_id);
	if (ptype && btf_type_is_func_proto(ptype))
		return ptype;

	return NULL;
}

/* Types that act only as a source, not sink or intermediate
 * type when resolving.
 */
static bool btf_type_is_resolve_source_only(const struct btf_type *t)
{
	return btf_type_is_var(t) ||
	       btf_type_is_decl_tag(t) ||
	       btf_type_is_datasec(t);
}

/* What types need to be resolved?
 *
 * btf_type_is_modifier() is an obvious one.
 *
 * btf_type_is_struct() because its member refers to
 * another type (through member->type).
 *
 * btf_type_is_var() because the variable refers to
 * another type. btf_type_is_datasec() holds multiple
 * btf_type_is_var() types that need resolving.
 *
 * btf_type_is_array() because its element (array->type)
 * refers to another type.  Array can be thought of a
 * special case of struct while array just has the same
 * member-type repeated by array->nelems of times.
 */
static bool btf_type_needs_resolve(const struct btf_type *t)
{
	return btf_type_is_modifier(t) ||
	       btf_type_is_ptr(t) ||
	       btf_type_is_struct(t) ||
	       btf_type_is_array(t) ||
	       btf_type_is_var(t) ||
	       btf_type_is_func(t) ||
	       btf_type_is_decl_tag(t) ||
	       btf_type_is_datasec(t);
}

/* t->size can be used */
static bool btf_type_has_size(const struct btf_type *t)
{
	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_INT:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_ENUM:
	case BTF_KIND_DATASEC:
	case BTF_KIND_FLOAT:
	case BTF_KIND_ENUM64:
		return true;
	}

	return false;
}

static const char *btf_int_encoding_str(u8 encoding)
{
	if (encoding == 0)
		return "(none)";
	else if (encoding == BTF_INT_SIGNED)
		return "SIGNED";
	else if (encoding == BTF_INT_CHAR)
		return "CHAR";
	else if (encoding == BTF_INT_BOOL)
		return "BOOL";
	else
		return "UNKN";
}

static u32 btf_type_int(const struct btf_type *t)
{
	return *(u32 *)(t + 1);
}

static const struct btf_array *btf_type_array(const struct btf_type *t)
{
	return (const struct btf_array *)(t + 1);
}

static const struct btf_enum *btf_type_enum(const struct btf_type *t)
{
	return (const struct btf_enum *)(t + 1);
}

static const struct btf_var *btf_type_var(const struct btf_type *t)
{
	return (const struct btf_var *)(t + 1);
}

static const struct btf_decl_tag *btf_type_decl_tag(const struct btf_type *t)
{
	return (const struct btf_decl_tag *)(t + 1);
}

static const struct btf_enum64 *btf_type_enum64(const struct btf_type *t)
{
	return (const struct btf_enum64 *)(t + 1);
}

static const struct btf_kind_operations *btf_type_ops(const struct btf_type *t)
{
	return kind_ops[BTF_INFO_KIND(t->info)];
}

static bool btf_name_offset_valid(const struct btf *btf, u32 offset)
{
	if (!BTF_STR_OFFSET_VALID(offset))
		return false;

	while (offset < btf->start_str_off)
		btf = btf->base_btf;

	offset -= btf->start_str_off;
	return offset < btf->hdr.str_len;
}

static bool __btf_name_char_ok(char c, bool first)
{
	if ((first ? !isalpha(c) :
		     !isalnum(c)) &&
	    c != '_' &&
	    c != '.')
		return false;
	return true;
}

static const char *btf_str_by_offset(const struct btf *btf, u32 offset)
{
	while (offset < btf->start_str_off)
		btf = btf->base_btf;

	offset -= btf->start_str_off;
	if (offset < btf->hdr.str_len)
		return &btf->strings[offset];

	return NULL;
}

static bool __btf_name_valid(const struct btf *btf, u32 offset)
{
	/* offset must be valid */
	const char *src = btf_str_by_offset(btf, offset);
	const char *src_limit;

	if (!__btf_name_char_ok(*src, true))
		return false;

	/* set a limit on identifier length */
	src_limit = src + KSYM_NAME_LEN;
	src++;
	while (*src && src < src_limit) {
		if (!__btf_name_char_ok(*src, false))
			return false;
		src++;
	}

	return !*src;
}

static bool btf_name_valid_identifier(const struct btf *btf, u32 offset)
{
	return __btf_name_valid(btf, offset);
}

static bool btf_name_valid_section(const struct btf *btf, u32 offset)
{
	return __btf_name_valid(btf, offset);
}

static const char *__btf_name_by_offset(const struct btf *btf, u32 offset)
{
	const char *name;

	if (!offset)
		return "(anon)";

	name = btf_str_by_offset(btf, offset);
	return name ?: "(invalid-name-offset)";
}

const char *btf_name_by_offset(const struct btf *btf, u32 offset)
{
	return btf_str_by_offset(btf, offset);
}

const struct btf_type *btf_type_by_id(const struct btf *btf, u32 type_id)
{
	while (type_id < btf->start_id)
		btf = btf->base_btf;

	type_id -= btf->start_id;
	if (type_id >= btf->nr_types)
		return NULL;
	return btf->types[type_id];
}
EXPORT_SYMBOL_GPL(btf_type_by_id);

/*
 * Regular int is not a bit field and it must be either
 * u8/u16/u32/u64 or __int128.
 */
static bool btf_type_int_is_regular(const struct btf_type *t)
{
	u8 nr_bits, nr_bytes;
	u32 int_data;

	int_data = btf_type_int(t);
	nr_bits = BTF_INT_BITS(int_data);
	nr_bytes = BITS_ROUNDUP_BYTES(nr_bits);
	if (BITS_PER_BYTE_MASKED(nr_bits) ||
	    BTF_INT_OFFSET(int_data) ||
	    (nr_bytes != sizeof(u8) && nr_bytes != sizeof(u16) &&
	     nr_bytes != sizeof(u32) && nr_bytes != sizeof(u64) &&
	     nr_bytes != (2 * sizeof(u64)))) {
		return false;
	}

	return true;
}

/*
 * Check that given struct member is a regular int with expected
 * offset and size.
 */
bool btf_member_is_reg_int(const struct btf *btf, const struct btf_type *s,
			   const struct btf_member *m,
			   u32 expected_offset, u32 expected_size)
{
	const struct btf_type *t;
	u32 id, int_data;
	u8 nr_bits;

	id = m->type;
	t = btf_type_id_size(btf, &id, NULL);
	if (!t || !btf_type_is_int(t))
		return false;

	int_data = btf_type_int(t);
	nr_bits = BTF_INT_BITS(int_data);
	if (btf_type_kflag(s)) {
		u32 bitfield_size = BTF_MEMBER_BITFIELD_SIZE(m->offset);
		u32 bit_offset = BTF_MEMBER_BIT_OFFSET(m->offset);

		/* if kflag set, int should be a regular int and
		 * bit offset should be at byte boundary.
		 */
		return !bitfield_size &&
		       BITS_ROUNDUP_BYTES(bit_offset) == expected_offset &&
		       BITS_ROUNDUP_BYTES(nr_bits) == expected_size;
	}

	if (BTF_INT_OFFSET(int_data) ||
	    BITS_PER_BYTE_MASKED(m->offset) ||
	    BITS_ROUNDUP_BYTES(m->offset) != expected_offset ||
	    BITS_PER_BYTE_MASKED(nr_bits) ||
	    BITS_ROUNDUP_BYTES(nr_bits) != expected_size)
		return false;

	return true;
}

/* Similar to btf_type_skip_modifiers() but does not skip typedefs. */
static const struct btf_type *btf_type_skip_qualifiers(const struct btf *btf,
						       u32 id)
{
	const struct btf_type *t = btf_type_by_id(btf, id);

	while (btf_type_is_modifier(t) &&
	       BTF_INFO_KIND(t->info) != BTF_KIND_TYPEDEF) {
		t = btf_type_by_id(btf, t->type);
	}

	return t;
}

#define BTF_SHOW_MAX_ITER	10

#define BTF_KIND_BIT(kind)	(1ULL << kind)

/*
 * Populate show->state.name with type name information.
 * Format of type name is
 *
 * [.member_name = ] (type_name)
 */
static const char *btf_show_name(struct btf_show *show)
{
	/* BTF_MAX_ITER array suffixes "[]" */
	const char *array_suffixes = "[][][][][][][][][][]";
	const char *array_suffix = &array_suffixes[strlen(array_suffixes)];
	/* BTF_MAX_ITER pointer suffixes "*" */
	const char *ptr_suffixes = "**********";
	const char *ptr_suffix = &ptr_suffixes[strlen(ptr_suffixes)];
	const char *name = NULL, *prefix = "", *parens = "";
	const struct btf_member *m = show->state.member;
	const struct btf_type *t;
	const struct btf_array *array;
	u32 id = show->state.type_id;
	const char *member = NULL;
	bool show_member = false;
	u64 kinds = 0;
	int i;

	show->state.name[0] = '\0';

	/*
	 * Don't show type name if we're showing an array member;
	 * in that case we show the array type so don't need to repeat
	 * ourselves for each member.
	 */
	if (show->state.array_member)
		return "";

	/* Retrieve member name, if any. */
	if (m) {
		member = btf_name_by_offset(show->btf, m->name_off);
		show_member = strlen(member) > 0;
		id = m->type;
	}

	/*
	 * Start with type_id, as we have resolved the struct btf_type *
	 * via btf_modifier_show() past the parent typedef to the child
	 * struct, int etc it is defined as.  In such cases, the type_id
	 * still represents the starting type while the struct btf_type *
	 * in our show->state points at the resolved type of the typedef.
	 */
	t = btf_type_by_id(show->btf, id);
	if (!t)
		return "";

	/*
	 * The goal here is to build up the right number of pointer and
	 * array suffixes while ensuring the type name for a typedef
	 * is represented.  Along the way we accumulate a list of
	 * BTF kinds we have encountered, since these will inform later
	 * display; for example, pointer types will not require an
	 * opening "{" for struct, we will just display the pointer value.
	 *
	 * We also want to accumulate the right number of pointer or array
	 * indices in the format string while iterating until we get to
	 * the typedef/pointee/array member target type.
	 *
	 * We start by pointing at the end of pointer and array suffix
	 * strings; as we accumulate pointers and arrays we move the pointer
	 * or array string backwards so it will show the expected number of
	 * '*' or '[]' for the type.  BTF_SHOW_MAX_ITER of nesting of pointers
	 * and/or arrays and typedefs are supported as a precaution.
	 *
	 * We also want to get typedef name while proceeding to resolve
	 * type it points to so that we can add parentheses if it is a
	 * "typedef struct" etc.
	 */
	for (i = 0; i < BTF_SHOW_MAX_ITER; i++) {

		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_TYPEDEF:
			if (!name)
				name = btf_name_by_offset(show->btf,
							       t->name_off);
			kinds |= BTF_KIND_BIT(BTF_KIND_TYPEDEF);
			id = t->type;
			break;
		case BTF_KIND_ARRAY:
			kinds |= BTF_KIND_BIT(BTF_KIND_ARRAY);
			parens = "[";
			if (!t)
				return "";
			array = btf_type_array(t);
			if (array_suffix > array_suffixes)
				array_suffix -= 2;
			id = array->type;
			break;
		case BTF_KIND_PTR:
			kinds |= BTF_KIND_BIT(BTF_KIND_PTR);
			if (ptr_suffix > ptr_suffixes)
				ptr_suffix -= 1;
			id = t->type;
			break;
		default:
			id = 0;
			break;
		}
		if (!id)
			break;
		t = btf_type_skip_qualifiers(show->btf, id);
	}
	/* We may not be able to represent this type; bail to be safe */
	if (i == BTF_SHOW_MAX_ITER)
		return "";

	if (!name)
		name = btf_name_by_offset(show->btf, t->name_off);

	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		prefix = BTF_INFO_KIND(t->info) == BTF_KIND_STRUCT ?
			 "struct" : "union";
		/* if it's an array of struct/union, parens is already set */
		if (!(kinds & (BTF_KIND_BIT(BTF_KIND_ARRAY))))
			parens = "{";
		break;
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		prefix = "enum";
		break;
	default:
		break;
	}

	/* pointer does not require parens */
	if (kinds & BTF_KIND_BIT(BTF_KIND_PTR))
		parens = "";
	/* typedef does not require struct/union/enum prefix */
	if (kinds & BTF_KIND_BIT(BTF_KIND_TYPEDEF))
		prefix = "";

	if (!name)
		name = "";

	/* Even if we don't want type name info, we want parentheses etc */
	if (show->flags & BTF_SHOW_NONAME)
		snprintf(show->state.name, sizeof(show->state.name), "%s",
			 parens);
	else
		snprintf(show->state.name, sizeof(show->state.name),
			 "%s%s%s(%s%s%s%s%s%s)%s",
			 /* first 3 strings comprise ".member = " */
			 show_member ? "." : "",
			 show_member ? member : "",
			 show_member ? " = " : "",
			 /* ...next is our prefix (struct, enum, etc) */
			 prefix,
			 strlen(prefix) > 0 && strlen(name) > 0 ? " " : "",
			 /* ...this is the type name itself */
			 name,
			 /* ...suffixed by the appropriate '*', '[]' suffixes */
			 strlen(ptr_suffix) > 0 ? " " : "", ptr_suffix,
			 array_suffix, parens);

	return show->state.name;
}

static const char *__btf_show_indent(struct btf_show *show)
{
	const char *indents = "                                ";
	const char *indent = &indents[strlen(indents)];

	if ((indent - show->state.depth) >= indents)
		return indent - show->state.depth;
	return indents;
}

static const char *btf_show_indent(struct btf_show *show)
{
	return show->flags & BTF_SHOW_COMPACT ? "" : __btf_show_indent(show);
}

static const char *btf_show_newline(struct btf_show *show)
{
	return show->flags & BTF_SHOW_COMPACT ? "" : "\n";
}

static const char *btf_show_delim(struct btf_show *show)
{
	if (show->state.depth == 0)
		return "";

	if ((show->flags & BTF_SHOW_COMPACT) && show->state.type &&
		BTF_INFO_KIND(show->state.type->info) == BTF_KIND_UNION)
		return "|";

	return ",";
}

__printf(2, 3) static void btf_show(struct btf_show *show, const char *fmt, ...)
{
	va_list args;

	if (!show->state.depth_check) {
		va_start(args, fmt);
		show->showfn(show, fmt, args);
		va_end(args);
	}
}

/* Macros are used here as btf_show_type_value[s]() prepends and appends
 * format specifiers to the format specifier passed in; these do the work of
 * adding indentation, delimiters etc while the caller simply has to specify
 * the type value(s) in the format specifier + value(s).
 */
#define btf_show_type_value(show, fmt, value)				       \
	do {								       \
		if ((value) != (__typeof__(value))0 ||			       \
		    (show->flags & BTF_SHOW_ZERO) ||			       \
		    show->state.depth == 0) {				       \
			btf_show(show, "%s%s" fmt "%s%s",		       \
				 btf_show_indent(show),			       \
				 btf_show_name(show),			       \
				 value, btf_show_delim(show),		       \
				 btf_show_newline(show));		       \
			if (show->state.depth > show->state.depth_to_show)     \
				show->state.depth_to_show = show->state.depth; \
		}							       \
	} while (0)

#define btf_show_type_values(show, fmt, ...)				       \
	do {								       \
		btf_show(show, "%s%s" fmt "%s%s", btf_show_indent(show),       \
			 btf_show_name(show),				       \
			 __VA_ARGS__, btf_show_delim(show),		       \
			 btf_show_newline(show));			       \
		if (show->state.depth > show->state.depth_to_show)	       \
			show->state.depth_to_show = show->state.depth;	       \
	} while (0)

/* How much is left to copy to safe buffer after @data? */
static int btf_show_obj_size_left(struct btf_show *show, void *data)
{
	return show->obj.head + show->obj.size - data;
}

/* Is object pointed to by @data of @size already copied to our safe buffer? */
static bool btf_show_obj_is_safe(struct btf_show *show, void *data, int size)
{
	return data >= show->obj.data &&
	       (data + size) < (show->obj.data + BTF_SHOW_OBJ_SAFE_SIZE);
}

/*
 * If object pointed to by @data of @size falls within our safe buffer, return
 * the equivalent pointer to the same safe data.  Assumes
 * copy_from_kernel_nofault() has already happened and our safe buffer is
 * populated.
 */
static void *__btf_show_obj_safe(struct btf_show *show, void *data, int size)
{
	if (btf_show_obj_is_safe(show, data, size))
		return show->obj.safe + (data - show->obj.data);
	return NULL;
}

/*
 * Return a safe-to-access version of data pointed to by @data.
 * We do this by copying the relevant amount of information
 * to the struct btf_show obj.safe buffer using copy_from_kernel_nofault().
 *
 * If BTF_SHOW_UNSAFE is specified, just return data as-is; no
 * safe copy is needed.
 *
 * Otherwise we need to determine if we have the required amount
 * of data (determined by the @data pointer and the size of the
 * largest base type we can encounter (represented by
 * BTF_SHOW_OBJ_BASE_TYPE_SIZE). Having that much data ensures
 * that we will be able to print some of the current object,
 * and if more is needed a copy will be triggered.
 * Some objects such as structs will not fit into the buffer;
 * in such cases additional copies when we iterate over their
 * members may be needed.
 *
 * btf_show_obj_safe() is used to return a safe buffer for
 * btf_show_start_type(); this ensures that as we recurse into
 * nested types we always have safe data for the given type.
 * This approach is somewhat wasteful; it's possible for example
 * that when iterating over a large union we'll end up copying the
 * same data repeatedly, but the goal is safety not performance.
 * We use stack data as opposed to per-CPU buffers because the
 * iteration over a type can take some time, and preemption handling
 * would greatly complicate use of the safe buffer.
 */
static void *btf_show_obj_safe(struct btf_show *show,
			       const struct btf_type *t,
			       void *data)
{
	const struct btf_type *rt;
	int size_left, size;
	void *safe = NULL;

	if (show->flags & BTF_SHOW_UNSAFE)
		return data;

	rt = btf_resolve_size(show->btf, t, &size);
	if (IS_ERR(rt)) {
		show->state.status = PTR_ERR(rt);
		return NULL;
	}

	/*
	 * Is this toplevel object? If so, set total object size and
	 * initialize pointers.  Otherwise check if we still fall within
	 * our safe object data.
	 */
	if (show->state.depth == 0) {
		show->obj.size = size;
		show->obj.head = data;
	} else {
		/*
		 * If the size of the current object is > our remaining
		 * safe buffer we _may_ need to do a new copy.  However
		 * consider the case of a nested struct; it's size pushes
		 * us over the safe buffer limit, but showing any individual
		 * struct members does not.  In such cases, we don't need
		 * to initiate a fresh copy yet; however we definitely need
		 * at least BTF_SHOW_OBJ_BASE_TYPE_SIZE bytes left
		 * in our buffer, regardless of the current object size.
		 * The logic here is that as we resolve types we will
		 * hit a base type at some point, and we need to be sure
		 * the next chunk of data is safely available to display
		 * that type info safely.  We cannot rely on the size of
		 * the current object here because it may be much larger
		 * than our current buffer (e.g. task_struct is 8k).
		 * All we want to do here is ensure that we can print the
		 * next basic type, which we can if either
		 * - the current type size is within the safe buffer; or
		 * - at least BTF_SHOW_OBJ_BASE_TYPE_SIZE bytes are left in
		 *   the safe buffer.
		 */
		safe = __btf_show_obj_safe(show, data,
					   min(size,
					       BTF_SHOW_OBJ_BASE_TYPE_SIZE));
	}

	/*
	 * We need a new copy to our safe object, either because we haven't
	 * yet copied and are initializing safe data, or because the data
	 * we want falls outside the boundaries of the safe object.
	 */
	if (!safe) {
		size_left = btf_show_obj_size_left(show, data);
		if (size_left > BTF_SHOW_OBJ_SAFE_SIZE)
			size_left = BTF_SHOW_OBJ_SAFE_SIZE;
		show->state.status = copy_from_kernel_nofault(show->obj.safe,
							      data, size_left);
		if (!show->state.status) {
			show->obj.data = data;
			safe = show->obj.safe;
		}
	}

	return safe;
}

/*
 * Set the type we are starting to show and return a safe data pointer
 * to be used for showing the associated data.
 */
static void *btf_show_start_type(struct btf_show *show,
				 const struct btf_type *t,
				 u32 type_id, void *data)
{
	show->state.type = t;
	show->state.type_id = type_id;
	show->state.name[0] = '\0';

	return btf_show_obj_safe(show, t, data);
}

static void btf_show_end_type(struct btf_show *show)
{
	show->state.type = NULL;
	show->state.type_id = 0;
	show->state.name[0] = '\0';
}

static void *btf_show_start_aggr_type(struct btf_show *show,
				      const struct btf_type *t,
				      u32 type_id, void *data)
{
	void *safe_data = btf_show_start_type(show, t, type_id, data);

	if (!safe_data)
		return safe_data;

	btf_show(show, "%s%s%s", btf_show_indent(show),
		 btf_show_name(show),
		 btf_show_newline(show));
	show->state.depth++;
	return safe_data;
}

static void btf_show_end_aggr_type(struct btf_show *show,
				   const char *suffix)
{
	show->state.depth--;
	btf_show(show, "%s%s%s%s", btf_show_indent(show), suffix,
		 btf_show_delim(show), btf_show_newline(show));
	btf_show_end_type(show);
}

static void btf_show_start_member(struct btf_show *show,
				  const struct btf_member *m)
{
	show->state.member = m;
}

static void btf_show_start_array_member(struct btf_show *show)
{
	show->state.array_member = 1;
	btf_show_start_member(show, NULL);
}

static void btf_show_end_member(struct btf_show *show)
{
	show->state.member = NULL;
}

static void btf_show_end_array_member(struct btf_show *show)
{
	show->state.array_member = 0;
	btf_show_end_member(show);
}

static void *btf_show_start_array_type(struct btf_show *show,
				       const struct btf_type *t,
				       u32 type_id,
				       u16 array_encoding,
				       void *data)
{
	show->state.array_encoding = array_encoding;
	show->state.array_terminated = 0;
	return btf_show_start_aggr_type(show, t, type_id, data);
}

static void btf_show_end_array_type(struct btf_show *show)
{
	show->state.array_encoding = 0;
	show->state.array_terminated = 0;
	btf_show_end_aggr_type(show, "]");
}

static void *btf_show_start_struct_type(struct btf_show *show,
					const struct btf_type *t,
					u32 type_id,
					void *data)
{
	return btf_show_start_aggr_type(show, t, type_id, data);
}

static void btf_show_end_struct_type(struct btf_show *show)
{
	btf_show_end_aggr_type(show, "}");
}

__printf(2, 3) static void __btf_verifier_log(struct bpf_verifier_log *log,
					      const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}

__printf(2, 3) static void btf_verifier_log(struct btf_verifier_env *env,
					    const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}

__printf(4, 5) static void __btf_verifier_log_type(struct btf_verifier_env *env,
						   const struct btf_type *t,
						   bool log_details,
						   const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	struct btf *btf = env->btf;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	/* btf verifier prints all types it is processing via
	 * btf_verifier_log_type(..., fmt = NULL).
	 * Skip those prints for in-kernel BTF verification.
	 */
	if (log->level == BPF_LOG_KERNEL && !fmt)
		return;

	__btf_verifier_log(log, "[%u] %s %s%s",
			   env->log_type_id,
			   btf_type_str(t),
			   __btf_name_by_offset(btf, t->name_off),
			   log_details ? " " : "");

	if (log_details)
		btf_type_ops(t)->log_details(env, t);

	if (fmt && *fmt) {
		__btf_verifier_log(log, " ");
		va_start(args, fmt);
		bpf_verifier_vlog(log, fmt, args);
		va_end(args);
	}

	__btf_verifier_log(log, "\n");
}

#define btf_verifier_log_type(env, t, ...) \
	__btf_verifier_log_type((env), (t), true, __VA_ARGS__)
#define btf_verifier_log_basic(env, t, ...) \
	__btf_verifier_log_type((env), (t), false, __VA_ARGS__)

__printf(4, 5)
static void btf_verifier_log_member(struct btf_verifier_env *env,
				    const struct btf_type *struct_type,
				    const struct btf_member *member,
				    const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	struct btf *btf = env->btf;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	if (log->level == BPF_LOG_KERNEL && !fmt)
		return;
	/* The CHECK_META phase already did a btf dump.
	 *
	 * If member is logged again, it must hit an error in
	 * parsing this member.  It is useful to print out which
	 * struct this member belongs to.
	 */
	if (env->phase != CHECK_META)
		btf_verifier_log_type(env, struct_type, NULL);

	if (btf_type_kflag(struct_type))
		__btf_verifier_log(log,
				   "\t%s type_id=%u bitfield_size=%u bits_offset=%u",
				   __btf_name_by_offset(btf, member->name_off),
				   member->type,
				   BTF_MEMBER_BITFIELD_SIZE(member->offset),
				   BTF_MEMBER_BIT_OFFSET(member->offset));
	else
		__btf_verifier_log(log, "\t%s type_id=%u bits_offset=%u",
				   __btf_name_by_offset(btf, member->name_off),
				   member->type, member->offset);

	if (fmt && *fmt) {
		__btf_verifier_log(log, " ");
		va_start(args, fmt);
		bpf_verifier_vlog(log, fmt, args);
		va_end(args);
	}

	__btf_verifier_log(log, "\n");
}

__printf(4, 5)
static void btf_verifier_log_vsi(struct btf_verifier_env *env,
				 const struct btf_type *datasec_type,
				 const struct btf_var_secinfo *vsi,
				 const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;
	if (log->level == BPF_LOG_KERNEL && !fmt)
		return;
	if (env->phase != CHECK_META)
		btf_verifier_log_type(env, datasec_type, NULL);

	__btf_verifier_log(log, "\t type_id=%u offset=%u size=%u",
			   vsi->type, vsi->offset, vsi->size);
	if (fmt && *fmt) {
		__btf_verifier_log(log, " ");
		va_start(args, fmt);
		bpf_verifier_vlog(log, fmt, args);
		va_end(args);
	}

	__btf_verifier_log(log, "\n");
}

static void btf_verifier_log_hdr(struct btf_verifier_env *env,
				 u32 btf_data_size)
{
	struct bpf_verifier_log *log = &env->log;
	const struct btf *btf = env->btf;
	const struct btf_header *hdr;

	if (!bpf_verifier_log_needed(log))
		return;

	if (log->level == BPF_LOG_KERNEL)
		return;
	hdr = &btf->hdr;
	__btf_verifier_log(log, "magic: 0x%x\n", hdr->magic);
	__btf_verifier_log(log, "version: %u\n", hdr->version);
	__btf_verifier_log(log, "flags: 0x%x\n", hdr->flags);
	__btf_verifier_log(log, "hdr_len: %u\n", hdr->hdr_len);
	__btf_verifier_log(log, "type_off: %u\n", hdr->type_off);
	__btf_verifier_log(log, "type_len: %u\n", hdr->type_len);
	__btf_verifier_log(log, "str_off: %u\n", hdr->str_off);
	__btf_verifier_log(log, "str_len: %u\n", hdr->str_len);
	__btf_verifier_log(log, "btf_total_size: %u\n", btf_data_size);
}

static int btf_add_type(struct btf_verifier_env *env, struct btf_type *t)
{
	struct btf *btf = env->btf;

	if (btf->types_size == btf->nr_types) {
		/* Expand 'types' array */

		struct btf_type **new_types;
		u32 expand_by, new_size;

		if (btf->start_id + btf->types_size == BTF_MAX_TYPE) {
			btf_verifier_log(env, "Exceeded max num of types");
			return -E2BIG;
		}

		expand_by = max_t(u32, btf->types_size >> 2, 16);
		new_size = min_t(u32, BTF_MAX_TYPE,
				 btf->types_size + expand_by);

		new_types = kvcalloc(new_size, sizeof(*new_types),
				     GFP_KERNEL | __GFP_NOWARN);
		if (!new_types)
			return -ENOMEM;

		if (btf->nr_types == 0) {
			if (!btf->base_btf) {
				/* lazily init VOID type */
				new_types[0] = &btf_void;
				btf->nr_types++;
			}
		} else {
			memcpy(new_types, btf->types,
			       sizeof(*btf->types) * btf->nr_types);
		}

		kvfree(btf->types);
		btf->types = new_types;
		btf->types_size = new_size;
	}

	btf->types[btf->nr_types++] = t;

	return 0;
}

static int btf_alloc_id(struct btf *btf)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&btf_idr_lock);
	id = idr_alloc_cyclic(&btf_idr, btf, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		btf->id = id;
	spin_unlock_bh(&btf_idr_lock);
	idr_preload_end();

	if (WARN_ON_ONCE(!id))
		return -ENOSPC;

	return id > 0 ? 0 : id;
}

static void btf_free_id(struct btf *btf)
{
	unsigned long flags;

	/*
	 * In map-in-map, calling map_delete_elem() on outer
	 * map will call bpf_map_put on the inner map.
	 * It will then eventually call btf_free_id()
	 * on the inner map.  Some of the map_delete_elem()
	 * implementation may have irq disabled, so
	 * we need to use the _irqsave() version instead
	 * of the _bh() version.
	 */
	spin_lock_irqsave(&btf_idr_lock, flags);
	idr_remove(&btf_idr, btf->id);
	spin_unlock_irqrestore(&btf_idr_lock, flags);
}

static void btf_free_kfunc_set_tab(struct btf *btf)
{
	struct btf_kfunc_set_tab *tab = btf->kfunc_set_tab;
	int hook;

	if (!tab)
		return;
	/* For module BTF, we directly assign the sets being registered, so
	 * there is nothing to free except kfunc_set_tab.
	 */
	if (btf_is_module(btf))
		goto free_tab;
	for (hook = 0; hook < ARRAY_SIZE(tab->sets); hook++)
		kfree(tab->sets[hook]);
free_tab:
	kfree(tab);
	btf->kfunc_set_tab = NULL;
}

static void btf_free_dtor_kfunc_tab(struct btf *btf)
{
	struct btf_id_dtor_kfunc_tab *tab = btf->dtor_kfunc_tab;

	if (!tab)
		return;
	kfree(tab);
	btf->dtor_kfunc_tab = NULL;
}

static void btf_free(struct btf *btf)
{
	btf_free_dtor_kfunc_tab(btf);
	btf_free_kfunc_set_tab(btf);
	kvfree(btf->types);
	kvfree(btf->resolved_sizes);
	kvfree(btf->resolved_ids);
	kvfree(btf->data);
	kfree(btf);
}

static void btf_free_rcu(struct rcu_head *rcu)
{
	struct btf *btf = container_of(rcu, struct btf, rcu);

	btf_free(btf);
}

void btf_get(struct btf *btf)
{
	refcount_inc(&btf->refcnt);
}

void btf_put(struct btf *btf)
{
	if (btf && refcount_dec_and_test(&btf->refcnt)) {
		btf_free_id(btf);
		call_rcu(&btf->rcu, btf_free_rcu);
	}
}

static int env_resolve_init(struct btf_verifier_env *env)
{
	struct btf *btf = env->btf;
	u32 nr_types = btf->nr_types;
	u32 *resolved_sizes = NULL;
	u32 *resolved_ids = NULL;
	u8 *visit_states = NULL;

	resolved_sizes = kvcalloc(nr_types, sizeof(*resolved_sizes),
				  GFP_KERNEL | __GFP_NOWARN);
	if (!resolved_sizes)
		goto nomem;

	resolved_ids = kvcalloc(nr_types, sizeof(*resolved_ids),
				GFP_KERNEL | __GFP_NOWARN);
	if (!resolved_ids)
		goto nomem;

	visit_states = kvcalloc(nr_types, sizeof(*visit_states),
				GFP_KERNEL | __GFP_NOWARN);
	if (!visit_states)
		goto nomem;

	btf->resolved_sizes = resolved_sizes;
	btf->resolved_ids = resolved_ids;
	env->visit_states = visit_states;

	return 0;

nomem:
	kvfree(resolved_sizes);
	kvfree(resolved_ids);
	kvfree(visit_states);
	return -ENOMEM;
}

static void btf_verifier_env_free(struct btf_verifier_env *env)
{
	kvfree(env->visit_states);
	kfree(env);
}

static bool env_type_is_resolve_sink(const struct btf_verifier_env *env,
				     const struct btf_type *next_type)
{
	switch (env->resolve_mode) {
	case RESOLVE_TBD:
		/* int, enum or void is a sink */
		return !btf_type_needs_resolve(next_type);
	case RESOLVE_PTR:
		/* int, enum, void, struct, array, func or func_proto is a sink
		 * for ptr
		 */
		return !btf_type_is_modifier(next_type) &&
			!btf_type_is_ptr(next_type);
	case RESOLVE_STRUCT_OR_ARRAY:
		/* int, enum, void, ptr, func or func_proto is a sink
		 * for struct and array
		 */
		return !btf_type_is_modifier(next_type) &&
			!btf_type_is_array(next_type) &&
			!btf_type_is_struct(next_type);
	default:
		BUG();
	}
}

static bool env_type_is_resolved(const struct btf_verifier_env *env,
				 u32 type_id)
{
	/* base BTF types should be resolved by now */
	if (type_id < env->btf->start_id)
		return true;

	return env->visit_states[type_id - env->btf->start_id] == RESOLVED;
}

static int env_stack_push(struct btf_verifier_env *env,
			  const struct btf_type *t, u32 type_id)
{
	const struct btf *btf = env->btf;
	struct resolve_vertex *v;

	if (env->top_stack == MAX_RESOLVE_DEPTH)
		return -E2BIG;

	if (type_id < btf->start_id
	    || env->visit_states[type_id - btf->start_id] != NOT_VISITED)
		return -EEXIST;

	env->visit_states[type_id - btf->start_id] = VISITED;

	v = &env->stack[env->top_stack++];
	v->t = t;
	v->type_id = type_id;
	v->next_member = 0;

	if (env->resolve_mode == RESOLVE_TBD) {
		if (btf_type_is_ptr(t))
			env->resolve_mode = RESOLVE_PTR;
		else if (btf_type_is_struct(t) || btf_type_is_array(t))
			env->resolve_mode = RESOLVE_STRUCT_OR_ARRAY;
	}

	return 0;
}

static void env_stack_set_next_member(struct btf_verifier_env *env,
				      u16 next_member)
{
	env->stack[env->top_stack - 1].next_member = next_member;
}

static void env_stack_pop_resolved(struct btf_verifier_env *env,
				   u32 resolved_type_id,
				   u32 resolved_size)
{
	u32 type_id = env->stack[--(env->top_stack)].type_id;
	struct btf *btf = env->btf;

	type_id -= btf->start_id; /* adjust to local type id */
	btf->resolved_sizes[type_id] = resolved_size;
	btf->resolved_ids[type_id] = resolved_type_id;
	env->visit_states[type_id] = RESOLVED;
}

static const struct resolve_vertex *env_stack_peak(struct btf_verifier_env *env)
{
	return env->top_stack ? &env->stack[env->top_stack - 1] : NULL;
}

/* Resolve the size of a passed-in "type"
 *
 * type: is an array (e.g. u32 array[x][y])
 * return type: type "u32[x][y]", i.e. BTF_KIND_ARRAY,
 * *type_size: (x * y * sizeof(u32)).  Hence, *type_size always
 *             corresponds to the return type.
 * *elem_type: u32
 * *elem_id: id of u32
 * *total_nelems: (x * y).  Hence, individual elem size is
 *                (*type_size / *total_nelems)
 * *type_id: id of type if it's changed within the function, 0 if not
 *
 * type: is not an array (e.g. const struct X)
 * return type: type "struct X"
 * *type_size: sizeof(struct X)
 * *elem_type: same as return type ("struct X")
 * *elem_id: 0
 * *total_nelems: 1
 * *type_id: id of type if it's changed within the function, 0 if not
 */
static const struct btf_type *
__btf_resolve_size(const struct btf *btf, const struct btf_type *type,
		   u32 *type_size, const struct btf_type **elem_type,
		   u32 *elem_id, u32 *total_nelems, u32 *type_id)
{
	const struct btf_type *array_type = NULL;
	const struct btf_array *array = NULL;
	u32 i, size, nelems = 1, id = 0;

	for (i = 0; i < MAX_RESOLVE_DEPTH; i++) {
		switch (BTF_INFO_KIND(type->info)) {
		/* type->size can be used */
		case BTF_KIND_INT:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_ENUM:
		case BTF_KIND_FLOAT:
		case BTF_KIND_ENUM64:
			size = type->size;
			goto resolved;

		case BTF_KIND_PTR:
			size = sizeof(void *);
			goto resolved;

		/* Modifiers */
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_TYPE_TAG:
			id = type->type;
			type = btf_type_by_id(btf, type->type);
			break;

		case BTF_KIND_ARRAY:
			if (!array_type)
				array_type = type;
			array = btf_type_array(type);
			if (nelems && array->nelems > U32_MAX / nelems)
				return ERR_PTR(-EINVAL);
			nelems *= array->nelems;
			type = btf_type_by_id(btf, array->type);
			break;

		/* type without size */
		default:
			return ERR_PTR(-EINVAL);
		}
	}

	return ERR_PTR(-EINVAL);

resolved:
	if (nelems && size > U32_MAX / nelems)
		return ERR_PTR(-EINVAL);

	*type_size = nelems * size;
	if (total_nelems)
		*total_nelems = nelems;
	if (elem_type)
		*elem_type = type;
	if (elem_id)
		*elem_id = array ? array->type : 0;
	if (type_id && id)
		*type_id = id;

	return array_type ? : type;
}

const struct btf_type *
btf_resolve_size(const struct btf *btf, const struct btf_type *type,
		 u32 *type_size)
{
	return __btf_resolve_size(btf, type, type_size, NULL, NULL, NULL, NULL);
}

static u32 btf_resolved_type_id(const struct btf *btf, u32 type_id)
{
	while (type_id < btf->start_id)
		btf = btf->base_btf;

	return btf->resolved_ids[type_id - btf->start_id];
}

/* The input param "type_id" must point to a needs_resolve type */
static const struct btf_type *btf_type_id_resolve(const struct btf *btf,
						  u32 *type_id)
{
	*type_id = btf_resolved_type_id(btf, *type_id);
	return btf_type_by_id(btf, *type_id);
}

static u32 btf_resolved_type_size(const struct btf *btf, u32 type_id)
{
	while (type_id < btf->start_id)
		btf = btf->base_btf;

	return btf->resolved_sizes[type_id - btf->start_id];
}

const struct btf_type *btf_type_id_size(const struct btf *btf,
					u32 *type_id, u32 *ret_size)
{
	const struct btf_type *size_type;
	u32 size_type_id = *type_id;
	u32 size = 0;

	size_type = btf_type_by_id(btf, size_type_id);
	if (btf_type_nosize_or_null(size_type))
		return NULL;

	if (btf_type_has_size(size_type)) {
		size = size_type->size;
	} else if (btf_type_is_array(size_type)) {
		size = btf_resolved_type_size(btf, size_type_id);
	} else if (btf_type_is_ptr(size_type)) {
		size = sizeof(void *);
	} else {
		if (WARN_ON_ONCE(!btf_type_is_modifier(size_type) &&
				 !btf_type_is_var(size_type)))
			return NULL;

		size_type_id = btf_resolved_type_id(btf, size_type_id);
		size_type = btf_type_by_id(btf, size_type_id);
		if (btf_type_nosize_or_null(size_type))
			return NULL;
		else if (btf_type_has_size(size_type))
			size = size_type->size;
		else if (btf_type_is_array(size_type))
			size = btf_resolved_type_size(btf, size_type_id);
		else if (btf_type_is_ptr(size_type))
			size = sizeof(void *);
		else
			return NULL;
	}

	*type_id = size_type_id;
	if (ret_size)
		*ret_size = size;

	return size_type;
}

static int btf_df_check_member(struct btf_verifier_env *env,
			       const struct btf_type *struct_type,
			       const struct btf_member *member,
			       const struct btf_type *member_type)
{
	btf_verifier_log_basic(env, struct_type,
			       "Unsupported check_member");
	return -EINVAL;
}

static int btf_df_check_kflag_member(struct btf_verifier_env *env,
				     const struct btf_type *struct_type,
				     const struct btf_member *member,
				     const struct btf_type *member_type)
{
	btf_verifier_log_basic(env, struct_type,
			       "Unsupported check_kflag_member");
	return -EINVAL;
}

/* Used for ptr, array struct/union and float type members.
 * int, enum and modifier types have their specific callback functions.
 */
static int btf_generic_check_kflag_member(struct btf_verifier_env *env,
					  const struct btf_type *struct_type,
					  const struct btf_member *member,
					  const struct btf_type *member_type)
{
	if (BTF_MEMBER_BITFIELD_SIZE(member->offset)) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member bitfield_size");
		return -EINVAL;
	}

	/* bitfield size is 0, so member->offset represents bit offset only.
	 * It is safe to call non kflag check_member variants.
	 */
	return btf_type_ops(member_type)->check_member(env, struct_type,
						       member,
						       member_type);
}

static int btf_df_resolve(struct btf_verifier_env *env,
			  const struct resolve_vertex *v)
{
	btf_verifier_log_basic(env, v->t, "Unsupported resolve");
	return -EINVAL;
}

static void btf_df_show(const struct btf *btf, const struct btf_type *t,
			u32 type_id, void *data, u8 bits_offsets,
			struct btf_show *show)
{
	btf_show(show, "<unsupported kind:%u>", BTF_INFO_KIND(t->info));
}

static int btf_int_check_member(struct btf_verifier_env *env,
				const struct btf_type *struct_type,
				const struct btf_member *member,
				const struct btf_type *member_type)
{
	u32 int_data = btf_type_int(member_type);
	u32 struct_bits_off = member->offset;
	u32 struct_size = struct_type->size;
	u32 nr_copy_bits;
	u32 bytes_offset;

	if (U32_MAX - struct_bits_off < BTF_INT_OFFSET(int_data)) {
		btf_verifier_log_member(env, struct_type, member,
					"bits_offset exceeds U32_MAX");
		return -EINVAL;
	}

	struct_bits_off += BTF_INT_OFFSET(int_data);
	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);
	nr_copy_bits = BTF_INT_BITS(int_data) +
		BITS_PER_BYTE_MASKED(struct_bits_off);

	if (nr_copy_bits > BITS_PER_U128) {
		btf_verifier_log_member(env, struct_type, member,
					"nr_copy_bits exceeds 128");
		return -EINVAL;
	}

	if (struct_size < bytes_offset ||
	    struct_size - bytes_offset < BITS_ROUNDUP_BYTES(nr_copy_bits)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static int btf_int_check_kflag_member(struct btf_verifier_env *env,
				      const struct btf_type *struct_type,
				      const struct btf_member *member,
				      const struct btf_type *member_type)
{
	u32 struct_bits_off, nr_bits, nr_int_data_bits, bytes_offset;
	u32 int_data = btf_type_int(member_type);
	u32 struct_size = struct_type->size;
	u32 nr_copy_bits;

	/* a regular int type is required for the kflag int member */
	if (!btf_type_int_is_regular(member_type)) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member base type");
		return -EINVAL;
	}

	/* check sanity of bitfield size */
	nr_bits = BTF_MEMBER_BITFIELD_SIZE(member->offset);
	struct_bits_off = BTF_MEMBER_BIT_OFFSET(member->offset);
	nr_int_data_bits = BTF_INT_BITS(int_data);
	if (!nr_bits) {
		/* Not a bitfield member, member offset must be at byte
		 * boundary.
		 */
		if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
			btf_verifier_log_member(env, struct_type, member,
						"Invalid member offset");
			return -EINVAL;
		}

		nr_bits = nr_int_data_bits;
	} else if (nr_bits > nr_int_data_bits) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member bitfield_size");
		return -EINVAL;
	}

	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);
	nr_copy_bits = nr_bits + BITS_PER_BYTE_MASKED(struct_bits_off);
	if (nr_copy_bits > BITS_PER_U128) {
		btf_verifier_log_member(env, struct_type, member,
					"nr_copy_bits exceeds 128");
		return -EINVAL;
	}

	if (struct_size < bytes_offset ||
	    struct_size - bytes_offset < BITS_ROUNDUP_BYTES(nr_copy_bits)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static s32 btf_int_check_meta(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 meta_left)
{
	u32 int_data, nr_bits, meta_needed = sizeof(int_data);
	u16 encoding;

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	int_data = btf_type_int(t);
	if (int_data & ~BTF_INT_MASK) {
		btf_verifier_log_basic(env, t, "Invalid int_data:%x",
				       int_data);
		return -EINVAL;
	}

	nr_bits = BTF_INT_BITS(int_data) + BTF_INT_OFFSET(int_data);

	if (nr_bits > BITS_PER_U128) {
		btf_verifier_log_type(env, t, "nr_bits exceeds %zu",
				      BITS_PER_U128);
		return -EINVAL;
	}

	if (BITS_ROUNDUP_BYTES(nr_bits) > t->size) {
		btf_verifier_log_type(env, t, "nr_bits exceeds type_size");
		return -EINVAL;
	}

	/*
	 * Only one of the encoding bits is allowed and it
	 * should be sufficient for the pretty print purpose (i.e. decoding).
	 * Multiple bits can be allowed later if it is found
	 * to be insufficient.
	 */
	encoding = BTF_INT_ENCODING(int_data);
	if (encoding &&
	    encoding != BTF_INT_SIGNED &&
	    encoding != BTF_INT_CHAR &&
	    encoding != BTF_INT_BOOL) {
		btf_verifier_log_type(env, t, "Unsupported encoding");
		return -ENOTSUPP;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static void btf_int_log(struct btf_verifier_env *env,
			const struct btf_type *t)
{
	int int_data = btf_type_int(t);

	btf_verifier_log(env,
			 "size=%u bits_offset=%u nr_bits=%u encoding=%s",
			 t->size, BTF_INT_OFFSET(int_data),
			 BTF_INT_BITS(int_data),
			 btf_int_encoding_str(BTF_INT_ENCODING(int_data)));
}

static void btf_int128_print(struct btf_show *show, void *data)
{
	/* data points to a __int128 number.
	 * Suppose
	 *     int128_num = *(__int128 *)data;
	 * The below formulas shows what upper_num and lower_num represents:
	 *     upper_num = int128_num >> 64;
	 *     lower_num = int128_num & 0xffffffffFFFFFFFFULL;
	 */
	u64 upper_num, lower_num;

#ifdef __BIG_ENDIAN_BITFIELD
	upper_num = *(u64 *)data;
	lower_num = *(u64 *)(data + 8);
#else
	upper_num = *(u64 *)(data + 8);
	lower_num = *(u64 *)data;
#endif
	if (upper_num == 0)
		btf_show_type_value(show, "0x%llx", lower_num);
	else
		btf_show_type_values(show, "0x%llx%016llx", upper_num,
				     lower_num);
}

static void btf_int128_shift(u64 *print_num, u16 left_shift_bits,
			     u16 right_shift_bits)
{
	u64 upper_num, lower_num;

#ifdef __BIG_ENDIAN_BITFIELD
	upper_num = print_num[0];
	lower_num = print_num[1];
#else
	upper_num = print_num[1];
	lower_num = print_num[0];
#endif

	/* shake out un-needed bits by shift/or operations */
	if (left_shift_bits >= 64) {
		upper_num = lower_num << (left_shift_bits - 64);
		lower_num = 0;
	} else {
		upper_num = (upper_num << left_shift_bits) |
			    (lower_num >> (64 - left_shift_bits));
		lower_num = lower_num << left_shift_bits;
	}

	if (right_shift_bits >= 64) {
		lower_num = upper_num >> (right_shift_bits - 64);
		upper_num = 0;
	} else {
		lower_num = (lower_num >> right_shift_bits) |
			    (upper_num << (64 - right_shift_bits));
		upper_num = upper_num >> right_shift_bits;
	}

#ifdef __BIG_ENDIAN_BITFIELD
	print_num[0] = upper_num;
	print_num[1] = lower_num;
#else
	print_num[0] = lower_num;
	print_num[1] = upper_num;
#endif
}

static void btf_bitfield_show(void *data, u8 bits_offset,
			      u8 nr_bits, struct btf_show *show)
{
	u16 left_shift_bits, right_shift_bits;
	u8 nr_copy_bytes;
	u8 nr_copy_bits;
	u64 print_num[2] = {};

	nr_copy_bits = nr_bits + bits_offset;
	nr_copy_bytes = BITS_ROUNDUP_BYTES(nr_copy_bits);

	memcpy(print_num, data, nr_copy_bytes);

#ifdef __BIG_ENDIAN_BITFIELD
	left_shift_bits = bits_offset;
#else
	left_shift_bits = BITS_PER_U128 - nr_copy_bits;
#endif
	right_shift_bits = BITS_PER_U128 - nr_bits;

	btf_int128_shift(print_num, left_shift_bits, right_shift_bits);
	btf_int128_print(show, print_num);
}


static void btf_int_bits_show(const struct btf *btf,
			      const struct btf_type *t,
			      void *data, u8 bits_offset,
			      struct btf_show *show)
{
	u32 int_data = btf_type_int(t);
	u8 nr_bits = BTF_INT_BITS(int_data);
	u8 total_bits_offset;

	/*
	 * bits_offset is at most 7.
	 * BTF_INT_OFFSET() cannot exceed 128 bits.
	 */
	total_bits_offset = bits_offset + BTF_INT_OFFSET(int_data);
	data += BITS_ROUNDDOWN_BYTES(total_bits_offset);
	bits_offset = BITS_PER_BYTE_MASKED(total_bits_offset);
	btf_bitfield_show(data, bits_offset, nr_bits, show);
}

static void btf_int_show(const struct btf *btf, const struct btf_type *t,
			 u32 type_id, void *data, u8 bits_offset,
			 struct btf_show *show)
{
	u32 int_data = btf_type_int(t);
	u8 encoding = BTF_INT_ENCODING(int_data);
	bool sign = encoding & BTF_INT_SIGNED;
	u8 nr_bits = BTF_INT_BITS(int_data);
	void *safe_data;

	safe_data = btf_show_start_type(show, t, type_id, data);
	if (!safe_data)
		return;

	if (bits_offset || BTF_INT_OFFSET(int_data) ||
	    BITS_PER_BYTE_MASKED(nr_bits)) {
		btf_int_bits_show(btf, t, safe_data, bits_offset, show);
		goto out;
	}

	switch (nr_bits) {
	case 128:
		btf_int128_print(show, safe_data);
		break;
	case 64:
		if (sign)
			btf_show_type_value(show, "%lld", *(s64 *)safe_data);
		else
			btf_show_type_value(show, "%llu", *(u64 *)safe_data);
		break;
	case 32:
		if (sign)
			btf_show_type_value(show, "%d", *(s32 *)safe_data);
		else
			btf_show_type_value(show, "%u", *(u32 *)safe_data);
		break;
	case 16:
		if (sign)
			btf_show_type_value(show, "%d", *(s16 *)safe_data);
		else
			btf_show_type_value(show, "%u", *(u16 *)safe_data);
		break;
	case 8:
		if (show->state.array_encoding == BTF_INT_CHAR) {
			/* check for null terminator */
			if (show->state.array_terminated)
				break;
			if (*(char *)data == '\0') {
				show->state.array_terminated = 1;
				break;
			}
			if (isprint(*(char *)data)) {
				btf_show_type_value(show, "'%c'",
						    *(char *)safe_data);
				break;
			}
		}
		if (sign)
			btf_show_type_value(show, "%d", *(s8 *)safe_data);
		else
			btf_show_type_value(show, "%u", *(u8 *)safe_data);
		break;
	default:
		btf_int_bits_show(btf, t, safe_data, bits_offset, show);
		break;
	}
out:
	btf_show_end_type(show);
}

static const struct btf_kind_operations int_ops = {
	.check_meta = btf_int_check_meta,
	.resolve = btf_df_resolve,
	.check_member = btf_int_check_member,
	.check_kflag_member = btf_int_check_kflag_member,
	.log_details = btf_int_log,
	.show = btf_int_show,
};

static int btf_modifier_check_member(struct btf_verifier_env *env,
				     const struct btf_type *struct_type,
				     const struct btf_member *member,
				     const struct btf_type *member_type)
{
	const struct btf_type *resolved_type;
	u32 resolved_type_id = member->type;
	struct btf_member resolved_member;
	struct btf *btf = env->btf;

	resolved_type = btf_type_id_size(btf, &resolved_type_id, NULL);
	if (!resolved_type) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member");
		return -EINVAL;
	}

	resolved_member = *member;
	resolved_member.type = resolved_type_id;

	return btf_type_ops(resolved_type)->check_member(env, struct_type,
							 &resolved_member,
							 resolved_type);
}

static int btf_modifier_check_kflag_member(struct btf_verifier_env *env,
					   const struct btf_type *struct_type,
					   const struct btf_member *member,
					   const struct btf_type *member_type)
{
	const struct btf_type *resolved_type;
	u32 resolved_type_id = member->type;
	struct btf_member resolved_member;
	struct btf *btf = env->btf;

	resolved_type = btf_type_id_size(btf, &resolved_type_id, NULL);
	if (!resolved_type) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member");
		return -EINVAL;
	}

	resolved_member = *member;
	resolved_member.type = resolved_type_id;

	return btf_type_ops(resolved_type)->check_kflag_member(env, struct_type,
							       &resolved_member,
							       resolved_type);
}

static int btf_ptr_check_member(struct btf_verifier_env *env,
				const struct btf_type *struct_type,
				const struct btf_member *member,
				const struct btf_type *member_type)
{
	u32 struct_size, struct_bits_off, bytes_offset;

	struct_size = struct_type->size;
	struct_bits_off = member->offset;
	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);

	if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member is not byte aligned");
		return -EINVAL;
	}

	if (struct_size - bytes_offset < sizeof(void *)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static int btf_ref_type_check_meta(struct btf_verifier_env *env,
				   const struct btf_type *t,
				   u32 meta_left)
{
	const char *value;

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	if (!BTF_TYPE_ID_VALID(t->type)) {
		btf_verifier_log_type(env, t, "Invalid type_id");
		return -EINVAL;
	}

	/* typedef/type_tag type must have a valid name, and other ref types,
	 * volatile, const, restrict, should have a null name.
	 */
	if (BTF_INFO_KIND(t->info) == BTF_KIND_TYPEDEF) {
		if (!t->name_off ||
		    !btf_name_valid_identifier(env->btf, t->name_off)) {
			btf_verifier_log_type(env, t, "Invalid name");
			return -EINVAL;
		}
	} else if (BTF_INFO_KIND(t->info) == BTF_KIND_TYPE_TAG) {
		value = btf_name_by_offset(env->btf, t->name_off);
		if (!value || !value[0]) {
			btf_verifier_log_type(env, t, "Invalid name");
			return -EINVAL;
		}
	} else {
		if (t->name_off) {
			btf_verifier_log_type(env, t, "Invalid name");
			return -EINVAL;
		}
	}

	btf_verifier_log_type(env, t, NULL);

	return 0;
}

static int btf_modifier_resolve(struct btf_verifier_env *env,
				const struct resolve_vertex *v)
{
	const struct btf_type *t = v->t;
	const struct btf_type *next_type;
	u32 next_type_id = t->type;
	struct btf *btf = env->btf;

	next_type = btf_type_by_id(btf, next_type_id);
	if (!next_type || btf_type_is_resolve_source_only(next_type)) {
		btf_verifier_log_type(env, v->t, "Invalid type_id");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, next_type) &&
	    !env_type_is_resolved(env, next_type_id))
		return env_stack_push(env, next_type, next_type_id);

	/* Figure out the resolved next_type_id with size.
	 * They will be stored in the current modifier's
	 * resolved_ids and resolved_sizes such that it can
	 * save us a few type-following when we use it later (e.g. in
	 * pretty print).
	 */
	if (!btf_type_id_size(btf, &next_type_id, NULL)) {
		if (env_type_is_resolved(env, next_type_id))
			next_type = btf_type_id_resolve(btf, &next_type_id);

		/* "typedef void new_void", "const void"...etc */
		if (!btf_type_is_void(next_type) &&
		    !btf_type_is_fwd(next_type) &&
		    !btf_type_is_func_proto(next_type)) {
			btf_verifier_log_type(env, v->t, "Invalid type_id");
			return -EINVAL;
		}
	}

	env_stack_pop_resolved(env, next_type_id, 0);

	return 0;
}

static int btf_var_resolve(struct btf_verifier_env *env,
			   const struct resolve_vertex *v)
{
	const struct btf_type *next_type;
	const struct btf_type *t = v->t;
	u32 next_type_id = t->type;
	struct btf *btf = env->btf;

	next_type = btf_type_by_id(btf, next_type_id);
	if (!next_type || btf_type_is_resolve_source_only(next_type)) {
		btf_verifier_log_type(env, v->t, "Invalid type_id");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, next_type) &&
	    !env_type_is_resolved(env, next_type_id))
		return env_stack_push(env, next_type, next_type_id);

	if (btf_type_is_modifier(next_type)) {
		const struct btf_type *resolved_type;
		u32 resolved_type_id;

		resolved_type_id = next_type_id;
		resolved_type = btf_type_id_resolve(btf, &resolved_type_id);

		if (btf_type_is_ptr(resolved_type) &&
		    !env_type_is_resolve_sink(env, resolved_type) &&
		    !env_type_is_resolved(env, resolved_type_id))
			return env_stack_push(env, resolved_type,
					      resolved_type_id);
	}

	/* We must resolve to something concrete at this point, no
	 * forward types or similar that would resolve to size of
	 * zero is allowed.
	 */
	if (!btf_type_id_size(btf, &next_type_id, NULL)) {
		btf_verifier_log_type(env, v->t, "Invalid type_id");
		return -EINVAL;
	}

	env_stack_pop_resolved(env, next_type_id, 0);

	return 0;
}

static int btf_ptr_resolve(struct btf_verifier_env *env,
			   const struct resolve_vertex *v)
{
	const struct btf_type *next_type;
	const struct btf_type *t = v->t;
	u32 next_type_id = t->type;
	struct btf *btf = env->btf;

	next_type = btf_type_by_id(btf, next_type_id);
	if (!next_type || btf_type_is_resolve_source_only(next_type)) {
		btf_verifier_log_type(env, v->t, "Invalid type_id");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, next_type) &&
	    !env_type_is_resolved(env, next_type_id))
		return env_stack_push(env, next_type, next_type_id);

	/* If the modifier was RESOLVED during RESOLVE_STRUCT_OR_ARRAY,
	 * the modifier may have stopped resolving when it was resolved
	 * to a ptr (last-resolved-ptr).
	 *
	 * We now need to continue from the last-resolved-ptr to
	 * ensure the last-resolved-ptr will not referring back to
	 * the current ptr (t).
	 */
	if (btf_type_is_modifier(next_type)) {
		const struct btf_type *resolved_type;
		u32 resolved_type_id;

		resolved_type_id = next_type_id;
		resolved_type = btf_type_id_resolve(btf, &resolved_type_id);

		if (btf_type_is_ptr(resolved_type) &&
		    !env_type_is_resolve_sink(env, resolved_type) &&
		    !env_type_is_resolved(env, resolved_type_id))
			return env_stack_push(env, resolved_type,
					      resolved_type_id);
	}

	if (!btf_type_id_size(btf, &next_type_id, NULL)) {
		if (env_type_is_resolved(env, next_type_id))
			next_type = btf_type_id_resolve(btf, &next_type_id);

		if (!btf_type_is_void(next_type) &&
		    !btf_type_is_fwd(next_type) &&
		    !btf_type_is_func_proto(next_type)) {
			btf_verifier_log_type(env, v->t, "Invalid type_id");
			return -EINVAL;
		}
	}

	env_stack_pop_resolved(env, next_type_id, 0);

	return 0;
}

static void btf_modifier_show(const struct btf *btf,
			      const struct btf_type *t,
			      u32 type_id, void *data,
			      u8 bits_offset, struct btf_show *show)
{
	if (btf->resolved_ids)
		t = btf_type_id_resolve(btf, &type_id);
	else
		t = btf_type_skip_modifiers(btf, type_id, NULL);

	btf_type_ops(t)->show(btf, t, type_id, data, bits_offset, show);
}

static void btf_var_show(const struct btf *btf, const struct btf_type *t,
			 u32 type_id, void *data, u8 bits_offset,
			 struct btf_show *show)
{
	t = btf_type_id_resolve(btf, &type_id);

	btf_type_ops(t)->show(btf, t, type_id, data, bits_offset, show);
}

static void btf_ptr_show(const struct btf *btf, const struct btf_type *t,
			 u32 type_id, void *data, u8 bits_offset,
			 struct btf_show *show)
{
	void *safe_data;

	safe_data = btf_show_start_type(show, t, type_id, data);
	if (!safe_data)
		return;

	/* It is a hashed value unless BTF_SHOW_PTR_RAW is specified */
	if (show->flags & BTF_SHOW_PTR_RAW)
		btf_show_type_value(show, "0x%px", *(void **)safe_data);
	else
		btf_show_type_value(show, "0x%p", *(void **)safe_data);
	btf_show_end_type(show);
}

static void btf_ref_type_log(struct btf_verifier_env *env,
			     const struct btf_type *t)
{
	btf_verifier_log(env, "type_id=%u", t->type);
}

static struct btf_kind_operations modifier_ops = {
	.check_meta = btf_ref_type_check_meta,
	.resolve = btf_modifier_resolve,
	.check_member = btf_modifier_check_member,
	.check_kflag_member = btf_modifier_check_kflag_member,
	.log_details = btf_ref_type_log,
	.show = btf_modifier_show,
};

static struct btf_kind_operations ptr_ops = {
	.check_meta = btf_ref_type_check_meta,
	.resolve = btf_ptr_resolve,
	.check_member = btf_ptr_check_member,
	.check_kflag_member = btf_generic_check_kflag_member,
	.log_details = btf_ref_type_log,
	.show = btf_ptr_show,
};

static s32 btf_fwd_check_meta(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 meta_left)
{
	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (t->type) {
		btf_verifier_log_type(env, t, "type != 0");
		return -EINVAL;
	}

	/* fwd type must have a valid name */
	if (!t->name_off ||
	    !btf_name_valid_identifier(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return 0;
}

static void btf_fwd_type_log(struct btf_verifier_env *env,
			     const struct btf_type *t)
{
	btf_verifier_log(env, "%s", btf_type_kflag(t) ? "union" : "struct");
}

static struct btf_kind_operations fwd_ops = {
	.check_meta = btf_fwd_check_meta,
	.resolve = btf_df_resolve,
	.check_member = btf_df_check_member,
	.check_kflag_member = btf_df_check_kflag_member,
	.log_details = btf_fwd_type_log,
	.show = btf_df_show,
};

static int btf_array_check_member(struct btf_verifier_env *env,
				  const struct btf_type *struct_type,
				  const struct btf_member *member,
				  const struct btf_type *member_type)
{
	u32 struct_bits_off = member->offset;
	u32 struct_size, bytes_offset;
	u32 array_type_id, array_size;
	struct btf *btf = env->btf;

	if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member is not byte aligned");
		return -EINVAL;
	}

	array_type_id = member->type;
	btf_type_id_size(btf, &array_type_id, &array_size);
	struct_size = struct_type->size;
	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);
	if (struct_size - bytes_offset < array_size) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static s32 btf_array_check_meta(struct btf_verifier_env *env,
				const struct btf_type *t,
				u32 meta_left)
{
	const struct btf_array *array = btf_type_array(t);
	u32 meta_needed = sizeof(*array);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	/* array type should not have a name */
	if (t->name_off) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	if (t->size) {
		btf_verifier_log_type(env, t, "size != 0");
		return -EINVAL;
	}

	/* Array elem type and index type cannot be in type void,
	 * so !array->type and !array->index_type are not allowed.
	 */
	if (!array->type || !BTF_TYPE_ID_VALID(array->type)) {
		btf_verifier_log_type(env, t, "Invalid elem");
		return -EINVAL;
	}

	if (!array->index_type || !BTF_TYPE_ID_VALID(array->index_type)) {
		btf_verifier_log_type(env, t, "Invalid index");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static int btf_array_resolve(struct btf_verifier_env *env,
			     const struct resolve_vertex *v)
{
	const struct btf_array *array = btf_type_array(v->t);
	const struct btf_type *elem_type, *index_type;
	u32 elem_type_id, index_type_id;
	struct btf *btf = env->btf;
	u32 elem_size;

	/* Check array->index_type */
	index_type_id = array->index_type;
	index_type = btf_type_by_id(btf, index_type_id);
	if (btf_type_nosize_or_null(index_type) ||
	    btf_type_is_resolve_source_only(index_type)) {
		btf_verifier_log_type(env, v->t, "Invalid index");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, index_type) &&
	    !env_type_is_resolved(env, index_type_id))
		return env_stack_push(env, index_type, index_type_id);

	index_type = btf_type_id_size(btf, &index_type_id, NULL);
	if (!index_type || !btf_type_is_int(index_type) ||
	    !btf_type_int_is_regular(index_type)) {
		btf_verifier_log_type(env, v->t, "Invalid index");
		return -EINVAL;
	}

	/* Check array->type */
	elem_type_id = array->type;
	elem_type = btf_type_by_id(btf, elem_type_id);
	if (btf_type_nosize_or_null(elem_type) ||
	    btf_type_is_resolve_source_only(elem_type)) {
		btf_verifier_log_type(env, v->t,
				      "Invalid elem");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, elem_type) &&
	    !env_type_is_resolved(env, elem_type_id))
		return env_stack_push(env, elem_type, elem_type_id);

	elem_type = btf_type_id_size(btf, &elem_type_id, &elem_size);
	if (!elem_type) {
		btf_verifier_log_type(env, v->t, "Invalid elem");
		return -EINVAL;
	}

	if (btf_type_is_int(elem_type) && !btf_type_int_is_regular(elem_type)) {
		btf_verifier_log_type(env, v->t, "Invalid array of int");
		return -EINVAL;
	}

	if (array->nelems && elem_size > U32_MAX / array->nelems) {
		btf_verifier_log_type(env, v->t,
				      "Array size overflows U32_MAX");
		return -EINVAL;
	}

	env_stack_pop_resolved(env, elem_type_id, elem_size * array->nelems);

	return 0;
}

static void btf_array_log(struct btf_verifier_env *env,
			  const struct btf_type *t)
{
	const struct btf_array *array = btf_type_array(t);

	btf_verifier_log(env, "type_id=%u index_type_id=%u nr_elems=%u",
			 array->type, array->index_type, array->nelems);
}

static void __btf_array_show(const struct btf *btf, const struct btf_type *t,
			     u32 type_id, void *data, u8 bits_offset,
			     struct btf_show *show)
{
	const struct btf_array *array = btf_type_array(t);
	const struct btf_kind_operations *elem_ops;
	const struct btf_type *elem_type;
	u32 i, elem_size = 0, elem_type_id;
	u16 encoding = 0;

	elem_type_id = array->type;
	elem_type = btf_type_skip_modifiers(btf, elem_type_id, NULL);
	if (elem_type && btf_type_has_size(elem_type))
		elem_size = elem_type->size;

	if (elem_type && btf_type_is_int(elem_type)) {
		u32 int_type = btf_type_int(elem_type);

		encoding = BTF_INT_ENCODING(int_type);

		/*
		 * BTF_INT_CHAR encoding never seems to be set for
		 * char arrays, so if size is 1 and element is
		 * printable as a char, we'll do that.
		 */
		if (elem_size == 1)
			encoding = BTF_INT_CHAR;
	}

	if (!btf_show_start_array_type(show, t, type_id, encoding, data))
		return;

	if (!elem_type)
		goto out;
	elem_ops = btf_type_ops(elem_type);

	for (i = 0; i < array->nelems; i++) {

		btf_show_start_array_member(show);

		elem_ops->show(btf, elem_type, elem_type_id, data,
			       bits_offset, show);
		data += elem_size;

		btf_show_end_array_member(show);

		if (show->state.array_terminated)
			break;
	}
out:
	btf_show_end_array_type(show);
}

static void btf_array_show(const struct btf *btf, const struct btf_type *t,
			   u32 type_id, void *data, u8 bits_offset,
			   struct btf_show *show)
{
	const struct btf_member *m = show->state.member;

	/*
	 * First check if any members would be shown (are non-zero).
	 * See comments above "struct btf_show" definition for more
	 * details on how this works at a high-level.
	 */
	if (show->state.depth > 0 && !(show->flags & BTF_SHOW_ZERO)) {
		if (!show->state.depth_check) {
			show->state.depth_check = show->state.depth + 1;
			show->state.depth_to_show = 0;
		}
		__btf_array_show(btf, t, type_id, data, bits_offset, show);
		show->state.member = m;

		if (show->state.depth_check != show->state.depth + 1)
			return;
		show->state.depth_check = 0;

		if (show->state.depth_to_show <= show->state.depth)
			return;
		/*
		 * Reaching here indicates we have recursed and found
		 * non-zero array member(s).
		 */
	}
	__btf_array_show(btf, t, type_id, data, bits_offset, show);
}

static struct btf_kind_operations array_ops = {
	.check_meta = btf_array_check_meta,
	.resolve = btf_array_resolve,
	.check_member = btf_array_check_member,
	.check_kflag_member = btf_generic_check_kflag_member,
	.log_details = btf_array_log,
	.show = btf_array_show,
};

static int btf_struct_check_member(struct btf_verifier_env *env,
				   const struct btf_type *struct_type,
				   const struct btf_member *member,
				   const struct btf_type *member_type)
{
	u32 struct_bits_off = member->offset;
	u32 struct_size, bytes_offset;

	if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member is not byte aligned");
		return -EINVAL;
	}

	struct_size = struct_type->size;
	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);
	if (struct_size - bytes_offset < member_type->size) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static s32 btf_struct_check_meta(struct btf_verifier_env *env,
				 const struct btf_type *t,
				 u32 meta_left)
{
	bool is_union = BTF_INFO_KIND(t->info) == BTF_KIND_UNION;
	const struct btf_member *member;
	u32 meta_needed, last_offset;
	struct btf *btf = env->btf;
	u32 struct_size = t->size;
	u32 offset;
	u16 i;

	meta_needed = btf_type_vlen(t) * sizeof(*member);
	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	/* struct type either no name or a valid one */
	if (t->name_off &&
	    !btf_name_valid_identifier(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	last_offset = 0;
	for_each_member(i, t, member) {
		if (!btf_name_offset_valid(btf, member->name_off)) {
			btf_verifier_log_member(env, t, member,
						"Invalid member name_offset:%u",
						member->name_off);
			return -EINVAL;
		}

		/* struct member either no name or a valid one */
		if (member->name_off &&
		    !btf_name_valid_identifier(btf, member->name_off)) {
			btf_verifier_log_member(env, t, member, "Invalid name");
			return -EINVAL;
		}
		/* A member cannot be in type void */
		if (!member->type || !BTF_TYPE_ID_VALID(member->type)) {
			btf_verifier_log_member(env, t, member,
						"Invalid type_id");
			return -EINVAL;
		}

		offset = __btf_member_bit_offset(t, member);
		if (is_union && offset) {
			btf_verifier_log_member(env, t, member,
						"Invalid member bits_offset");
			return -EINVAL;
		}

		/*
		 * ">" instead of ">=" because the last member could be
		 * "char a[0];"
		 */
		if (last_offset > offset) {
			btf_verifier_log_member(env, t, member,
						"Invalid member bits_offset");
			return -EINVAL;
		}

		if (BITS_ROUNDUP_BYTES(offset) > struct_size) {
			btf_verifier_log_member(env, t, member,
						"Member bits_offset exceeds its struct size");
			return -EINVAL;
		}

		btf_verifier_log_member(env, t, member, NULL);
		last_offset = offset;
	}

	return meta_needed;
}

static int btf_struct_resolve(struct btf_verifier_env *env,
			      const struct resolve_vertex *v)
{
	const struct btf_member *member;
	int err;
	u16 i;

	/* Before continue resolving the next_member,
	 * ensure the last member is indeed resolved to a
	 * type with size info.
	 */
	if (v->next_member) {
		const struct btf_type *last_member_type;
		const struct btf_member *last_member;
		u32 last_member_type_id;

		last_member = btf_type_member(v->t) + v->next_member - 1;
		last_member_type_id = last_member->type;
		if (WARN_ON_ONCE(!env_type_is_resolved(env,
						       last_member_type_id)))
			return -EINVAL;

		last_member_type = btf_type_by_id(env->btf,
						  last_member_type_id);
		if (btf_type_kflag(v->t))
			err = btf_type_ops(last_member_type)->check_kflag_member(env, v->t,
								last_member,
								last_member_type);
		else
			err = btf_type_ops(last_member_type)->check_member(env, v->t,
								last_member,
								last_member_type);
		if (err)
			return err;
	}

	for_each_member_from(i, v->next_member, v->t, member) {
		u32 member_type_id = member->type;
		const struct btf_type *member_type = btf_type_by_id(env->btf,
								member_type_id);

		if (btf_type_nosize_or_null(member_type) ||
		    btf_type_is_resolve_source_only(member_type)) {
			btf_verifier_log_member(env, v->t, member,
						"Invalid member");
			return -EINVAL;
		}

		if (!env_type_is_resolve_sink(env, member_type) &&
		    !env_type_is_resolved(env, member_type_id)) {
			env_stack_set_next_member(env, i + 1);
			return env_stack_push(env, member_type, member_type_id);
		}

		if (btf_type_kflag(v->t))
			err = btf_type_ops(member_type)->check_kflag_member(env, v->t,
									    member,
									    member_type);
		else
			err = btf_type_ops(member_type)->check_member(env, v->t,
								      member,
								      member_type);
		if (err)
			return err;
	}

	env_stack_pop_resolved(env, 0, 0);

	return 0;
}

static void btf_struct_log(struct btf_verifier_env *env,
			   const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u vlen=%u", t->size, btf_type_vlen(t));
}

enum btf_field_type {
	BTF_FIELD_SPIN_LOCK,
	BTF_FIELD_TIMER,
	BTF_FIELD_KPTR,
};

enum {
	BTF_FIELD_IGNORE = 0,
	BTF_FIELD_FOUND  = 1,
};

struct btf_field_info {
	u32 type_id;
	u32 off;
	enum bpf_kptr_type type;
};

static int btf_find_struct(const struct btf *btf, const struct btf_type *t,
			   u32 off, int sz, struct btf_field_info *info)
{
	if (!__btf_type_is_struct(t))
		return BTF_FIELD_IGNORE;
	if (t->size != sz)
		return BTF_FIELD_IGNORE;
	info->off = off;
	return BTF_FIELD_FOUND;
}

static int btf_find_kptr(const struct btf *btf, const struct btf_type *t,
			 u32 off, int sz, struct btf_field_info *info)
{
	enum bpf_kptr_type type;
	u32 res_id;

	/* For PTR, sz is always == 8 */
	if (!btf_type_is_ptr(t))
		return BTF_FIELD_IGNORE;
	t = btf_type_by_id(btf, t->type);

	if (!btf_type_is_type_tag(t))
		return BTF_FIELD_IGNORE;
	/* Reject extra tags */
	if (btf_type_is_type_tag(btf_type_by_id(btf, t->type)))
		return -EINVAL;
	if (!strcmp("kptr", __btf_name_by_offset(btf, t->name_off)))
		type = BPF_KPTR_UNREF;
	else if (!strcmp("kptr_ref", __btf_name_by_offset(btf, t->name_off)))
		type = BPF_KPTR_REF;
	else
		return -EINVAL;

	/* Get the base type */
	t = btf_type_skip_modifiers(btf, t->type, &res_id);
	/* Only pointer to struct is allowed */
	if (!__btf_type_is_struct(t))
		return -EINVAL;

	info->type_id = res_id;
	info->off = off;
	info->type = type;
	return BTF_FIELD_FOUND;
}

static int btf_find_struct_field(const struct btf *btf, const struct btf_type *t,
				 const char *name, int sz, int align,
				 enum btf_field_type field_type,
				 struct btf_field_info *info, int info_cnt)
{
	const struct btf_member *member;
	struct btf_field_info tmp;
	int ret, idx = 0;
	u32 i, off;

	for_each_member(i, t, member) {
		const struct btf_type *member_type = btf_type_by_id(btf,
								    member->type);

		if (name && strcmp(__btf_name_by_offset(btf, member_type->name_off), name))
			continue;

		off = __btf_member_bit_offset(t, member);
		if (off % 8)
			/* valid C code cannot generate such BTF */
			return -EINVAL;
		off /= 8;
		if (off % align)
			return -EINVAL;

		switch (field_type) {
		case BTF_FIELD_SPIN_LOCK:
		case BTF_FIELD_TIMER:
			ret = btf_find_struct(btf, member_type, off, sz,
					      idx < info_cnt ? &info[idx] : &tmp);
			if (ret < 0)
				return ret;
			break;
		case BTF_FIELD_KPTR:
			ret = btf_find_kptr(btf, member_type, off, sz,
					    idx < info_cnt ? &info[idx] : &tmp);
			if (ret < 0)
				return ret;
			break;
		default:
			return -EFAULT;
		}

		if (ret == BTF_FIELD_IGNORE)
			continue;
		if (idx >= info_cnt)
			return -E2BIG;
		++idx;
	}
	return idx;
}

static int btf_find_datasec_var(const struct btf *btf, const struct btf_type *t,
				const char *name, int sz, int align,
				enum btf_field_type field_type,
				struct btf_field_info *info, int info_cnt)
{
	const struct btf_var_secinfo *vsi;
	struct btf_field_info tmp;
	int ret, idx = 0;
	u32 i, off;

	for_each_vsi(i, t, vsi) {
		const struct btf_type *var = btf_type_by_id(btf, vsi->type);
		const struct btf_type *var_type = btf_type_by_id(btf, var->type);

		off = vsi->offset;

		if (name && strcmp(__btf_name_by_offset(btf, var_type->name_off), name))
			continue;
		if (vsi->size != sz)
			continue;
		if (off % align)
			return -EINVAL;

		switch (field_type) {
		case BTF_FIELD_SPIN_LOCK:
		case BTF_FIELD_TIMER:
			ret = btf_find_struct(btf, var_type, off, sz,
					      idx < info_cnt ? &info[idx] : &tmp);
			if (ret < 0)
				return ret;
			break;
		case BTF_FIELD_KPTR:
			ret = btf_find_kptr(btf, var_type, off, sz,
					    idx < info_cnt ? &info[idx] : &tmp);
			if (ret < 0)
				return ret;
			break;
		default:
			return -EFAULT;
		}

		if (ret == BTF_FIELD_IGNORE)
			continue;
		if (idx >= info_cnt)
			return -E2BIG;
		++idx;
	}
	return idx;
}

static int btf_find_field(const struct btf *btf, const struct btf_type *t,
			  enum btf_field_type field_type,
			  struct btf_field_info *info, int info_cnt)
{
	const char *name;
	int sz, align;

	switch (field_type) {
	case BTF_FIELD_SPIN_LOCK:
		name = "bpf_spin_lock";
		sz = sizeof(struct bpf_spin_lock);
		align = __alignof__(struct bpf_spin_lock);
		break;
	case BTF_FIELD_TIMER:
		name = "bpf_timer";
		sz = sizeof(struct bpf_timer);
		align = __alignof__(struct bpf_timer);
		break;
	case BTF_FIELD_KPTR:
		name = NULL;
		sz = sizeof(u64);
		align = 8;
		break;
	default:
		return -EFAULT;
	}

	if (__btf_type_is_struct(t))
		return btf_find_struct_field(btf, t, name, sz, align, field_type, info, info_cnt);
	else if (btf_type_is_datasec(t))
		return btf_find_datasec_var(btf, t, name, sz, align, field_type, info, info_cnt);
	return -EINVAL;
}

/* find 'struct bpf_spin_lock' in map value.
 * return >= 0 offset if found
 * and < 0 in case of error
 */
int btf_find_spin_lock(const struct btf *btf, const struct btf_type *t)
{
	struct btf_field_info info;
	int ret;

	ret = btf_find_field(btf, t, BTF_FIELD_SPIN_LOCK, &info, 1);
	if (ret < 0)
		return ret;
	if (!ret)
		return -ENOENT;
	return info.off;
}

int btf_find_timer(const struct btf *btf, const struct btf_type *t)
{
	struct btf_field_info info;
	int ret;

	ret = btf_find_field(btf, t, BTF_FIELD_TIMER, &info, 1);
	if (ret < 0)
		return ret;
	if (!ret)
		return -ENOENT;
	return info.off;
}

struct bpf_map_value_off *btf_parse_kptrs(const struct btf *btf,
					  const struct btf_type *t)
{
	struct btf_field_info info_arr[BPF_MAP_VALUE_OFF_MAX];
	struct bpf_map_value_off *tab;
	struct btf *kernel_btf = NULL;
	struct module *mod = NULL;
	int ret, i, nr_off;

	ret = btf_find_field(btf, t, BTF_FIELD_KPTR, info_arr, ARRAY_SIZE(info_arr));
	if (ret < 0)
		return ERR_PTR(ret);
	if (!ret)
		return NULL;

	nr_off = ret;
	tab = kzalloc(offsetof(struct bpf_map_value_off, off[nr_off]), GFP_KERNEL | __GFP_NOWARN);
	if (!tab)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_off; i++) {
		const struct btf_type *t;
		s32 id;

		/* Find type in map BTF, and use it to look up the matching type
		 * in vmlinux or module BTFs, by name and kind.
		 */
		t = btf_type_by_id(btf, info_arr[i].type_id);
		id = bpf_find_btf_id(__btf_name_by_offset(btf, t->name_off), BTF_INFO_KIND(t->info),
				     &kernel_btf);
		if (id < 0) {
			ret = id;
			goto end;
		}

		/* Find and stash the function pointer for the destruction function that
		 * needs to be eventually invoked from the map free path.
		 */
		if (info_arr[i].type == BPF_KPTR_REF) {
			const struct btf_type *dtor_func;
			const char *dtor_func_name;
			unsigned long addr;
			s32 dtor_btf_id;

			/* This call also serves as a whitelist of allowed objects that
			 * can be used as a referenced pointer and be stored in a map at
			 * the same time.
			 */
			dtor_btf_id = btf_find_dtor_kfunc(kernel_btf, id);
			if (dtor_btf_id < 0) {
				ret = dtor_btf_id;
				goto end_btf;
			}

			dtor_func = btf_type_by_id(kernel_btf, dtor_btf_id);
			if (!dtor_func) {
				ret = -ENOENT;
				goto end_btf;
			}

			if (btf_is_module(kernel_btf)) {
				mod = btf_try_get_module(kernel_btf);
				if (!mod) {
					ret = -ENXIO;
					goto end_btf;
				}
			}

			/* We already verified dtor_func to be btf_type_is_func
			 * in register_btf_id_dtor_kfuncs.
			 */
			dtor_func_name = __btf_name_by_offset(kernel_btf, dtor_func->name_off);
			addr = kallsyms_lookup_name(dtor_func_name);
			if (!addr) {
				ret = -EINVAL;
				goto end_mod;
			}
			tab->off[i].kptr.dtor = (void *)addr;
		}

		tab->off[i].offset = info_arr[i].off;
		tab->off[i].type = info_arr[i].type;
		tab->off[i].kptr.btf_id = id;
		tab->off[i].kptr.btf = kernel_btf;
		tab->off[i].kptr.module = mod;
	}
	tab->nr_off = nr_off;
	return tab;
end_mod:
	module_put(mod);
end_btf:
	btf_put(kernel_btf);
end:
	while (i--) {
		btf_put(tab->off[i].kptr.btf);
		if (tab->off[i].kptr.module)
			module_put(tab->off[i].kptr.module);
	}
	kfree(tab);
	return ERR_PTR(ret);
}

static void __btf_struct_show(const struct btf *btf, const struct btf_type *t,
			      u32 type_id, void *data, u8 bits_offset,
			      struct btf_show *show)
{
	const struct btf_member *member;
	void *safe_data;
	u32 i;

	safe_data = btf_show_start_struct_type(show, t, type_id, data);
	if (!safe_data)
		return;

	for_each_member(i, t, member) {
		const struct btf_type *member_type = btf_type_by_id(btf,
								member->type);
		const struct btf_kind_operations *ops;
		u32 member_offset, bitfield_size;
		u32 bytes_offset;
		u8 bits8_offset;

		btf_show_start_member(show, member);

		member_offset = __btf_member_bit_offset(t, member);
		bitfield_size = __btf_member_bitfield_size(t, member);
		bytes_offset = BITS_ROUNDDOWN_BYTES(member_offset);
		bits8_offset = BITS_PER_BYTE_MASKED(member_offset);
		if (bitfield_size) {
			safe_data = btf_show_start_type(show, member_type,
							member->type,
							data + bytes_offset);
			if (safe_data)
				btf_bitfield_show(safe_data,
						  bits8_offset,
						  bitfield_size, show);
			btf_show_end_type(show);
		} else {
			ops = btf_type_ops(member_type);
			ops->show(btf, member_type, member->type,
				  data + bytes_offset, bits8_offset, show);
		}

		btf_show_end_member(show);
	}

	btf_show_end_struct_type(show);
}

static void btf_struct_show(const struct btf *btf, const struct btf_type *t,
			    u32 type_id, void *data, u8 bits_offset,
			    struct btf_show *show)
{
	const struct btf_member *m = show->state.member;

	/*
	 * First check if any members would be shown (are non-zero).
	 * See comments above "struct btf_show" definition for more
	 * details on how this works at a high-level.
	 */
	if (show->state.depth > 0 && !(show->flags & BTF_SHOW_ZERO)) {
		if (!show->state.depth_check) {
			show->state.depth_check = show->state.depth + 1;
			show->state.depth_to_show = 0;
		}
		__btf_struct_show(btf, t, type_id, data, bits_offset, show);
		/* Restore saved member data here */
		show->state.member = m;
		if (show->state.depth_check != show->state.depth + 1)
			return;
		show->state.depth_check = 0;

		if (show->state.depth_to_show <= show->state.depth)
			return;
		/*
		 * Reaching here indicates we have recursed and found
		 * non-zero child values.
		 */
	}

	__btf_struct_show(btf, t, type_id, data, bits_offset, show);
}

static struct btf_kind_operations struct_ops = {
	.check_meta = btf_struct_check_meta,
	.resolve = btf_struct_resolve,
	.check_member = btf_struct_check_member,
	.check_kflag_member = btf_generic_check_kflag_member,
	.log_details = btf_struct_log,
	.show = btf_struct_show,
};

static int btf_enum_check_member(struct btf_verifier_env *env,
				 const struct btf_type *struct_type,
				 const struct btf_member *member,
				 const struct btf_type *member_type)
{
	u32 struct_bits_off = member->offset;
	u32 struct_size, bytes_offset;

	if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
		btf_verifier_log_member(env, struct_type, member,
					"Member is not byte aligned");
		return -EINVAL;
	}

	struct_size = struct_type->size;
	bytes_offset = BITS_ROUNDDOWN_BYTES(struct_bits_off);
	if (struct_size - bytes_offset < member_type->size) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static int btf_enum_check_kflag_member(struct btf_verifier_env *env,
				       const struct btf_type *struct_type,
				       const struct btf_member *member,
				       const struct btf_type *member_type)
{
	u32 struct_bits_off, nr_bits, bytes_end, struct_size;
	u32 int_bitsize = sizeof(int) * BITS_PER_BYTE;

	struct_bits_off = BTF_MEMBER_BIT_OFFSET(member->offset);
	nr_bits = BTF_MEMBER_BITFIELD_SIZE(member->offset);
	if (!nr_bits) {
		if (BITS_PER_BYTE_MASKED(struct_bits_off)) {
			btf_verifier_log_member(env, struct_type, member,
						"Member is not byte aligned");
			return -EINVAL;
		}

		nr_bits = int_bitsize;
	} else if (nr_bits > int_bitsize) {
		btf_verifier_log_member(env, struct_type, member,
					"Invalid member bitfield_size");
		return -EINVAL;
	}

	struct_size = struct_type->size;
	bytes_end = BITS_ROUNDUP_BYTES(struct_bits_off + nr_bits);
	if (struct_size < bytes_end) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static s32 btf_enum_check_meta(struct btf_verifier_env *env,
			       const struct btf_type *t,
			       u32 meta_left)
{
	const struct btf_enum *enums = btf_type_enum(t);
	struct btf *btf = env->btf;
	const char *fmt_str;
	u16 i, nr_enums;
	u32 meta_needed;

	nr_enums = btf_type_vlen(t);
	meta_needed = nr_enums * sizeof(*enums);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (t->size > 8 || !is_power_of_2(t->size)) {
		btf_verifier_log_type(env, t, "Unexpected size");
		return -EINVAL;
	}

	/* enum type either no name or a valid one */
	if (t->name_off &&
	    !btf_name_valid_identifier(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	for (i = 0; i < nr_enums; i++) {
		if (!btf_name_offset_valid(btf, enums[i].name_off)) {
			btf_verifier_log(env, "\tInvalid name_offset:%u",
					 enums[i].name_off);
			return -EINVAL;
		}

		/* enum member must have a valid name */
		if (!enums[i].name_off ||
		    !btf_name_valid_identifier(btf, enums[i].name_off)) {
			btf_verifier_log_type(env, t, "Invalid name");
			return -EINVAL;
		}

		if (env->log.level == BPF_LOG_KERNEL)
			continue;
		fmt_str = btf_type_kflag(t) ? "\t%s val=%d\n" : "\t%s val=%u\n";
		btf_verifier_log(env, fmt_str,
				 __btf_name_by_offset(btf, enums[i].name_off),
				 enums[i].val);
	}

	return meta_needed;
}

static void btf_enum_log(struct btf_verifier_env *env,
			 const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u vlen=%u", t->size, btf_type_vlen(t));
}

static void btf_enum_show(const struct btf *btf, const struct btf_type *t,
			  u32 type_id, void *data, u8 bits_offset,
			  struct btf_show *show)
{
	const struct btf_enum *enums = btf_type_enum(t);
	u32 i, nr_enums = btf_type_vlen(t);
	void *safe_data;
	int v;

	safe_data = btf_show_start_type(show, t, type_id, data);
	if (!safe_data)
		return;

	v = *(int *)safe_data;

	for (i = 0; i < nr_enums; i++) {
		if (v != enums[i].val)
			continue;

		btf_show_type_value(show, "%s",
				    __btf_name_by_offset(btf,
							 enums[i].name_off));

		btf_show_end_type(show);
		return;
	}

	if (btf_type_kflag(t))
		btf_show_type_value(show, "%d", v);
	else
		btf_show_type_value(show, "%u", v);
	btf_show_end_type(show);
}

static struct btf_kind_operations enum_ops = {
	.check_meta = btf_enum_check_meta,
	.resolve = btf_df_resolve,
	.check_member = btf_enum_check_member,
	.check_kflag_member = btf_enum_check_kflag_member,
	.log_details = btf_enum_log,
	.show = btf_enum_show,
};

static s32 btf_enum64_check_meta(struct btf_verifier_env *env,
				 const struct btf_type *t,
				 u32 meta_left)
{
	const struct btf_enum64 *enums = btf_type_enum64(t);
	struct btf *btf = env->btf;
	const char *fmt_str;
	u16 i, nr_enums;
	u32 meta_needed;

	nr_enums = btf_type_vlen(t);
	meta_needed = nr_enums * sizeof(*enums);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (t->size > 8 || !is_power_of_2(t->size)) {
		btf_verifier_log_type(env, t, "Unexpected size");
		return -EINVAL;
	}

	/* enum type either no name or a valid one */
	if (t->name_off &&
	    !btf_name_valid_identifier(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	for (i = 0; i < nr_enums; i++) {
		if (!btf_name_offset_valid(btf, enums[i].name_off)) {
			btf_verifier_log(env, "\tInvalid name_offset:%u",
					 enums[i].name_off);
			return -EINVAL;
		}

		/* enum member must have a valid name */
		if (!enums[i].name_off ||
		    !btf_name_valid_identifier(btf, enums[i].name_off)) {
			btf_verifier_log_type(env, t, "Invalid name");
			return -EINVAL;
		}

		if (env->log.level == BPF_LOG_KERNEL)
			continue;

		fmt_str = btf_type_kflag(t) ? "\t%s val=%lld\n" : "\t%s val=%llu\n";
		btf_verifier_log(env, fmt_str,
				 __btf_name_by_offset(btf, enums[i].name_off),
				 btf_enum64_value(enums + i));
	}

	return meta_needed;
}

static void btf_enum64_show(const struct btf *btf, const struct btf_type *t,
			    u32 type_id, void *data, u8 bits_offset,
			    struct btf_show *show)
{
	const struct btf_enum64 *enums = btf_type_enum64(t);
	u32 i, nr_enums = btf_type_vlen(t);
	void *safe_data;
	s64 v;

	safe_data = btf_show_start_type(show, t, type_id, data);
	if (!safe_data)
		return;

	v = *(u64 *)safe_data;

	for (i = 0; i < nr_enums; i++) {
		if (v != btf_enum64_value(enums + i))
			continue;

		btf_show_type_value(show, "%s",
				    __btf_name_by_offset(btf,
							 enums[i].name_off));

		btf_show_end_type(show);
		return;
	}

	if (btf_type_kflag(t))
		btf_show_type_value(show, "%lld", v);
	else
		btf_show_type_value(show, "%llu", v);
	btf_show_end_type(show);
}

static struct btf_kind_operations enum64_ops = {
	.check_meta = btf_enum64_check_meta,
	.resolve = btf_df_resolve,
	.check_member = btf_enum_check_member,
	.check_kflag_member = btf_enum_check_kflag_member,
	.log_details = btf_enum_log,
	.show = btf_enum64_show,
};

static s32 btf_func_proto_check_meta(struct btf_verifier_env *env,
				     const struct btf_type *t,
				     u32 meta_left)
{
	u32 meta_needed = btf_type_vlen(t) * sizeof(struct btf_param);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (t->name_off) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static void btf_func_proto_log(struct btf_verifier_env *env,
			       const struct btf_type *t)
{
	const struct btf_param *args = (const struct btf_param *)(t + 1);
	u16 nr_args = btf_type_vlen(t), i;

	btf_verifier_log(env, "return=%u args=(", t->type);
	if (!nr_args) {
		btf_verifier_log(env, "void");
		goto done;
	}

	if (nr_args == 1 && !args[0].type) {
		/* Only one vararg */
		btf_verifier_log(env, "vararg");
		goto done;
	}

	btf_verifier_log(env, "%u %s", args[0].type,
			 __btf_name_by_offset(env->btf,
					      args[0].name_off));
	for (i = 1; i < nr_args - 1; i++)
		btf_verifier_log(env, ", %u %s", args[i].type,
				 __btf_name_by_offset(env->btf,
						      args[i].name_off));

	if (nr_args > 1) {
		const struct btf_param *last_arg = &args[nr_args - 1];

		if (last_arg->type)
			btf_verifier_log(env, ", %u %s", last_arg->type,
					 __btf_name_by_offset(env->btf,
							      last_arg->name_off));
		else
			btf_verifier_log(env, ", vararg");
	}

done:
	btf_verifier_log(env, ")");
}

static struct btf_kind_operations func_proto_ops = {
	.check_meta = btf_func_proto_check_meta,
	.resolve = btf_df_resolve,
	/*
	 * BTF_KIND_FUNC_PROTO cannot be directly referred by
	 * a struct's member.
	 *
	 * It should be a function pointer instead.
	 * (i.e. struct's member -> BTF_KIND_PTR -> BTF_KIND_FUNC_PROTO)
	 *
	 * Hence, there is no btf_func_check_member().
	 */
	.check_member = btf_df_check_member,
	.check_kflag_member = btf_df_check_kflag_member,
	.log_details = btf_func_proto_log,
	.show = btf_df_show,
};

static s32 btf_func_check_meta(struct btf_verifier_env *env,
			       const struct btf_type *t,
			       u32 meta_left)
{
	if (!t->name_off ||
	    !btf_name_valid_identifier(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	if (btf_type_vlen(t) > BTF_FUNC_GLOBAL) {
		btf_verifier_log_type(env, t, "Invalid func linkage");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return 0;
}

static int btf_func_resolve(struct btf_verifier_env *env,
			    const struct resolve_vertex *v)
{
	const struct btf_type *t = v->t;
	u32 next_type_id = t->type;
	int err;

	err = btf_func_check(env, t);
	if (err)
		return err;

	env_stack_pop_resolved(env, next_type_id, 0);
	return 0;
}

static struct btf_kind_operations func_ops = {
	.check_meta = btf_func_check_meta,
	.resolve = btf_func_resolve,
	.check_member = btf_df_check_member,
	.check_kflag_member = btf_df_check_kflag_member,
	.log_details = btf_ref_type_log,
	.show = btf_df_show,
};

static s32 btf_var_check_meta(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 meta_left)
{
	const struct btf_var *var;
	u32 meta_needed = sizeof(*var);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	if (!t->name_off ||
	    !__btf_name_valid(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	/* A var cannot be in type void */
	if (!t->type || !BTF_TYPE_ID_VALID(t->type)) {
		btf_verifier_log_type(env, t, "Invalid type_id");
		return -EINVAL;
	}

	var = btf_type_var(t);
	if (var->linkage != BTF_VAR_STATIC &&
	    var->linkage != BTF_VAR_GLOBAL_ALLOCATED) {
		btf_verifier_log_type(env, t, "Linkage not supported");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static void btf_var_log(struct btf_verifier_env *env, const struct btf_type *t)
{
	const struct btf_var *var = btf_type_var(t);

	btf_verifier_log(env, "type_id=%u linkage=%u", t->type, var->linkage);
}

static const struct btf_kind_operations var_ops = {
	.check_meta		= btf_var_check_meta,
	.resolve		= btf_var_resolve,
	.check_member		= btf_df_check_member,
	.check_kflag_member	= btf_df_check_kflag_member,
	.log_details		= btf_var_log,
	.show			= btf_var_show,
};

static s32 btf_datasec_check_meta(struct btf_verifier_env *env,
				  const struct btf_type *t,
				  u32 meta_left)
{
	const struct btf_var_secinfo *vsi;
	u64 last_vsi_end_off = 0, sum = 0;
	u32 i, meta_needed;

	meta_needed = btf_type_vlen(t) * sizeof(*vsi);
	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (!t->size) {
		btf_verifier_log_type(env, t, "size == 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	if (!t->name_off ||
	    !btf_name_valid_section(env->btf, t->name_off)) {
		btf_verifier_log_type(env, t, "Invalid name");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	for_each_vsi(i, t, vsi) {
		/* A var cannot be in type void */
		if (!vsi->type || !BTF_TYPE_ID_VALID(vsi->type)) {
			btf_verifier_log_vsi(env, t, vsi,
					     "Invalid type_id");
			return -EINVAL;
		}

		if (vsi->offset < last_vsi_end_off || vsi->offset >= t->size) {
			btf_verifier_log_vsi(env, t, vsi,
					     "Invalid offset");
			return -EINVAL;
		}

		if (!vsi->size || vsi->size > t->size) {
			btf_verifier_log_vsi(env, t, vsi,
					     "Invalid size");
			return -EINVAL;
		}

		last_vsi_end_off = vsi->offset + vsi->size;
		if (last_vsi_end_off > t->size) {
			btf_verifier_log_vsi(env, t, vsi,
					     "Invalid offset+size");
			return -EINVAL;
		}

		btf_verifier_log_vsi(env, t, vsi, NULL);
		sum += vsi->size;
	}

	if (t->size < sum) {
		btf_verifier_log_type(env, t, "Invalid btf_info size");
		return -EINVAL;
	}

	return meta_needed;
}

static int btf_datasec_resolve(struct btf_verifier_env *env,
			       const struct resolve_vertex *v)
{
	const struct btf_var_secinfo *vsi;
	struct btf *btf = env->btf;
	u16 i;

	env->resolve_mode = RESOLVE_TBD;
	for_each_vsi_from(i, v->next_member, v->t, vsi) {
		u32 var_type_id = vsi->type, type_id, type_size = 0;
		const struct btf_type *var_type = btf_type_by_id(env->btf,
								 var_type_id);
		if (!var_type || !btf_type_is_var(var_type)) {
			btf_verifier_log_vsi(env, v->t, vsi,
					     "Not a VAR kind member");
			return -EINVAL;
		}

		if (!env_type_is_resolve_sink(env, var_type) &&
		    !env_type_is_resolved(env, var_type_id)) {
			env_stack_set_next_member(env, i + 1);
			return env_stack_push(env, var_type, var_type_id);
		}

		type_id = var_type->type;
		if (!btf_type_id_size(btf, &type_id, &type_size)) {
			btf_verifier_log_vsi(env, v->t, vsi, "Invalid type");
			return -EINVAL;
		}

		if (vsi->size < type_size) {
			btf_verifier_log_vsi(env, v->t, vsi, "Invalid size");
			return -EINVAL;
		}
	}

	env_stack_pop_resolved(env, 0, 0);
	return 0;
}

static void btf_datasec_log(struct btf_verifier_env *env,
			    const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u vlen=%u", t->size, btf_type_vlen(t));
}

static void btf_datasec_show(const struct btf *btf,
			     const struct btf_type *t, u32 type_id,
			     void *data, u8 bits_offset,
			     struct btf_show *show)
{
	const struct btf_var_secinfo *vsi;
	const struct btf_type *var;
	u32 i;

	if (!btf_show_start_type(show, t, type_id, data))
		return;

	btf_show_type_value(show, "section (\"%s\") = {",
			    __btf_name_by_offset(btf, t->name_off));
	for_each_vsi(i, t, vsi) {
		var = btf_type_by_id(btf, vsi->type);
		if (i)
			btf_show(show, ",");
		btf_type_ops(var)->show(btf, var, vsi->type,
					data + vsi->offset, bits_offset, show);
	}
	btf_show_end_type(show);
}

static const struct btf_kind_operations datasec_ops = {
	.check_meta		= btf_datasec_check_meta,
	.resolve		= btf_datasec_resolve,
	.check_member		= btf_df_check_member,
	.check_kflag_member	= btf_df_check_kflag_member,
	.log_details		= btf_datasec_log,
	.show			= btf_datasec_show,
};

static s32 btf_float_check_meta(struct btf_verifier_env *env,
				const struct btf_type *t,
				u32 meta_left)
{
	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	if (t->size != 2 && t->size != 4 && t->size != 8 && t->size != 12 &&
	    t->size != 16) {
		btf_verifier_log_type(env, t, "Invalid type_size");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return 0;
}

static int btf_float_check_member(struct btf_verifier_env *env,
				  const struct btf_type *struct_type,
				  const struct btf_member *member,
				  const struct btf_type *member_type)
{
	u64 start_offset_bytes;
	u64 end_offset_bytes;
	u64 misalign_bits;
	u64 align_bytes;
	u64 align_bits;

	/* Different architectures have different alignment requirements, so
	 * here we check only for the reasonable minimum. This way we ensure
	 * that types after CO-RE can pass the kernel BTF verifier.
	 */
	align_bytes = min_t(u64, sizeof(void *), member_type->size);
	align_bits = align_bytes * BITS_PER_BYTE;
	div64_u64_rem(member->offset, align_bits, &misalign_bits);
	if (misalign_bits) {
		btf_verifier_log_member(env, struct_type, member,
					"Member is not properly aligned");
		return -EINVAL;
	}

	start_offset_bytes = member->offset / BITS_PER_BYTE;
	end_offset_bytes = start_offset_bytes + member_type->size;
	if (end_offset_bytes > struct_type->size) {
		btf_verifier_log_member(env, struct_type, member,
					"Member exceeds struct_size");
		return -EINVAL;
	}

	return 0;
}

static void btf_float_log(struct btf_verifier_env *env,
			  const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u", t->size);
}

static const struct btf_kind_operations float_ops = {
	.check_meta = btf_float_check_meta,
	.resolve = btf_df_resolve,
	.check_member = btf_float_check_member,
	.check_kflag_member = btf_generic_check_kflag_member,
	.log_details = btf_float_log,
	.show = btf_df_show,
};

static s32 btf_decl_tag_check_meta(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 meta_left)
{
	const struct btf_decl_tag *tag;
	u32 meta_needed = sizeof(*tag);
	s32 component_idx;
	const char *value;

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	value = btf_name_by_offset(env->btf, t->name_off);
	if (!value || !value[0]) {
		btf_verifier_log_type(env, t, "Invalid value");
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (btf_type_kflag(t)) {
		btf_verifier_log_type(env, t, "Invalid btf_info kind_flag");
		return -EINVAL;
	}

	component_idx = btf_type_decl_tag(t)->component_idx;
	if (component_idx < -1) {
		btf_verifier_log_type(env, t, "Invalid component_idx");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static int btf_decl_tag_resolve(struct btf_verifier_env *env,
			   const struct resolve_vertex *v)
{
	const struct btf_type *next_type;
	const struct btf_type *t = v->t;
	u32 next_type_id = t->type;
	struct btf *btf = env->btf;
	s32 component_idx;
	u32 vlen;

	next_type = btf_type_by_id(btf, next_type_id);
	if (!next_type || !btf_type_is_decl_tag_target(next_type)) {
		btf_verifier_log_type(env, v->t, "Invalid type_id");
		return -EINVAL;
	}

	if (!env_type_is_resolve_sink(env, next_type) &&
	    !env_type_is_resolved(env, next_type_id))
		return env_stack_push(env, next_type, next_type_id);

	component_idx = btf_type_decl_tag(t)->component_idx;
	if (component_idx != -1) {
		if (btf_type_is_var(next_type) || btf_type_is_typedef(next_type)) {
			btf_verifier_log_type(env, v->t, "Invalid component_idx");
			return -EINVAL;
		}

		if (btf_type_is_struct(next_type)) {
			vlen = btf_type_vlen(next_type);
		} else {
			/* next_type should be a function */
			next_type = btf_type_by_id(btf, next_type->type);
			vlen = btf_type_vlen(next_type);
		}

		if ((u32)component_idx >= vlen) {
			btf_verifier_log_type(env, v->t, "Invalid component_idx");
			return -EINVAL;
		}
	}

	env_stack_pop_resolved(env, next_type_id, 0);

	return 0;
}

static void btf_decl_tag_log(struct btf_verifier_env *env, const struct btf_type *t)
{
	btf_verifier_log(env, "type=%u component_idx=%d", t->type,
			 btf_type_decl_tag(t)->component_idx);
}

static const struct btf_kind_operations decl_tag_ops = {
	.check_meta = btf_decl_tag_check_meta,
	.resolve = btf_decl_tag_resolve,
	.check_member = btf_df_check_member,
	.check_kflag_member = btf_df_check_kflag_member,
	.log_details = btf_decl_tag_log,
	.show = btf_df_show,
};

static int btf_func_proto_check(struct btf_verifier_env *env,
				const struct btf_type *t)
{
	const struct btf_type *ret_type;
	const struct btf_param *args;
	const struct btf *btf;
	u16 nr_args, i;
	int err;

	btf = env->btf;
	args = (const struct btf_param *)(t + 1);
	nr_args = btf_type_vlen(t);

	/* Check func return type which could be "void" (t->type == 0) */
	if (t->type) {
		u32 ret_type_id = t->type;

		ret_type = btf_type_by_id(btf, ret_type_id);
		if (!ret_type) {
			btf_verifier_log_type(env, t, "Invalid return type");
			return -EINVAL;
		}

		if (btf_type_is_resolve_source_only(ret_type)) {
			btf_verifier_log_type(env, t, "Invalid return type");
			return -EINVAL;
		}

		if (btf_type_needs_resolve(ret_type) &&
		    !env_type_is_resolved(env, ret_type_id)) {
			err = btf_resolve(env, ret_type, ret_type_id);
			if (err)
				return err;
		}

		/* Ensure the return type is a type that has a size */
		if (!btf_type_id_size(btf, &ret_type_id, NULL)) {
			btf_verifier_log_type(env, t, "Invalid return type");
			return -EINVAL;
		}
	}

	if (!nr_args)
		return 0;

	/* Last func arg type_id could be 0 if it is a vararg */
	if (!args[nr_args - 1].type) {
		if (args[nr_args - 1].name_off) {
			btf_verifier_log_type(env, t, "Invalid arg#%u",
					      nr_args);
			return -EINVAL;
		}
		nr_args--;
	}

	err = 0;
	for (i = 0; i < nr_args; i++) {
		const struct btf_type *arg_type;
		u32 arg_type_id;

		arg_type_id = args[i].type;
		arg_type = btf_type_by_id(btf, arg_type_id);
		if (!arg_type) {
			btf_verifier_log_type(env, t, "Invalid arg#%u", i + 1);
			err = -EINVAL;
			break;
		}

		if (btf_type_is_resolve_source_only(arg_type)) {
			btf_verifier_log_type(env, t, "Invalid arg#%u", i + 1);
			return -EINVAL;
		}

		if (args[i].name_off &&
		    (!btf_name_offset_valid(btf, args[i].name_off) ||
		     !btf_name_valid_identifier(btf, args[i].name_off))) {
			btf_verifier_log_type(env, t,
					      "Invalid arg#%u", i + 1);
			err = -EINVAL;
			break;
		}

		if (btf_type_needs_resolve(arg_type) &&
		    !env_type_is_resolved(env, arg_type_id)) {
			err = btf_resolve(env, arg_type, arg_type_id);
			if (err)
				break;
		}

		if (!btf_type_id_size(btf, &arg_type_id, NULL)) {
			btf_verifier_log_type(env, t, "Invalid arg#%u", i + 1);
			err = -EINVAL;
			break;
		}
	}

	return err;
}

static int btf_func_check(struct btf_verifier_env *env,
			  const struct btf_type *t)
{
	const struct btf_type *proto_type;
	const struct btf_param *args;
	const struct btf *btf;
	u16 nr_args, i;

	btf = env->btf;
	proto_type = btf_type_by_id(btf, t->type);

	if (!proto_type || !btf_type_is_func_proto(proto_type)) {
		btf_verifier_log_type(env, t, "Invalid type_id");
		return -EINVAL;
	}

	args = (const struct btf_param *)(proto_type + 1);
	nr_args = btf_type_vlen(proto_type);
	for (i = 0; i < nr_args; i++) {
		if (!args[i].name_off && args[i].type) {
			btf_verifier_log_type(env, t, "Invalid arg#%u", i + 1);
			return -EINVAL;
		}
	}

	return 0;
}

static const struct btf_kind_operations * const kind_ops[NR_BTF_KINDS] = {
	[BTF_KIND_INT] = &int_ops,
	[BTF_KIND_PTR] = &ptr_ops,
	[BTF_KIND_ARRAY] = &array_ops,
	[BTF_KIND_STRUCT] = &struct_ops,
	[BTF_KIND_UNION] = &struct_ops,
	[BTF_KIND_ENUM] = &enum_ops,
	[BTF_KIND_FWD] = &fwd_ops,
	[BTF_KIND_TYPEDEF] = &modifier_ops,
	[BTF_KIND_VOLATILE] = &modifier_ops,
	[BTF_KIND_CONST] = &modifier_ops,
	[BTF_KIND_RESTRICT] = &modifier_ops,
	[BTF_KIND_FUNC] = &func_ops,
	[BTF_KIND_FUNC_PROTO] = &func_proto_ops,
	[BTF_KIND_VAR] = &var_ops,
	[BTF_KIND_DATASEC] = &datasec_ops,
	[BTF_KIND_FLOAT] = &float_ops,
	[BTF_KIND_DECL_TAG] = &decl_tag_ops,
	[BTF_KIND_TYPE_TAG] = &modifier_ops,
	[BTF_KIND_ENUM64] = &enum64_ops,
};

static s32 btf_check_meta(struct btf_verifier_env *env,
			  const struct btf_type *t,
			  u32 meta_left)
{
	u32 saved_meta_left = meta_left;
	s32 var_meta_size;

	if (meta_left < sizeof(*t)) {
		btf_verifier_log(env, "[%u] meta_left:%u meta_needed:%zu",
				 env->log_type_id, meta_left, sizeof(*t));
		return -EINVAL;
	}
	meta_left -= sizeof(*t);

	if (t->info & ~BTF_INFO_MASK) {
		btf_verifier_log(env, "[%u] Invalid btf_info:%x",
				 env->log_type_id, t->info);
		return -EINVAL;
	}

	if (BTF_INFO_KIND(t->info) > BTF_KIND_MAX ||
	    BTF_INFO_KIND(t->info) == BTF_KIND_UNKN) {
		btf_verifier_log(env, "[%u] Invalid kind:%u",
				 env->log_type_id, BTF_INFO_KIND(t->info));
		return -EINVAL;
	}

	if (!btf_name_offset_valid(env->btf, t->name_off)) {
		btf_verifier_log(env, "[%u] Invalid name_offset:%u",
				 env->log_type_id, t->name_off);
		return -EINVAL;
	}

	var_meta_size = btf_type_ops(t)->check_meta(env, t, meta_left);
	if (var_meta_size < 0)
		return var_meta_size;

	meta_left -= var_meta_size;

	return saved_meta_left - meta_left;
}

static int btf_check_all_metas(struct btf_verifier_env *env)
{
	struct btf *btf = env->btf;
	struct btf_header *hdr;
	void *cur, *end;

	hdr = &btf->hdr;
	cur = btf->nohdr_data + hdr->type_off;
	end = cur + hdr->type_len;

	env->log_type_id = btf->base_btf ? btf->start_id : 1;
	while (cur < end) {
		struct btf_type *t = cur;
		s32 meta_size;

		meta_size = btf_check_meta(env, t, end - cur);
		if (meta_size < 0)
			return meta_size;

		btf_add_type(env, t);
		cur += meta_size;
		env->log_type_id++;
	}

	return 0;
}

static bool btf_resolve_valid(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 type_id)
{
	struct btf *btf = env->btf;

	if (!env_type_is_resolved(env, type_id))
		return false;

	if (btf_type_is_struct(t) || btf_type_is_datasec(t))
		return !btf_resolved_type_id(btf, type_id) &&
		       !btf_resolved_type_size(btf, type_id);

	if (btf_type_is_decl_tag(t) || btf_type_is_func(t))
		return btf_resolved_type_id(btf, type_id) &&
		       !btf_resolved_type_size(btf, type_id);

	if (btf_type_is_modifier(t) || btf_type_is_ptr(t) ||
	    btf_type_is_var(t)) {
		t = btf_type_id_resolve(btf, &type_id);
		return t &&
		       !btf_type_is_modifier(t) &&
		       !btf_type_is_var(t) &&
		       !btf_type_is_datasec(t);
	}

	if (btf_type_is_array(t)) {
		const struct btf_array *array = btf_type_array(t);
		const struct btf_type *elem_type;
		u32 elem_type_id = array->type;
		u32 elem_size;

		elem_type = btf_type_id_size(btf, &elem_type_id, &elem_size);
		return elem_type && !btf_type_is_modifier(elem_type) &&
			(array->nelems * elem_size ==
			 btf_resolved_type_size(btf, type_id));
	}

	return false;
}

static int btf_resolve(struct btf_verifier_env *env,
		       const struct btf_type *t, u32 type_id)
{
	u32 save_log_type_id = env->log_type_id;
	const struct resolve_vertex *v;
	int err = 0;

	env->resolve_mode = RESOLVE_TBD;
	env_stack_push(env, t, type_id);
	while (!err && (v = env_stack_peak(env))) {
		env->log_type_id = v->type_id;
		err = btf_type_ops(v->t)->resolve(env, v);
	}

	env->log_type_id = type_id;
	if (err == -E2BIG) {
		btf_verifier_log_type(env, t,
				      "Exceeded max resolving depth:%u",
				      MAX_RESOLVE_DEPTH);
	} else if (err == -EEXIST) {
		btf_verifier_log_type(env, t, "Loop detected");
	}

	/* Final sanity check */
	if (!err && !btf_resolve_valid(env, t, type_id)) {
		btf_verifier_log_type(env, t, "Invalid resolve state");
		err = -EINVAL;
	}

	env->log_type_id = save_log_type_id;
	return err;
}

static int btf_check_all_types(struct btf_verifier_env *env)
{
	struct btf *btf = env->btf;
	const struct btf_type *t;
	u32 type_id, i;
	int err;

	err = env_resolve_init(env);
	if (err)
		return err;

	env->phase++;
	for (i = btf->base_btf ? 0 : 1; i < btf->nr_types; i++) {
		type_id = btf->start_id + i;
		t = btf_type_by_id(btf, type_id);

		env->log_type_id = type_id;
		if (btf_type_needs_resolve(t) &&
		    !env_type_is_resolved(env, type_id)) {
			err = btf_resolve(env, t, type_id);
			if (err)
				return err;
		}

		if (btf_type_is_func_proto(t)) {
			err = btf_func_proto_check(env, t);
			if (err)
				return err;
		}
	}

	return 0;
}

static int btf_parse_type_sec(struct btf_verifier_env *env)
{
	const struct btf_header *hdr = &env->btf->hdr;
	int err;

	/* Type section must align to 4 bytes */
	if (hdr->type_off & (sizeof(u32) - 1)) {
		btf_verifier_log(env, "Unaligned type_off");
		return -EINVAL;
	}

	if (!env->btf->base_btf && !hdr->type_len) {
		btf_verifier_log(env, "No type found");
		return -EINVAL;
	}

	err = btf_check_all_metas(env);
	if (err)
		return err;

	return btf_check_all_types(env);
}

static int btf_parse_str_sec(struct btf_verifier_env *env)
{
	const struct btf_header *hdr;
	struct btf *btf = env->btf;
	const char *start, *end;

	hdr = &btf->hdr;
	start = btf->nohdr_data + hdr->str_off;
	end = start + hdr->str_len;

	if (end != btf->data + btf->data_size) {
		btf_verifier_log(env, "String section is not at the end");
		return -EINVAL;
	}

	btf->strings = start;

	if (btf->base_btf && !hdr->str_len)
		return 0;
	if (!hdr->str_len || hdr->str_len - 1 > BTF_MAX_NAME_OFFSET || end[-1]) {
		btf_verifier_log(env, "Invalid string section");
		return -EINVAL;
	}
	if (!btf->base_btf && start[0]) {
		btf_verifier_log(env, "Invalid string section");
		return -EINVAL;
	}

	return 0;
}

static const size_t btf_sec_info_offset[] = {
	offsetof(struct btf_header, type_off),
	offsetof(struct btf_header, str_off),
};

static int btf_sec_info_cmp(const void *a, const void *b)
{
	const struct btf_sec_info *x = a;
	const struct btf_sec_info *y = b;

	return (int)(x->off - y->off) ? : (int)(x->len - y->len);
}

static int btf_check_sec_info(struct btf_verifier_env *env,
			      u32 btf_data_size)
{
	struct btf_sec_info secs[ARRAY_SIZE(btf_sec_info_offset)];
	u32 total, expected_total, i;
	const struct btf_header *hdr;
	const struct btf *btf;

	btf = env->btf;
	hdr = &btf->hdr;

	/* Populate the secs from hdr */
	for (i = 0; i < ARRAY_SIZE(btf_sec_info_offset); i++)
		secs[i] = *(struct btf_sec_info *)((void *)hdr +
						   btf_sec_info_offset[i]);

	sort(secs, ARRAY_SIZE(btf_sec_info_offset),
	     sizeof(struct btf_sec_info), btf_sec_info_cmp, NULL);

	/* Check for gaps and overlap among sections */
	total = 0;
	expected_total = btf_data_size - hdr->hdr_len;
	for (i = 0; i < ARRAY_SIZE(btf_sec_info_offset); i++) {
		if (expected_total < secs[i].off) {
			btf_verifier_log(env, "Invalid section offset");
			return -EINVAL;
		}
		if (total < secs[i].off) {
			/* gap */
			btf_verifier_log(env, "Unsupported section found");
			return -EINVAL;
		}
		if (total > secs[i].off) {
			btf_verifier_log(env, "Section overlap found");
			return -EINVAL;
		}
		if (expected_total - total < secs[i].len) {
			btf_verifier_log(env,
					 "Total section length too long");
			return -EINVAL;
		}
		total += secs[i].len;
	}

	/* There is data other than hdr and known sections */
	if (expected_total != total) {
		btf_verifier_log(env, "Unsupported section found");
		return -EINVAL;
	}

	return 0;
}

static int btf_parse_hdr(struct btf_verifier_env *env)
{
	u32 hdr_len, hdr_copy, btf_data_size;
	const struct btf_header *hdr;
	struct btf *btf;

	btf = env->btf;
	btf_data_size = btf->data_size;

	if (btf_data_size < offsetofend(struct btf_header, hdr_len)) {
		btf_verifier_log(env, "hdr_len not found");
		return -EINVAL;
	}

	hdr = btf->data;
	hdr_len = hdr->hdr_len;
	if (btf_data_size < hdr_len) {
		btf_verifier_log(env, "btf_header not found");
		return -EINVAL;
	}

	/* Ensure the unsupported header fields are zero */
	if (hdr_len > sizeof(btf->hdr)) {
		u8 *expected_zero = btf->data + sizeof(btf->hdr);
		u8 *end = btf->data + hdr_len;

		for (; expected_zero < end; expected_zero++) {
			if (*expected_zero) {
				btf_verifier_log(env, "Unsupported btf_header");
				return -E2BIG;
			}
		}
	}

	hdr_copy = min_t(u32, hdr_len, sizeof(btf->hdr));
	memcpy(&btf->hdr, btf->data, hdr_copy);

	hdr = &btf->hdr;

	btf_verifier_log_hdr(env, btf_data_size);

	if (hdr->magic != BTF_MAGIC) {
		btf_verifier_log(env, "Invalid magic");
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		btf_verifier_log(env, "Unsupported version");
		return -ENOTSUPP;
	}

	if (hdr->flags) {
		btf_verifier_log(env, "Unsupported flags");
		return -ENOTSUPP;
	}

	if (!btf->base_btf && btf_data_size == hdr->hdr_len) {
		btf_verifier_log(env, "No data");
		return -EINVAL;
	}

	return btf_check_sec_info(env, btf_data_size);
}

static int btf_check_type_tags(struct btf_verifier_env *env,
			       struct btf *btf, int start_id)
{
	int i, n, good_id = start_id - 1;
	bool in_tags;

	n = btf_nr_types(btf);
	for (i = start_id; i < n; i++) {
		const struct btf_type *t;
		int chain_limit = 32;
		u32 cur_id = i;

		t = btf_type_by_id(btf, i);
		if (!t)
			return -EINVAL;
		if (!btf_type_is_modifier(t))
			continue;

		cond_resched();

		in_tags = btf_type_is_type_tag(t);
		while (btf_type_is_modifier(t)) {
			if (!chain_limit--) {
				btf_verifier_log(env, "Max chain length or cycle detected");
				return -ELOOP;
			}
			if (btf_type_is_type_tag(t)) {
				if (!in_tags) {
					btf_verifier_log(env, "Type tags don't precede modifiers");
					return -EINVAL;
				}
			} else if (in_tags) {
				in_tags = false;
			}
			if (cur_id <= good_id)
				break;
			/* Move to next type */
			cur_id = t->type;
			t = btf_type_by_id(btf, cur_id);
			if (!t)
				return -EINVAL;
		}
		good_id = i;
	}
	return 0;
}

static struct btf *btf_parse(bpfptr_t btf_data, u32 btf_data_size,
			     u32 log_level, char __user *log_ubuf, u32 log_size)
{
	struct btf_verifier_env *env = NULL;
	struct bpf_verifier_log *log;
	struct btf *btf = NULL;
	u8 *data;
	int err;

	if (btf_data_size > BTF_MAX_SIZE)
		return ERR_PTR(-E2BIG);

	env = kzalloc(sizeof(*env), GFP_KERNEL | __GFP_NOWARN);
	if (!env)
		return ERR_PTR(-ENOMEM);

	log = &env->log;
	if (log_level || log_ubuf || log_size) {
		/* user requested verbose verifier output
		 * and supplied buffer to store the verification trace
		 */
		log->level = log_level;
		log->ubuf = log_ubuf;
		log->len_total = log_size;

		/* log attributes have to be sane */
		if (!bpf_verifier_log_attr_valid(log)) {
			err = -EINVAL;
			goto errout;
		}
	}

	btf = kzalloc(sizeof(*btf), GFP_KERNEL | __GFP_NOWARN);
	if (!btf) {
		err = -ENOMEM;
		goto errout;
	}
	env->btf = btf;

	data = kvmalloc(btf_data_size, GFP_KERNEL | __GFP_NOWARN);
	if (!data) {
		err = -ENOMEM;
		goto errout;
	}

	btf->data = data;
	btf->data_size = btf_data_size;

	if (copy_from_bpfptr(data, btf_data, btf_data_size)) {
		err = -EFAULT;
		goto errout;
	}

	err = btf_parse_hdr(env);
	if (err)
		goto errout;

	btf->nohdr_data = btf->data + btf->hdr.hdr_len;

	err = btf_parse_str_sec(env);
	if (err)
		goto errout;

	err = btf_parse_type_sec(env);
	if (err)
		goto errout;

	err = btf_check_type_tags(env, btf, 1);
	if (err)
		goto errout;

	if (log->level && bpf_verifier_log_full(log)) {
		err = -ENOSPC;
		goto errout;
	}

	btf_verifier_env_free(env);
	refcount_set(&btf->refcnt, 1);
	return btf;

errout:
	btf_verifier_env_free(env);
	if (btf)
		btf_free(btf);
	return ERR_PTR(err);
}

extern char __weak __start_BTF[];
extern char __weak __stop_BTF[];
extern struct btf *btf_vmlinux;

#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name)
static union {
	struct bpf_ctx_convert {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	prog_ctx_type _id##_prog; \
	kern_ctx_type _id##_kern;
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
	} *__t;
	/* 't' is written once under lock. Read many times. */
	const struct btf_type *t;
} bpf_ctx_convert;
enum {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	__ctx_convert##_id,
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
	__ctx_convert_unused, /* to avoid empty enum in extreme .config */
};
static u8 bpf_ctx_convert_map[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	[_id] = __ctx_convert##_id,
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
	0, /* avoid empty array */
};
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE

static const struct btf_member *
btf_get_prog_ctx_type(struct bpf_verifier_log *log, const struct btf *btf,
		      const struct btf_type *t, enum bpf_prog_type prog_type,
		      int arg)
{
	const struct btf_type *conv_struct;
	const struct btf_type *ctx_struct;
	const struct btf_member *ctx_type;
	const char *tname, *ctx_tname;

	conv_struct = bpf_ctx_convert.t;
	if (!conv_struct) {
		bpf_log(log, "btf_vmlinux is malformed\n");
		return NULL;
	}
	t = btf_type_by_id(btf, t->type);
	while (btf_type_is_modifier(t))
		t = btf_type_by_id(btf, t->type);
	if (!btf_type_is_struct(t)) {
		/* Only pointer to struct is supported for now.
		 * That means that BPF_PROG_TYPE_TRACEPOINT with BTF
		 * is not supported yet.
		 * BPF_PROG_TYPE_RAW_TRACEPOINT is fine.
		 */
		return NULL;
	}
	tname = btf_name_by_offset(btf, t->name_off);
	if (!tname) {
		bpf_log(log, "arg#%d struct doesn't have a name\n", arg);
		return NULL;
	}
	/* prog_type is valid bpf program type. No need for bounds check. */
	ctx_type = btf_type_member(conv_struct) + bpf_ctx_convert_map[prog_type] * 2;
	/* ctx_struct is a pointer to prog_ctx_type in vmlinux.
	 * Like 'struct __sk_buff'
	 */
	ctx_struct = btf_type_by_id(btf_vmlinux, ctx_type->type);
	if (!ctx_struct)
		/* should not happen */
		return NULL;
again:
	ctx_tname = btf_name_by_offset(btf_vmlinux, ctx_struct->name_off);
	if (!ctx_tname) {
		/* should not happen */
		bpf_log(log, "Please fix kernel include/linux/bpf_types.h\n");
		return NULL;
	}
	/* only compare that prog's ctx type name is the same as
	 * kernel expects. No need to compare field by field.
	 * It's ok for bpf prog to do:
	 * struct __sk_buff {};
	 * int socket_filter_bpf_prog(struct __sk_buff *skb)
	 * { // no fields of skb are ever used }
	 */
	if (strcmp(ctx_tname, tname)) {
		/* bpf_user_pt_regs_t is a typedef, so resolve it to
		 * underlying struct and check name again
		 */
		if (!btf_type_is_modifier(ctx_struct))
			return NULL;
		while (btf_type_is_modifier(ctx_struct))
			ctx_struct = btf_type_by_id(btf_vmlinux, ctx_struct->type);
		goto again;
	}
	return ctx_type;
}

static int btf_translate_to_vmlinux(struct bpf_verifier_log *log,
				     struct btf *btf,
				     const struct btf_type *t,
				     enum bpf_prog_type prog_type,
				     int arg)
{
	const struct btf_member *prog_ctx_type, *kern_ctx_type;

	prog_ctx_type = btf_get_prog_ctx_type(log, btf, t, prog_type, arg);
	if (!prog_ctx_type)
		return -ENOENT;
	kern_ctx_type = prog_ctx_type + 1;
	return kern_ctx_type->type;
}

BTF_ID_LIST(bpf_ctx_convert_btf_id)
BTF_ID(struct, bpf_ctx_convert)

struct btf *btf_parse_vmlinux(void)
{
	struct btf_verifier_env *env = NULL;
	struct bpf_verifier_log *log;
	struct btf *btf = NULL;
	int err;

	env = kzalloc(sizeof(*env), GFP_KERNEL | __GFP_NOWARN);
	if (!env)
		return ERR_PTR(-ENOMEM);

	log = &env->log;
	log->level = BPF_LOG_KERNEL;

	btf = kzalloc(sizeof(*btf), GFP_KERNEL | __GFP_NOWARN);
	if (!btf) {
		err = -ENOMEM;
		goto errout;
	}
	env->btf = btf;

	btf->data = __start_BTF;
	btf->data_size = __stop_BTF - __start_BTF;
	btf->kernel_btf = true;
	snprintf(btf->name, sizeof(btf->name), "vmlinux");

	err = btf_parse_hdr(env);
	if (err)
		goto errout;

	btf->nohdr_data = btf->data + btf->hdr.hdr_len;

	err = btf_parse_str_sec(env);
	if (err)
		goto errout;

	err = btf_check_all_metas(env);
	if (err)
		goto errout;

	err = btf_check_type_tags(env, btf, 1);
	if (err)
		goto errout;

	/* btf_parse_vmlinux() runs under bpf_verifier_lock */
	bpf_ctx_convert.t = btf_type_by_id(btf, bpf_ctx_convert_btf_id[0]);

	bpf_struct_ops_init(btf, log);

	refcount_set(&btf->refcnt, 1);

	err = btf_alloc_id(btf);
	if (err)
		goto errout;

	btf_verifier_env_free(env);
	return btf;

errout:
	btf_verifier_env_free(env);
	if (btf) {
		kvfree(btf->types);
		kfree(btf);
	}
	return ERR_PTR(err);
}

#ifdef CONFIG_DEBUG_INFO_BTF_MODULES

static struct btf *btf_parse_module(const char *module_name, const void *data, unsigned int data_size)
{
	struct btf_verifier_env *env = NULL;
	struct bpf_verifier_log *log;
	struct btf *btf = NULL, *base_btf;
	int err;

	base_btf = bpf_get_btf_vmlinux();
	if (IS_ERR(base_btf))
		return base_btf;
	if (!base_btf)
		return ERR_PTR(-EINVAL);

	env = kzalloc(sizeof(*env), GFP_KERNEL | __GFP_NOWARN);
	if (!env)
		return ERR_PTR(-ENOMEM);

	log = &env->log;
	log->level = BPF_LOG_KERNEL;

	btf = kzalloc(sizeof(*btf), GFP_KERNEL | __GFP_NOWARN);
	if (!btf) {
		err = -ENOMEM;
		goto errout;
	}
	env->btf = btf;

	btf->base_btf = base_btf;
	btf->start_id = base_btf->nr_types;
	btf->start_str_off = base_btf->hdr.str_len;
	btf->kernel_btf = true;
	snprintf(btf->name, sizeof(btf->name), "%s", module_name);

	btf->data = kvmalloc(data_size, GFP_KERNEL | __GFP_NOWARN);
	if (!btf->data) {
		err = -ENOMEM;
		goto errout;
	}
	memcpy(btf->data, data, data_size);
	btf->data_size = data_size;

	err = btf_parse_hdr(env);
	if (err)
		goto errout;

	btf->nohdr_data = btf->data + btf->hdr.hdr_len;

	err = btf_parse_str_sec(env);
	if (err)
		goto errout;

	err = btf_check_all_metas(env);
	if (err)
		goto errout;

	err = btf_check_type_tags(env, btf, btf_nr_types(base_btf));
	if (err)
		goto errout;

	btf_verifier_env_free(env);
	refcount_set(&btf->refcnt, 1);
	return btf;

errout:
	btf_verifier_env_free(env);
	if (btf) {
		kvfree(btf->data);
		kvfree(btf->types);
		kfree(btf);
	}
	return ERR_PTR(err);
}

#endif /* CONFIG_DEBUG_INFO_BTF_MODULES */

struct btf *bpf_prog_get_target_btf(const struct bpf_prog *prog)
{
	struct bpf_prog *tgt_prog = prog->aux->dst_prog;

	if (tgt_prog)
		return tgt_prog->aux->btf;
	else
		return prog->aux->attach_btf;
}

static bool is_int_ptr(struct btf *btf, const struct btf_type *t)
{
	/* skip modifiers */
	t = btf_type_skip_modifiers(btf, t->type, NULL);

	return btf_type_is_int(t);
}

static u32 get_ctx_arg_idx(struct btf *btf, const struct btf_type *func_proto,
			   int off)
{
	const struct btf_param *args;
	const struct btf_type *t;
	u32 offset = 0, nr_args;
	int i;

	if (!func_proto)
		return off / 8;

	nr_args = btf_type_vlen(func_proto);
	args = (const struct btf_param *)(func_proto + 1);
	for (i = 0; i < nr_args; i++) {
		t = btf_type_skip_modifiers(btf, args[i].type, NULL);
		offset += btf_type_is_ptr(t) ? 8 : roundup(t->size, 8);
		if (off < offset)
			return i;
	}

	t = btf_type_skip_modifiers(btf, func_proto->type, NULL);
	offset += btf_type_is_ptr(t) ? 8 : roundup(t->size, 8);
	if (off < offset)
		return nr_args;

	return nr_args + 1;
}

bool btf_ctx_access(int off, int size, enum bpf_access_type type,
		    const struct bpf_prog *prog,
		    struct bpf_insn_access_aux *info)
{
	const struct btf_type *t = prog->aux->attach_func_proto;
	struct bpf_prog *tgt_prog = prog->aux->dst_prog;
	struct btf *btf = bpf_prog_get_target_btf(prog);
	const char *tname = prog->aux->attach_func_name;
	struct bpf_verifier_log *log = info->log;
	const struct btf_param *args;
	const char *tag_value;
	u32 nr_args, arg;
	int i, ret;

	if (off % 8) {
		bpf_log(log, "func '%s' offset %d is not multiple of 8\n",
			tname, off);
		return false;
	}
	arg = get_ctx_arg_idx(btf, t, off);
	args = (const struct btf_param *)(t + 1);
	/* if (t == NULL) Fall back to default BPF prog with
	 * MAX_BPF_FUNC_REG_ARGS u64 arguments.
	 */
	nr_args = t ? btf_type_vlen(t) : MAX_BPF_FUNC_REG_ARGS;
	if (prog->aux->attach_btf_trace) {
		/* skip first 'void *__data' argument in btf_trace_##name typedef */
		args++;
		nr_args--;
	}

	if (arg > nr_args) {
		bpf_log(log, "func '%s' doesn't have %d-th argument\n",
			tname, arg + 1);
		return false;
	}

	if (arg == nr_args) {
		switch (prog->expected_attach_type) {
		case BPF_LSM_CGROUP:
		case BPF_LSM_MAC:
		case BPF_TRACE_FEXIT:
			/* When LSM programs are attached to void LSM hooks
			 * they use FEXIT trampolines and when attached to
			 * int LSM hooks, they use MODIFY_RETURN trampolines.
			 *
			 * While the LSM programs are BPF_MODIFY_RETURN-like
			 * the check:
			 *
			 *	if (ret_type != 'int')
			 *		return -EINVAL;
			 *
			 * is _not_ done here. This is still safe as LSM hooks
			 * have only void and int return types.
			 */
			if (!t)
				return true;
			t = btf_type_by_id(btf, t->type);
			break;
		case BPF_MODIFY_RETURN:
			/* For now the BPF_MODIFY_RETURN can only be attached to
			 * functions that return an int.
			 */
			if (!t)
				return false;

			t = btf_type_skip_modifiers(btf, t->type, NULL);
			if (!btf_type_is_small_int(t)) {
				bpf_log(log,
					"ret type %s not allowed for fmod_ret\n",
					btf_type_str(t));
				return false;
			}
			break;
		default:
			bpf_log(log, "func '%s' doesn't have %d-th argument\n",
				tname, arg + 1);
			return false;
		}
	} else {
		if (!t)
			/* Default prog with MAX_BPF_FUNC_REG_ARGS args */
			return true;
		t = btf_type_by_id(btf, args[arg].type);
	}

	/* skip modifiers */
	while (btf_type_is_modifier(t))
		t = btf_type_by_id(btf, t->type);
	if (btf_type_is_small_int(t) || btf_is_any_enum(t) || __btf_type_is_struct(t))
		/* accessing a scalar */
		return true;
	if (!btf_type_is_ptr(t)) {
		bpf_log(log,
			"func '%s' arg%d '%s' has type %s. Only pointer access is allowed\n",
			tname, arg,
			__btf_name_by_offset(btf, t->name_off),
			btf_type_str(t));
		return false;
	}

	/* check for PTR_TO_RDONLY_BUF_OR_NULL or PTR_TO_RDWR_BUF_OR_NULL */
	for (i = 0; i < prog->aux->ctx_arg_info_size; i++) {
		const struct bpf_ctx_arg_aux *ctx_arg_info = &prog->aux->ctx_arg_info[i];
		u32 type, flag;

		type = base_type(ctx_arg_info->reg_type);
		flag = type_flag(ctx_arg_info->reg_type);
		if (ctx_arg_info->offset == off && type == PTR_TO_BUF &&
		    (flag & PTR_MAYBE_NULL)) {
			info->reg_type = ctx_arg_info->reg_type;
			return true;
		}
	}

	if (t->type == 0)
		/* This is a pointer to void.
		 * It is the same as scalar from the verifier safety pov.
		 * No further pointer walking is allowed.
		 */
		return true;

	if (is_int_ptr(btf, t))
		return true;

	/* this is a pointer to another type */
	for (i = 0; i < prog->aux->ctx_arg_info_size; i++) {
		const struct bpf_ctx_arg_aux *ctx_arg_info = &prog->aux->ctx_arg_info[i];

		if (ctx_arg_info->offset == off) {
			if (!ctx_arg_info->btf_id) {
				bpf_log(log,"invalid btf_id for context argument offset %u\n", off);
				return false;
			}

			info->reg_type = ctx_arg_info->reg_type;
			info->btf = btf_vmlinux;
			info->btf_id = ctx_arg_info->btf_id;
			return true;
		}
	}

	info->reg_type = PTR_TO_BTF_ID;
	if (tgt_prog) {
		enum bpf_prog_type tgt_type;

		if (tgt_prog->type == BPF_PROG_TYPE_EXT)
			tgt_type = tgt_prog->aux->saved_dst_prog_type;
		else
			tgt_type = tgt_prog->type;

		ret = btf_translate_to_vmlinux(log, btf, t, tgt_type, arg);
		if (ret > 0) {
			info->btf = btf_vmlinux;
			info->btf_id = ret;
			return true;
		} else {
			return false;
		}
	}

	info->btf = btf;
	info->btf_id = t->type;
	t = btf_type_by_id(btf, t->type);

	if (btf_type_is_type_tag(t)) {
		tag_value = __btf_name_by_offset(btf, t->name_off);
		if (strcmp(tag_value, "user") == 0)
			info->reg_type |= MEM_USER;
		if (strcmp(tag_value, "percpu") == 0)
			info->reg_type |= MEM_PERCPU;
	}

	/* skip modifiers */
	while (btf_type_is_modifier(t)) {
		info->btf_id = t->type;
		t = btf_type_by_id(btf, t->type);
	}
	if (!btf_type_is_struct(t)) {
		bpf_log(log,
			"func '%s' arg%d type %s is not a struct\n",
			tname, arg, btf_type_str(t));
		return false;
	}
	bpf_log(log, "func '%s' arg%d has btf_id %d type %s '%s'\n",
		tname, arg, info->btf_id, btf_type_str(t),
		__btf_name_by_offset(btf, t->name_off));
	return true;
}

enum bpf_struct_walk_result {
	/* < 0 error */
	WALK_SCALAR = 0,
	WALK_PTR,
	WALK_STRUCT,
};

static int btf_struct_walk(struct bpf_verifier_log *log, const struct btf *btf,
			   const struct btf_type *t, int off, int size,
			   u32 *next_btf_id, enum bpf_type_flag *flag)
{
	u32 i, moff, mtrue_end, msize = 0, total_nelems = 0;
	const struct btf_type *mtype, *elem_type = NULL;
	const struct btf_member *member;
	const char *tname, *mname, *tag_value;
	u32 vlen, elem_id, mid;

again:
	tname = __btf_name_by_offset(btf, t->name_off);
	if (!btf_type_is_struct(t)) {
		bpf_log(log, "Type '%s' is not a struct\n", tname);
		return -EINVAL;
	}

	vlen = btf_type_vlen(t);
	if (off + size > t->size) {
		/* If the last element is a variable size array, we may
		 * need to relax the rule.
		 */
		struct btf_array *array_elem;

		if (vlen == 0)
			goto error;

		member = btf_type_member(t) + vlen - 1;
		mtype = btf_type_skip_modifiers(btf, member->type,
						NULL);
		if (!btf_type_is_array(mtype))
			goto error;

		array_elem = (struct btf_array *)(mtype + 1);
		if (array_elem->nelems != 0)
			goto error;

		moff = __btf_member_bit_offset(t, member) / 8;
		if (off < moff)
			goto error;

		/* Only allow structure for now, can be relaxed for
		 * other types later.
		 */
		t = btf_type_skip_modifiers(btf, array_elem->type,
					    NULL);
		if (!btf_type_is_struct(t))
			goto error;

		off = (off - moff) % t->size;
		goto again;

error:
		bpf_log(log, "access beyond struct %s at off %u size %u\n",
			tname, off, size);
		return -EACCES;
	}

	for_each_member(i, t, member) {
		/* offset of the field in bytes */
		moff = __btf_member_bit_offset(t, member) / 8;
		if (off + size <= moff)
			/* won't find anything, field is already too far */
			break;

		if (__btf_member_bitfield_size(t, member)) {
			u32 end_bit = __btf_member_bit_offset(t, member) +
				__btf_member_bitfield_size(t, member);

			/* off <= moff instead of off == moff because clang
			 * does not generate a BTF member for anonymous
			 * bitfield like the ":16" here:
			 * struct {
			 *	int :16;
			 *	int x:8;
			 * };
			 */
			if (off <= moff &&
			    BITS_ROUNDUP_BYTES(end_bit) <= off + size)
				return WALK_SCALAR;

			/* off may be accessing a following member
			 *
			 * or
			 *
			 * Doing partial access at either end of this
			 * bitfield.  Continue on this case also to
			 * treat it as not accessing this bitfield
			 * and eventually error out as field not
			 * found to keep it simple.
			 * It could be relaxed if there was a legit
			 * partial access case later.
			 */
			continue;
		}

		/* In case of "off" is pointing to holes of a struct */
		if (off < moff)
			break;

		/* type of the field */
		mid = member->type;
		mtype = btf_type_by_id(btf, member->type);
		mname = __btf_name_by_offset(btf, member->name_off);

		mtype = __btf_resolve_size(btf, mtype, &msize,
					   &elem_type, &elem_id, &total_nelems,
					   &mid);
		if (IS_ERR(mtype)) {
			bpf_log(log, "field %s doesn't have size\n", mname);
			return -EFAULT;
		}

		mtrue_end = moff + msize;
		if (off >= mtrue_end)
			/* no overlap with member, keep iterating */
			continue;

		if (btf_type_is_array(mtype)) {
			u32 elem_idx;

			/* __btf_resolve_size() above helps to
			 * linearize a multi-dimensional array.
			 *
			 * The logic here is treating an array
			 * in a struct as the following way:
			 *
			 * struct outer {
			 *	struct inner array[2][2];
			 * };
			 *
			 * looks like:
			 *
			 * struct outer {
			 *	struct inner array_elem0;
			 *	struct inner array_elem1;
			 *	struct inner array_elem2;
			 *	struct inner array_elem3;
			 * };
			 *
			 * When accessing outer->array[1][0], it moves
			 * moff to "array_elem2", set mtype to
			 * "struct inner", and msize also becomes
			 * sizeof(struct inner).  Then most of the
			 * remaining logic will fall through without
			 * caring the current member is an array or
			 * not.
			 *
			 * Unlike mtype/msize/moff, mtrue_end does not
			 * change.  The naming difference ("_true") tells
			 * that it is not always corresponding to
			 * the current mtype/msize/moff.
			 * It is the true end of the current
			 * member (i.e. array in this case).  That
			 * will allow an int array to be accessed like
			 * a scratch space,
			 * i.e. allow access beyond the size of
			 *      the array's element as long as it is
			 *      within the mtrue_end boundary.
			 */

			/* skip empty array */
			if (moff == mtrue_end)
				continue;

			msize /= total_nelems;
			elem_idx = (off - moff) / msize;
			moff += elem_idx * msize;
			mtype = elem_type;
			mid = elem_id;
		}

		/* the 'off' we're looking for is either equal to start
		 * of this field or inside of this struct
		 */
		if (btf_type_is_struct(mtype)) {
			/* our field must be inside that union or struct */
			t = mtype;

			/* return if the offset matches the member offset */
			if (off == moff) {
				*next_btf_id = mid;
				return WALK_STRUCT;
			}

			/* adjust offset we're looking for */
			off -= moff;
			goto again;
		}

		if (btf_type_is_ptr(mtype)) {
			const struct btf_type *stype, *t;
			enum bpf_type_flag tmp_flag = 0;
			u32 id;

			if (msize != size || off != moff) {
				bpf_log(log,
					"cannot access ptr member %s with moff %u in struct %s with off %u size %u\n",
					mname, moff, tname, off, size);
				return -EACCES;
			}

			/* check type tag */
			t = btf_type_by_id(btf, mtype->type);
			if (btf_type_is_type_tag(t)) {
				tag_value = __btf_name_by_offset(btf, t->name_off);
				/* check __user tag */
				if (strcmp(tag_value, "user") == 0)
					tmp_flag = MEM_USER;
				/* check __percpu tag */
				if (strcmp(tag_value, "percpu") == 0)
					tmp_flag = MEM_PERCPU;
			}

			stype = btf_type_skip_modifiers(btf, mtype->type, &id);
			if (btf_type_is_struct(stype)) {
				*next_btf_id = id;
				*flag = tmp_flag;
				return WALK_PTR;
			}
		}

		/* Allow more flexible access within an int as long as
		 * it is within mtrue_end.
		 * Since mtrue_end could be the end of an array,
		 * that also allows using an array of int as a scratch
		 * space. e.g. skb->cb[].
		 */
		if (off + size > mtrue_end && !(*flag & PTR_UNTRUSTED)) {
			bpf_log(log,
				"access beyond the end of member %s (mend:%u) in struct %s with off %u size %u\n",
				mname, mtrue_end, tname, off, size);
			return -EACCES;
		}

		return WALK_SCALAR;
	}
	bpf_log(log, "struct %s doesn't have field at offset %d\n", tname, off);
	return -EINVAL;
}

int btf_struct_access(struct bpf_verifier_log *log, const struct btf *btf,
		      const struct btf_type *t, int off, int size,
		      enum bpf_access_type atype __maybe_unused,
		      u32 *next_btf_id, enum bpf_type_flag *flag)
{
	enum bpf_type_flag tmp_flag = 0;
	int err;
	u32 id;

	do {
		err = btf_struct_walk(log, btf, t, off, size, &id, &tmp_flag);

		switch (err) {
		case WALK_PTR:
			/* If we found the pointer or scalar on t+off,
			 * we're done.
			 */
			*next_btf_id = id;
			*flag = tmp_flag;
			return PTR_TO_BTF_ID;
		case WALK_SCALAR:
			return SCALAR_VALUE;
		case WALK_STRUCT:
			/* We found nested struct, so continue the search
			 * by diving in it. At this point the offset is
			 * aligned with the new type, so set it to 0.
			 */
			t = btf_type_by_id(btf, id);
			off = 0;
			break;
		default:
			/* It's either error or unknown return value..
			 * scream and leave.
			 */
			if (WARN_ONCE(err > 0, "unknown btf_struct_walk return value"))
				return -EINVAL;
			return err;
		}
	} while (t);

	return -EINVAL;
}

/* Check that two BTF types, each specified as an BTF object + id, are exactly
 * the same. Trivial ID check is not enough due to module BTFs, because we can
 * end up with two different module BTFs, but IDs point to the common type in
 * vmlinux BTF.
 */
static bool btf_types_are_same(const struct btf *btf1, u32 id1,
			       const struct btf *btf2, u32 id2)
{
	if (id1 != id2)
		return false;
	if (btf1 == btf2)
		return true;
	return btf_type_by_id(btf1, id1) == btf_type_by_id(btf2, id2);
}

bool btf_struct_ids_match(struct bpf_verifier_log *log,
			  const struct btf *btf, u32 id, int off,
			  const struct btf *need_btf, u32 need_type_id,
			  bool strict)
{
	const struct btf_type *type;
	enum bpf_type_flag flag;
	int err;

	/* Are we already done? */
	if (off == 0 && btf_types_are_same(btf, id, need_btf, need_type_id))
		return true;
	/* In case of strict type match, we do not walk struct, the top level
	 * type match must succeed. When strict is true, off should have already
	 * been 0.
	 */
	if (strict)
		return false;
again:
	type = btf_type_by_id(btf, id);
	if (!type)
		return false;
	err = btf_struct_walk(log, btf, type, off, 1, &id, &flag);
	if (err != WALK_STRUCT)
		return false;

	/* We found nested struct object. If it matches
	 * the requested ID, we're done. Otherwise let's
	 * continue the search with offset 0 in the new
	 * type.
	 */
	if (!btf_types_are_same(btf, id, need_btf, need_type_id)) {
		off = 0;
		goto again;
	}

	return true;
}

static int __get_type_size(struct btf *btf, u32 btf_id,
			   const struct btf_type **ret_type)
{
	const struct btf_type *t;

	*ret_type = btf_type_by_id(btf, 0);
	if (!btf_id)
		/* void */
		return 0;
	t = btf_type_by_id(btf, btf_id);
	while (t && btf_type_is_modifier(t))
		t = btf_type_by_id(btf, t->type);
	if (!t)
		return -EINVAL;
	*ret_type = t;
	if (btf_type_is_ptr(t))
		/* kernel size of pointer. Not BPF's size of pointer*/
		return sizeof(void *);
	if (btf_type_is_int(t) || btf_is_any_enum(t) || __btf_type_is_struct(t))
		return t->size;
	return -EINVAL;
}

int btf_distill_func_proto(struct bpf_verifier_log *log,
			   struct btf *btf,
			   const struct btf_type *func,
			   const char *tname,
			   struct btf_func_model *m)
{
	const struct btf_param *args;
	const struct btf_type *t;
	u32 i, nargs;
	int ret;

	if (!func) {
		/* BTF function prototype doesn't match the verifier types.
		 * Fall back to MAX_BPF_FUNC_REG_ARGS u64 args.
		 */
		for (i = 0; i < MAX_BPF_FUNC_REG_ARGS; i++) {
			m->arg_size[i] = 8;
			m->arg_flags[i] = 0;
		}
		m->ret_size = 8;
		m->nr_args = MAX_BPF_FUNC_REG_ARGS;
		return 0;
	}
	args = (const struct btf_param *)(func + 1);
	nargs = btf_type_vlen(func);
	if (nargs > MAX_BPF_FUNC_ARGS) {
		bpf_log(log,
			"The function %s has %d arguments. Too many.\n",
			tname, nargs);
		return -EINVAL;
	}
	ret = __get_type_size(btf, func->type, &t);
	if (ret < 0 || __btf_type_is_struct(t)) {
		bpf_log(log,
			"The function %s return type %s is unsupported.\n",
			tname, btf_type_str(t));
		return -EINVAL;
	}
	m->ret_size = ret;

	for (i = 0; i < nargs; i++) {
		if (i == nargs - 1 && args[i].type == 0) {
			bpf_log(log,
				"The function %s with variable args is unsupported.\n",
				tname);
			return -EINVAL;
		}
		ret = __get_type_size(btf, args[i].type, &t);

		/* No support of struct argument size greater than 16 bytes */
		if (ret < 0 || ret > 16) {
			bpf_log(log,
				"The function %s arg%d type %s is unsupported.\n",
				tname, i, btf_type_str(t));
			return -EINVAL;
		}
		if (ret == 0) {
			bpf_log(log,
				"The function %s has malformed void argument.\n",
				tname);
			return -EINVAL;
		}
		m->arg_size[i] = ret;
		m->arg_flags[i] = __btf_type_is_struct(t) ? BTF_FMODEL_STRUCT_ARG : 0;
	}
	m->nr_args = nargs;
	return 0;
}

/* Compare BTFs of two functions assuming only scalars and pointers to context.
 * t1 points to BTF_KIND_FUNC in btf1
 * t2 points to BTF_KIND_FUNC in btf2
 * Returns:
 * EINVAL - function prototype mismatch
 * EFAULT - verifier bug
 * 0 - 99% match. The last 1% is validated by the verifier.
 */
static int btf_check_func_type_match(struct bpf_verifier_log *log,
				     struct btf *btf1, const struct btf_type *t1,
				     struct btf *btf2, const struct btf_type *t2)
{
	const struct btf_param *args1, *args2;
	const char *fn1, *fn2, *s1, *s2;
	u32 nargs1, nargs2, i;

	fn1 = btf_name_by_offset(btf1, t1->name_off);
	fn2 = btf_name_by_offset(btf2, t2->name_off);

	if (btf_func_linkage(t1) != BTF_FUNC_GLOBAL) {
		bpf_log(log, "%s() is not a global function\n", fn1);
		return -EINVAL;
	}
	if (btf_func_linkage(t2) != BTF_FUNC_GLOBAL) {
		bpf_log(log, "%s() is not a global function\n", fn2);
		return -EINVAL;
	}

	t1 = btf_type_by_id(btf1, t1->type);
	if (!t1 || !btf_type_is_func_proto(t1))
		return -EFAULT;
	t2 = btf_type_by_id(btf2, t2->type);
	if (!t2 || !btf_type_is_func_proto(t2))
		return -EFAULT;

	args1 = (const struct btf_param *)(t1 + 1);
	nargs1 = btf_type_vlen(t1);
	args2 = (const struct btf_param *)(t2 + 1);
	nargs2 = btf_type_vlen(t2);

	if (nargs1 != nargs2) {
		bpf_log(log, "%s() has %d args while %s() has %d args\n",
			fn1, nargs1, fn2, nargs2);
		return -EINVAL;
	}

	t1 = btf_type_skip_modifiers(btf1, t1->type, NULL);
	t2 = btf_type_skip_modifiers(btf2, t2->type, NULL);
	if (t1->info != t2->info) {
		bpf_log(log,
			"Return type %s of %s() doesn't match type %s of %s()\n",
			btf_type_str(t1), fn1,
			btf_type_str(t2), fn2);
		return -EINVAL;
	}

	for (i = 0; i < nargs1; i++) {
		t1 = btf_type_skip_modifiers(btf1, args1[i].type, NULL);
		t2 = btf_type_skip_modifiers(btf2, args2[i].type, NULL);

		if (t1->info != t2->info) {
			bpf_log(log, "arg%d in %s() is %s while %s() has %s\n",
				i, fn1, btf_type_str(t1),
				fn2, btf_type_str(t2));
			return -EINVAL;
		}
		if (btf_type_has_size(t1) && t1->size != t2->size) {
			bpf_log(log,
				"arg%d in %s() has size %d while %s() has %d\n",
				i, fn1, t1->size,
				fn2, t2->size);
			return -EINVAL;
		}

		/* global functions are validated with scalars and pointers
		 * to context only. And only global functions can be replaced.
		 * Hence type check only those types.
		 */
		if (btf_type_is_int(t1) || btf_is_any_enum(t1))
			continue;
		if (!btf_type_is_ptr(t1)) {
			bpf_log(log,
				"arg%d in %s() has unrecognized type\n",
				i, fn1);
			return -EINVAL;
		}
		t1 = btf_type_skip_modifiers(btf1, t1->type, NULL);
		t2 = btf_type_skip_modifiers(btf2, t2->type, NULL);
		if (!btf_type_is_struct(t1)) {
			bpf_log(log,
				"arg%d in %s() is not a pointer to context\n",
				i, fn1);
			return -EINVAL;
		}
		if (!btf_type_is_struct(t2)) {
			bpf_log(log,
				"arg%d in %s() is not a pointer to context\n",
				i, fn2);
			return -EINVAL;
		}
		/* This is an optional check to make program writing easier.
		 * Compare names of structs and report an error to the user.
		 * btf_prepare_func_args() already checked that t2 struct
		 * is a context type. btf_prepare_func_args() will check
		 * later that t1 struct is a context type as well.
		 */
		s1 = btf_name_by_offset(btf1, t1->name_off);
		s2 = btf_name_by_offset(btf2, t2->name_off);
		if (strcmp(s1, s2)) {
			bpf_log(log,
				"arg%d %s(struct %s *) doesn't match %s(struct %s *)\n",
				i, fn1, s1, fn2, s2);
			return -EINVAL;
		}
	}
	return 0;
}

/* Compare BTFs of given program with BTF of target program */
int btf_check_type_match(struct bpf_verifier_log *log, const struct bpf_prog *prog,
			 struct btf *btf2, const struct btf_type *t2)
{
	struct btf *btf1 = prog->aux->btf;
	const struct btf_type *t1;
	u32 btf_id = 0;

	if (!prog->aux->func_info) {
		bpf_log(log, "Program extension requires BTF\n");
		return -EINVAL;
	}

	btf_id = prog->aux->func_info[0].type_id;
	if (!btf_id)
		return -EFAULT;

	t1 = btf_type_by_id(btf1, btf_id);
	if (!t1 || !btf_type_is_func(t1))
		return -EFAULT;

	return btf_check_func_type_match(log, btf1, t1, btf2, t2);
}

static u32 *reg2btf_ids[__BPF_REG_TYPE_MAX] = {
#ifdef CONFIG_NET
	[PTR_TO_SOCKET] = &btf_sock_ids[BTF_SOCK_TYPE_SOCK],
	[PTR_TO_SOCK_COMMON] = &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
	[PTR_TO_TCP_SOCK] = &btf_sock_ids[BTF_SOCK_TYPE_TCP],
#endif
};

/* Returns true if struct is composed of scalars, 4 levels of nesting allowed */
static bool __btf_type_is_scalar_struct(struct bpf_verifier_log *log,
					const struct btf *btf,
					const struct btf_type *t, int rec)
{
	const struct btf_type *member_type;
	const struct btf_member *member;
	u32 i;

	if (!btf_type_is_struct(t))
		return false;

	for_each_member(i, t, member) {
		const struct btf_array *array;

		member_type = btf_type_skip_modifiers(btf, member->type, NULL);
		if (btf_type_is_struct(member_type)) {
			if (rec >= 3) {
				bpf_log(log, "max struct nesting depth exceeded\n");
				return false;
			}
			if (!__btf_type_is_scalar_struct(log, btf, member_type, rec + 1))
				return false;
			continue;
		}
		if (btf_type_is_array(member_type)) {
			array = btf_type_array(member_type);
			if (!array->nelems)
				return false;
			member_type = btf_type_skip_modifiers(btf, array->type, NULL);
			if (!btf_type_is_scalar(member_type))
				return false;
			continue;
		}
		if (!btf_type_is_scalar(member_type))
			return false;
	}
	return true;
}

static bool is_kfunc_arg_mem_size(const struct btf *btf,
				  const struct btf_param *arg,
				  const struct bpf_reg_state *reg)
{
	int len, sfx_len = sizeof("__sz") - 1;
	const struct btf_type *t;
	const char *param_name;

	t = btf_type_skip_modifiers(btf, arg->type, NULL);
	if (!btf_type_is_scalar(t) || reg->type != SCALAR_VALUE)
		return false;

	/* In the future, this can be ported to use BTF tagging */
	param_name = btf_name_by_offset(btf, arg->name_off);
	if (str_is_empty(param_name))
		return false;
	len = strlen(param_name);
	if (len < sfx_len)
		return false;
	param_name += len - sfx_len;
	if (strncmp(param_name, "__sz", sfx_len))
		return false;

	return true;
}

static bool btf_is_kfunc_arg_mem_size(const struct btf *btf,
				      const struct btf_param *arg,
				      const struct bpf_reg_state *reg,
				      const char *name)
{
	int len, target_len = strlen(name);
	const struct btf_type *t;
	const char *param_name;

	t = btf_type_skip_modifiers(btf, arg->type, NULL);
	if (!btf_type_is_scalar(t) || reg->type != SCALAR_VALUE)
		return false;

	param_name = btf_name_by_offset(btf, arg->name_off);
	if (str_is_empty(param_name))
		return false;
	len = strlen(param_name);
	if (len != target_len)
		return false;
	if (strcmp(param_name, name))
		return false;

	return true;
}

static int btf_check_func_arg_match(struct bpf_verifier_env *env,
				    const struct btf *btf, u32 func_id,
				    struct bpf_reg_state *regs,
				    bool ptr_to_mem_ok,
				    struct bpf_kfunc_arg_meta *kfunc_meta,
				    bool processing_call)
{
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);
	bool rel = false, kptr_get = false, trusted_args = false;
	bool sleepable = false;
	struct bpf_verifier_log *log = &env->log;
	u32 i, nargs, ref_id, ref_obj_id = 0;
	bool is_kfunc = btf_is_kernel(btf);
	const char *func_name, *ref_tname;
	const struct btf_type *t, *ref_t;
	const struct btf_param *args;
	int ref_regno = 0, ret;

	t = btf_type_by_id(btf, func_id);
	if (!t || !btf_type_is_func(t)) {
		/* These checks were already done by the verifier while loading
		 * struct bpf_func_info or in add_kfunc_call().
		 */
		bpf_log(log, "BTF of func_id %u doesn't point to KIND_FUNC\n",
			func_id);
		return -EFAULT;
	}
	func_name = btf_name_by_offset(btf, t->name_off);

	t = btf_type_by_id(btf, t->type);
	if (!t || !btf_type_is_func_proto(t)) {
		bpf_log(log, "Invalid BTF of func %s\n", func_name);
		return -EFAULT;
	}
	args = (const struct btf_param *)(t + 1);
	nargs = btf_type_vlen(t);
	if (nargs > MAX_BPF_FUNC_REG_ARGS) {
		bpf_log(log, "Function %s has %d > %d args\n", func_name, nargs,
			MAX_BPF_FUNC_REG_ARGS);
		return -EINVAL;
	}

	if (is_kfunc && kfunc_meta) {
		/* Only kfunc can be release func */
		rel = kfunc_meta->flags & KF_RELEASE;
		kptr_get = kfunc_meta->flags & KF_KPTR_GET;
		trusted_args = kfunc_meta->flags & KF_TRUSTED_ARGS;
		sleepable = kfunc_meta->flags & KF_SLEEPABLE;
	}

	/* check that BTF function arguments match actual types that the
	 * verifier sees.
	 */
	for (i = 0; i < nargs; i++) {
		enum bpf_arg_type arg_type = ARG_DONTCARE;
		u32 regno = i + 1;
		struct bpf_reg_state *reg = &regs[regno];
		bool obj_ptr = false;

		t = btf_type_skip_modifiers(btf, args[i].type, NULL);
		if (btf_type_is_scalar(t)) {
			if (is_kfunc && kfunc_meta) {
				bool is_buf_size = false;

				/* check for any const scalar parameter of name "rdonly_buf_size"
				 * or "rdwr_buf_size"
				 */
				if (btf_is_kfunc_arg_mem_size(btf, &args[i], reg,
							      "rdonly_buf_size")) {
					kfunc_meta->r0_rdonly = true;
					is_buf_size = true;
				} else if (btf_is_kfunc_arg_mem_size(btf, &args[i], reg,
								     "rdwr_buf_size"))
					is_buf_size = true;

				if (is_buf_size) {
					if (kfunc_meta->r0_size) {
						bpf_log(log, "2 or more rdonly/rdwr_buf_size parameters for kfunc");
						return -EINVAL;
					}

					if (!tnum_is_const(reg->var_off)) {
						bpf_log(log, "R%d is not a const\n", regno);
						return -EINVAL;
					}

					kfunc_meta->r0_size = reg->var_off.value;
					ret = mark_chain_precision(env, regno);
					if (ret)
						return ret;
				}
			}

			if (reg->type == SCALAR_VALUE)
				continue;
			bpf_log(log, "R%d is not a scalar\n", regno);
			return -EINVAL;
		}

		if (!btf_type_is_ptr(t)) {
			bpf_log(log, "Unrecognized arg#%d type %s\n",
				i, btf_type_str(t));
			return -EINVAL;
		}

		/* These register types have special constraints wrt ref_obj_id
		 * and offset checks. The rest of trusted args don't.
		 */
		obj_ptr = reg->type == PTR_TO_CTX || reg->type == PTR_TO_BTF_ID ||
			  reg2btf_ids[base_type(reg->type)];

		/* Check if argument must be a referenced pointer, args + i has
		 * been verified to be a pointer (after skipping modifiers).
		 * PTR_TO_CTX is ok without having non-zero ref_obj_id.
		 */
		if (is_kfunc && trusted_args && (obj_ptr && reg->type != PTR_TO_CTX) && !reg->ref_obj_id) {
			bpf_log(log, "R%d must be referenced\n", regno);
			return -EINVAL;
		}

		ref_t = btf_type_skip_modifiers(btf, t->type, &ref_id);
		ref_tname = btf_name_by_offset(btf, ref_t->name_off);

		/* Trusted args have the same offset checks as release arguments */
		if ((trusted_args && obj_ptr) || (rel && reg->ref_obj_id))
			arg_type |= OBJ_RELEASE;
		ret = check_func_arg_reg_off(env, reg, regno, arg_type);
		if (ret < 0)
			return ret;

		if (is_kfunc && reg->ref_obj_id) {
			/* Ensure only one argument is referenced PTR_TO_BTF_ID */
			if (ref_obj_id) {
				bpf_log(log, "verifier internal error: more than one arg with ref_obj_id R%d %u %u\n",
					regno, reg->ref_obj_id, ref_obj_id);
				return -EFAULT;
			}
			ref_regno = regno;
			ref_obj_id = reg->ref_obj_id;
		}

		/* kptr_get is only true for kfunc */
		if (i == 0 && kptr_get) {
			struct bpf_map_value_off_desc *off_desc;

			if (reg->type != PTR_TO_MAP_VALUE) {
				bpf_log(log, "arg#0 expected pointer to map value\n");
				return -EINVAL;
			}

			/* check_func_arg_reg_off allows var_off for
			 * PTR_TO_MAP_VALUE, but we need fixed offset to find
			 * off_desc.
			 */
			if (!tnum_is_const(reg->var_off)) {
				bpf_log(log, "arg#0 must have constant offset\n");
				return -EINVAL;
			}

			off_desc = bpf_map_kptr_off_contains(reg->map_ptr, reg->off + reg->var_off.value);
			if (!off_desc || off_desc->type != BPF_KPTR_REF) {
				bpf_log(log, "arg#0 no referenced kptr at map value offset=%llu\n",
					reg->off + reg->var_off.value);
				return -EINVAL;
			}

			if (!btf_type_is_ptr(ref_t)) {
				bpf_log(log, "arg#0 BTF type must be a double pointer\n");
				return -EINVAL;
			}

			ref_t = btf_type_skip_modifiers(btf, ref_t->type, &ref_id);
			ref_tname = btf_name_by_offset(btf, ref_t->name_off);

			if (!btf_type_is_struct(ref_t)) {
				bpf_log(log, "kernel function %s args#%d pointer type %s %s is not supported\n",
					func_name, i, btf_type_str(ref_t), ref_tname);
				return -EINVAL;
			}
			if (!btf_struct_ids_match(log, btf, ref_id, 0, off_desc->kptr.btf,
						  off_desc->kptr.btf_id, true)) {
				bpf_log(log, "kernel function %s args#%d expected pointer to %s %s\n",
					func_name, i, btf_type_str(ref_t), ref_tname);
				return -EINVAL;
			}
			/* rest of the arguments can be anything, like normal kfunc */
		} else if (btf_get_prog_ctx_type(log, btf, t, prog_type, i)) {
			/* If function expects ctx type in BTF check that caller
			 * is passing PTR_TO_CTX.
			 */
			if (reg->type != PTR_TO_CTX) {
				bpf_log(log,
					"arg#%d expected pointer to ctx, but got %s\n",
					i, btf_type_str(t));
				return -EINVAL;
			}
		} else if (is_kfunc && (reg->type == PTR_TO_BTF_ID ||
			   (reg2btf_ids[base_type(reg->type)] && !type_flag(reg->type)))) {
			const struct btf_type *reg_ref_t;
			const struct btf *reg_btf;
			const char *reg_ref_tname;
			u32 reg_ref_id;

			if (!btf_type_is_struct(ref_t)) {
				bpf_log(log, "kernel function %s args#%d pointer type %s %s is not supported\n",
					func_name, i, btf_type_str(ref_t),
					ref_tname);
				return -EINVAL;
			}

			if (reg->type == PTR_TO_BTF_ID) {
				reg_btf = reg->btf;
				reg_ref_id = reg->btf_id;
			} else {
				reg_btf = btf_vmlinux;
				reg_ref_id = *reg2btf_ids[base_type(reg->type)];
			}

			reg_ref_t = btf_type_skip_modifiers(reg_btf, reg_ref_id,
							    &reg_ref_id);
			reg_ref_tname = btf_name_by_offset(reg_btf,
							   reg_ref_t->name_off);
			if (!btf_struct_ids_match(log, reg_btf, reg_ref_id,
						  reg->off, btf, ref_id,
						  trusted_args || (rel && reg->ref_obj_id))) {
				bpf_log(log, "kernel function %s args#%d expected pointer to %s %s but R%d has a pointer to %s %s\n",
					func_name, i,
					btf_type_str(ref_t), ref_tname,
					regno, btf_type_str(reg_ref_t),
					reg_ref_tname);
				return -EINVAL;
			}
		} else if (ptr_to_mem_ok && processing_call) {
			const struct btf_type *resolve_ret;
			u32 type_size;

			if (is_kfunc) {
				bool arg_mem_size = i + 1 < nargs && is_kfunc_arg_mem_size(btf, &args[i + 1], &regs[regno + 1]);
				bool arg_dynptr = btf_type_is_struct(ref_t) &&
						  !strcmp(ref_tname,
							  stringify_struct(bpf_dynptr_kern));

				/* Permit pointer to mem, but only when argument
				 * type is pointer to scalar, or struct composed
				 * (recursively) of scalars.
				 * When arg_mem_size is true, the pointer can be
				 * void *.
				 * Also permit initialized local dynamic pointers.
				 */
				if (!btf_type_is_scalar(ref_t) &&
				    !__btf_type_is_scalar_struct(log, btf, ref_t, 0) &&
				    !arg_dynptr &&
				    (arg_mem_size ? !btf_type_is_void(ref_t) : 1)) {
					bpf_log(log,
						"arg#%d pointer type %s %s must point to %sscalar, or struct with scalar\n",
						i, btf_type_str(ref_t), ref_tname, arg_mem_size ? "void, " : "");
					return -EINVAL;
				}

				if (arg_dynptr) {
					if (reg->type != PTR_TO_STACK) {
						bpf_log(log, "arg#%d pointer type %s %s not to stack\n",
							i, btf_type_str(ref_t),
							ref_tname);
						return -EINVAL;
					}

					if (!is_dynptr_reg_valid_init(env, reg)) {
						bpf_log(log,
							"arg#%d pointer type %s %s must be valid and initialized\n",
							i, btf_type_str(ref_t),
							ref_tname);
						return -EINVAL;
					}

					if (!is_dynptr_type_expected(env, reg,
							ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_LOCAL)) {
						bpf_log(log,
							"arg#%d pointer type %s %s points to unsupported dynamic pointer type\n",
							i, btf_type_str(ref_t),
							ref_tname);
						return -EINVAL;
					}

					continue;
				}

				/* Check for mem, len pair */
				if (arg_mem_size) {
					if (check_kfunc_mem_size_reg(env, &regs[regno + 1], regno + 1)) {
						bpf_log(log, "arg#%d arg#%d memory, len pair leads to invalid memory access\n",
							i, i + 1);
						return -EINVAL;
					}
					i++;
					continue;
				}
			}

			resolve_ret = btf_resolve_size(btf, ref_t, &type_size);
			if (IS_ERR(resolve_ret)) {
				bpf_log(log,
					"arg#%d reference type('%s %s') size cannot be determined: %ld\n",
					i, btf_type_str(ref_t), ref_tname,
					PTR_ERR(resolve_ret));
				return -EINVAL;
			}

			if (check_mem_reg(env, reg, regno, type_size))
				return -EINVAL;
		} else {
			bpf_log(log, "reg type unsupported for arg#%d %sfunction %s#%d\n", i,
				is_kfunc ? "kernel " : "", func_name, func_id);
			return -EINVAL;
		}
	}

	/* Either both are set, or neither */
	WARN_ON_ONCE((ref_obj_id && !ref_regno) || (!ref_obj_id && ref_regno));
	/* We already made sure ref_obj_id is set only for one argument. We do
	 * allow (!rel && ref_obj_id), so that passing such referenced
	 * PTR_TO_BTF_ID to other kfuncs works. Note that rel is only true when
	 * is_kfunc is true.
	 */
	if (rel && !ref_obj_id) {
		bpf_log(log, "release kernel function %s expects refcounted PTR_TO_BTF_ID\n",
			func_name);
		return -EINVAL;
	}

	if (sleepable && !env->prog->aux->sleepable) {
		bpf_log(log, "kernel function %s is sleepable but the program is not\n",
			func_name);
		return -EINVAL;
	}

	if (kfunc_meta && ref_obj_id)
		kfunc_meta->ref_obj_id = ref_obj_id;

	/* returns argument register number > 0 in case of reference release kfunc */
	return rel ? ref_regno : 0;
}

/* Compare BTF of a function declaration with given bpf_reg_state.
 * Returns:
 * EFAULT - there is a verifier bug. Abort verification.
 * EINVAL - there is a type mismatch or BTF is not available.
 * 0 - BTF matches with what bpf_reg_state expects.
 * Only PTR_TO_CTX and SCALAR_VALUE states are recognized.
 */
int btf_check_subprog_arg_match(struct bpf_verifier_env *env, int subprog,
				struct bpf_reg_state *regs)
{
	struct bpf_prog *prog = env->prog;
	struct btf *btf = prog->aux->btf;
	bool is_global;
	u32 btf_id;
	int err;

	if (!prog->aux->func_info)
		return -EINVAL;

	btf_id = prog->aux->func_info[subprog].type_id;
	if (!btf_id)
		return -EFAULT;

	if (prog->aux->func_info_aux[subprog].unreliable)
		return -EINVAL;

	is_global = prog->aux->func_info_aux[subprog].linkage == BTF_FUNC_GLOBAL;
	err = btf_check_func_arg_match(env, btf, btf_id, regs, is_global, NULL, false);

	/* Compiler optimizations can remove arguments from static functions
	 * or mismatched type can be passed into a global function.
	 * In such cases mark the function as unreliable from BTF point of view.
	 */
	if (err)
		prog->aux->func_info_aux[subprog].unreliable = true;
	return err;
}

/* Compare BTF of a function call with given bpf_reg_state.
 * Returns:
 * EFAULT - there is a verifier bug. Abort verification.
 * EINVAL - there is a type mismatch or BTF is not available.
 * 0 - BTF matches with what bpf_reg_state expects.
 * Only PTR_TO_CTX and SCALAR_VALUE states are recognized.
 *
 * NOTE: the code is duplicated from btf_check_subprog_arg_match()
 * because btf_check_func_arg_match() is still doing both. Once that
 * function is split in 2, we can call from here btf_check_subprog_arg_match()
 * first, and then treat the calling part in a new code path.
 */
int btf_check_subprog_call(struct bpf_verifier_env *env, int subprog,
			   struct bpf_reg_state *regs)
{
	struct bpf_prog *prog = env->prog;
	struct btf *btf = prog->aux->btf;
	bool is_global;
	u32 btf_id;
	int err;

	if (!prog->aux->func_info)
		return -EINVAL;

	btf_id = prog->aux->func_info[subprog].type_id;
	if (!btf_id)
		return -EFAULT;

	if (prog->aux->func_info_aux[subprog].unreliable)
		return -EINVAL;

	is_global = prog->aux->func_info_aux[subprog].linkage == BTF_FUNC_GLOBAL;
	err = btf_check_func_arg_match(env, btf, btf_id, regs, is_global, NULL, true);

	/* Compiler optimizations can remove arguments from static functions
	 * or mismatched type can be passed into a global function.
	 * In such cases mark the function as unreliable from BTF point of view.
	 */
	if (err)
		prog->aux->func_info_aux[subprog].unreliable = true;
	return err;
}

int btf_check_kfunc_arg_match(struct bpf_verifier_env *env,
			      const struct btf *btf, u32 func_id,
			      struct bpf_reg_state *regs,
			      struct bpf_kfunc_arg_meta *meta)
{
	return btf_check_func_arg_match(env, btf, func_id, regs, true, meta, true);
}

/* Convert BTF of a function into bpf_reg_state if possible
 * Returns:
 * EFAULT - there is a verifier bug. Abort verification.
 * EINVAL - cannot convert BTF.
 * 0 - Successfully converted BTF into bpf_reg_state
 * (either PTR_TO_CTX or SCALAR_VALUE).
 */
int btf_prepare_func_args(struct bpf_verifier_env *env, int subprog,
			  struct bpf_reg_state *regs)
{
	struct bpf_verifier_log *log = &env->log;
	struct bpf_prog *prog = env->prog;
	enum bpf_prog_type prog_type = prog->type;
	struct btf *btf = prog->aux->btf;
	const struct btf_param *args;
	const struct btf_type *t, *ref_t;
	u32 i, nargs, btf_id;
	const char *tname;

	if (!prog->aux->func_info ||
	    prog->aux->func_info_aux[subprog].linkage != BTF_FUNC_GLOBAL) {
		bpf_log(log, "Verifier bug\n");
		return -EFAULT;
	}

	btf_id = prog->aux->func_info[subprog].type_id;
	if (!btf_id) {
		bpf_log(log, "Global functions need valid BTF\n");
		return -EFAULT;
	}

	t = btf_type_by_id(btf, btf_id);
	if (!t || !btf_type_is_func(t)) {
		/* These checks were already done by the verifier while loading
		 * struct bpf_func_info
		 */
		bpf_log(log, "BTF of func#%d doesn't point to KIND_FUNC\n",
			subprog);
		return -EFAULT;
	}
	tname = btf_name_by_offset(btf, t->name_off);

	if (log->level & BPF_LOG_LEVEL)
		bpf_log(log, "Validating %s() func#%d...\n",
			tname, subprog);

	if (prog->aux->func_info_aux[subprog].unreliable) {
		bpf_log(log, "Verifier bug in function %s()\n", tname);
		return -EFAULT;
	}
	if (prog_type == BPF_PROG_TYPE_EXT)
		prog_type = prog->aux->dst_prog->type;

	t = btf_type_by_id(btf, t->type);
	if (!t || !btf_type_is_func_proto(t)) {
		bpf_log(log, "Invalid type of function %s()\n", tname);
		return -EFAULT;
	}
	args = (const struct btf_param *)(t + 1);
	nargs = btf_type_vlen(t);
	if (nargs > MAX_BPF_FUNC_REG_ARGS) {
		bpf_log(log, "Global function %s() with %d > %d args. Buggy compiler.\n",
			tname, nargs, MAX_BPF_FUNC_REG_ARGS);
		return -EINVAL;
	}
	/* check that function returns int */
	t = btf_type_by_id(btf, t->type);
	while (btf_type_is_modifier(t))
		t = btf_type_by_id(btf, t->type);
	if (!btf_type_is_int(t) && !btf_is_any_enum(t)) {
		bpf_log(log,
			"Global function %s() doesn't return scalar. Only those are supported.\n",
			tname);
		return -EINVAL;
	}
	/* Convert BTF function arguments into verifier types.
	 * Only PTR_TO_CTX and SCALAR are supported atm.
	 */
	for (i = 0; i < nargs; i++) {
		struct bpf_reg_state *reg = &regs[i + 1];

		t = btf_type_by_id(btf, args[i].type);
		while (btf_type_is_modifier(t))
			t = btf_type_by_id(btf, t->type);
		if (btf_type_is_int(t) || btf_is_any_enum(t)) {
			reg->type = SCALAR_VALUE;
			continue;
		}
		if (btf_type_is_ptr(t)) {
			if (btf_get_prog_ctx_type(log, btf, t, prog_type, i)) {
				reg->type = PTR_TO_CTX;
				continue;
			}

			t = btf_type_skip_modifiers(btf, t->type, NULL);

			ref_t = btf_resolve_size(btf, t, &reg->mem_size);
			if (IS_ERR(ref_t)) {
				bpf_log(log,
				    "arg#%d reference type('%s %s') size cannot be determined: %ld\n",
				    i, btf_type_str(t), btf_name_by_offset(btf, t->name_off),
					PTR_ERR(ref_t));
				return -EINVAL;
			}

			reg->type = PTR_TO_MEM | PTR_MAYBE_NULL;
			reg->id = ++env->id_gen;

			continue;
		}
		bpf_log(log, "Arg#%d type %s in %s() is not supported yet.\n",
			i, btf_type_str(t), tname);
		return -EINVAL;
	}
	return 0;
}

static void btf_type_show(const struct btf *btf, u32 type_id, void *obj,
			  struct btf_show *show)
{
	const struct btf_type *t = btf_type_by_id(btf, type_id);

	show->btf = btf;
	memset(&show->state, 0, sizeof(show->state));
	memset(&show->obj, 0, sizeof(show->obj));

	btf_type_ops(t)->show(btf, t, type_id, obj, 0, show);
}

static void btf_seq_show(struct btf_show *show, const char *fmt,
			 va_list args)
{
	seq_vprintf((struct seq_file *)show->target, fmt, args);
}

int btf_type_seq_show_flags(const struct btf *btf, u32 type_id,
			    void *obj, struct seq_file *m, u64 flags)
{
	struct btf_show sseq;

	sseq.target = m;
	sseq.showfn = btf_seq_show;
	sseq.flags = flags;

	btf_type_show(btf, type_id, obj, &sseq);

	return sseq.state.status;
}

void btf_type_seq_show(const struct btf *btf, u32 type_id, void *obj,
		       struct seq_file *m)
{
	(void) btf_type_seq_show_flags(btf, type_id, obj, m,
				       BTF_SHOW_NONAME | BTF_SHOW_COMPACT |
				       BTF_SHOW_ZERO | BTF_SHOW_UNSAFE);
}

struct btf_show_snprintf {
	struct btf_show show;
	int len_left;		/* space left in string */
	int len;		/* length we would have written */
};

static void btf_snprintf_show(struct btf_show *show, const char *fmt,
			      va_list args)
{
	struct btf_show_snprintf *ssnprintf = (struct btf_show_snprintf *)show;
	int len;

	len = vsnprintf(show->target, ssnprintf->len_left, fmt, args);

	if (len < 0) {
		ssnprintf->len_left = 0;
		ssnprintf->len = len;
	} else if (len >= ssnprintf->len_left) {
		/* no space, drive on to get length we would have written */
		ssnprintf->len_left = 0;
		ssnprintf->len += len;
	} else {
		ssnprintf->len_left -= len;
		ssnprintf->len += len;
		show->target += len;
	}
}

int btf_type_snprintf_show(const struct btf *btf, u32 type_id, void *obj,
			   char *buf, int len, u64 flags)
{
	struct btf_show_snprintf ssnprintf;

	ssnprintf.show.target = buf;
	ssnprintf.show.flags = flags;
	ssnprintf.show.showfn = btf_snprintf_show;
	ssnprintf.len_left = len;
	ssnprintf.len = 0;

	btf_type_show(btf, type_id, obj, (struct btf_show *)&ssnprintf);

	/* If we encountered an error, return it. */
	if (ssnprintf.show.state.status)
		return ssnprintf.show.state.status;

	/* Otherwise return length we would have written */
	return ssnprintf.len;
}

#ifdef CONFIG_PROC_FS
static void bpf_btf_show_fdinfo(struct seq_file *m, struct file *filp)
{
	const struct btf *btf = filp->private_data;

	seq_printf(m, "btf_id:\t%u\n", btf->id);
}
#endif

static int btf_release(struct inode *inode, struct file *filp)
{
	btf_put(filp->private_data);
	return 0;
}

const struct file_operations btf_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_btf_show_fdinfo,
#endif
	.release	= btf_release,
};

static int __btf_new_fd(struct btf *btf)
{
	return anon_inode_getfd("btf", &btf_fops, btf, O_RDONLY | O_CLOEXEC);
}

int btf_new_fd(const union bpf_attr *attr, bpfptr_t uattr)
{
	struct btf *btf;
	int ret;

	btf = btf_parse(make_bpfptr(attr->btf, uattr.is_kernel),
			attr->btf_size, attr->btf_log_level,
			u64_to_user_ptr(attr->btf_log_buf),
			attr->btf_log_size);
	if (IS_ERR(btf))
		return PTR_ERR(btf);

	ret = btf_alloc_id(btf);
	if (ret) {
		btf_free(btf);
		return ret;
	}

	/*
	 * The BTF ID is published to the userspace.
	 * All BTF free must go through call_rcu() from
	 * now on (i.e. free by calling btf_put()).
	 */

	ret = __btf_new_fd(btf);
	if (ret < 0)
		btf_put(btf);

	return ret;
}

struct btf *btf_get_by_fd(int fd)
{
	struct btf *btf;
	struct fd f;

	f = fdget(fd);

	if (!f.file)
		return ERR_PTR(-EBADF);

	if (f.file->f_op != &btf_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	btf = f.file->private_data;
	refcount_inc(&btf->refcnt);
	fdput(f);

	return btf;
}

int btf_get_info_by_fd(const struct btf *btf,
		       const union bpf_attr *attr,
		       union bpf_attr __user *uattr)
{
	struct bpf_btf_info __user *uinfo;
	struct bpf_btf_info info;
	u32 info_copy, btf_copy;
	void __user *ubtf;
	char __user *uname;
	u32 uinfo_len, uname_len, name_len;
	int ret = 0;

	uinfo = u64_to_user_ptr(attr->info.info);
	uinfo_len = attr->info.info_len;

	info_copy = min_t(u32, uinfo_len, sizeof(info));
	memset(&info, 0, sizeof(info));
	if (copy_from_user(&info, uinfo, info_copy))
		return -EFAULT;

	info.id = btf->id;
	ubtf = u64_to_user_ptr(info.btf);
	btf_copy = min_t(u32, btf->data_size, info.btf_size);
	if (copy_to_user(ubtf, btf->data, btf_copy))
		return -EFAULT;
	info.btf_size = btf->data_size;

	info.kernel_btf = btf->kernel_btf;

	uname = u64_to_user_ptr(info.name);
	uname_len = info.name_len;
	if (!uname ^ !uname_len)
		return -EINVAL;

	name_len = strlen(btf->name);
	info.name_len = name_len;

	if (uname) {
		if (uname_len >= name_len + 1) {
			if (copy_to_user(uname, btf->name, name_len + 1))
				return -EFAULT;
		} else {
			char zero = '\0';

			if (copy_to_user(uname, btf->name, uname_len - 1))
				return -EFAULT;
			if (put_user(zero, uname + uname_len - 1))
				return -EFAULT;
			/* let user-space know about too short buffer */
			ret = -ENOSPC;
		}
	}

	if (copy_to_user(uinfo, &info, info_copy) ||
	    put_user(info_copy, &uattr->info.info_len))
		return -EFAULT;

	return ret;
}

int btf_get_fd_by_id(u32 id)
{
	struct btf *btf;
	int fd;

	rcu_read_lock();
	btf = idr_find(&btf_idr, id);
	if (!btf || !refcount_inc_not_zero(&btf->refcnt))
		btf = ERR_PTR(-ENOENT);
	rcu_read_unlock();

	if (IS_ERR(btf))
		return PTR_ERR(btf);

	fd = __btf_new_fd(btf);
	if (fd < 0)
		btf_put(btf);

	return fd;
}

u32 btf_obj_id(const struct btf *btf)
{
	return btf->id;
}

bool btf_is_kernel(const struct btf *btf)
{
	return btf->kernel_btf;
}

bool btf_is_module(const struct btf *btf)
{
	return btf->kernel_btf && strcmp(btf->name, "vmlinux") != 0;
}

static int btf_id_cmp_func(const void *a, const void *b)
{
	const int *pa = a, *pb = b;

	return *pa - *pb;
}

bool btf_id_set_contains(const struct btf_id_set *set, u32 id)
{
	return bsearch(&id, set->ids, set->cnt, sizeof(u32), btf_id_cmp_func) != NULL;
}

static void *btf_id_set8_contains(const struct btf_id_set8 *set, u32 id)
{
	return bsearch(&id, set->pairs, set->cnt, sizeof(set->pairs[0]), btf_id_cmp_func);
}

enum {
	BTF_MODULE_F_LIVE = (1 << 0),
};

#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
struct btf_module {
	struct list_head list;
	struct module *module;
	struct btf *btf;
	struct bin_attribute *sysfs_attr;
	int flags;
};

static LIST_HEAD(btf_modules);
static DEFINE_MUTEX(btf_module_mutex);

static ssize_t
btf_module_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t len)
{
	const struct btf *btf = bin_attr->private;

	memcpy(buf, btf->data + off, len);
	return len;
}

static void purge_cand_cache(struct btf *btf);

static int btf_module_notify(struct notifier_block *nb, unsigned long op,
			     void *module)
{
	struct btf_module *btf_mod, *tmp;
	struct module *mod = module;
	struct btf *btf;
	int err = 0;

	if (mod->btf_data_size == 0 ||
	    (op != MODULE_STATE_COMING && op != MODULE_STATE_LIVE &&
	     op != MODULE_STATE_GOING))
		goto out;

	switch (op) {
	case MODULE_STATE_COMING:
		btf_mod = kzalloc(sizeof(*btf_mod), GFP_KERNEL);
		if (!btf_mod) {
			err = -ENOMEM;
			goto out;
		}
		btf = btf_parse_module(mod->name, mod->btf_data, mod->btf_data_size);
		if (IS_ERR(btf)) {
			pr_warn("failed to validate module [%s] BTF: %ld\n",
				mod->name, PTR_ERR(btf));
			kfree(btf_mod);
			if (!IS_ENABLED(CONFIG_MODULE_ALLOW_BTF_MISMATCH))
				err = PTR_ERR(btf);
			goto out;
		}
		err = btf_alloc_id(btf);
		if (err) {
			btf_free(btf);
			kfree(btf_mod);
			goto out;
		}

		purge_cand_cache(NULL);
		mutex_lock(&btf_module_mutex);
		btf_mod->module = module;
		btf_mod->btf = btf;
		list_add(&btf_mod->list, &btf_modules);
		mutex_unlock(&btf_module_mutex);

		if (IS_ENABLED(CONFIG_SYSFS)) {
			struct bin_attribute *attr;

			attr = kzalloc(sizeof(*attr), GFP_KERNEL);
			if (!attr)
				goto out;

			sysfs_bin_attr_init(attr);
			attr->attr.name = btf->name;
			attr->attr.mode = 0444;
			attr->size = btf->data_size;
			attr->private = btf;
			attr->read = btf_module_read;

			err = sysfs_create_bin_file(btf_kobj, attr);
			if (err) {
				pr_warn("failed to register module [%s] BTF in sysfs: %d\n",
					mod->name, err);
				kfree(attr);
				err = 0;
				goto out;
			}

			btf_mod->sysfs_attr = attr;
		}

		break;
	case MODULE_STATE_LIVE:
		mutex_lock(&btf_module_mutex);
		list_for_each_entry_safe(btf_mod, tmp, &btf_modules, list) {
			if (btf_mod->module != module)
				continue;

			btf_mod->flags |= BTF_MODULE_F_LIVE;
			break;
		}
		mutex_unlock(&btf_module_mutex);
		break;
	case MODULE_STATE_GOING:
		mutex_lock(&btf_module_mutex);
		list_for_each_entry_safe(btf_mod, tmp, &btf_modules, list) {
			if (btf_mod->module != module)
				continue;

			list_del(&btf_mod->list);
			if (btf_mod->sysfs_attr)
				sysfs_remove_bin_file(btf_kobj, btf_mod->sysfs_attr);
			purge_cand_cache(btf_mod->btf);
			btf_put(btf_mod->btf);
			kfree(btf_mod->sysfs_attr);
			kfree(btf_mod);
			break;
		}
		mutex_unlock(&btf_module_mutex);
		break;
	}
out:
	return notifier_from_errno(err);
}

static struct notifier_block btf_module_nb = {
	.notifier_call = btf_module_notify,
};

static int __init btf_module_init(void)
{
	register_module_notifier(&btf_module_nb);
	return 0;
}

fs_initcall(btf_module_init);
#endif /* CONFIG_DEBUG_INFO_BTF_MODULES */

struct module *btf_try_get_module(const struct btf *btf)
{
	struct module *res = NULL;
#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	struct btf_module *btf_mod, *tmp;

	mutex_lock(&btf_module_mutex);
	list_for_each_entry_safe(btf_mod, tmp, &btf_modules, list) {
		if (btf_mod->btf != btf)
			continue;

		/* We must only consider module whose __init routine has
		 * finished, hence we must check for BTF_MODULE_F_LIVE flag,
		 * which is set from the notifier callback for
		 * MODULE_STATE_LIVE.
		 */
		if ((btf_mod->flags & BTF_MODULE_F_LIVE) && try_module_get(btf_mod->module))
			res = btf_mod->module;

		break;
	}
	mutex_unlock(&btf_module_mutex);
#endif

	return res;
}

/* Returns struct btf corresponding to the struct module.
 * This function can return NULL or ERR_PTR.
 */
static struct btf *btf_get_module_btf(const struct module *module)
{
#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	struct btf_module *btf_mod, *tmp;
#endif
	struct btf *btf = NULL;

	if (!module) {
		btf = bpf_get_btf_vmlinux();
		if (!IS_ERR_OR_NULL(btf))
			btf_get(btf);
		return btf;
	}

#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	mutex_lock(&btf_module_mutex);
	list_for_each_entry_safe(btf_mod, tmp, &btf_modules, list) {
		if (btf_mod->module != module)
			continue;

		btf_get(btf_mod->btf);
		btf = btf_mod->btf;
		break;
	}
	mutex_unlock(&btf_module_mutex);
#endif

	return btf;
}

BPF_CALL_4(bpf_btf_find_by_name_kind, char *, name, int, name_sz, u32, kind, int, flags)
{
	struct btf *btf = NULL;
	int btf_obj_fd = 0;
	long ret;

	if (flags)
		return -EINVAL;

	if (name_sz <= 1 || name[name_sz - 1])
		return -EINVAL;

	ret = bpf_find_btf_id(name, kind, &btf);
	if (ret > 0 && btf_is_module(btf)) {
		btf_obj_fd = __btf_new_fd(btf);
		if (btf_obj_fd < 0) {
			btf_put(btf);
			return btf_obj_fd;
		}
		return ret | (((u64)btf_obj_fd) << 32);
	}
	if (ret > 0)
		btf_put(btf);
	return ret;
}

const struct bpf_func_proto bpf_btf_find_by_name_kind_proto = {
	.func		= bpf_btf_find_by_name_kind,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_ANYTHING,
};

BTF_ID_LIST_GLOBAL(btf_tracing_ids, MAX_BTF_TRACING_TYPE)
#define BTF_TRACING_TYPE(name, type) BTF_ID(struct, type)
BTF_TRACING_TYPE_xxx
#undef BTF_TRACING_TYPE

/* Kernel Function (kfunc) BTF ID set registration API */

static int btf_populate_kfunc_set(struct btf *btf, enum btf_kfunc_hook hook,
				  struct btf_id_set8 *add_set)
{
	bool vmlinux_set = !btf_is_module(btf);
	struct btf_kfunc_set_tab *tab;
	struct btf_id_set8 *set;
	u32 set_cnt;
	int ret;

	if (hook >= BTF_KFUNC_HOOK_MAX) {
		ret = -EINVAL;
		goto end;
	}

	if (!add_set->cnt)
		return 0;

	tab = btf->kfunc_set_tab;
	if (!tab) {
		tab = kzalloc(sizeof(*tab), GFP_KERNEL | __GFP_NOWARN);
		if (!tab)
			return -ENOMEM;
		btf->kfunc_set_tab = tab;
	}

	set = tab->sets[hook];
	/* Warn when register_btf_kfunc_id_set is called twice for the same hook
	 * for module sets.
	 */
	if (WARN_ON_ONCE(set && !vmlinux_set)) {
		ret = -EINVAL;
		goto end;
	}

	/* We don't need to allocate, concatenate, and sort module sets, because
	 * only one is allowed per hook. Hence, we can directly assign the
	 * pointer and return.
	 */
	if (!vmlinux_set) {
		tab->sets[hook] = add_set;
		return 0;
	}

	/* In case of vmlinux sets, there may be more than one set being
	 * registered per hook. To create a unified set, we allocate a new set
	 * and concatenate all individual sets being registered. While each set
	 * is individually sorted, they may become unsorted when concatenated,
	 * hence re-sorting the final set again is required to make binary
	 * searching the set using btf_id_set8_contains function work.
	 */
	set_cnt = set ? set->cnt : 0;

	if (set_cnt > U32_MAX - add_set->cnt) {
		ret = -EOVERFLOW;
		goto end;
	}

	if (set_cnt + add_set->cnt > BTF_KFUNC_SET_MAX_CNT) {
		ret = -E2BIG;
		goto end;
	}

	/* Grow set */
	set = krealloc(tab->sets[hook],
		       offsetof(struct btf_id_set8, pairs[set_cnt + add_set->cnt]),
		       GFP_KERNEL | __GFP_NOWARN);
	if (!set) {
		ret = -ENOMEM;
		goto end;
	}

	/* For newly allocated set, initialize set->cnt to 0 */
	if (!tab->sets[hook])
		set->cnt = 0;
	tab->sets[hook] = set;

	/* Concatenate the two sets */
	memcpy(set->pairs + set->cnt, add_set->pairs, add_set->cnt * sizeof(set->pairs[0]));
	set->cnt += add_set->cnt;

	sort(set->pairs, set->cnt, sizeof(set->pairs[0]), btf_id_cmp_func, NULL);

	return 0;
end:
	btf_free_kfunc_set_tab(btf);
	return ret;
}

static u32 *__btf_kfunc_id_set_contains(const struct btf *btf,
					enum btf_kfunc_hook hook,
					u32 kfunc_btf_id)
{
	struct btf_id_set8 *set;
	u32 *id;

	if (hook >= BTF_KFUNC_HOOK_MAX)
		return NULL;
	if (!btf->kfunc_set_tab)
		return NULL;
	set = btf->kfunc_set_tab->sets[hook];
	if (!set)
		return NULL;
	id = btf_id_set8_contains(set, kfunc_btf_id);
	if (!id)
		return NULL;
	/* The flags for BTF ID are located next to it */
	return id + 1;
}

static int bpf_prog_type_to_kfunc_hook(enum bpf_prog_type prog_type)
{
	switch (prog_type) {
	case BPF_PROG_TYPE_XDP:
		return BTF_KFUNC_HOOK_XDP;
	case BPF_PROG_TYPE_SCHED_CLS:
		return BTF_KFUNC_HOOK_TC;
	case BPF_PROG_TYPE_STRUCT_OPS:
		return BTF_KFUNC_HOOK_STRUCT_OPS;
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_LSM:
		return BTF_KFUNC_HOOK_TRACING;
	case BPF_PROG_TYPE_SYSCALL:
		return BTF_KFUNC_HOOK_SYSCALL;
	default:
		return BTF_KFUNC_HOOK_MAX;
	}
}

/* Caution:
 * Reference to the module (obtained using btf_try_get_module) corresponding to
 * the struct btf *MUST* be held when calling this function from verifier
 * context. This is usually true as we stash references in prog's kfunc_btf_tab;
 * keeping the reference for the duration of the call provides the necessary
 * protection for looking up a well-formed btf->kfunc_set_tab.
 */
u32 *btf_kfunc_id_set_contains(const struct btf *btf,
			       enum bpf_prog_type prog_type,
			       u32 kfunc_btf_id)
{
	enum btf_kfunc_hook hook;

	hook = bpf_prog_type_to_kfunc_hook(prog_type);
	return __btf_kfunc_id_set_contains(btf, hook, kfunc_btf_id);
}

/* This function must be invoked only from initcalls/module init functions */
int register_btf_kfunc_id_set(enum bpf_prog_type prog_type,
			      const struct btf_kfunc_id_set *kset)
{
	enum btf_kfunc_hook hook;
	struct btf *btf;
	int ret;

	btf = btf_get_module_btf(kset->owner);
	if (!btf) {
		if (!kset->owner && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) {
			pr_err("missing vmlinux BTF, cannot register kfuncs\n");
			return -ENOENT;
		}
		if (kset->owner && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES))
			pr_warn("missing module BTF, cannot register kfuncs\n");
		return 0;
	}
	if (IS_ERR(btf))
		return PTR_ERR(btf);

	hook = bpf_prog_type_to_kfunc_hook(prog_type);
	ret = btf_populate_kfunc_set(btf, hook, kset->set);
	btf_put(btf);
	return ret;
}
EXPORT_SYMBOL_GPL(register_btf_kfunc_id_set);

s32 btf_find_dtor_kfunc(struct btf *btf, u32 btf_id)
{
	struct btf_id_dtor_kfunc_tab *tab = btf->dtor_kfunc_tab;
	struct btf_id_dtor_kfunc *dtor;

	if (!tab)
		return -ENOENT;
	/* Even though the size of tab->dtors[0] is > sizeof(u32), we only need
	 * to compare the first u32 with btf_id, so we can reuse btf_id_cmp_func.
	 */
	BUILD_BUG_ON(offsetof(struct btf_id_dtor_kfunc, btf_id) != 0);
	dtor = bsearch(&btf_id, tab->dtors, tab->cnt, sizeof(tab->dtors[0]), btf_id_cmp_func);
	if (!dtor)
		return -ENOENT;
	return dtor->kfunc_btf_id;
}

static int btf_check_dtor_kfuncs(struct btf *btf, const struct btf_id_dtor_kfunc *dtors, u32 cnt)
{
	const struct btf_type *dtor_func, *dtor_func_proto, *t;
	const struct btf_param *args;
	s32 dtor_btf_id;
	u32 nr_args, i;

	for (i = 0; i < cnt; i++) {
		dtor_btf_id = dtors[i].kfunc_btf_id;

		dtor_func = btf_type_by_id(btf, dtor_btf_id);
		if (!dtor_func || !btf_type_is_func(dtor_func))
			return -EINVAL;

		dtor_func_proto = btf_type_by_id(btf, dtor_func->type);
		if (!dtor_func_proto || !btf_type_is_func_proto(dtor_func_proto))
			return -EINVAL;

		/* Make sure the prototype of the destructor kfunc is 'void func(type *)' */
		t = btf_type_by_id(btf, dtor_func_proto->type);
		if (!t || !btf_type_is_void(t))
			return -EINVAL;

		nr_args = btf_type_vlen(dtor_func_proto);
		if (nr_args != 1)
			return -EINVAL;
		args = btf_params(dtor_func_proto);
		t = btf_type_by_id(btf, args[0].type);
		/* Allow any pointer type, as width on targets Linux supports
		 * will be same for all pointer types (i.e. sizeof(void *))
		 */
		if (!t || !btf_type_is_ptr(t))
			return -EINVAL;
	}
	return 0;
}

/* This function must be invoked only from initcalls/module init functions */
int register_btf_id_dtor_kfuncs(const struct btf_id_dtor_kfunc *dtors, u32 add_cnt,
				struct module *owner)
{
	struct btf_id_dtor_kfunc_tab *tab;
	struct btf *btf;
	u32 tab_cnt;
	int ret;

	btf = btf_get_module_btf(owner);
	if (!btf) {
		if (!owner && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) {
			pr_err("missing vmlinux BTF, cannot register dtor kfuncs\n");
			return -ENOENT;
		}
		if (owner && IS_ENABLED(CONFIG_DEBUG_INFO_BTF_MODULES)) {
			pr_err("missing module BTF, cannot register dtor kfuncs\n");
			return -ENOENT;
		}
		return 0;
	}
	if (IS_ERR(btf))
		return PTR_ERR(btf);

	if (add_cnt >= BTF_DTOR_KFUNC_MAX_CNT) {
		pr_err("cannot register more than %d kfunc destructors\n", BTF_DTOR_KFUNC_MAX_CNT);
		ret = -E2BIG;
		goto end;
	}

	/* Ensure that the prototype of dtor kfuncs being registered is sane */
	ret = btf_check_dtor_kfuncs(btf, dtors, add_cnt);
	if (ret < 0)
		goto end;

	tab = btf->dtor_kfunc_tab;
	/* Only one call allowed for modules */
	if (WARN_ON_ONCE(tab && btf_is_module(btf))) {
		ret = -EINVAL;
		goto end;
	}

	tab_cnt = tab ? tab->cnt : 0;
	if (tab_cnt > U32_MAX - add_cnt) {
		ret = -EOVERFLOW;
		goto end;
	}
	if (tab_cnt + add_cnt >= BTF_DTOR_KFUNC_MAX_CNT) {
		pr_err("cannot register more than %d kfunc destructors\n", BTF_DTOR_KFUNC_MAX_CNT);
		ret = -E2BIG;
		goto end;
	}

	tab = krealloc(btf->dtor_kfunc_tab,
		       offsetof(struct btf_id_dtor_kfunc_tab, dtors[tab_cnt + add_cnt]),
		       GFP_KERNEL | __GFP_NOWARN);
	if (!tab) {
		ret = -ENOMEM;
		goto end;
	}

	if (!btf->dtor_kfunc_tab)
		tab->cnt = 0;
	btf->dtor_kfunc_tab = tab;

	memcpy(tab->dtors + tab->cnt, dtors, add_cnt * sizeof(tab->dtors[0]));
	tab->cnt += add_cnt;

	sort(tab->dtors, tab->cnt, sizeof(tab->dtors[0]), btf_id_cmp_func, NULL);

end:
	if (ret)
		btf_free_dtor_kfunc_tab(btf);
	btf_put(btf);
	return ret;
}
EXPORT_SYMBOL_GPL(register_btf_id_dtor_kfuncs);

#define MAX_TYPES_ARE_COMPAT_DEPTH 2

/* Check local and target types for compatibility. This check is used for
 * type-based CO-RE relocations and follow slightly different rules than
 * field-based relocations. This function assumes that root types were already
 * checked for name match. Beyond that initial root-level name check, names
 * are completely ignored. Compatibility rules are as follows:
 *   - any two STRUCTs/UNIONs/FWDs/ENUMs/INTs/ENUM64s are considered compatible, but
 *     kind should match for local and target types (i.e., STRUCT is not
 *     compatible with UNION);
 *   - for ENUMs/ENUM64s, the size is ignored;
 *   - for INT, size and signedness are ignored;
 *   - for ARRAY, dimensionality is ignored, element types are checked for
 *     compatibility recursively;
 *   - CONST/VOLATILE/RESTRICT modifiers are ignored;
 *   - TYPEDEFs/PTRs are compatible if types they pointing to are compatible;
 *   - FUNC_PROTOs are compatible if they have compatible signature: same
 *     number of input args and compatible return and argument types.
 * These rules are not set in stone and probably will be adjusted as we get
 * more experience with using BPF CO-RE relocations.
 */
int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
			      const struct btf *targ_btf, __u32 targ_id)
{
	return __bpf_core_types_are_compat(local_btf, local_id, targ_btf, targ_id,
					   MAX_TYPES_ARE_COMPAT_DEPTH);
}

#define MAX_TYPES_MATCH_DEPTH 2

int bpf_core_types_match(const struct btf *local_btf, u32 local_id,
			 const struct btf *targ_btf, u32 targ_id)
{
	return __bpf_core_types_match(local_btf, local_id, targ_btf, targ_id, false,
				      MAX_TYPES_MATCH_DEPTH);
}

static bool bpf_core_is_flavor_sep(const char *s)
{
	/* check X___Y name pattern, where X and Y are not underscores */
	return s[0] != '_' &&				      /* X */
	       s[1] == '_' && s[2] == '_' && s[3] == '_' &&   /* ___ */
	       s[4] != '_';				      /* Y */
}

size_t bpf_core_essential_name_len(const char *name)
{
	size_t n = strlen(name);
	int i;

	for (i = n - 5; i >= 0; i--) {
		if (bpf_core_is_flavor_sep(name + i))
			return i + 1;
	}
	return n;
}

struct bpf_cand_cache {
	const char *name;
	u32 name_len;
	u16 kind;
	u16 cnt;
	struct {
		const struct btf *btf;
		u32 id;
	} cands[];
};

static void bpf_free_cands(struct bpf_cand_cache *cands)
{
	if (!cands->cnt)
		/* empty candidate array was allocated on stack */
		return;
	kfree(cands);
}

static void bpf_free_cands_from_cache(struct bpf_cand_cache *cands)
{
	kfree(cands->name);
	kfree(cands);
}

#define VMLINUX_CAND_CACHE_SIZE 31
static struct bpf_cand_cache *vmlinux_cand_cache[VMLINUX_CAND_CACHE_SIZE];

#define MODULE_CAND_CACHE_SIZE 31
static struct bpf_cand_cache *module_cand_cache[MODULE_CAND_CACHE_SIZE];

static DEFINE_MUTEX(cand_cache_mutex);

static void __print_cand_cache(struct bpf_verifier_log *log,
			       struct bpf_cand_cache **cache,
			       int cache_size)
{
	struct bpf_cand_cache *cc;
	int i, j;

	for (i = 0; i < cache_size; i++) {
		cc = cache[i];
		if (!cc)
			continue;
		bpf_log(log, "[%d]%s(", i, cc->name);
		for (j = 0; j < cc->cnt; j++) {
			bpf_log(log, "%d", cc->cands[j].id);
			if (j < cc->cnt - 1)
				bpf_log(log, " ");
		}
		bpf_log(log, "), ");
	}
}

static void print_cand_cache(struct bpf_verifier_log *log)
{
	mutex_lock(&cand_cache_mutex);
	bpf_log(log, "vmlinux_cand_cache:");
	__print_cand_cache(log, vmlinux_cand_cache, VMLINUX_CAND_CACHE_SIZE);
	bpf_log(log, "\nmodule_cand_cache:");
	__print_cand_cache(log, module_cand_cache, MODULE_CAND_CACHE_SIZE);
	bpf_log(log, "\n");
	mutex_unlock(&cand_cache_mutex);
}

static u32 hash_cands(struct bpf_cand_cache *cands)
{
	return jhash(cands->name, cands->name_len, 0);
}

static struct bpf_cand_cache *check_cand_cache(struct bpf_cand_cache *cands,
					       struct bpf_cand_cache **cache,
					       int cache_size)
{
	struct bpf_cand_cache *cc = cache[hash_cands(cands) % cache_size];

	if (cc && cc->name_len == cands->name_len &&
	    !strncmp(cc->name, cands->name, cands->name_len))
		return cc;
	return NULL;
}

static size_t sizeof_cands(int cnt)
{
	return offsetof(struct bpf_cand_cache, cands[cnt]);
}

static struct bpf_cand_cache *populate_cand_cache(struct bpf_cand_cache *cands,
						  struct bpf_cand_cache **cache,
						  int cache_size)
{
	struct bpf_cand_cache **cc = &cache[hash_cands(cands) % cache_size], *new_cands;

	if (*cc) {
		bpf_free_cands_from_cache(*cc);
		*cc = NULL;
	}
	new_cands = kmemdup(cands, sizeof_cands(cands->cnt), GFP_KERNEL);
	if (!new_cands) {
		bpf_free_cands(cands);
		return ERR_PTR(-ENOMEM);
	}
	/* strdup the name, since it will stay in cache.
	 * the cands->name points to strings in prog's BTF and the prog can be unloaded.
	 */
	new_cands->name = kmemdup_nul(cands->name, cands->name_len, GFP_KERNEL);
	bpf_free_cands(cands);
	if (!new_cands->name) {
		kfree(new_cands);
		return ERR_PTR(-ENOMEM);
	}
	*cc = new_cands;
	return new_cands;
}

#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
static void __purge_cand_cache(struct btf *btf, struct bpf_cand_cache **cache,
			       int cache_size)
{
	struct bpf_cand_cache *cc;
	int i, j;

	for (i = 0; i < cache_size; i++) {
		cc = cache[i];
		if (!cc)
			continue;
		if (!btf) {
			/* when new module is loaded purge all of module_cand_cache,
			 * since new module might have candidates with the name
			 * that matches cached cands.
			 */
			bpf_free_cands_from_cache(cc);
			cache[i] = NULL;
			continue;
		}
		/* when module is unloaded purge cache entries
		 * that match module's btf
		 */
		for (j = 0; j < cc->cnt; j++)
			if (cc->cands[j].btf == btf) {
				bpf_free_cands_from_cache(cc);
				cache[i] = NULL;
				break;
			}
	}

}

static void purge_cand_cache(struct btf *btf)
{
	mutex_lock(&cand_cache_mutex);
	__purge_cand_cache(btf, module_cand_cache, MODULE_CAND_CACHE_SIZE);
	mutex_unlock(&cand_cache_mutex);
}
#endif

static struct bpf_cand_cache *
bpf_core_add_cands(struct bpf_cand_cache *cands, const struct btf *targ_btf,
		   int targ_start_id)
{
	struct bpf_cand_cache *new_cands;
	const struct btf_type *t;
	const char *targ_name;
	size_t targ_essent_len;
	int n, i;

	n = btf_nr_types(targ_btf);
	for (i = targ_start_id; i < n; i++) {
		t = btf_type_by_id(targ_btf, i);
		if (btf_kind(t) != cands->kind)
			continue;

		targ_name = btf_name_by_offset(targ_btf, t->name_off);
		if (!targ_name)
			continue;

		/* the resched point is before strncmp to make sure that search
		 * for non-existing name will have a chance to schedule().
		 */
		cond_resched();

		if (strncmp(cands->name, targ_name, cands->name_len) != 0)
			continue;

		targ_essent_len = bpf_core_essential_name_len(targ_name);
		if (targ_essent_len != cands->name_len)
			continue;

		/* most of the time there is only one candidate for a given kind+name pair */
		new_cands = kmalloc(sizeof_cands(cands->cnt + 1), GFP_KERNEL);
		if (!new_cands) {
			bpf_free_cands(cands);
			return ERR_PTR(-ENOMEM);
		}

		memcpy(new_cands, cands, sizeof_cands(cands->cnt));
		bpf_free_cands(cands);
		cands = new_cands;
		cands->cands[cands->cnt].btf = targ_btf;
		cands->cands[cands->cnt].id = i;
		cands->cnt++;
	}
	return cands;
}

static struct bpf_cand_cache *
bpf_core_find_cands(struct bpf_core_ctx *ctx, u32 local_type_id)
{
	struct bpf_cand_cache *cands, *cc, local_cand = {};
	const struct btf *local_btf = ctx->btf;
	const struct btf_type *local_type;
	const struct btf *main_btf;
	size_t local_essent_len;
	struct btf *mod_btf;
	const char *name;
	int id;

	main_btf = bpf_get_btf_vmlinux();
	if (IS_ERR(main_btf))
		return ERR_CAST(main_btf);
	if (!main_btf)
		return ERR_PTR(-EINVAL);

	local_type = btf_type_by_id(local_btf, local_type_id);
	if (!local_type)
		return ERR_PTR(-EINVAL);

	name = btf_name_by_offset(local_btf, local_type->name_off);
	if (str_is_empty(name))
		return ERR_PTR(-EINVAL);
	local_essent_len = bpf_core_essential_name_len(name);

	cands = &local_cand;
	cands->name = name;
	cands->kind = btf_kind(local_type);
	cands->name_len = local_essent_len;

	cc = check_cand_cache(cands, vmlinux_cand_cache, VMLINUX_CAND_CACHE_SIZE);
	/* cands is a pointer to stack here */
	if (cc) {
		if (cc->cnt)
			return cc;
		goto check_modules;
	}

	/* Attempt to find target candidates in vmlinux BTF first */
	cands = bpf_core_add_cands(cands, main_btf, 1);
	if (IS_ERR(cands))
		return ERR_CAST(cands);

	/* cands is a pointer to kmalloced memory here if cands->cnt > 0 */

	/* populate cache even when cands->cnt == 0 */
	cc = populate_cand_cache(cands, vmlinux_cand_cache, VMLINUX_CAND_CACHE_SIZE);
	if (IS_ERR(cc))
		return ERR_CAST(cc);

	/* if vmlinux BTF has any candidate, don't go for module BTFs */
	if (cc->cnt)
		return cc;

check_modules:
	/* cands is a pointer to stack here and cands->cnt == 0 */
	cc = check_cand_cache(cands, module_cand_cache, MODULE_CAND_CACHE_SIZE);
	if (cc)
		/* if cache has it return it even if cc->cnt == 0 */
		return cc;

	/* If candidate is not found in vmlinux's BTF then search in module's BTFs */
	spin_lock_bh(&btf_idr_lock);
	idr_for_each_entry(&btf_idr, mod_btf, id) {
		if (!btf_is_module(mod_btf))
			continue;
		/* linear search could be slow hence unlock/lock
		 * the IDR to avoiding holding it for too long
		 */
		btf_get(mod_btf);
		spin_unlock_bh(&btf_idr_lock);
		cands = bpf_core_add_cands(cands, mod_btf, btf_nr_types(main_btf));
		btf_put(mod_btf);
		if (IS_ERR(cands))
			return ERR_CAST(cands);
		spin_lock_bh(&btf_idr_lock);
	}
	spin_unlock_bh(&btf_idr_lock);
	/* cands is a pointer to kmalloced memory here if cands->cnt > 0
	 * or pointer to stack if cands->cnd == 0.
	 * Copy it into the cache even when cands->cnt == 0 and
	 * return the result.
	 */
	return populate_cand_cache(cands, module_cand_cache, MODULE_CAND_CACHE_SIZE);
}

int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,
		   int relo_idx, void *insn)
{
	bool need_cands = relo->kind != BPF_CORE_TYPE_ID_LOCAL;
	struct bpf_core_cand_list cands = {};
	struct bpf_core_relo_res targ_res;
	struct bpf_core_spec *specs;
	int err;

	/* ~4k of temp memory necessary to convert LLVM spec like "0:1:0:5"
	 * into arrays of btf_ids of struct fields and array indices.
	 */
	specs = kcalloc(3, sizeof(*specs), GFP_KERNEL);
	if (!specs)
		return -ENOMEM;

	if (need_cands) {
		struct bpf_cand_cache *cc;
		int i;

		mutex_lock(&cand_cache_mutex);
		cc = bpf_core_find_cands(ctx, relo->type_id);
		if (IS_ERR(cc)) {
			bpf_log(ctx->log, "target candidate search failed for %d\n",
				relo->type_id);
			err = PTR_ERR(cc);
			goto out;
		}
		if (cc->cnt) {
			cands.cands = kcalloc(cc->cnt, sizeof(*cands.cands), GFP_KERNEL);
			if (!cands.cands) {
				err = -ENOMEM;
				goto out;
			}
		}
		for (i = 0; i < cc->cnt; i++) {
			bpf_log(ctx->log,
				"CO-RE relocating %s %s: found target candidate [%d]\n",
				btf_kind_str[cc->kind], cc->name, cc->cands[i].id);
			cands.cands[i].btf = cc->cands[i].btf;
			cands.cands[i].id = cc->cands[i].id;
		}
		cands.len = cc->cnt;
		/* cand_cache_mutex needs to span the cache lookup and
		 * copy of btf pointer into bpf_core_cand_list,
		 * since module can be unloaded while bpf_core_calc_relo_insn
		 * is working with module's btf.
		 */
	}

	err = bpf_core_calc_relo_insn((void *)ctx->log, relo, relo_idx, ctx->btf, &cands, specs,
				      &targ_res);
	if (err)
		goto out;

	err = bpf_core_patch_insn((void *)ctx->log, insn, relo->insn_off / 8, relo, relo_idx,
				  &targ_res);

out:
	kfree(specs);
	if (need_cands) {
		kfree(cands.cands);
		mutex_unlock(&cand_cache_mutex);
		if (ctx->log->level & BPF_LOG_LEVEL2)
			print_cand_cache(ctx->log);
	}
	return err;
}
