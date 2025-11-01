/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022-2024 Red Hat */

#include "../kselftest_harness.h"

#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <linux/hidraw.h>
#include <linux/uhid.h>

#define SHOW_UHID_DEBUG 0

#define min(a, b) \
	({ __typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b; })

struct uhid_device {
	int dev_id;		/* uniq (random) number to identify the device */
	int uhid_fd;
	int hid_id;		/* HID device id in the system */
	__u16 bus;
	__u32 vid;
	__u32 pid;
	pthread_t tid;		/* thread for reading uhid events */
};

static unsigned char rdesc[] = {
	0x06, 0x00, 0xff,	/* Usage Page (Vendor Defined Page 1) */
	0x09, 0x21,		/* Usage (Vendor Usage 0x21) */
	0xa1, 0x01,		/* COLLECTION (Application) */
	0x09, 0x01,			/* Usage (Vendor Usage 0x01) */
	0xa1, 0x00,			/* COLLECTION (Physical) */
	0x85, 0x02,				/* REPORT_ID (2) */
	0x19, 0x01,				/* USAGE_MINIMUM (1) */
	0x29, 0x08,				/* USAGE_MAXIMUM (3) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0xff,				/* LOGICAL_MAXIMUM (255) */
	0x95, 0x08,				/* REPORT_COUNT (8) */
	0x75, 0x08,				/* REPORT_SIZE (8) */
	0x81, 0x02,				/* INPUT (Data,Var,Abs) */
	0xc0,				/* END_COLLECTION */
	0x09, 0x01,			/* Usage (Vendor Usage 0x01) */
	0xa1, 0x00,			/* COLLECTION (Physical) */
	0x85, 0x01,				/* REPORT_ID (1) */
	0x06, 0x00, 0xff,			/* Usage Page (Vendor Defined Page 1) */
	0x19, 0x01,				/* USAGE_MINIMUM (1) */
	0x29, 0x03,				/* USAGE_MAXIMUM (3) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,				/* LOGICAL_MAXIMUM (1) */
	0x95, 0x03,				/* REPORT_COUNT (3) */
	0x75, 0x01,				/* REPORT_SIZE (1) */
	0x81, 0x02,				/* INPUT (Data,Var,Abs) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x75, 0x05,				/* REPORT_SIZE (5) */
	0x81, 0x01,				/* INPUT (Cnst,Var,Abs) */
	0x05, 0x01,				/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x30,				/* USAGE (X) */
	0x09, 0x31,				/* USAGE (Y) */
	0x15, 0x81,				/* LOGICAL_MINIMUM (-127) */
	0x25, 0x7f,				/* LOGICAL_MAXIMUM (127) */
	0x75, 0x10,				/* REPORT_SIZE (16) */
	0x95, 0x02,				/* REPORT_COUNT (2) */
	0x81, 0x06,				/* INPUT (Data,Var,Rel) */

	0x06, 0x00, 0xff,			/* Usage Page (Vendor Defined Page 1) */
	0x19, 0x01,				/* USAGE_MINIMUM (1) */
	0x29, 0x03,				/* USAGE_MAXIMUM (3) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,				/* LOGICAL_MAXIMUM (1) */
	0x95, 0x03,				/* REPORT_COUNT (3) */
	0x75, 0x01,				/* REPORT_SIZE (1) */
	0x91, 0x02,				/* Output (Data,Var,Abs) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x75, 0x05,				/* REPORT_SIZE (5) */
	0x91, 0x01,				/* Output (Cnst,Var,Abs) */

	0x06, 0x00, 0xff,			/* Usage Page (Vendor Defined Page 1) */
	0x19, 0x06,				/* USAGE_MINIMUM (6) */
	0x29, 0x08,				/* USAGE_MAXIMUM (8) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,				/* LOGICAL_MAXIMUM (1) */
	0x95, 0x03,				/* REPORT_COUNT (3) */
	0x75, 0x01,				/* REPORT_SIZE (1) */
	0xb1, 0x02,				/* Feature (Data,Var,Abs) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x75, 0x05,				/* REPORT_SIZE (5) */
	0x91, 0x01,				/* Output (Cnst,Var,Abs) */

	0xc0,				/* END_COLLECTION */
	0xc0,			/* END_COLLECTION */
};

static __u8 feature_data[] = { 1, 2 };

#define ASSERT_OK(data) ASSERT_FALSE(data)
#define ASSERT_OK_PTR(ptr) ASSERT_NE(NULL, ptr)

#define UHID_LOG(fmt, ...) do { \
	if (SHOW_UHID_DEBUG) \
		TH_LOG(fmt, ##__VA_ARGS__); \
} while (0)

static pthread_mutex_t uhid_started_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uhid_started = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t uhid_output_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uhid_output_cond = PTHREAD_COND_INITIALIZER;
static unsigned char output_report[10];

/* no need to protect uhid_stopped, only one thread accesses it */
static bool uhid_stopped;

static int uhid_write(struct __test_metadata *_metadata, int fd, const struct uhid_event *ev)
{
	ssize_t ret;

	ret = write(fd, ev, sizeof(*ev));
	if (ret < 0) {
		TH_LOG("Cannot write to uhid: %m");
		return -errno;
	} else if (ret != sizeof(*ev)) {
		TH_LOG("Wrong size written to uhid: %zd != %zu",
			ret, sizeof(ev));
		return -EFAULT;
	} else {
		return 0;
	}
}

static int uhid_create(struct __test_metadata *_metadata, int fd, int rand_nb,
		       __u16 bus, __u32 vid, __u32 pid, __u8 *rdesc,
		       size_t rdesc_size)
{
	struct uhid_event ev;
	char buf[25];

	sprintf(buf, "test-uhid-device-%d", rand_nb);

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_CREATE;
	strcpy((char *)ev.u.create.name, buf);
	ev.u.create.rd_data = rdesc;
	ev.u.create.rd_size = rdesc_size;
	ev.u.create.bus = bus;
	ev.u.create.vendor = vid;
	ev.u.create.product = pid;
	ev.u.create.version = 0;
	ev.u.create.country = 0;

	sprintf(buf, "%d", rand_nb);
	strcpy((char *)ev.u.create.phys, buf);

	return uhid_write(_metadata, fd, &ev);
}

static void uhid_destroy(struct __test_metadata *_metadata, struct uhid_device *hid)
{
	struct uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_DESTROY;

	uhid_write(_metadata, hid->uhid_fd, &ev);
}

static int uhid_event(struct __test_metadata *_metadata, int fd)
{
	struct uhid_event ev, answer;
	ssize_t ret;

	memset(&ev, 0, sizeof(ev));
	ret = read(fd, &ev, sizeof(ev));
	if (ret == 0) {
		UHID_LOG("Read HUP on uhid-cdev");
		return -EFAULT;
	} else if (ret < 0) {
		UHID_LOG("Cannot read uhid-cdev: %m");
		return -errno;
	} else if (ret != sizeof(ev)) {
		UHID_LOG("Invalid size read from uhid-dev: %zd != %zu",
			ret, sizeof(ev));
		return -EFAULT;
	}

	switch (ev.type) {
	case UHID_START:
		pthread_mutex_lock(&uhid_started_mtx);
		pthread_cond_signal(&uhid_started);
		pthread_mutex_unlock(&uhid_started_mtx);

		UHID_LOG("UHID_START from uhid-dev");
		break;
	case UHID_STOP:
		uhid_stopped = true;

		UHID_LOG("UHID_STOP from uhid-dev");
		break;
	case UHID_OPEN:
		UHID_LOG("UHID_OPEN from uhid-dev");
		break;
	case UHID_CLOSE:
		UHID_LOG("UHID_CLOSE from uhid-dev");
		break;
	case UHID_OUTPUT:
		UHID_LOG("UHID_OUTPUT from uhid-dev");

		pthread_mutex_lock(&uhid_output_mtx);
		memcpy(output_report,
		       ev.u.output.data,
		       min(ev.u.output.size, sizeof(output_report)));
		pthread_cond_signal(&uhid_output_cond);
		pthread_mutex_unlock(&uhid_output_mtx);
		break;
	case UHID_GET_REPORT:
		UHID_LOG("UHID_GET_REPORT from uhid-dev");

		answer.type = UHID_GET_REPORT_REPLY;
		answer.u.get_report_reply.id = ev.u.get_report.id;
		answer.u.get_report_reply.err = ev.u.get_report.rnum == 1 ? 0 : -EIO;
		answer.u.get_report_reply.size = sizeof(feature_data);
		memcpy(answer.u.get_report_reply.data, feature_data, sizeof(feature_data));

		uhid_write(_metadata, fd, &answer);

		break;
	case UHID_SET_REPORT:
		UHID_LOG("UHID_SET_REPORT from uhid-dev");

		answer.type = UHID_SET_REPORT_REPLY;
		answer.u.set_report_reply.id = ev.u.set_report.id;
		answer.u.set_report_reply.err = 0; /* success */

		uhid_write(_metadata, fd, &answer);
		break;
	default:
		TH_LOG("Invalid event from uhid-dev: %u", ev.type);
	}

	return 0;
}

struct uhid_thread_args {
	int fd;
	struct __test_metadata *_metadata;
};
static void *uhid_read_events_thread(void *arg)
{
	struct uhid_thread_args *args = (struct uhid_thread_args *)arg;
	struct __test_metadata *_metadata = args->_metadata;
	struct pollfd pfds[1];
	int fd = args->fd;
	int ret = 0;

	pfds[0].fd = fd;
	pfds[0].events = POLLIN;

	uhid_stopped = false;

	while (!uhid_stopped) {
		ret = poll(pfds, 1, 100);
		if (ret < 0) {
			TH_LOG("Cannot poll for fds: %m");
			break;
		}
		if (pfds[0].revents & POLLIN) {
			ret = uhid_event(_metadata, fd);
			if (ret)
				break;
		}
	}

	return (void *)(long)ret;
}

static int uhid_start_listener(struct __test_metadata *_metadata, pthread_t *tid, int uhid_fd)
{
	struct uhid_thread_args args = {
		.fd = uhid_fd,
		._metadata = _metadata,
	};
	int err;

	pthread_mutex_lock(&uhid_started_mtx);
	err = pthread_create(tid, NULL, uhid_read_events_thread, (void *)&args);
	ASSERT_EQ(0, err) {
		TH_LOG("Could not start the uhid thread: %d", err);
		pthread_mutex_unlock(&uhid_started_mtx);
		close(uhid_fd);
		return -EIO;
	}
	pthread_cond_wait(&uhid_started, &uhid_started_mtx);
	pthread_mutex_unlock(&uhid_started_mtx);

	return 0;
}

static int uhid_send_event(struct __test_metadata *_metadata, struct uhid_device *hid,
			   __u8 *buf, size_t size)
{
	struct uhid_event ev;

	if (size > sizeof(ev.u.input.data))
		return -E2BIG;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_INPUT2;
	ev.u.input2.size = size;

	memcpy(ev.u.input2.data, buf, size);

	return uhid_write(_metadata, hid->uhid_fd, &ev);
}

static bool match_sysfs_device(struct uhid_device *hid, const char *workdir, struct dirent *dir)
{
	char target[20] = "";
	char phys[512];
	char uevent[1024];
	char temp[512];
	int fd, nread;
	bool found = false;

	snprintf(target, sizeof(target), "%04X:%04X:%04X.*", hid->bus, hid->vid, hid->pid);

	if (fnmatch(target, dir->d_name, 0))
		return false;

	/* we found the correct VID/PID, now check for phys */
	sprintf(uevent, "%s/%s/uevent", workdir, dir->d_name);

	fd = open(uevent, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return false;

	sprintf(phys, "PHYS=%d", hid->dev_id);

	nread = read(fd, temp, ARRAY_SIZE(temp));
	if (nread > 0 && (strstr(temp, phys)) != NULL)
		found = true;

	close(fd);

	return found;
}

static int get_hid_id(struct uhid_device *hid)
{
	const char *workdir = "/sys/devices/virtual/misc/uhid";
	const char *str_id;
	DIR *d;
	struct dirent *dir;
	int found = -1, attempts = 3;

	/* it would be nice to be able to use nftw, but the no_alu32 target doesn't support it */

	while (found < 0 && attempts > 0) {
		attempts--;
		d = opendir(workdir);
		if (d) {
			while ((dir = readdir(d)) != NULL) {
				if (!match_sysfs_device(hid, workdir, dir))
					continue;

				str_id = dir->d_name + sizeof("0000:0000:0000.");
				found = (int)strtol(str_id, NULL, 16);

				break;
			}
			closedir(d);
		}
		if (found < 0)
			usleep(100000);
	}

	return found;
}

static int get_hidraw(struct uhid_device *hid)
{
	const char *workdir = "/sys/devices/virtual/misc/uhid";
	char sysfs[1024];
	DIR *d, *subd;
	struct dirent *dir, *subdir;
	int i, found = -1;

	/* retry 5 times in case the system is loaded */
	for (i = 5; i > 0; i--) {
		usleep(10);
		d = opendir(workdir);

		if (!d)
			continue;

		while ((dir = readdir(d)) != NULL) {
			if (!match_sysfs_device(hid, workdir, dir))
				continue;

			sprintf(sysfs, "%s/%s/hidraw", workdir, dir->d_name);

			subd = opendir(sysfs);
			if (!subd)
				continue;

			while ((subdir = readdir(subd)) != NULL) {
				if (fnmatch("hidraw*", subdir->d_name, 0))
					continue;

				found = atoi(subdir->d_name + strlen("hidraw"));
			}

			closedir(subd);

			if (found > 0)
				break;
		}
		closedir(d);
	}

	return found;
}

static int open_hidraw(struct uhid_device *hid)
{
	int hidraw_number;
	char hidraw_path[64] = { 0 };

	hidraw_number = get_hidraw(hid);
	if (hidraw_number < 0)
		return hidraw_number;

	/* open hidraw node to check the other side of the pipe */
	sprintf(hidraw_path, "/dev/hidraw%d", hidraw_number);
	return open(hidraw_path, O_RDWR | O_NONBLOCK);
}

static int setup_uhid(struct __test_metadata *_metadata, struct uhid_device *hid,
		      __u16 bus, __u32 vid, __u32 pid, const __u8 *rdesc, size_t rdesc_size)
{
	const char *path = "/dev/uhid";
	time_t t;
	int ret;

	/* initialize random number generator */
	srand((unsigned int)time(&t));

	hid->dev_id = rand() % 1024;
	hid->bus = bus;
	hid->vid = vid;
	hid->pid = pid;

	hid->uhid_fd = open(path, O_RDWR | O_CLOEXEC);
	ASSERT_GE(hid->uhid_fd, 0) TH_LOG("open uhid-cdev failed; %d", hid->uhid_fd);

	ret = uhid_create(_metadata, hid->uhid_fd, hid->dev_id, bus, vid, pid,
			  (__u8 *)rdesc, rdesc_size);
	ASSERT_EQ(0, ret) {
		TH_LOG("create uhid device failed: %d", ret);
		close(hid->uhid_fd);
		return ret;
	}

	/* locate the uevent file of the created device */
	hid->hid_id = get_hid_id(hid);
	ASSERT_GT(hid->hid_id, 0)
		TH_LOG("Could not locate uhid device id: %d", hid->hid_id);

	ret = uhid_start_listener(_metadata, &hid->tid, hid->uhid_fd);
	ASSERT_EQ(0, ret) {
		TH_LOG("could not start udev listener: %d", ret);
		close(hid->uhid_fd);
		return ret;
	}

	return 0;
}
