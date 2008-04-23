/* Changes made by Lineo Inc.    May 2001
 *
 * Based on: include/asm-m68knommu/uaccess.h
 */

#ifndef __BLACKFIN_UACCESS_H
#define __BLACKFIN_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/segment.h>
#ifdef CONFIG_ACCESS_CHECK
# include <asm/bfin-global.h>
#endif

#define get_ds()        (KERNEL_DS)
#define get_fs()        (current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}

#define segment_eq(a,b) ((a) == (b))

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define access_ok(type, addr, size) _access_ok((unsigned long)(addr), (size))

static inline int is_in_rom(unsigned long addr)
{
	/*
	 * What we are really trying to do is determine if addr is
	 * in an allocated kernel memory region. If not then assume
	 * we cannot free it or otherwise de-allocate it. Ideally
	 * we could restrict this to really being in a ROM or flash,
	 * but that would need to be done on a board by board basis,
	 * not globally.
	 */
	if ((addr < _ramstart) || (addr >= _ramend))
		return (1);

	/* Default case, not in ROM */
	return (0);
}

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 */

#ifndef CONFIG_ACCESS_CHECK
static inline int _access_ok(unsigned long addr, unsigned long size) { return 1; }
#else
#ifdef CONFIG_ACCESS_OK_L1
extern int _access_ok(unsigned long addr, unsigned long size)__attribute__((l1_text));
#else
extern int _access_ok(unsigned long addr, unsigned long size);
#endif
#endif

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

struct exception_table_entry {
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x,p)						\
	({							\
		int _err = 0;					\
		typeof(*(p)) _x = (x);				\
		typeof(*(p)) *_p = (p);				\
		if (!access_ok(VERIFY_WRITE, _p, sizeof(*(_p)))) {\
			_err = -EFAULT;				\
		}						\
		else {						\
		switch (sizeof (*(_p))) {			\
		case 1:						\
			__put_user_asm(_x, _p, B);		\
			break;					\
		case 2:						\
			__put_user_asm(_x, _p, W);		\
			break;					\
		case 4:						\
			__put_user_asm(_x, _p,  );		\
			break;					\
		case 8: {					\
			long _xl, _xh;				\
			_xl = ((long *)&_x)[0];			\
			_xh = ((long *)&_x)[1];			\
			__put_user_asm(_xl, ((long *)_p)+0, );	\
			__put_user_asm(_xh, ((long *)_p)+1, );	\
		} break;					\
		default:					\
			_err = __put_user_bad();		\
			break;					\
		}						\
		}						\
		_err;						\
	})

#define __put_user(x,p) put_user(x,p)
static inline int bad_user_access_length(void)
{
	panic("bad_user_access_length");
	return -1;
}

#define __put_user_bad() (printk(KERN_INFO "put_user_bad %s:%d %s\n",\
                           __FILE__, __LINE__, __func__),\
                           bad_user_access_length(), (-EFAULT))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */

#define __ptr(x) ((unsigned long *)(x))

#define __put_user_asm(x,p,bhw)				\
	__asm__ (#bhw"[%1] = %0;\n\t"			\
		 : /* no outputs */			\
		 :"d" (x),"a" (__ptr(p)) : "memory")

#define get_user(x,p)							\
	({								\
		int _err = 0;						\
		typeof(*(p)) *_p = (p);					\
		if (!access_ok(VERIFY_READ, _p, sizeof(*(_p)))) {	\
			_err = -EFAULT;					\
		}							\
		else {							\
		switch (sizeof(*(_p))) {				\
		case 1:							\
			__get_user_asm(x, _p, B,(Z));			\
			break;						\
		case 2:							\
			__get_user_asm(x, _p, W,(Z));			\
			break;						\
		case 4:							\
			__get_user_asm(x, _p,  , );			\
			break;						\
		case 8: {						\
			unsigned long _xl, _xh;				\
			__get_user_asm(_xl, ((unsigned long *)_p)+0,  , ); \
			__get_user_asm(_xh, ((unsigned long *)_p)+1,  , ); \
			((unsigned long *)&x)[0] = _xl;			\
			((unsigned long *)&x)[1] = _xh;			\
		} break;						\
		default:						\
			x = 0;						\
			printk(KERN_INFO "get_user_bad: %s:%d %s\n",    \
			       __FILE__, __LINE__, __func__);	\
			_err = __get_user_bad();			\
			break;						\
		}							\
		}							\
		_err;							\
	})

#define __get_user(x,p) get_user(x,p)

#define __get_user_bad() (bad_user_access_length(), (-EFAULT))

#define __get_user_asm(x,p,bhw,option)				\
	{							\
		unsigned long _tmp;				\
		__asm__ ("%0 =" #bhw "[%1]"#option";\n\t"	\
			 : "=d" (_tmp)				\
			 : "a" (__ptr(p)));			\
		(x) = (__typeof__(*(p))) _tmp;			\
	}

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)
#define __copy_to_user(to, from, n) copy_to_user(to, from, n)
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

#define copy_to_user_ret(to,from,n,retval) ({ if (copy_to_user(to,from,n))\
				                 return retval; })

#define copy_from_user_ret(to,from,n,retval) ({ if (copy_from_user(to,from,n))\
                                                   return retval; })

static inline long copy_from_user(void *to,
				  const void __user * from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		memcpy(to, from, n);
	else
		return n;
	return 0;
}

static inline long copy_to_user(void *to,
				const void __user * from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		memcpy(to, from, n);
	else
		return n;
	return 0;
}

/*
 * Copy a null terminated string from userspace.
 */

static inline long strncpy_from_user(char *dst,
                                     const char *src, long count)
{
	char *tmp;
	if (!access_ok(VERIFY_READ, src, 1))
		return -EFAULT;
	strncpy(dst, src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--) ;
	return (tmp - dst);
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
static inline long strnlen_user(const char *src, long n)
{
	return (strlen(src) + 1);
}

#define strlen_user(str) strnlen_user(str, 32767)

/*
 * Zero Userspace
 */

static inline unsigned long __clear_user(void *to, unsigned long n)
{
	memset(to, 0, n);
	return 0;
}

#define clear_user(to, n) __clear_user(to, n)

#endif				/* _BLACKFIN_UACCESS_H */
