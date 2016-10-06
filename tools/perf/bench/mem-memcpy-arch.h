
#ifdef HAVE_ARCH_X86_64_SUPPORT

#define MEMCPY_FN(fn, name, desc)		\
	void *fn(void *, const void *, size_t);

#include "mem-memcpy-x86-64-asm-def.h"

#undef MEMCPY_FN

#endif

