/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIB_UBSAN_H
#define _LIB_UBSAN_H

/*
 * ABI defined by Clang's UBSAN enum SanitizerHandler:
 * https://github.com/llvm/llvm-project/blob/release/16.x/clang/lib/CodeGen/CodeGenFunction.h#L113
 */
enum ubsan_checks {
	ubsan_add_overflow,
	ubsan_builtin_unreachable,
	ubsan_cfi_check_fail,
	ubsan_divrem_overflow,
	ubsan_dynamic_type_cache_miss,
	ubsan_float_cast_overflow,
	ubsan_function_type_mismatch,
	ubsan_implicit_conversion,
	ubsan_invalid_builtin,
	ubsan_invalid_objc_cast,
	ubsan_load_invalid_value,
	ubsan_missing_return,
	ubsan_mul_overflow,
	ubsan_negate_overflow,
	ubsan_nullability_arg,
	ubsan_nullability_return,
	ubsan_nonnull_arg,
	ubsan_nonnull_return,
	ubsan_out_of_bounds,
	ubsan_pointer_overflow,
	ubsan_shift_out_of_bounds,
	ubsan_sub_overflow,
	ubsan_type_mismatch,
	ubsan_alignment_assumption,
	ubsan_vla_bound_not_positive,
};

enum {
	type_kind_int = 0,
	type_kind_float = 1,
	type_unknown = 0xffff
};

struct type_descriptor {
	u16 type_kind;
	u16 type_info;
	char type_name[];
};

struct source_location {
	const char *file_name;
	union {
		unsigned long reported;
		struct {
			u32 line;
			u32 column;
		};
	};
};

struct overflow_data {
	struct source_location location;
	struct type_descriptor *type;
};

struct type_mismatch_data {
	struct source_location location;
	struct type_descriptor *type;
	unsigned long alignment;
	unsigned char type_check_kind;
};

struct type_mismatch_data_v1 {
	struct source_location location;
	struct type_descriptor *type;
	unsigned char log_alignment;
	unsigned char type_check_kind;
};

struct type_mismatch_data_common {
	struct source_location *location;
	struct type_descriptor *type;
	unsigned long alignment;
	unsigned char type_check_kind;
};

struct nonnull_arg_data {
	struct source_location location;
	struct source_location attr_location;
	int arg_index;
};

struct out_of_bounds_data {
	struct source_location location;
	struct type_descriptor *array_type;
	struct type_descriptor *index_type;
};

struct shift_out_of_bounds_data {
	struct source_location location;
	struct type_descriptor *lhs_type;
	struct type_descriptor *rhs_type;
};

struct unreachable_data {
	struct source_location location;
};

struct invalid_value_data {
	struct source_location location;
	struct type_descriptor *type;
};

struct alignment_assumption_data {
	struct source_location location;
	struct source_location assumption_location;
	struct type_descriptor *type;
};

#if defined(CONFIG_ARCH_SUPPORTS_INT128)
typedef __int128 s_max;
typedef unsigned __int128 u_max;
#else
typedef s64 s_max;
typedef u64 u_max;
#endif

/*
 * When generating Runtime Calls, Clang doesn't respect the -mregparm=3
 * option used on i386: https://github.com/llvm/llvm-project/issues/89670
 * Fix this for earlier Clang versions by forcing the calling convention
 * to use non-register arguments.
 */
#if defined(CONFIG_X86_32) && \
    defined(CONFIG_CC_IS_CLANG) && CONFIG_CLANG_VERSION < 190000
# define ubsan_linkage asmlinkage
#else
# define ubsan_linkage
#endif

void ubsan_linkage __ubsan_handle_add_overflow(void *data, void *lhs, void *rhs);
void ubsan_linkage __ubsan_handle_sub_overflow(void *data, void *lhs, void *rhs);
void ubsan_linkage __ubsan_handle_mul_overflow(void *data, void *lhs, void *rhs);
void ubsan_linkage __ubsan_handle_negate_overflow(void *_data, void *old_val);
void ubsan_linkage __ubsan_handle_divrem_overflow(void *_data, void *lhs, void *rhs);
void ubsan_linkage __ubsan_handle_type_mismatch(struct type_mismatch_data *data, void *ptr);
void ubsan_linkage __ubsan_handle_type_mismatch_v1(void *_data, void *ptr);
void ubsan_linkage __ubsan_handle_out_of_bounds(void *_data, void *index);
void ubsan_linkage __ubsan_handle_shift_out_of_bounds(void *_data, void *lhs, void *rhs);
void ubsan_linkage __ubsan_handle_builtin_unreachable(void *_data);
void ubsan_linkage __ubsan_handle_load_invalid_value(void *_data, void *val);
void ubsan_linkage __ubsan_handle_alignment_assumption(void *_data, unsigned long ptr,
						       unsigned long align,
						       unsigned long offset);

#endif
