#include <sys/sdt.h>

int main(void)
{
	DTRACE_PROBE(provider, name);
	return 0;
}
