// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red Hat */
#include "hid.skel.h"

#include "../kselftest_harness.h"

#include <bpf/bpf.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <linux/hidraw.h>
#include <linux/uhid.h>

#define SHOW_UHID_DEBUG 0

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

struct attach_prog_args {
	int prog_fd;
	unsigned int hid;
	int retval;
	int insert_head;
};

struct hid_hw_request_syscall_args {
	__u8 data[10];
	unsigned int hid;
	int retval;
	size_t size;
	enum hid_report_type type;
	__u8 request_type;
};

#define ASSERT_OK(data) ASSERT_FALSE(data)
#define ASSERT_OK_PTR(ptr) ASSERT_NE(NULL, ptr)

#define UHID_LOG(fmt, ...) do { \
	if (SHOW_UHID_DEBUG) \
		TH_LOG(fmt, ##__VA_ARGS__); \
} while (0)

static pthread_mutex_t uhid_started_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uhid_started = PTHREAD_COND_INITIALIZER;

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

static int uhid_create(struct __test_metadata *_metadata, int fd, int rand_nb)
{
	struct uhid_event ev;
	char buf[25];

	sprintf(buf, "test-uhid-device-%d", rand_nb);

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_CREATE;
	strcpy((char *)ev.u.create.name, buf);
	ev.u.create.rd_data = rdesc;
	ev.u.create.rd_size = sizeof(rdesc);
	ev.u.create.bus = BUS_USB;
	ev.u.create.vendor = 0x0001;
	ev.u.create.product = 0x0a37;
	ev.u.create.version = 0;
	ev.u.create.country = 0;

	sprintf(buf, "%d", rand_nb);
	strcpy((char *)ev.u.create.phys, buf);

	return uhid_write(_metadata, fd, &ev);
}

static void uhid_destroy(struct __test_metadata *_metadata, int fd)
{
	struct uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_DESTROY;

	uhid_write(_metadata, fd, &ev);
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

static int uhid_send_event(struct __test_metadata *_metadata, int fd, __u8 *buf, size_t size)
{
	struct uhid_event ev;

	if (size > sizeof(ev.u.input.data))
		return -E2BIG;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_INPUT2;
	ev.u.input2.size = size;

	memcpy(ev.u.input2.data, buf, size);

	return uhid_write(_metadata, fd, &ev);
}

static int setup_uhid(struct __test_metadata *_metadata, int rand_nb)
{
	int fd;
	const char *path = "/dev/uhid";
	int ret;

	fd = open(path, O_RDWR | O_CLOEXEC);
	ASSERT_GE(fd, 0) TH_LOG("open uhid-cdev failed; %d", fd);

	ret = uhid_create(_metadata, fd, rand_nb);
	ASSERT_EQ(0, ret) {
		TH_LOG("create uhid device failed: %d", ret);
		close(fd);
	}

	return fd;
}

static bool match_sysfs_device(int dev_id, const char *workdir, struct dirent *dir)
{
	const char *target = "0003:0001:0A37.*";
	char phys[512];
	char uevent[1024];
	char temp[512];
	int fd, nread;
	bool found = false;

	if (fnmatch(target, dir->d_name, 0))
		return false;

	/* we found the correct VID/PID, now check for phys */
	sprintf(uevent, "%s/%s/uevent", workdir, dir->d_name);

	fd = open(uevent, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return false;

	sprintf(phys, "PHYS=%d", dev_id);

	nread = read(fd, temp, ARRAY_SIZE(temp));
	if (nread > 0 && (strstr(temp, phys)) != NULL)
		found = true;

	close(fd);

	return found;
}

static int get_hid_id(int dev_id)
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
				if (!match_sysfs_device(dev_id, workdir, dir))
					continue;

				str_id = dir->d_name + sizeof("0003:0001:0A37.");
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

static int get_hidraw(int dev_id)
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
			if (!match_sysfs_device(dev_id, workdir, dir))
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

static int open_hidraw(int dev_id)
{
	int hidraw_number;
	char hidraw_path[64] = { 0 };

	hidraw_number = get_hidraw(dev_id);
	if (hidraw_number < 0)
		return hidraw_number;

	/* open hidraw node to check the other side of the pipe */
	sprintf(hidraw_path, "/dev/hidraw%d", hidraw_number);
	return open(hidraw_path, O_RDWR | O_NONBLOCK);
}

FIXTURE(hid_bpf) {
	int dev_id;
	int uhid_fd;
	int hidraw_fd;
	int hid_id;
	pthread_t tid;
	struct hid *skel;
	int hid_links[3]; /* max number of programs loaded in a single test */
};
static void detach_bpf(FIXTURE_DATA(hid_bpf) * self)
{
	int i;

	if (self->hidraw_fd)
		close(self->hidraw_fd);
	self->hidraw_fd = 0;

	for (i = 0; i < ARRAY_SIZE(self->hid_links); i++) {
		if (self->hid_links[i])
			close(self->hid_links[i]);
	}

	hid__destroy(self->skel);
	self->skel = NULL;
}

FIXTURE_TEARDOWN(hid_bpf) {
	void *uhid_err;

	uhid_destroy(_metadata, self->uhid_fd);

	detach_bpf(self);
	pthread_join(self->tid, &uhid_err);
}
#define TEARDOWN_LOG(fmt, ...) do { \
	TH_LOG(fmt, ##__VA_ARGS__); \
	hid_bpf_teardown(_metadata, self, variant); \
} while (0)

FIXTURE_SETUP(hid_bpf)
{
	time_t t;
	int err;

	/* initialize random number generator */
	srand((unsigned int)time(&t));

	self->dev_id = rand() % 1024;

	self->uhid_fd = setup_uhid(_metadata, self->dev_id);

	/* locate the uev, self, variant);ent file of the created device */
	self->hid_id = get_hid_id(self->dev_id);
	ASSERT_GT(self->hid_id, 0)
		TEARDOWN_LOG("Could not locate uhid device id: %d", self->hid_id);

	err = uhid_start_listener(_metadata, &self->tid, self->uhid_fd);
	ASSERT_EQ(0, err) TEARDOWN_LOG("could not start udev listener: %d", err);
}

struct test_program {
	const char *name;
	int insert_head;
};
#define LOAD_PROGRAMS(progs) \
	load_programs(progs, ARRAY_SIZE(progs), _metadata, self, variant)
#define LOAD_BPF \
	load_programs(NULL, 0, _metadata, self, variant)
static void load_programs(const struct test_program programs[],
			  const size_t progs_count,
			  struct __test_metadata *_metadata,
			  FIXTURE_DATA(hid_bpf) * self,
			  const FIXTURE_VARIANT(hid_bpf) * variant)
{
	int attach_fd, err = -EINVAL;
	struct attach_prog_args args = {
		.retval = -1,
	};
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, tattr,
			    .ctx_in = &args,
			    .ctx_size_in = sizeof(args),
	);

	ASSERT_LE(progs_count, ARRAY_SIZE(self->hid_links))
		TH_LOG("too many programs are to be loaded");

	/* open the bpf file */
	self->skel = hid__open();
	ASSERT_OK_PTR(self->skel) TEARDOWN_LOG("Error while calling hid__open");

	for (int i = 0; i < progs_count; i++) {
		struct bpf_program *prog;

		prog = bpf_object__find_program_by_name(*self->skel->skeleton->obj,
							programs[i].name);
		ASSERT_OK_PTR(prog) TH_LOG("can not find program by name '%s'", programs[i].name);

		bpf_program__set_autoload(prog, true);
	}

	err = hid__load(self->skel);
	ASSERT_OK(err) TH_LOG("hid_skel_load failed: %d", err);

	attach_fd = bpf_program__fd(self->skel->progs.attach_prog);
	ASSERT_GE(attach_fd, 0) TH_LOG("locate attach_prog: %d", attach_fd);

	for (int i = 0; i < progs_count; i++) {
		struct bpf_program *prog;

		prog = bpf_object__find_program_by_name(*self->skel->skeleton->obj,
							programs[i].name);
		ASSERT_OK_PTR(prog) TH_LOG("can not find program by name '%s'", programs[i].name);

		args.prog_fd = bpf_program__fd(prog);
		args.hid = self->hid_id;
		args.insert_head = programs[i].insert_head;
		err = bpf_prog_test_run_opts(attach_fd, &tattr);
		ASSERT_GE(args.retval, 0)
			TH_LOG("attach_hid(%s): %d", programs[i].name, args.retval);

		self->hid_links[i] = args.retval;
	}

	self->hidraw_fd = open_hidraw(self->dev_id);
	ASSERT_GE(self->hidraw_fd, 0) TH_LOG("open_hidraw");
}

/*
 * A simple test to see if the fixture is working fine.
 * If this fails, none of the other tests will pass.
 */
TEST_F(hid_bpf, test_create_uhid)
{
}

/*
 * Attach hid_first_event to the given uhid device,
 * retrieve and open the matching hidraw node,
 * inject one event in the uhid device,
 * check that the program sees it and can change the data
 */
TEST_F(hid_bpf, raw_event)
{
	const struct test_program progs[] = {
		{ .name = "hid_first_event" },
	};
	__u8 buf[10] = {0};
	int err;

	LOAD_PROGRAMS(progs);

	/* check that the program is correctly loaded */
	ASSERT_EQ(self->skel->data->callback_check, 52) TH_LOG("callback_check1");
	ASSERT_EQ(self->skel->data->callback2_check, 52) TH_LOG("callback2_check1");

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* check that hid_first_event() was executed */
	ASSERT_EQ(self->skel->data->callback_check, 42) TH_LOG("callback_check1");

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[2], 47);

	/* inject another event */
	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	buf[1] = 47;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* check that hid_first_event() was executed */
	ASSERT_EQ(self->skel->data->callback_check, 47) TH_LOG("callback_check1");

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[2], 52);
}

/*
 * Ensures that we can attach/detach programs
 */
TEST_F(hid_bpf, test_attach_detach)
{
	const struct test_program progs[] = {
		{ .name = "hid_first_event" },
		{ .name = "hid_second_event" },
	};
	__u8 buf[10] = {0};
	int err, link;

	LOAD_PROGRAMS(progs);

	link = self->hid_links[0];
	ASSERT_GT(link, 0) TH_LOG("HID-BPF link not created");

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[2], 47);

	/* make sure both programs are run */
	ASSERT_EQ(buf[3], 52);

	/* pin the first program and immediately unpin it */
#define PIN_PATH "/sys/fs/bpf/hid_first_event"
	err = bpf_obj_pin(link, PIN_PATH);
	ASSERT_OK(err) TH_LOG("error while calling bpf_obj_pin");
	remove(PIN_PATH);
#undef PIN_PATH
	usleep(100000);

	/* detach the program */
	detach_bpf(self);

	self->hidraw_fd = open_hidraw(self->dev_id);
	ASSERT_GE(self->hidraw_fd, 0) TH_LOG("open_hidraw");

	/* inject another event */
	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	buf[1] = 47;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw_no_bpf");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[1], 47);
	ASSERT_EQ(buf[2], 0);
	ASSERT_EQ(buf[3], 0);

	/* re-attach our program */

	LOAD_PROGRAMS(progs);

	/* inject one event */
	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[2], 47);
	ASSERT_EQ(buf[3], 52);
}

/*
 * Attach hid_change_report_id to the given uhid device,
 * retrieve and open the matching hidraw node,
 * inject one event in the uhid device,
 * check that the program sees it and can change the data
 */
TEST_F(hid_bpf, test_hid_change_report)
{
	const struct test_program progs[] = {
		{ .name = "hid_change_report_id" },
	};
	__u8 buf[10] = {0};
	int err;

	LOAD_PROGRAMS(progs);

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 9) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 2);
	ASSERT_EQ(buf[1], 42);
	ASSERT_EQ(buf[2], 0) TH_LOG("leftovers_from_previous_test");
}

/*
 * Attach hid_user_raw_request to the given uhid device,
 * call the bpf program from userspace
 * check that the program is called and does the expected.
 */
TEST_F(hid_bpf, test_hid_user_raw_request_call)
{
	struct hid_hw_request_syscall_args args = {
		.retval = -1,
		.type = HID_FEATURE_REPORT,
		.request_type = HID_REQ_GET_REPORT,
		.size = 10,
	};
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, tattrs,
			    .ctx_in = &args,
			    .ctx_size_in = sizeof(args),
	);
	int err, prog_fd;

	LOAD_BPF;

	args.hid = self->hid_id;
	args.data[0] = 1; /* report ID */

	prog_fd = bpf_program__fd(self->skel->progs.hid_user_raw_request);

	err = bpf_prog_test_run_opts(prog_fd, &tattrs);
	ASSERT_OK(err) TH_LOG("error while calling bpf_prog_test_run_opts");

	ASSERT_EQ(args.retval, 2);

	ASSERT_EQ(args.data[1], 2);
}

/*
 * Attach hid_insert{0,1,2} to the given uhid device,
 * retrieve and open the matching hidraw node,
 * inject one event in the uhid device,
 * check that the programs have been inserted in the correct order.
 */
TEST_F(hid_bpf, test_hid_attach_flags)
{
	const struct test_program progs[] = {
		{
			.name = "hid_test_insert2",
			.insert_head = 0,
		},
		{
			.name = "hid_test_insert1",
			.insert_head = 1,
		},
		{
			.name = "hid_test_insert3",
			.insert_head = 0,
		},
	};
	__u8 buf[10] = {0};
	int err;

	LOAD_PROGRAMS(progs);

	/* inject one event */
	buf[0] = 1;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[1], 1);
	ASSERT_EQ(buf[2], 2);
	ASSERT_EQ(buf[3], 3);
}

/*
 * Attach hid_rdesc_fixup to the given uhid device,
 * retrieve and open the matching hidraw node,
 * check that the hidraw report descriptor has been updated.
 */
TEST_F(hid_bpf, test_rdesc_fixup)
{
	struct hidraw_report_descriptor rpt_desc = {0};
	const struct test_program progs[] = {
		{ .name = "hid_rdesc_fixup" },
	};
	int err, desc_size;

	LOAD_PROGRAMS(progs);

	/* check that hid_rdesc_fixup() was executed */
	ASSERT_EQ(self->skel->data->callback2_check, 0x21);

	/* read the exposed report descriptor from hidraw */
	err = ioctl(self->hidraw_fd, HIDIOCGRDESCSIZE, &desc_size);
	ASSERT_GE(err, 0) TH_LOG("error while reading HIDIOCGRDESCSIZE: %d", err);

	/* ensure the new size of the rdesc is bigger than the old one */
	ASSERT_GT(desc_size, sizeof(rdesc));

	rpt_desc.size = desc_size;
	err = ioctl(self->hidraw_fd, HIDIOCGRDESC, &rpt_desc);
	ASSERT_GE(err, 0) TH_LOG("error while reading HIDIOCGRDESC: %d", err);

	ASSERT_EQ(rpt_desc.value[4], 0x42);
}

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	char buf[1024];

	if (level == LIBBPF_DEBUG)
		return 0;

	snprintf(buf, sizeof(buf), "# %s", format);

	vfprintf(stdout, buf, args);
	return 0;
}

static void __attribute__((constructor)) __constructor_order_last(void)
{
	if (!__constructor_order)
		__constructor_order = _CONSTRUCTOR_ORDER_BACKWARD;
}

int main(int argc, char **argv)
{
	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	libbpf_set_print(libbpf_print_fn);

	return test_harness_run(argc, argv);
}
