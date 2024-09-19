// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022-2024 Red Hat */

#include "hid_common.h"

/* for older kernels */
#ifndef HIDIOCREVOKE
#define HIDIOCREVOKE	      _IOW('H', 0x0D, int) /* Revoke device access */
#endif /* HIDIOCREVOKE */

FIXTURE(hidraw) {
	int dev_id;
	int uhid_fd;
	int hidraw_fd;
	int hid_id;
	pthread_t tid;
};
static void close_hidraw(FIXTURE_DATA(hidraw) * self)
{
	if (self->hidraw_fd)
		close(self->hidraw_fd);
	self->hidraw_fd = 0;
}

FIXTURE_TEARDOWN(hidraw) {
	void *uhid_err;

	uhid_destroy(_metadata, self->uhid_fd);

	close_hidraw(self);
	pthread_join(self->tid, &uhid_err);
}
#define TEARDOWN_LOG(fmt, ...) do { \
	TH_LOG(fmt, ##__VA_ARGS__); \
	hidraw_teardown(_metadata, self, variant); \
} while (0)

FIXTURE_SETUP(hidraw)
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

	self->hidraw_fd = open_hidraw(self->dev_id);
	ASSERT_GE(self->hidraw_fd, 0) TH_LOG("open_hidraw");
}

/*
 * A simple test to see if the fixture is working fine.
 * If this fails, none of the other tests will pass.
 */
TEST_F(hidraw, test_create_uhid)
{
}

/*
 * Inject one event in the uhid device,
 * check that we get the same data through hidraw
 */
TEST_F(hidraw, raw_event)
{
	__u8 buf[10] = {0};
	int err;

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[1], 42);
}

/*
 * After initial opening/checks of hidraw, revoke the hidraw
 * node and check that we can not read any more data.
 */
TEST_F(hidraw, raw_event_revoked)
{
	__u8 buf[10] = {0};
	int err;

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
	ASSERT_EQ(buf[0], 1);
	ASSERT_EQ(buf[1], 42);

	/* call the revoke ioctl */
	err = ioctl(self->hidraw_fd, HIDIOCREVOKE, NULL);
	ASSERT_OK(err) TH_LOG("couldn't revoke the hidraw fd");

	/* inject one other event */
	buf[0] = 1;
	buf[1] = 43;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	/* read the data from hidraw */
	memset(buf, 0, sizeof(buf));
	err = read(self->hidraw_fd, buf, sizeof(buf));
	ASSERT_EQ(err, -1) TH_LOG("read_hidraw");
	ASSERT_EQ(errno, ENODEV) TH_LOG("unexpected error code while reading the hidraw node: %d",
					errno);
}

/*
 * Revoke the hidraw node and check that we can not do any ioctl.
 */
TEST_F(hidraw, ioctl_revoked)
{
	int err, desc_size = 0;

	/* call the revoke ioctl */
	err = ioctl(self->hidraw_fd, HIDIOCREVOKE, NULL);
	ASSERT_OK(err) TH_LOG("couldn't revoke the hidraw fd");

	/* do an ioctl */
	err = ioctl(self->hidraw_fd, HIDIOCGRDESCSIZE, &desc_size);
	ASSERT_EQ(err, -1) TH_LOG("ioctl_hidraw");
	ASSERT_EQ(errno, ENODEV) TH_LOG("unexpected error code while doing an ioctl: %d",
					errno);
}

/*
 * Setup polling of the fd, and check that revoke works properly.
 */
TEST_F(hidraw, poll_revoked)
{
	struct pollfd pfds[1];
	__u8 buf[10] = {0};
	int err, ready;

	/* setup polling */
	pfds[0].fd = self->hidraw_fd;
	pfds[0].events = POLLIN;

	/* inject one event */
	buf[0] = 1;
	buf[1] = 42;
	uhid_send_event(_metadata, self->uhid_fd, buf, 6);

	while (true) {
		ready = poll(pfds, 1, 5000);
		ASSERT_EQ(ready, 1) TH_LOG("poll return value");

		if (pfds[0].revents & POLLIN) {
			memset(buf, 0, sizeof(buf));
			err = read(self->hidraw_fd, buf, sizeof(buf));
			ASSERT_EQ(err, 6) TH_LOG("read_hidraw");
			ASSERT_EQ(buf[0], 1);
			ASSERT_EQ(buf[1], 42);

			/* call the revoke ioctl */
			err = ioctl(self->hidraw_fd, HIDIOCREVOKE, NULL);
			ASSERT_OK(err) TH_LOG("couldn't revoke the hidraw fd");
		} else {
			break;
		}
	}

	ASSERT_TRUE(pfds[0].revents & POLLHUP);
}

/*
 * After initial opening/checks of hidraw, revoke the hidraw
 * node and check that we can not read any more data.
 */
TEST_F(hidraw, write_event_revoked)
{
	struct timespec time_to_wait;
	__u8 buf[10] = {0};
	int err;

	/* inject one event from hidraw */
	buf[0] = 1; /* report ID */
	buf[1] = 2;
	buf[2] = 42;

	pthread_mutex_lock(&uhid_output_mtx);

	memset(output_report, 0, sizeof(output_report));
	clock_gettime(CLOCK_REALTIME, &time_to_wait);
	time_to_wait.tv_sec += 2;

	err = write(self->hidraw_fd, buf, 3);
	ASSERT_EQ(err, 3) TH_LOG("unexpected error while writing to hidraw node: %d", err);

	err = pthread_cond_timedwait(&uhid_output_cond, &uhid_output_mtx, &time_to_wait);
	ASSERT_OK(err) TH_LOG("error while calling waiting for the condition");

	ASSERT_EQ(output_report[0], 1);
	ASSERT_EQ(output_report[1], 2);
	ASSERT_EQ(output_report[2], 42);

	/* call the revoke ioctl */
	err = ioctl(self->hidraw_fd, HIDIOCREVOKE, NULL);
	ASSERT_OK(err) TH_LOG("couldn't revoke the hidraw fd");

	/* inject one other event */
	buf[0] = 1;
	buf[1] = 43;
	err = write(self->hidraw_fd, buf, 3);
	ASSERT_LT(err, 0) TH_LOG("unexpected success while writing to hidraw node: %d", err);
	ASSERT_EQ(errno, ENODEV) TH_LOG("unexpected error code while writing to hidraw node: %d",
					errno);

	pthread_mutex_unlock(&uhid_output_mtx);
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
