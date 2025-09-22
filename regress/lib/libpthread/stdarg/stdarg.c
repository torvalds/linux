/*	$OpenBSD: stdarg.c,v 1.5 2003/07/31 21:48:06 deraadt Exp $	*/
/* David Leonard <d@openbsd.org>, 2001. Public Domain. */

/*
 * Test <stdarg.h>
 */

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "test.h"

#define EQ(v,exp) _CHECK(v, == exp, NULL) 

int thing;

static int
test1(char *fmt, ...)
{
	va_list	ap;

	char	ch;
	int	i;
	int	c;
	long	l;
	void 	*p;
	char 	*ofmt = fmt;

	va_start(ap, fmt);
	for (; *fmt; fmt++)
	    switch ((ch =*fmt)) {
	    case 'i':		
		i = va_arg(ap, int); 
		EQ(i, 1234);
		break;
	    case 'c':		
		c = va_arg(ap, int); 
		EQ(c, 'x');
		break;
	    case 'l':		
		l = va_arg(ap, long); 
		EQ(l, 123456789L);
		break;
	    case 'p':		
		p = va_arg(ap, void *); 
		EQ(p, &thing);
		break;
	    default:
		fprintf(stderr,
		    "unexpected character 0x%02x `%c' in %s(%p) at %p\n",
		    ch, ch, ofmt, ofmt, fmt);
		ASSERT(0);
	    }
	va_end(ap);
	return 9;
}

static void * 
run_test(void *arg)
{
	char *msg = (char *)arg;
	int i;

	SET_NAME(msg);

	puts(msg);
	for (i = 0; i < 1000000; i++) {
		ASSERT(test1("iclp", 1234, 'x', 123456789L, &thing) == 9);
	}
	printf("ok\n");
	return NULL;
}

int
main(int argc, char *argv[])
{
	pthread_t t1, t2;

	printf("trying loop in single-threaded mode:\n");
	run_test("main");
	printf("now running loop with 2 threads:\n");
	CHECKr(pthread_create(&t1, NULL, run_test, "child 1"));
	CHECKr(pthread_create(&t2, NULL, run_test, "child 2"));
	CHECKr(pthread_join(t1, NULL));
	CHECKr(pthread_join(t2, NULL));
	SUCCEED;
}
