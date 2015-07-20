#include <stdint.h>

volatile uint64_t x;

int main(int argc, char *argv[])
{
	uint64_t old, new = argc;

	argv = argv;
	do {
		old = __sync_val_compare_and_swap(&x, 0, 0);
	} while (!__sync_bool_compare_and_swap(&x, old, new));
	return old == new;
}
