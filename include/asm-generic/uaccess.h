#ifndef _ASM_GENERIC_UACCESS_H_
#define _ASM_GENERIC_UACCESS_H_

/*
 * This macro should be used instead of __get_user() when accessing
 * values at locations that are not known to be aligned.
 */
#define __get_user_unaligned(x, ptr)					\
({									\
	__typeof__ (*(ptr)) __x;					\
	__copy_from_user(&__x, (ptr), sizeof(*(ptr))) ? -EFAULT : 0;	\
	(x) = __x;							\
})


/*
 * This macro should be used instead of __put_user() when accessing
 * values at locations that are not known to be aligned.
 */
#define __put_user_unaligned(x, ptr)					\
({									\
	__typeof__ (*(ptr)) __x = (x);					\
	__copy_to_user((ptr), &__x, sizeof(*(ptr))) ? -EFAULT : 0;	\
})

#endif /* _ASM_GENERIC_UACCESS_H */
