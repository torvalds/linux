/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/btf.h>

enum {
	RETVAL_FMT_HEX   = BIT(0),
	RETVAL_FMT_DEC   = BIT(1),
	RETVAL_FMT_BOOL  = BIT(2),
	RETVAL_FMT_TRUNC = BIT(3),
	RETVAL_FMT_BTF   = BIT(4),
	RETVAL_FMT_UNSIGNED = BIT(5),
};

const struct btf_type *btf_find_func_proto(const char *func_name,
					   struct btf **btf_p);
const struct btf_param *btf_get_func_param(const struct btf_type *func_proto,
					   s32 *nr);
const struct btf_member *btf_find_struct_member(struct btf *btf,
						const struct btf_type *type,
						const char *member_name,
						u32 *anon_offset,
						const struct btf_type **member_type);
#ifdef CONFIG_DEBUG_INFO_BTF
void btf_trim_retval(unsigned long func, unsigned long *retval, bool *print_retval,
		     int *fmt, bool hex);
#else
static inline void btf_trim_retval(unsigned long func, unsigned long *retval,
				   bool *print_retval, int *fmt, bool hex)
{
}
#endif
