#include <setjmp.h>
#include <lkl_host.h>

void jmp_buf_set(struct lkl_jmp_buf *jmpb, void (*f)(void))
{
	if (!setjmp(*((jmp_buf *)jmpb->buf)))
		f();
}

void jmp_buf_longjmp(struct lkl_jmp_buf *jmpb, int val)
{
	longjmp(*((jmp_buf *)jmpb->buf), val);
}
