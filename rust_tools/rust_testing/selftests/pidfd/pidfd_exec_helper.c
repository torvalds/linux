#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	if (pause())
		_exit(EXIT_FAILURE);

	_exit(EXIT_SUCCESS);
}
