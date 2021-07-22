/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A tagged pointer implementation
 */
#ifndef __EROFS_FS_TAGPTR_H
#define __EROFS_FS_TAGPTR_H

#include <linux/types.h>
#include <linux/build_bug.h>

/*
 * the name of tagged pointer types are tagptr{1, 2, 3...}_t
 * avoid directly using the internal structs __tagptr{1, 2, 3...}
 */
#define __MAKE_TAGPTR(n) \
typedef struct __tagptr##n {	\
	uintptr_t v;	\
} tagptr##n##_t;

__MAKE_TAGPTR(1)
__MAKE_TAGPTR(2)
__MAKE_TAGPTR(3)
__MAKE_TAGPTR(4)

#undef __MAKE_TAGPTR

extern void __compiletime_error("bad tagptr tags")
	__bad_tagptr_tags(void);

extern void __compiletime_error("bad tagptr type")
	__bad_tagptr_type(void);

/* fix the broken usage of "#define tagptr2_t tagptr3_t" by users */
#define __tagptr_mask_1(ptr, n)	\
	__builtin_types_compatible_p(typeof(ptr), struct __tagptr##n) ? \
		(1UL << (n)) - 1 :

#define __tagptr_mask(ptr)	(\
	__tagptr_mask_1(ptr, 1) ( \
	__tagptr_mask_1(ptr, 2) ( \
	__tagptr_mask_1(ptr, 3) ( \
	__tagptr_mask_1(ptr, 4) ( \
	__bad_tagptr_type(), 0)))))

/* generate a tagged pointer from a raw value */
#define tagptr_init(type, val) \
	((typeof(type)){ .v = (uintptr_t)(val) })

/*
 * directly cast a tagged pointer to the native pointer type, which
 * could be used for backward compatibility of existing code.
 */
#define tagptr_cast_ptr(tptr) ((void *)(tptr).v)

/* encode tagged pointers */
#define tagptr_fold(type, ptr, _tags) ({ \
	const typeof(_tags) tags = (_tags); \
	if (__builtin_constant_p(tags) && (tags & ~__tagptr_mask(type))) \
		__bad_tagptr_tags(); \
tagptr_init(type, (uintptr_t)(ptr) | tags); })

/* decode tagged pointers */
#define tagptr_unfold_ptr(tptr) \
	((void *)((tptr).v & ~__tagptr_mask(tptr)))

#define tagptr_unfold_tags(tptr) \
	((tptr).v & __tagptr_mask(tptr))

/* operations for the tagger pointer */
#define tagptr_eq(_tptr1, _tptr2) ({ \
	typeof(_tptr1) tptr1 = (_tptr1); \
	typeof(_tptr2) tptr2 = (_tptr2); \
	(void)(&tptr1 == &tptr2); \
(tptr1).v == (tptr2).v; })

/* lock-free CAS operation */
#define tagptr_cmpxchg(_ptptr, _o, _n) ({ \
	typeof(_ptptr) ptptr = (_ptptr); \
	typeof(_o) o = (_o); \
	typeof(_n) n = (_n); \
	(void)(&o == &n); \
	(void)(&o == ptptr); \
tagptr_init(o, cmpxchg(&ptptr->v, o.v, n.v)); })

/* wrap WRITE_ONCE if atomic update is needed */
#define tagptr_replace_tags(_ptptr, tags) ({ \
	typeof(_ptptr) ptptr = (_ptptr); \
	*ptptr = tagptr_fold(*ptptr, tagptr_unfold_ptr(*ptptr), tags); \
*ptptr; })

#define tagptr_set_tags(_ptptr, _tags) ({ \
	typeof(_ptptr) ptptr = (_ptptr); \
	const typeof(_tags) tags = (_tags); \
	if (__builtin_constant_p(tags) && (tags & ~__tagptr_mask(*ptptr))) \
		__bad_tagptr_tags(); \
	ptptr->v |= tags; \
*ptptr; })

#define tagptr_clear_tags(_ptptr, _tags) ({ \
	typeof(_ptptr) ptptr = (_ptptr); \
	const typeof(_tags) tags = (_tags); \
	if (__builtin_constant_p(tags) && (tags & ~__tagptr_mask(*ptptr))) \
		__bad_tagptr_tags(); \
	ptptr->v &= ~tags; \
*ptptr; })

#endif	/* __EROFS_FS_TAGPTR_H */
