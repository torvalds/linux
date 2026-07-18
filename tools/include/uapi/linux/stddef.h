/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H


#ifndef __always_inline
#define __always_inline __inline__
#endif

/* Not all C++ standards support type declarations inside an anonymous union */
#ifndef __cplusplus
#define __struct_group_tag(TAG)		TAG
#else
#define __struct_group_tag(TAG)
#endif

/**
 * __struct_group() - Create a mirrored named and anonyomous struct
 *
 * @TAG: The tag name for the named sub-struct (usually empty)
 * @NAME: The identifier name of the mirrored sub-struct
 * @ATTRS: Any struct attributes (usually empty)
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical layout
 * and size: one anonymous and one named. The former's members can be used
 * normally without sub-struct naming, and the latter can be used to
 * reason about the start, end, and size of the group of struct members.
 * The named struct can also be explicitly tagged for layer reuse (C only),
 * as well as both having struct attributes appended.
 */
#define __struct_group(TAG, NAME, ATTRS, MEMBERS...) \
	union { \
		struct { MEMBERS } ATTRS; \
		struct __struct_group_tag(TAG) { MEMBERS } ATTRS NAME; \
	} ATTRS

#ifdef __cplusplus
/* sizeof(struct{}) is 1 in C++, not 0, can't use C version of the macro. */
#define __DECLARE_FLEX_ARRAY(T, member)	\
	T member[0]
#else
/**
 * __DECLARE_FLEX_ARRAY() - Declare a flexible array usable in a union
 *
 * @TYPE: The type of each flexible array element
 * @NAME: The name of the flexible array member
 *
 * In order to have a flexible array member in a union or alone in a
 * struct, it needs to be wrapped in an anonymous struct with at least 1
 * named member, but that member can be empty.
 */
#define __DECLARE_FLEX_ARRAY(TYPE, NAME)	\
	struct { \
		struct { } __empty_ ## NAME; \
		TYPE NAME[]; \
	}
#endif

#ifndef __counted_by
#define __counted_by(m)
#endif

#ifndef __counted_by_le
#define __counted_by_le(m)
#endif

#ifndef __counted_by_be
#define __counted_by_be(m)
#endif

#ifndef __counted_by_ptr
#define __counted_by_ptr(m)
#endif

#define __kernel_nonstring

#endif /* _LINUX_STDDEF_H */
