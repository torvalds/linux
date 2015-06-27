#include <unistd.h>

int main(int argc, char *argv[])
{
	write(1, "Hello\n", 6);
	while (1)
		;
}
