// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022-2024 Red Hat */

#include "hid_common.h"

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

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
