#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/libkern.h>

long __stack_chk_guard[8] = {};
void __stack_chk_fail(void);

void
__stack_chk_fail(void)
{

	panic("stack overflow detected; backtrace may be corrupted");
}

static void
__stack_chk_init(void *dummy __unused)
{
	size_t i;
	long guard[nitems(__stack_chk_guard)];

	arc4rand(guard, sizeof(guard), 0);
	for (i = 0; i < nitems(guard); i++)
		__stack_chk_guard[i] = guard[i];
}
SYSINIT(stack_chk, SI_SUB_RANDOM, SI_ORDER_ANY, __stack_chk_init, NULL);
