#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/fsnotify_backend.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include "fanotify.h"

SYSCALL_DEFINE3(fanotify_init, unsigned int, flags, unsigned int, event_f_flags,
		unsigned int, priority)
{
	return -ENOSYS;
}
