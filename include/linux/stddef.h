/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#include <uapi/linux/stddef.h>

#undef NULL
#define NULL ((void *)0)

enum {
	false	= 0,
	true	= 1
};

#undef offsetof
#define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)

/**
 * sizeof_field() - Report the size of a struct field in bytes
 *
 * @TYPE: The structure containing the field of interest
 * @MEMBER: The field to return the size of
 */
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

/**
 * offsetofend() - Report the offset of a struct field within the struct
 *
 * @TYPE: The type of the structure
 * @MEMBER: The member within the structure to get the end offset of
 */
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof_field(TYPE, MEMBER))

/**
 * struct_group() - Wrap a set of declarations in a mirrored struct
 *
 * @NAME: The identifier name of the mirrored sub-struct
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members.
 */
#define struct_group(NAME, MEMBERS...)	\
	__struct_group(/* no tag */, NAME, /* no attrs */, MEMBERS)

/**
 * struct_group_attr() - Create a struct_group() with trailing attributes
 *
 * @NAME: The identifier name of the mirrored sub-struct
 * @ATTRS: Any struct attributes to apply
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members. Includes structure attributes argument.
 */
#define struct_group_attr(NAME, ATTRS, MEMBERS...) \
	__struct_group(/* no tag */, NAME, ATTRS, MEMBERS)

/**
 * struct_group_tagged() - Create a struct_group with a reusable tag
 *
 * @TAG: The tag name for the named sub-struct
 * @NAME: The identifier name of the mirrored sub-struct
 * @MEMBERS: The member declarations for the mirrored structs
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members. Includes struct tag argument for the named copy,
 * so the specified layout can be reused later.
 */
#define struct_group_tagged(TAG, NAME, MEMBERS...) \
	__struct_group(TAG, NAME, /* no attrs */, MEMBERS)

/**
 * DECLARE_FLEX_ARRAY() - Declare a flexible array usable in a union
 *
 * @TYPE: The type of each flexible array element
 * @NAME: The name of the flexible array member
 *
 * In order to have a flexible array member in a union or alone in a
 * struct, it needs to be wrapped in an anonymous struct with at least 1
 * named member, but that member can be empty.
 */
#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
	__DECLARE_FLEX_ARRAY(TYPE, NAME)

/**
 * __TRAILING_OVERLAP() - Overlap a flexible-array member with trailing
 *			  members.
 *
 * Creates a union between a flexible-array member (FAM) in a struct and a set
 * of additional members that would otherwise follow it.
 *
 * Beware that, as this helper encloses TYPE NAME and MEMBERS in the same
 * union, designated initializers for MEMBERS may overwrite portions
 * previously initialized through NAME.
 *
 * For example::
 *
 *	struct flex {
 *		size_t count;
 *		u8 fam[];
 *	};
 *
 *	struct composite {
 *		...
 *		__TRAILING_OVERLAP(struct flex, flex, fam, __packed,
 *			u8 data;
 *		);
 *	} __packed;
 *
 *	static struct composite comp = {
 *		.flex = {
 *			.count = 1,
 *		},
 *		.data = 2,
 *	};
 *
 * In the example above, .flex and .data initialize different views of the same
 * union storage. Since .data is initialized last, it _may_ overwrite portions
 * previously initialized through .flex, leading to .flex.count being zeroed
 * out.
 *
 * A couple of alternatives are shown below.
 *
 * a) Initialize only one view of the overlapped storage and assign the rest
 *    at runtime::
 *
 *	static struct composite comp = {
 *		.flex = {
 *			.count = 1,
 *		},
 *	};
 *
 *	static void foo(void)
 *	{
 *		comp.data = 2;
 *		...
 *	}
 *
 * b) Alternatively, replace designated initializers with runtime assignments::
 *
 *	static void foo(void)
 *	{
 *		struct composite comp;
 *
 *		comp.flex.count = 1;
 *		comp.data = 2;
 *		...
 *	}
 *
 * Compiler Explorer test code: https://godbolt.org/z/voM4E36dT
 *
 * For another example of the above see commit 5e54510a9389 ("acpi: nfit:
 * intel: avoid multiple -Wflex-array-member-not-at-end warnings")
 *
 * Link: https://git.kernel.org/linus/5e54510a9389caa9
 *
 * @TYPE: Flexible structure type name, including "struct" keyword.
 * @NAME: Name for a variable to define.
 * @FAM: The flexible-array member within @TYPE
 * @ATTRS: Any struct attributes (usually empty)
 * @MEMBERS: Trailing overlapping members.
 */
#define __TRAILING_OVERLAP(TYPE, NAME, FAM, ATTRS, MEMBERS)			\
	union {									\
		TYPE NAME;							\
		struct {							\
			unsigned char __offset_to_FAM[offsetof(TYPE, FAM)];	\
			MEMBERS							\
		} ATTRS;							\
	}

/**
 * TRAILING_OVERLAP() - Overlap a flexible-array member with trailing members.
 *
 * Creates a union between a flexible-array member (FAM) in a struct and a set
 * of additional members that would otherwise follow it.
 *
 * @TYPE: Flexible structure type name, including "struct" keyword.
 * @NAME: Name for a variable to define.
 * @FAM: The flexible-array member within @TYPE
 * @MEMBERS: Trailing overlapping members.
 */
#define TRAILING_OVERLAP(TYPE, NAME, FAM, MEMBERS)				\
	__TRAILING_OVERLAP(TYPE, NAME, FAM, /* no attrs */, MEMBERS)

#endif
