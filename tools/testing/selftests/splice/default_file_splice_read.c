#define _GNU_SOURCE
#include <fcntl.h>

int main(int argc, char **argv)
{
        splice(0, 0, 1, 0, 1<<30, 0);
	return 0;
}
