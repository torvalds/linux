#include "util.h"
#include <unistd.h>
#include <sys/syscall.h>

int setns(int fd, int nstype)
{
	return syscall(__NR_setns, fd, nstype);
}
