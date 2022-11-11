/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FORTIFY_STRING_H_
#define _LINUX_FORTIFY_STRING_H_

#include <linux/bug.h>
#include <linux/const.h>
#include <linux/limits.h>

#define __FORTIFY_INLINE extern __always_inline __gnu_inline __overloadable
#define __RENAME(x) __asm__(#x)

void fortify_panic(const char *name) __noreturn __cold;
void __read_overflow(void) __compiletime_error("detected read beyond size of object (1st parameter)");
void __read_overflow2(void) __compiletime_error("detected read beyond size of object (2nd parameter)");
void __read_overflow2_field(size_t avail, size_t wanted) __compiletime_warning("detected read beyond size of field (2nd parameter); maybe use struct_group()?");
void __write_overflow(void) __compiletime_error("detected write beyond size of object (1st parameter)");
void __write_overflow_field(size_t avail, size_t wanted) __compiletime_warning("detected write beyond size of field (1st parameter); maybe use struct_group()?");

#define __compiletime_strlen(p)					\
({								\
	unsigned char *__p = (unsigned char *)(p);		\
	size_t __ret = SIZE_MAX;				\
	size_t __p_size = __member_size(p);			\
	if (__p_size != SIZE_MAX &&				\
	    __builtin_constant_p(*__p)) {			\
		size_t __p_len = __p_size - 1;			\
		if (__builtin_constant_p(__p[__p_len]) &&	\
		    __p[__p_len] == '\0')			\
			__ret = __builtin_strlen(__p);		\
	}							\
	__ret;							\
})

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
extern void *__underlying_memchr(const void *p, int c, __kernel_size_t size) __RENAME(memchr);
extern int __underlying_memcmp(const void *p, const void *q, __kernel_size_t size) __RENAME(memcmp);
extern void *__underlying_memcpy(void *p, const void *q, __kernel_size_t size) __RENAME(memcpy);
extern void *__underlying_memmove(void *p, const void *q, __kernel_size_t size) __RENAME(memmove);
extern void *__underlying_memset(void *p, int c, __kernel_size_t size) __RENAME(memset);
extern char *__underlying_strcat(char *p, const char *q) __RENAME(strcat);
extern char *__underlying_strcpy(char *p, const char *q) __RENAME(strcpy);
extern __kernel_size_t __underlying_strlen(const char *p) __RENAME(strlen);
extern char *__underlying_strncat(char *p, const char *q, __kernel_size_t count) __RENAME(strncat);
extern char *__underlying_strncpy(char *p, const char *q, __kernel_size_t size) __RENAME(strncpy);
#else

#if defined(__SANITIZE_MEMORY__)
/*
 * For KMSAN builds all memcpy/memset/memmove calls should be replaced by the
 * corresponding __msan_XXX functions.
 */
#include <linux/kmsan_string.h>
#define __underlying_memcpy	__msan_memcpy
#define __underlying_memmove	__msan_memmove
#define __underlying_memset	__msan_memset
#else
#define __underlying_memcpy	__builtin_memcpy
#define __underlying_memmove	__builtin_memmove
#define __underlying_memset	__builtin_memset
#endif

#define __underlying_memchr	__builtin_memchr
#define __underlying_memcmp	__builtin_memcmp
#define __underlying_strcat	__builtin_strcat
#define __underlying_strcpy	__builtin_strcpy
#define __underlying_strlen	__builtin_strlen
#define __underlying_strncat	__builtin_strncat
#define __underlying_strncpy	__builtin_strncpy
#endif

/**
 * unsafe_memcpy - memcpy implementation with no FORTIFY bounds checking
 *
 * @dst: Destination memory address to write to
 * @src: Source memory address to read from
 * @bytes: How many bytes to write to @dst from @src
 * @justification: Free-form text or comment describing why the use is needed
 *
 * This should be used for corner cases where the compiler cannot do the
 * right thing, or during transitions between APIs, etc. It should be used
 * very rarely, and includes a place for justification detailing where bounds
 * checking has happened, and why existing solutions cannot be employed.
 */
#define unsafe_memcpy(dst, src, bytes, justification)		\
	__underlying_memcpy(dst, src, bytes)

/*
 * Clang's use of __builtin_*object_size() within inlines needs hinting via
 * __pass_*object_size(). The preference is to only ever use type 1 (member
 * size, rather than struct size), but there remain some stragglers using
 * type 0 that will be converted in the future.
 */
#define POS			__pass_object_size(1)
#define POS0			__pass_object_size(0)
#define __struct_size(p)	__builtin_object_size(p, 0)
#define __member_size(p)	__builtin_object_size(p, 1)

#define __compiletime_lessthan(bounds, length)	(	\
	__builtin_constant_p((bounds) < (length)) &&	\
	(bounds) < (length)				\
)

/**
 * strncpy - Copy a string to memory with non-guaranteed NUL padding
 *
 * @p: pointer to destination of copy
 * @q: pointer to NUL-terminated source string to copy
 * @size: bytes to write at @p
 *
 * If strlen(@q) >= @size, the copy of @q will stop after @size bytes,
 * and @p will NOT be NUL-terminated
 *
 * If strlen(@q) < @size, following the copy of @q, trailing NUL bytes
 * will be written to @p until @size total bytes have been written.
 *
 * Do not use this function. While FORTIFY_SOURCE tries to avoid
 * over-reads of @q, it cannot defend against writing unterminated
 * results to @p. Using strncpy() remains ambiguous and fragile.
 * Instead, please choose an alternative, so that the expectation
 * of @p's contents is unambiguous:
 *
 * +--------------------+-----------------+------------+
 * | @p needs to be:    | padded to @size | not padded |
 * +====================+=================+============+
 * |     NUL-terminated | strscpy_pad()   | strscpy()  |
 * +--------------------+-----------------+------------+
 * | not NUL-terminated | strtomem_pad()  | strtomem() |
 * +--------------------+-----------------+------------+
 *
 * Note strscpy*()'s differing return values for detecting truncation,
 * and strtomem*()'s expectation that the destination is marked with
 * __nonstring when it is a character array.
 *
 */
__FORTIFY_INLINE __diagnose_as(__builtin_strncpy, 1, 2, 3)
char *strncpy(char * const POS p, const char *q, __kernel_size_t size)
{
	size_t p_size = __member_size(p);

	if (__compiletime_lessthan(p_size, size))
		__write_overflow();
	if (p_size < size)
		fortify_panic(__func__);
	return __underlying_strncpy(p, q, size);
}

__FORTIFY_INLINE __diagnose_as(__builtin_strcat, 1, 2)
char *strcat(char * const POS p, const char *q)
{
	size_t p_size = __member_size(p);

	if (p_size == SIZE_MAX)
		return __underlying_strcat(p, q);
	if (strlcat(p, q, p_size) >= p_size)
		fortify_panic(__func__);
	return p;
}

extern __kernel_size_t __real_strnlen(const char *, __kernel_size_t) __RENAME(strnlen);
__FORTIFY_INLINE __kernel_size_t strnlen(const char * const POS p, __kernel_size_t maxlen)
{
	size_t p_size = __member_size(p);
	size_t p_len = __compiletime_strlen(p);
	size_t ret;

	/* We can take compile-time actions when maxlen is const. */
	if (__builtin_constant_p(maxlen) && p_len != SIZE_MAX) {
		/* If p is const, we can use its compile-time-known len. */
		if (maxlen >= p_size)
			return p_len;
	}

	/* Do not check characters beyond the end of p. */
	ret = __real_strnlen(p, maxlen < p_size ? maxlen : p_size);
	if (p_size <= ret && maxlen != ret)
		fortify_panic(__func__);
	return ret;
}

/*
 * Defined after fortified strnlen to reuse it. However, it must still be
 * possible for strlen() to be used on compile-time strings for use in
 * static initializers (i.e. as a constant expression).
 */
#define strlen(p)							\
	__builtin_choose_expr(__is_constexpr(__builtin_strlen(p)),	\
		__builtin_strlen(p), __fortify_strlen(p))
__FORTIFY_INLINE __diagnose_as(__builtin_strlen, 1)
__kernel_size_t __fortify_strlen(const char * const POS p)
{
	__kernel_size_t ret;
	size_t p_size = __member_size(p);

	/* Give up if we don't know how large p is. */
	if (p_size == SIZE_MAX)
		return __underlying_strlen(p);
	ret = strnlen(p, p_size);
	if (p_size <= ret)
		fortify_panic(__func__);
	return ret;
}

/* defined after fortified strlen to reuse it */
extern size_t __real_strlcpy(char *, const char *, size_t) __RENAME(strlcpy);
__FORTIFY_INLINE size_t strlcpy(char * const POS p, const char * const POS q, size_t size)
{
	size_t p_size = __member_size(p);
	size_t q_size = __member_size(q);
	size_t q_len;	/* Full count of source string length. */
	size_t len;	/* Count of characters going into destination. */

	if (p_size == SIZE_MAX && q_size == SIZE_MAX)
		return __real_strlcpy(p, q, size);
	q_len = strlen(q);
	len = (q_len >= size) ? size - 1 : q_len;
	if (__builtin_constant_p(size) && __builtin_constant_p(q_len) && size) {
		/* Write size is always larger than destination. */
		if (len >= p_size)
			__write_overflow();
	}
	if (size) {
		if (len >= p_size)
			fortify_panic(__func__);
		__underlying_memcpy(p, q, len);
		p[len] = '\0';
	}
	return q_len;
}

/* defined after fortified strnlen to reuse it */
extern ssize_t __real_strscpy(char *, const char *, size_t) __RENAME(strscpy);
__FORTIFY_INLINE ssize_t strscpy(char * const POS p, const char * const POS q, size_t size)
{
	size_t len;
	/* Use string size rather than possible enclosing struct size. */
	size_t p_size = __member_size(p);
	size_t q_size = __member_size(q);

	/* If we cannot get size of p and q default to call strscpy. */
	if (p_size == SIZE_MAX && q_size == SIZE_MAX)
		return __real_strscpy(p, q, size);

	/*
	 * If size can be known at compile time and is greater than
	 * p_size, generate a compile time write overflow error.
	 */
	if (__compiletime_lessthan(p_size, size))
		__write_overflow();

	/*
	 * This call protects from read overflow, because len will default to q
	 * length if it smaller than size.
	 */
	len = strnlen(q, size);
	/*
	 * If len equals size, we will copy only size bytes which leads to
	 * -E2BIG being returned.
	 * Otherwise we will copy len + 1 because of the final '\O'.
	 */
	len = len == size ? size : len + 1;

	/*
	 * Generate a runtime write overflow error if len is greater than
	 * p_size.
	 */
	if (len > p_size)
		fortify_panic(__func__);

	/*
	 * We can now safely call vanilla strscpy because we are protected from:
	 * 1. Read overflow thanks to call to strnlen().
	 * 2. Write overflow thanks to above ifs.
	 */
	return __real_strscpy(p, q, len);
}

/* defined after fortified strlen and strnlen to reuse them */
__FORTIFY_INLINE __diagnose_as(__builtin_strncat, 1, 2, 3)
char *strncat(char * const POS p, const char * const POS q, __kernel_size_t count)
{
	size_t p_len, copy_len;
	size_t p_size = __member_size(p);
	size_t q_size = __member_size(q);

	if (p_size == SIZE_MAX && q_size == SIZE_MAX)
		return __underlying_strncat(p, q, count);
	p_len = strlen(p);
	copy_len = strnlen(q, count);
	if (p_size < p_len + copy_len + 1)
		fortify_panic(__func__);
	__underlying_memcpy(p + p_len, q, copy_len);
	p[p_len + copy_len] = '\0';
	return p;
}

__FORTIFY_INLINE void fortify_memset_chk(__kernel_size_t size,
					 const size_t p_size,
					 const size_t p_size_field)
{
	if (__builtin_constant_p(size)) {
		/*
		 * Length argument is a constant expression, so we
		 * can perform compile-time bounds checking where
		 * buffer sizes are also known at compile time.
		 */

		/* Error when size is larger than enclosing struct. */
		if (__compiletime_lessthan(p_size_field, p_size) &&
		    __compiletime_lessthan(p_size, size))
			__write_overflow();

		/* Warn when write size is larger than dest field. */
		if (__compiletime_lessthan(p_size_field, size))
			__write_overflow_field(p_size_field, size);
	}
	/*
	 * At this point, length argument may not be a constant expression,
	 * so run-time bounds checking can be done where buffer sizes are
	 * known. (This is not an "else" because the above checks may only
	 * be compile-time warnings, and we want to still warn for run-time
	 * overflows.)
	 */

	/*
	 * Always stop accesses beyond the struct that contains the
	 * field, when the buffer's remaining size is known.
	 * (The SIZE_MAX test is to optimize away checks where the buffer
	 * lengths are unknown.)
	 */
	if (p_size != SIZE_MAX && p_size < size)
		fortify_panic("memset");
}

#define __fortify_memset_chk(p, c, size, p_size, p_size_field) ({	\
	size_t __fortify_size = (size_t)(size);				\
	fortify_memset_chk(__fortify_size, p_size, p_size_field),	\
	__underlying_memset(p, c, __fortify_size);			\
})

/*
 * __struct_size() vs __member_size() must be captured here to avoid
 * evaluating argument side-effects further into the macro layers.
 */
#ifndef CONFIG_KMSAN
#define memset(p, c, s) __fortify_memset_chk(p, c, s,			\
		__struct_size(p), __member_size(p))
#endif

/*
 * To make sure the compiler can enforce protection against buffer overflows,
 * memcpy(), memmove(), and memset() must not be used beyond individual
 * struct members. If you need to copy across multiple members, please use
 * struct_group() to create a named mirror of an anonymous struct union.
 * (e.g. see struct sk_buff.) Read overflow checking is currently only
 * done when a write overflow is also present, or when building with W=1.
 *
 * Mitigation coverage matrix
 *					Bounds checking at:
 *					+-------+-------+-------+-------+
 *					| Compile time  |   Run time    |
 * memcpy() argument sizes:		| write | read  | write | read  |
 *        dest     source   length      +-------+-------+-------+-------+
 * memcpy(known,   known,   constant)	|   y   |   y   |  n/a  |  n/a  |
 * memcpy(known,   unknown, constant)	|   y   |   n   |  n/a  |   V   |
 * memcpy(known,   known,   dynamic)	|   n   |   n   |   B   |   B   |
 * memcpy(known,   unknown, dynamic)	|   n   |   n   |   B   |   V   |
 * memcpy(unknown, known,   constant)	|   n   |   y   |   V   |  n/a  |
 * memcpy(unknown, unknown, constant)	|   n   |   n   |   V   |   V   |
 * memcpy(unknown, known,   dynamic)	|   n   |   n   |   V   |   B   |
 * memcpy(unknown, unknown, dynamic)	|   n   |   n   |   V   |   V   |
 *					+-------+-------+-------+-------+
 *
 * y = perform deterministic compile-time bounds checking
 * n = cannot perform deterministic compile-time bounds checking
 * n/a = no run-time bounds checking needed since compile-time deterministic
 * B = can perform run-time bounds checking (currently unimplemented)
 * V = vulnerable to run-time overflow (will need refactoring to solve)
 *
 */
__FORTIFY_INLINE bool fortify_memcpy_chk(__kernel_size_t size,
					 const size_t p_size,
					 const size_t q_size,
					 const size_t p_size_field,
					 const size_t q_size_field,
					 const char *func)
{
	if (__builtin_constant_p(size)) {
		/*
		 * Length argument is a constant expression, so we
		 * can perform compile-time bounds checking where
		 * buffer sizes are also known at compile time.
		 */

		/* Error when size is larger than enclosing struct. */
		if (__compiletime_lessthan(p_size_field, p_size) &&
		    __compiletime_lessthan(p_size, size))
			__write_overflow();
		if (__compiletime_lessthan(q_size_field, q_size) &&
		    __compiletime_lessthan(q_size, size))
			__read_overflow2();

		/* Warn when write size argument larger than dest field. */
		if (__compiletime_lessthan(p_size_field, size))
			__write_overflow_field(p_size_field, size);
		/*
		 * Warn for source field over-read when building with W=1
		 * or when an over-write happened, so both can be fixed at
		 * the same time.
		 */
		if ((IS_ENABLED(KBUILD_EXTRA_WARN1) ||
		     __compiletime_lessthan(p_size_field, size)) &&
		    __compiletime_lessthan(q_size_field, size))
			__read_overflow2_field(q_size_field, size);
	}
	/*
	 * At this point, length argument may not be a constant expression,
	 * so run-time bounds checking can be done where buffer sizes are
	 * known. (This is not an "else" because the above checks may only
	 * be compile-time warnings, and we want to still warn for run-time
	 * overflows.)
	 */

	/*
	 * Always stop accesses beyond the struct that contains the
	 * field, when the buffer's remaining size is known.
	 * (The SIZE_MAX test is to optimize away checks where the buffer
	 * lengths are unknown.)
	 */
	if ((p_size != SIZE_MAX && p_size < size) ||
	    (q_size != SIZE_MAX && q_size < size))
		fortify_panic(func);

	/*
	 * Warn when writing beyond destination field size.
	 *
	 * We must ignore p_size_field == 0 for existing 0-element
	 * fake flexible arrays, until they are all converted to
	 * proper flexible arrays.
	 *
	 * The implementation of __builtin_*object_size() behaves
	 * like sizeof() when not directly referencing a flexible
	 * array member, which means there will be many bounds checks
	 * that will appear at run-time, without a way for them to be
	 * detected at compile-time (as can be done when the destination
	 * is specifically the flexible array member).
	 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=101832
	 */
	if (p_size_field != 0 && p_size_field != SIZE_MAX &&
	    p_size != p_size_field && p_size_field < size)
		return true;

	return false;
}

#define __fortify_memcpy_chk(p, q, size, p_size, q_size,		\
			     p_size_field, q_size_field, op) ({		\
	size_t __fortify_size = (size_t)(size);				\
	WARN_ONCE(fortify_memcpy_chk(__fortify_size, p_size, q_size,	\
				     p_size_field, q_size_field, #op),	\
		  #op ": detected field-spanning write (size %zu) of single %s (size %zu)\n", \
		  __fortify_size,					\
		  "field \"" #p "\" at " __FILE__ ":" __stringify(__LINE__), \
		  p_size_field);					\
	__underlying_##op(p, q, __fortify_size);			\
})

/*
 * Notes about compile-time buffer size detection:
 *
 * With these types...
 *
 *	struct middle {
 *		u16 a;
 *		u8 middle_buf[16];
 *		int b;
 *	};
 *	struct end {
 *		u16 a;
 *		u8 end_buf[16];
 *	};
 *	struct flex {
 *		int a;
 *		u8 flex_buf[];
 *	};
 *
 *	void func(TYPE *ptr) { ... }
 *
 * Cases where destination size cannot be currently detected:
 * - the size of ptr's object (seemingly by design, gcc & clang fail):
 *	__builtin_object_size(ptr, 1) == SIZE_MAX
 * - the size of flexible arrays in ptr's obj (by design, dynamic size):
 *	__builtin_object_size(ptr->flex_buf, 1) == SIZE_MAX
 * - the size of ANY array at the end of ptr's obj (gcc and clang bug):
 *	__builtin_object_size(ptr->end_buf, 1) == SIZE_MAX
 *	https://gcc.gnu.org/bugzilla/show_bug.cgi?id=101836
 *
 * Cases where destination size is currently detected:
 * - the size of non-array members within ptr's object:
 *	__builtin_object_size(ptr->a, 1) == 2
 * - the size of non-flexible-array in the middle of ptr's obj:
 *	__builtin_object_size(ptr->middle_buf, 1) == 16
 *
 */

/*
 * __struct_size() vs __member_size() must be captured here to avoid
 * evaluating argument side-effects further into the macro layers.
 */
#define memcpy(p, q, s)  __fortify_memcpy_chk(p, q, s,			\
		__struct_size(p), __struct_size(q),			\
		__member_size(p), __member_size(q),			\
		memcpy)
#define memmove(p, q, s)  __fortify_memcpy_chk(p, q, s,			\
		__struct_size(p), __struct_size(q),			\
		__member_size(p), __member_size(q),			\
		memmove)

extern void *__real_memscan(void *, int, __kernel_size_t) __RENAME(memscan);
__FORTIFY_INLINE void *memscan(void * const POS0 p, int c, __kernel_size_t size)
{
	size_t p_size = __struct_size(p);

	if (__compiletime_lessthan(p_size, size))
		__read_overflow();
	if (p_size < size)
		fortify_panic(__func__);
	return __real_memscan(p, c, size);
}

__FORTIFY_INLINE __diagnose_as(__builtin_memcmp, 1, 2, 3)
int memcmp(const void * const POS0 p, const void * const POS0 q, __kernel_size_t size)
{
	size_t p_size = __struct_size(p);
	size_t q_size = __struct_size(q);

	if (__builtin_constant_p(size)) {
		if (__compiletime_lessthan(p_size, size))
			__read_overflow();
		if (__compiletime_lessthan(q_size, size))
			__read_overflow2();
	}
	if (p_size < size || q_size < size)
		fortify_panic(__func__);
	return __underlying_memcmp(p, q, size);
}

__FORTIFY_INLINE __diagnose_as(__builtin_memchr, 1, 2, 3)
void *memchr(const void * const POS0 p, int c, __kernel_size_t size)
{
	size_t p_size = __struct_size(p);

	if (__compiletime_lessthan(p_size, size))
		__read_overflow();
	if (p_size < size)
		fortify_panic(__func__);
	return __underlying_memchr(p, c, size);
}

void *__real_memchr_inv(const void *s, int c, size_t n) __RENAME(memchr_inv);
__FORTIFY_INLINE void *memchr_inv(const void * const POS0 p, int c, size_t size)
{
	size_t p_size = __struct_size(p);

	if (__compiletime_lessthan(p_size, size))
		__read_overflow();
	if (p_size < size)
		fortify_panic(__func__);
	return __real_memchr_inv(p, c, size);
}

extern void *__real_kmemdup(const void *src, size_t len, gfp_t gfp) __RENAME(kmemdup);
__FORTIFY_INLINE void *kmemdup(const void * const POS0 p, size_t size, gfp_t gfp)
{
	size_t p_size = __struct_size(p);

	if (__compiletime_lessthan(p_size, size))
		__read_overflow();
	if (p_size < size)
		fortify_panic(__func__);
	return __real_kmemdup(p, size, gfp);
}

/* Defined after fortified strlen to reuse it. */
__FORTIFY_INLINE __diagnose_as(__builtin_strcpy, 1, 2)
char *strcpy(char * const POS p, const char * const POS q)
{
	size_t p_size = __member_size(p);
	size_t q_size = __member_size(q);
	size_t size;

	/* If neither buffer size is known, immediately give up. */
	if (__builtin_constant_p(p_size) &&
	    __builtin_constant_p(q_size) &&
	    p_size == SIZE_MAX && q_size == SIZE_MAX)
		return __underlying_strcpy(p, q);
	size = strlen(q) + 1;
	/* Compile-time check for const size overflow. */
	if (__compiletime_lessthan(p_size, size))
		__write_overflow();
	/* Run-time check for dynamic size overflow. */
	if (p_size < size)
		fortify_panic(__func__);
	__underlying_memcpy(p, q, size);
	return p;
}

/* Don't use these outside the FORITFY_SOURCE implementation */
#undef __underlying_memchr
#undef __underlying_memcmp
#undef __underlying_strcat
#undef __underlying_strcpy
#undef __underlying_strlen
#undef __underlying_strncat
#undef __underlying_strncpy

#undef POS
#undef POS0

#endif /* _LINUX_FORTIFY_STRING_H_ */
