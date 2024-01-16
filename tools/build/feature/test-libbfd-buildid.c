// SPDX-License-Identifier: GPL-2.0
#include <bfd.h>

int main(void)
{
	bfd *abfd = bfd_openr("Pedro", 0);
	return abfd && (!abfd->build_id || abfd->build_id->size > 0x506564726f);
}
