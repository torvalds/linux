#include <gnu/libc-version.h>

int main(void)
{
	const char *version = gnu_get_libc_version();
	return (long)version;
}

