/* Public domain. */

#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

#include <sys/errno.h>

#define ERESTARTSYS	EINTR
#define ETIME		ETIMEDOUT
#define EREMOTEIO	EIO
#define ENOTSUPP	ENOTSUP
#define ENODATA		ENOTSUP
#define ECHRNG		EINVAL
#define EHWPOISON	EIO
#define ENOPKG		ENOENT
#define EMULTIHOP	EIPSEC
#define EBADSLT		EINVAL
#define ENOKEY		ENOENT
#define EPROBE_DEFER	EAGAIN
#define ENOLINK		EIO

#endif
