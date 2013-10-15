#include <stdio.h>

static void exit_fn(int status, void *__data)
{
	printf("exit status: %d, data: %d\n", status, *(int *)__data);
}

static int data = 123;

int main(void)
{
	on_exit(exit_fn, &data);

	return 321;
}
