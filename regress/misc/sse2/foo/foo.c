#include <emmintrin.h>
#include <pthread.h>

void foo_init(void) __attribute__((constructor));
void foo_fini(void) __attribute__((destructor));

void *
foo(void *arg)
{
	__m128i xmm_alpha;

	if ((((unsigned long)&xmm_alpha) & 15) != 0)
		exit(1);
}

void
foo_init(void)
{
	foo(NULL);
}

void
foo_fini(void)
{
	foo(NULL);
}

int
main(void)
{
	pthread_t thread;

	foo(NULL);
	pthread_create(&thread, NULL, foo, NULL);
	return 0;
}
