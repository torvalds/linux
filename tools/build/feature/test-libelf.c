#include <libelf.h>

int main(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ, 0);

	return (long)elf;
}
