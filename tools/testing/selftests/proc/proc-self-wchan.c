#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd;

	fd = open("/proc/self/wchan", O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			return 2;
		return 1;
	}

	buf[0] = '\0';
	if (read(fd, buf, sizeof(buf)) != 1)
		return 1;
	if (buf[0] != '0')
		return 1;
	return 0;
}
