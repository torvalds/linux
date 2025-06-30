#ifndef LINUX_IO_URING_MOCK_FILE_H
#define LINUX_IO_URING_MOCK_FILE_H

#include <linux/types.h>

struct io_uring_mock_probe {
	__u64		features;
	__u64		__resv[9];
};

struct io_uring_mock_create {
	__u32		out_fd;
	__u32		flags;
	__u64		__resv[15];
};

enum {
	IORING_MOCK_MGR_CMD_PROBE,
	IORING_MOCK_MGR_CMD_CREATE,
};

#endif
