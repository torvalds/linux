/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_UACCESS_H
#define __UM_UACCESS_H

#include "linux/sched.h"

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b) ((a).seg == (b).seg)

#include "um_uaccess.h"

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)

#define __copy_to_user(to, from, n) copy_to_user(to, from, n)

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

#define __get_user(x, ptr) \
({ \
        const __typeof__(ptr) __private_ptr = ptr; \
        __typeof__(*(__private_ptr)) __private_val; \
        int __private_ret = -EFAULT; \
        (x) = (__typeof__(*(__private_ptr)))0; \
	if (__copy_from_user(&__private_val, (__private_ptr), \
	    sizeof(*(__private_ptr))) == 0) {\
        	(x) = (__typeof__(*(__private_ptr))) __private_val; \
		__private_ret = 0; \
	} \
        __private_ret; \
}) 

#define get_user(x, ptr) \
({ \
        const __typeof__((*(ptr))) __user *private_ptr = (ptr); \
        (access_ok(VERIFY_READ, private_ptr, sizeof(*private_ptr)) ? \
	 __get_user(x, private_ptr) : ((x) = 0, -EFAULT)); \
})

#define __put_user(x, ptr) \
({ \
        __typeof__(ptr) __private_ptr = ptr; \
        __typeof__(*(__private_ptr)) __private_val; \
        int __private_ret = -EFAULT; \
        __private_val = (__typeof__(*(__private_ptr))) (x); \
        if (__copy_to_user((__private_ptr), &__private_val, \
			   sizeof(*(__private_ptr))) == 0) { \
		__private_ret = 0; \
	} \
        __private_ret; \
})

#define put_user(x, ptr) \
({ \
        __typeof__(*(ptr)) __user *private_ptr = (ptr); \
        (access_ok(VERIFY_WRITE, private_ptr, sizeof(*private_ptr)) ? \
	 __put_user(x, private_ptr) : -EFAULT); \
})

#define strlen_user(str) strnlen_user(str, ~0UL >> 1)

struct exception_table_entry
{
        unsigned long insn;
	unsigned long fixup;
};

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
