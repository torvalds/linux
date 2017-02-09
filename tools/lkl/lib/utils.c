#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <lkl_host.h>

static const char * const lkl_err_strings[] = {
	"Success",
	"Operation not permitted",
	"No such file or directory",
	"No such process",
	"Interrupted system call",
	"I/O error",
	"No such device or address",
	"Argument list too long",
	"Exec format error",
	"Bad file number",
	"No child processes",
	"Try again",
	"Out of memory",
	"Permission denied",
	"Bad address",
	"Block device required",
	"Device or resource busy",
	"File exists",
	"Cross-device link",
	"No such device",
	"Not a directory",
	"Is a directory",
	"Invalid argument",
	"File table overflow",
	"Too many open files",
	"Not a typewriter",
	"Text file busy",
	"File too large",
	"No space left on device",
	"Illegal seek",
	"Read-only file system",
	"Too many links",
	"Broken pipe",
	"Math argument out of domain of func",
	"Math result not representable",
	"Resource deadlock would occur",
	"File name too long",
	"No record locks available",
	"Invalid system call number",
	"Directory not empty",
	"Too many symbolic links encountered",
	"Bad error code", /* EWOULDBLOCK is EAGAIN */
	"No message of desired type",
	"Identifier removed",
	"Channel number out of range",
	"Level 2 not synchronized",
	"Level 3 halted",
	"Level 3 reset",
	"Link number out of range",
	"Protocol driver not attached",
	"No CSI structure available",
	"Level 2 halted",
	"Invalid exchange",
	"Invalid request descriptor",
	"Exchange full",
	"No anode",
	"Invalid request code",
	"Invalid slot",
	"Bad error code", /* EDEADLOCK is EDEADLK */
	"Bad font file format",
	"Device not a stream",
	"No data available",
	"Timer expired",
	"Out of streams resources",
	"Machine is not on the network",
	"Package not installed",
	"Object is remote",
	"Link has been severed",
	"Advertise error",
	"Srmount error",
	"Communication error on send",
	"Protocol error",
	"Multihop attempted",
	"RFS specific error",
	"Not a data message",
	"Value too large for defined data type",
	"Name not unique on network",
	"File descriptor in bad state",
	"Remote address changed",
	"Can not access a needed shared library",
	"Accessing a corrupted shared library",
	".lib section in a.out corrupted",
	"Attempting to link in too many shared libraries",
	"Cannot exec a shared library directly",
	"Illegal byte sequence",
	"Interrupted system call should be restarted",
	"Streams pipe error",
	"Too many users",
	"Socket operation on non-socket",
	"Destination address required",
	"Message too long",
	"Protocol wrong type for socket",
	"Protocol not available",
	"Protocol not supported",
	"Socket type not supported",
	"Operation not supported on transport endpoint",
	"Protocol family not supported",
	"Address family not supported by protocol",
	"Address already in use",
	"Cannot assign requested address",
	"Network is down",
	"Network is unreachable",
	"Network dropped connection because of reset",
	"Software caused connection abort",
	"Connection reset by peer",
	"No buffer space available",
	"Transport endpoint is already connected",
	"Transport endpoint is not connected",
	"Cannot send after transport endpoint shutdown",
	"Too many references: cannot splice",
	"Connection timed out",
	"Connection refused",
	"Host is down",
	"No route to host",
	"Operation already in progress",
	"Operation now in progress",
	"Stale file handle",
	"Structure needs cleaning",
	"Not a XENIX named type file",
	"No XENIX semaphores available",
	"Is a named type file",
	"Remote I/O error",
	"Quota exceeded",
	"No medium found",
	"Wrong medium type",
	"Operation Canceled",
	"Required key not available",
	"Key has expired",
	"Key has been revoked",
	"Key was rejected by service",
	"Owner died",
	"State not recoverable",
	"Operation not possible due to RF-kill",
	"Memory page has hardware error",
};

const char *lkl_strerror(int err)
{
	if (err < 0)
		err = -err;

	if ((size_t)err >= sizeof(lkl_err_strings) / sizeof(const char *))
		return "Bad error code";

	return lkl_err_strings[err];
}

void lkl_perror(char *msg, int err)
{
	const char *err_msg = lkl_strerror(err);
	/* We need to use 'real' printf because lkl_host_ops.print can
	 * be turned off when debugging is off. */
	lkl_printf("%s: %s\n", msg, err_msg);
}

static int lkl_vprintf(const char *fmt, va_list args)
{
	int n;
	char *buffer;
	va_list copy;

	if (!lkl_host_ops.print)
		return 0;

	va_copy(copy, args);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);

	buffer = lkl_host_ops.mem_alloc(n + 1);
	if (!buffer)
		return -1;

	vsnprintf(buffer, n + 1, fmt, args);

	lkl_host_ops.print(buffer, n);
	lkl_host_ops.mem_free(buffer);

	return n;
}

int lkl_printf(const char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = lkl_vprintf(fmt, args);
	va_end(args);

	return n;
}

void lkl_bug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	lkl_vprintf(fmt, args);
	va_end(args);

	lkl_host_ops.panic();
}

int lkl_sysctl(const char *path, const char *value)
{
	int ret;
	int fd;
	char *delim, *p;
	char full_path[256];

	lkl_mount_fs("proc");

	snprintf(full_path, sizeof(full_path), "/proc/sys/%s", path);
	p = full_path;
	while ((delim = strstr(p, "."))) {
		*delim = '/';
		p = delim + 1;
	}

	fd = lkl_sys_open(full_path, LKL_O_WRONLY | LKL_O_CREAT, 0);
	if (fd < 0) {
		lkl_printf("lkl_sys_open %s: %s\n",
			   full_path, lkl_strerror(fd));
		return -1;
	}
	ret = lkl_sys_write(fd, value, strlen(value));
	if (ret < 0) {
		lkl_printf("lkl_sys_write %s: %s\n",
			full_path, lkl_strerror(fd));
	}

	lkl_sys_close(fd);

	return 0;
}

/* Configure sysctl parameters as the form of "key=value;key=value;..." */
void lkl_sysctl_parse_write(const char *sysctls)
{
	char *saveptr = NULL, *token = NULL;
	char *key = NULL, *value = NULL;
	char strings[256];
	int ret = 0;

	strcpy(strings, sysctls);
	for (token = strtok_r(strings, ";", &saveptr); token;
	     token = strtok_r(NULL, ";", &saveptr)) {
		key = strtok(token, "=");
		value = strtok(NULL, "=");
		ret = lkl_sysctl(key, value);
		if (ret) {
			lkl_printf("Failed to configure sysctl entries: %s\n",
				   lkl_strerror(ret));
			return;
		}
	}
}
