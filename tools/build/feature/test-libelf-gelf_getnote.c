// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <gelf.h>

int main(void)
{
	return gelf_getnote(NULL, 0, NULL, NULL, NULL) == 0;
}
