#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "sysfs_utils.h"
#include "usbip_common.h"

int write_sysfs_attribute(const char *attr_path, const char *new_value,
			  size_t len)
{
	int fd;
	int length;

	fd = open(attr_path, O_WRONLY);
	if (fd < 0) {
		dbg("error opening attribute %s", attr_path);
		return -1;
	}

	length = write(fd, new_value, len);
	if (length < 0) {
		dbg("error writing to attribute %s", attr_path);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}
