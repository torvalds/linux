#include <stdlib.h>
extern size_t strlcpy(char *dest, const char *src, size_t size);

int main(void)
{
	strlcpy(NULL, NULL, 0);
	return 0;
}
