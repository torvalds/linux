/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ERR_H__
#define __ERR_H__

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) (unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO

#define __STR(x) #x

#define set_if_not_errno_or_zero(x, y)			\
({							\
	asm volatile ("if %0 s< -4095 goto +1\n"	\
		      "if %0 s<= 0 goto +1\n"		\
		      "%0 = " __STR(y) "\n"		\
		      : "+r"(x));			\
})

static inline int IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

#endif /* __ERR_H__ */
