#include <execinfo.h>
#include <stdio.h>

int main(void)
{
	backtrace(NULL, 0);
	backtrace_symbols(NULL, 0);

	return 0;
}
