#include <emmintrin.h>

void bar_init(void) __attribute__((constructor));
void bar_fini(void) __attribute__((destructor));

void
bar_init(void)
{
	__m128i xmm_alpha;

	if ((((unsigned long)&xmm_alpha) & 15) != 0)
		exit(1);
}

void
bar_fini(void)
{
	bar_init();
}
