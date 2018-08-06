#include <stdio.h>

int syscall_enter(openat)(void *args)
{
	puts("Hello, world\n");
	return 0;
}

license(GPL);
