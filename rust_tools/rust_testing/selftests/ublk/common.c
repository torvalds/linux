// SPDX-License-Identifier: GPL-2.0

#include "kublk.h"

void backing_file_tgt_deinit(struct ublk_dev *dev)
{
	int i;

	for (i = 1; i < dev->nr_fds; i++) {
		fsync(dev->fds[i]);
		close(dev->fds[i]);
	}
}

int backing_file_tgt_init(struct ublk_dev *dev)
{
	int fd, i;

	assert(dev->nr_fds == 1);

	for (i = 0; i < dev->tgt.nr_backing_files; i++) {
		char *file = dev->tgt.backing_file[i];
		unsigned long bytes;
		struct stat st;

		ublk_dbg(UBLK_DBG_DEV, "%s: file %d: %s\n", __func__, i, file);

		fd = open(file, O_RDWR | O_DIRECT);
		if (fd < 0) {
			ublk_err("%s: backing file %s can't be opened: %s\n",
					__func__, file, strerror(errno));
			return -EBADF;
		}

		if (fstat(fd, &st) < 0) {
			close(fd);
			return -EBADF;
		}

		if (S_ISREG(st.st_mode))
			bytes = st.st_size;
		else if (S_ISBLK(st.st_mode)) {
			if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
				return -1;
		} else {
			return -EINVAL;
		}

		dev->tgt.backing_file_size[i] = bytes;
		dev->fds[dev->nr_fds] = fd;
		dev->nr_fds += 1;
	}

	return 0;
}
