/*
 *  sync / sw_sync abstraction
 *  Copyright 2015-2016 Collabora Ltd.
 *
 *  Based on the implementation from the Android Open Source Project,
 *
 *  Copyright 2012 Google, Inc
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *  OTHER DEALINGS IN THE SOFTWARE.
 */

#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "sync.h"
#include "sw_sync.h"

#include <linux/sync_file.h>


/* SW_SYNC ioctls */
struct sw_sync_create_fence_data {
	__u32	value;
	char	name[32];
	__s32	fence;
};

#define SW_SYNC_IOC_MAGIC		'W'
#define SW_SYNC_IOC_CREATE_FENCE	_IOWR(SW_SYNC_IOC_MAGIC, 0,\
					      struct sw_sync_create_fence_data)
#define SW_SYNC_IOC_INC			_IOW(SW_SYNC_IOC_MAGIC, 1, __u32)


int sync_wait(int fd, int timeout)
{
	struct pollfd fds;

	fds.fd = fd;
	fds.events = POLLIN | POLLERR;

	return poll(&fds, 1, timeout);
}

int sync_merge(const char *name, int fd1, int fd2)
{
	struct sync_merge_data data = {};
	int err;

	data.fd2 = fd2;
	strncpy(data.name, name, sizeof(data.name) - 1);
	data.name[sizeof(data.name) - 1] = '\0';

	err = ioctl(fd1, SYNC_IOC_MERGE, &data);
	if (err < 0)
		return err;

	return data.fence;
}

static struct sync_file_info *sync_file_info(int fd)
{
	struct sync_file_info *info;
	struct sync_fence_info *fence_info;
	int err, num_fences;

	info = calloc(1, sizeof(*info));
	if (info == NULL)
		return NULL;

	err = ioctl(fd, SYNC_IOC_FILE_INFO, info);
	if (err < 0) {
		free(info);
		return NULL;
	}

	num_fences = info->num_fences;

	if (num_fences) {
		info->flags = 0;
		info->num_fences = num_fences;

		fence_info = calloc(num_fences, sizeof(*fence_info));
		if (!fence_info) {
			free(info);
			return NULL;
		}

		info->sync_fence_info = (uint64_t)fence_info;

		err = ioctl(fd, SYNC_IOC_FILE_INFO, info);
		if (err < 0) {
			free(fence_info);
			free(info);
			return NULL;
		}
	}

	return info;
}

static void sync_file_info_free(struct sync_file_info *info)
{
	free((void *)info->sync_fence_info);
	free(info);
}

int sync_fence_size(int fd)
{
	int count;
	struct sync_file_info *info = sync_file_info(fd);

	if (!info)
		return 0;

	count = info->num_fences;

	sync_file_info_free(info);

	return count;
}

int sync_fence_count_with_status(int fd, int status)
{
	unsigned int i, count = 0;
	struct sync_fence_info *fence_info = NULL;
	struct sync_file_info *info = sync_file_info(fd);

	if (!info)
		return -1;

	fence_info = (struct sync_fence_info *)info->sync_fence_info;
	for (i = 0 ; i < info->num_fences ; i++) {
		if (fence_info[i].status == status)
			count++;
	}

	sync_file_info_free(info);

	return count;
}

int sw_sync_timeline_create(void)
{
	return open("/sys/kernel/debug/sync/sw_sync", O_RDWR);
}

int sw_sync_timeline_inc(int fd, unsigned int count)
{
	__u32 arg = count;

	return ioctl(fd, SW_SYNC_IOC_INC, &arg);
}

int sw_sync_timeline_is_valid(int fd)
{
	int status;

	if (fd == -1)
		return 0;

	status = fcntl(fd, F_GETFD, 0);
	return (status >= 0);
}

void sw_sync_timeline_destroy(int fd)
{
	if (sw_sync_timeline_is_valid(fd))
		close(fd);
}

int sw_sync_fence_create(int fd, const char *name, unsigned int value)
{
	struct sw_sync_create_fence_data data = {};
	int err;

	data.value = value;
	strncpy(data.name, name, sizeof(data.name) - 1);
	data.name[sizeof(data.name) - 1] = '\0';

	err = ioctl(fd, SW_SYNC_IOC_CREATE_FENCE, &data);
	if (err < 0)
		return err;

	return data.fence;
}

int sw_sync_fence_is_valid(int fd)
{
	/* Same code! */
	return sw_sync_timeline_is_valid(fd);
}

void sw_sync_fence_destroy(int fd)
{
	if (sw_sync_fence_is_valid(fd))
		close(fd);
}
