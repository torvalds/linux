#ifndef __V850_UACCESS_H__
#define __V850_UACCESS_H__

/*
 * User space memory access functions
 */

#include <linux/errno.h>
#include <linux/string.h>

#include <asm/segment.h>
#include <asm/machdep.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

static inline int access_ok (int type, const void *addr, unsigned long size)
{
	/* XXX I guess we should check against real ram bounds at least, and
	   possibly make sure ADDR is not within the kernel.
	   For now we just check to make sure it's not a small positive
	   or negative value, as that will at least catch some kinds of
	   error.  In particular, we make sure that ADDR's not within the
	   interrupt vector area, which we know starts at zero, or within the
	   peripheral-I/O area, which is located just _before_ zero.  */
	unsigned long val = (unsigned long)addr;
	return val >= (0x80 + NUM_CPU_IRQS*16) && val < 0xFFFFF000;
}

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table (unsigned long);


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

extern int bad_user_access_length (void);

#define __get_user(var, ptr)						      \
  ({									      \
	  int __gu_err = 0;						      \
	  typeof(*(ptr)) __gu_val = 0;					      \
	  switch (sizeof (*(ptr))) {					      \
	  case 1:							      \
	  case 2:							      \
	  case 4:							      \
		  __gu_val = *(ptr);					      \
		  break;						      \
	  case 8:							      \
		  memcpy(&__gu_val, ptr, sizeof(__gu_val));		      \
		  break;						      \
	  default:							      \
		  __gu_val = 0;						      \
		  __gu_err = __get_user_bad ();				      \
		  break;						      \
	  }								      \
	  (var) = __gu_val;						      \
	  __gu_err;							      \
  })
#define __get_user_bad()	(bad_user_access_length (), (-EFAULT))

#define __put_user(var, ptr)						      \
  ({									      \
	  int __pu_err = 0;						      \
	  switch (sizeof (*(ptr))) {					      \
	  case 1:							      \
	  case 2:							      \
	  case 4:							      \
		  *(ptr) = (var);					      \
		  break;						      \
	  case 8: {							      \
	  	  typeof(*(ptr)) __pu_val = 0;				      \
		  memcpy(ptr, &__pu_val, sizeof(__pu_val));		      \
		  }							      \
		  break;						      \
	  default:							      \
		  __pu_err = __put_user_bad ();				      \
		  break;						      \
	  }								      \
	  __pu_err;							      \
  })
#define __put_user_bad()	(bad_user_access_length (), (-EFAULT))

#define put_user(x, ptr)	__put_user(x, ptr)
#define get_user(x, ptr)	__get_user(x, ptr)

#define __copy_from_user(to, from, n)	(memcpy (to, from, n), 0)
#define __copy_to_user(to, from, n)	(memcpy(to, from, n), 0)

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

#define copy_from_user(to, from, n)	__copy_from_user (to, from, n)
#define copy_to_user(to, from, n) 	__copy_to_user(to, from, n)

#define copy_to_user_ret(to,from,n,retval) \
  ({ if (copy_to_user (to,from,n)) return retval; })

#define copy_from_user_ret(to,from,n,retval) \
  ({ if (copy_from_user (to,from,n)) return retval; })

/*
 * Copy a null terminated string from userspace.
 */

static inline long
strncpy_from_user (char *dst, const char *src, long count)
{
	char *tmp;
	strncpy (dst, src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--)
		;
	return tmp - dst;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
static inline long strnlen_user (const char *src, long n)
{
	return strlen (src) + 1;
}

#define strlen_user(str)	strnlen_user (str, 32767)

/*
 * Zero Userspace
 */

static inline unsigned long
clear_user (void *to, unsigned long n)
{
	memset (to, 0, n);
	return 0;
}

#endif /* __V850_UACCESS_H__ */
