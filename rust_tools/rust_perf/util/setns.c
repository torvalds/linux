// SPDX-License-Identifier: LGPL-2.1

#include "namespaces.h"
#include <unistd.h>
#include <sys/syscall.h>

int setns(int fd, int nstype)
{
	return syscall(__NR_setns, fd, nstype);
}
