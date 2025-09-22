#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <radius.h>

typedef void (*testfunc)(void);

void check_failed(const char *expr, const char *file, int line);
void add_test(testfunc fn, const char *name);

#define CHECK(x) \
	do { if (!(x)) check_failed(#x, __FILE__, __LINE__); } while(0)

#define ADD_TEST(fn) \
extern void fn(void);			\
__attribute__((__constructor__))	\
void fn ## _add(void)			\
{					\
        add_test(fn, #fn);		\
}
