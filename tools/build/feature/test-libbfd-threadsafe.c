// SPDX-License-Identifier: GPL-2.0
#include <bfd.h>

static bool lock(void *unused)
{
	return true;
}

static bool unlock(void *unused)
{
	return true;
}

int main(void)
{
       /* Check for presence of new thread safety API (version 2.42) */
       return !bfd_thread_init(lock, unlock, NULL);
}
