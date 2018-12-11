// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>

int main(void)
{
	free(get_current_dir_name());
	return 0;
}
