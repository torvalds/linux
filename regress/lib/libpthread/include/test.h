/*	$OpenBSD: test.h,v 1.7 2024/08/29 15:18:17 claudio Exp $	*/

#ifndef _h_test_
#define _h_test_

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

static void __vpanic(const char *, const char *, const char *, 
	int, const char *, va_list) __attribute__((__noreturn__));
static void __panic(const char *, const char *, const char *,
	int, const char *, ...) __attribute__((__noreturn__))
	__attribute__((__format__ (printf, 5, 6)));

#if defined(__OpenBSD__) || defined(__FreeBSD__)
#include <pthread.h>
#include <pthread_np.h>
void	_thread_dump_info(void);
int	_thread_sys_write(int, const char*, size_t);
void	_thread_sys__exit(int) __attribute__((__noreturn__));
#define SET_NAME(x)	pthread_set_name_np(pthread_self(), x)
#define DUMP_INFO()	_thread_dump_info()
#else
#define	_thread_sys_write(fd,buf,len)	write(fd,buf,len)
#define	_thread_sys__exit(ret)		_exit(ret)
#define	strlcat(dst,src,siz)		strcat(dst,src)
#define SET_NAME(x)	/* nada */
#define DUMP_INFO()	/* nada */
#endif

static void
__vpanic(const char *type, const char *errstr, const char *filenm, int lineno,
    const char *fmt, va_list ap)
{
	char buf[1024];

	/* "<type> at <filenm>:<lineno>: <fmt ap...>:<errstr>" */
	snprintf(buf, sizeof buf, "%s at %s:%d\n", type, filenm, lineno);
	_thread_sys_write(2, buf, strlen(buf));
	vsnprintf(buf, sizeof buf, fmt, ap);
	if (errstr != NULL) {
		strlcat(buf, ": ", sizeof buf);
		strlcat(buf, errstr, sizeof buf);
	}
	strlcat(buf, "\n", sizeof buf);
	_thread_sys_write(2, buf, strlen(buf));

	DUMP_INFO();
	_thread_sys__exit(1);

	_thread_sys_write(2, "[locking]\n", 10);
	while(1);
}

static void
__panic(const char *type, const char *errstr, const char *filenm, int lineno,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__vpanic(type, errstr, filenm, lineno, fmt, ap);
	va_end(ap);
}

#define DIE(e, m, args...) \
	__panic("died", strerror(e), __FILE__, __LINE__, m , ## args)

#define PANIC(m, args...)  \
	__panic("panic", NULL, __FILE__, __LINE__, m, ## args)

#define ASSERT(x) do { \
	if (!(x)) \
		__panic("assert failed", NULL, __FILE__, __LINE__, "%s", #x); \
} while(0)

#define ASSERTe(x,rhs) do { \
	int _x; \
	_x = (x); \
	if (!(_x rhs)) { \
	    if (_x > 0) \
		__panic("assert failed", strerror(_x), __FILE__, __LINE__,  \
		    "%s %s", #x, #rhs); \
	    else \
		__panic("assert failed", NULL, __FILE__, __LINE__, \
		    "%s [=%d] %s", #x, _x, #rhs); \
	} \
} while(0)

#define _T(x) __builtin_classify_type(x)

#define _CHECK(x, rhs, efn) do { \
	typeof(x) _x; \
	_x = (x); \
	if (!(_x rhs)) \
		__panic("check failed", efn, __FILE__, __LINE__, \
		   ((_T(0) == _T(_x)   )? "failed check %s (=%d) %s " : \
		    (_T("") == _T(_x)  )? "failed check %s (=%s) %s " : \
		    (_T('x') == _T(_x) )? "failed check %s (=%c) %s " : \
		    (_T(0L) == _T(_x)  )? "failed check %s (=%ld) %s " : "?") \
		    , #x, _x, #rhs); \
} while(0)

#define CHECKr(x) _CHECK(x, == 0, strerror(_x))
#define CHECKe(x) _CHECK(x, != -1, strerror(errno))
#define CHECKn(x) _CHECK(x, != 0, strerror(errno))
#define CHECKhn(x) _CHECK(x, != 0, hstrerror(h_errno))

#define SUCCEED 	exit(0)

#define OK		(0)
#define NOTOK		(-1)

#endif /* _h_test_ */
