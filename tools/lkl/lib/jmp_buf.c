#include <setjmp.h>
#include <lkl_host.h>

int jmp_buf_set(struct lkl_jmp_buf *jmpb)
{
	return setjmp(*((jmp_buf *)jmpb->buf));
}

void jmp_buf_longjmp(struct lkl_jmp_buf *jmpb, int val)
{
	longjmp(*((jmp_buf *)jmpb->buf), val);
}
