#include <zlib.h>

int main(void)
{
	z_stream zs;

	inflateInit(&zs);
	return 0;
}
