#include <string.h>
#include <stdio.h>
#include <stdarg.h>

int
main(int argc, char **argv)
{
	char buf[100];
	char buf2[50];
	va_list l;
	FILE *f;
	bzero(buf, 200);
	memcpy(buf2, buf, sizeof buf);
	memcpy(buf2, buf, 105);
	memset(buf, 0, 500);
	strncat(buf2, "blahblah", 1000);
	strncpy(buf2, buf, 1234);
	snprintf(buf, 5432, "foo");
	vsnprintf(buf, 2345, "bar", l);
	fwrite(buf, 123, 4, f);
	return 1;
}

