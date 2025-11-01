// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022-2024 Red Hat */

#include "hid_common.h"
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>

/* for older kernels */
#ifndef HIDIOCREVOKE
#define HIDIOCREVOKE	      _IOW('H', 0x0D, int) /* Revoke device access */
#endif /* HIDIOCREVOKE */

FIXTURE(hidraw) {
	struct uhid_device hid;
	int hidraw_fd;
};
static void close_hidraw(FIXTURE_DATA(hidraw) * self)
{
	if (self->hidraw_fd)
		close(self->hidraw_fd);
	self->hidraw_fd = 0;
}

FIXTURE_TEARDOWN(hidraw) {
	void *uhid_err;

	uhid_destroy(_metadata, &self->hid);

	close_hidraw(self);
	pthread_join(self->hid.tid, &uhid_err);
}
#define TEARDOWN_LOG(fmt, ...) do { \
	TH_LOG(fmt, ##__VA_ARGS__); \
	hidraw_teardown(_metadata, self, variant); \
} while (0)

FIXTURE_SETUP(hidraw)
{
	int err;

	err = setup_uhid(_metadata, &self->hid, BUS_USB, 0x0001, 0x0a37, rdesc, sizeof(rdesc));
	ASSERT_OK(err);

	self->hidraw_fd = open_hidraw(&self->hid);
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
	uhid_send_event(_metadata, &self->hid, buf, 6);

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
	uhid_send_event(_metadata, &self->hid, buf, 6);

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
	uhid_send_event(_metadata, &self->hid, buf, 6);

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
	uhid_send_event(_metadata, &self->hid, buf, 6);

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

/*
 * Test HIDIOCGRDESCSIZE ioctl to get report descriptor size
 */
TEST_F(hidraw, ioctl_rdescsize)
{
	int desc_size = 0;
	int err;

	/* call HIDIOCGRDESCSIZE ioctl */
	err = ioctl(self->hidraw_fd, HIDIOCGRDESCSIZE, &desc_size);
	ASSERT_EQ(err, 0) TH_LOG("HIDIOCGRDESCSIZE ioctl failed");

	/* verify the size matches our test report descriptor */
	ASSERT_EQ(desc_size, sizeof(rdesc))
		TH_LOG("expected size %zu, got %d", sizeof(rdesc), desc_size);
}

/*
 * Test HIDIOCGRDESC ioctl to get report descriptor data
 */
TEST_F(hidraw, ioctl_rdesc)
{
	struct hidraw_report_descriptor desc;
	int err;

	/* get the full report descriptor */
	desc.size = sizeof(rdesc);
	err = ioctl(self->hidraw_fd, HIDIOCGRDESC, &desc);
	ASSERT_EQ(err, 0) TH_LOG("HIDIOCGRDESC ioctl failed");

	/* verify the descriptor data matches our test descriptor */
	ASSERT_EQ(memcmp(desc.value, rdesc, sizeof(rdesc)), 0)
		TH_LOG("report descriptor data mismatch");
}

/*
 * Test HIDIOCGRDESC ioctl with smaller buffer size
 */
TEST_F(hidraw, ioctl_rdesc_small_buffer)
{
	struct hidraw_report_descriptor desc;
	int err;
	size_t small_size = sizeof(rdesc) / 2; /* request half the descriptor size */

	/* get partial report descriptor */
	desc.size = small_size;
	err = ioctl(self->hidraw_fd, HIDIOCGRDESC, &desc);
	ASSERT_EQ(err, 0) TH_LOG("HIDIOCGRDESC ioctl failed with small buffer");

	/* verify we got the first part of the descriptor */
	ASSERT_EQ(memcmp(desc.value, rdesc, small_size), 0)
		TH_LOG("partial report descriptor data mismatch");
}

/*
 * Test HIDIOCGRAWINFO ioctl to get device information
 */
TEST_F(hidraw, ioctl_rawinfo)
{
	struct hidraw_devinfo devinfo;
	int err;

	/* get device info */
	err = ioctl(self->hidraw_fd, HIDIOCGRAWINFO, &devinfo);
	ASSERT_EQ(err, 0) TH_LOG("HIDIOCGRAWINFO ioctl failed");

	/* verify device info matches our test setup */
	ASSERT_EQ(devinfo.bustype, BUS_USB)
		TH_LOG("expected bustype 0x03, got 0x%x", devinfo.bustype);
	ASSERT_EQ(devinfo.vendor, 0x0001)
		TH_LOG("expected vendor 0x0001, got 0x%x", devinfo.vendor);
	ASSERT_EQ(devinfo.product, 0x0a37)
		TH_LOG("expected product 0x0a37, got 0x%x", devinfo.product);
}

/*
 * Test HIDIOCGFEATURE ioctl to get feature report
 */
TEST_F(hidraw, ioctl_gfeature)
{
	__u8 buf[10] = {0};
	int err;

	/* set report ID 1 in first byte */
	buf[0] = 1;

	/* get feature report */
	err = ioctl(self->hidraw_fd, HIDIOCGFEATURE(sizeof(buf)), buf);
	ASSERT_EQ(err, sizeof(feature_data)) TH_LOG("HIDIOCGFEATURE ioctl failed, got %d", err);

	/* verify we got the expected feature data */
	ASSERT_EQ(buf[0], feature_data[0])
		TH_LOG("expected feature_data[0] = %d, got %d", feature_data[0], buf[0]);
	ASSERT_EQ(buf[1], feature_data[1])
		TH_LOG("expected feature_data[1] = %d, got %d", feature_data[1], buf[1]);
}

/*
 * Test HIDIOCGFEATURE ioctl with invalid report ID
 */
TEST_F(hidraw, ioctl_gfeature_invalid)
{
	__u8 buf[10] = {0};
	int err;

	/* set invalid report ID (not 1) */
	buf[0] = 2;

	/* try to get feature report */
	err = ioctl(self->hidraw_fd, HIDIOCGFEATURE(sizeof(buf)), buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGFEATURE should have failed with invalid report ID");
	ASSERT_EQ(errno, EIO) TH_LOG("expected EIO, got errno %d", errno);
}

/*
 * Test ioctl with incorrect nr bits
 */
TEST_F(hidraw, ioctl_invalid_nr)
{
	char buf[256] = {0};
	int err;
	unsigned int bad_cmd;

	/*
	 * craft an ioctl command with wrong _IOC_NR bits
	 */
	bad_cmd = _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x00, sizeof(buf)); /* 0 is not valid */

	/* test the ioctl */
	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("ioctl read-write with wrong _IOC_NR (0) should have failed");
	ASSERT_EQ(errno, ENOTTY)
		TH_LOG("expected ENOTTY for wrong read-write _IOC_NR (0), got errno %d", errno);

	/*
	 * craft an ioctl command with wrong _IOC_NR bits
	 */
	bad_cmd = _IOC(_IOC_READ, 'H', 0x00, sizeof(buf)); /* 0 is not valid */

	/* test the ioctl */
	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("ioctl read-only with wrong _IOC_NR (0) should have failed");
	ASSERT_EQ(errno, ENOTTY)
		TH_LOG("expected ENOTTY for wrong read-only _IOC_NR (0), got errno %d", errno);

	/* also test with bigger number */
	bad_cmd = _IOC(_IOC_READ, 'H', 0x42, sizeof(buf)); /* 0x42 is not valid as well */

	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("ioctl read-only with wrong _IOC_NR (0x42) should have failed");
	ASSERT_EQ(errno, ENOTTY)
		TH_LOG("expected ENOTTY for wrong read-only _IOC_NR (0x42), got errno %d", errno);

	/* also test with bigger number: 0x42 is not valid as well */
	bad_cmd = _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x42, sizeof(buf));

	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("ioctl read-write with wrong _IOC_NR (0x42) should have failed");
	ASSERT_EQ(errno, ENOTTY)
		TH_LOG("expected ENOTTY for wrong read-write _IOC_NR (0x42), got errno %d", errno);
}

/*
 * Test ioctl with incorrect type bits
 */
TEST_F(hidraw, ioctl_invalid_type)
{
	char buf[256] = {0};
	int err;
	unsigned int bad_cmd;

	/*
	 * craft an ioctl command with wrong _IOC_TYPE bits
	 */
	bad_cmd = _IOC(_IOC_WRITE|_IOC_READ, 'I', 0x01, sizeof(buf)); /* 'I' should be 'H' */

	/* test the ioctl */
	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("ioctl with wrong _IOC_TYPE (I) should have failed");
	ASSERT_EQ(errno, EINVAL) TH_LOG("expected EINVAL for wrong _IOC_NR, got errno %d", errno);
}

/*
 * Test HIDIOCGFEATURE ioctl with incorrect _IOC_DIR bits
 */
TEST_F(hidraw, ioctl_gfeature_invalid_dir)
{
	__u8 buf[10] = {0};
	int err;
	unsigned int bad_cmd;

	/* set report ID 1 in first byte */
	buf[0] = 1;

	/*
	 * craft an ioctl command with wrong _IOC_DIR bits
	 * HIDIOCGFEATURE should have _IOC_WRITE|_IOC_READ, let's use only _IOC_WRITE
	 */
	bad_cmd = _IOC(_IOC_WRITE, 'H', 0x07, sizeof(buf)); /* should be _IOC_WRITE|_IOC_READ */

	/* try to get feature report with wrong direction bits */
	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGFEATURE with wrong _IOC_DIR should have failed");
	ASSERT_EQ(errno, EINVAL) TH_LOG("expected EINVAL for wrong _IOC_DIR, got errno %d", errno);

	/* also test with only _IOC_READ */
	bad_cmd = _IOC(_IOC_READ, 'H', 0x07, sizeof(buf)); /* should be _IOC_WRITE|_IOC_READ */

	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGFEATURE with wrong _IOC_DIR should have failed");
	ASSERT_EQ(errno, EINVAL) TH_LOG("expected EINVAL for wrong _IOC_DIR, got errno %d", errno);
}

/*
 * Test read-only ioctl with incorrect _IOC_DIR bits
 */
TEST_F(hidraw, ioctl_readonly_invalid_dir)
{
	char buf[256] = {0};
	int err;
	unsigned int bad_cmd;

	/*
	 * craft an ioctl command with wrong _IOC_DIR bits
	 * HIDIOCGRAWNAME should have _IOC_READ, let's use _IOC_WRITE
	 */
	bad_cmd = _IOC(_IOC_WRITE, 'H', 0x04, sizeof(buf)); /* should be _IOC_READ */

	/* try to get device name with wrong direction bits */
	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGRAWNAME with wrong _IOC_DIR should have failed");
	ASSERT_EQ(errno, EINVAL) TH_LOG("expected EINVAL for wrong _IOC_DIR, got errno %d", errno);

	/* also test with _IOC_WRITE|_IOC_READ */
	bad_cmd = _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x04, sizeof(buf)); /* should be only _IOC_READ */

	err = ioctl(self->hidraw_fd, bad_cmd, buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGRAWNAME with wrong _IOC_DIR should have failed");
	ASSERT_EQ(errno, EINVAL) TH_LOG("expected EINVAL for wrong _IOC_DIR, got errno %d", errno);
}

/*
 * Test HIDIOCSFEATURE ioctl to set feature report
 */
TEST_F(hidraw, ioctl_sfeature)
{
	__u8 buf[10] = {0};
	int err;

	/* prepare feature report data */
	buf[0] = 1; /* report ID */
	buf[1] = 0x42;
	buf[2] = 0x24;

	/* set feature report */
	err = ioctl(self->hidraw_fd, HIDIOCSFEATURE(3), buf);
	ASSERT_EQ(err, 3) TH_LOG("HIDIOCSFEATURE ioctl failed, got %d", err);

	/*
	 * Note: The uhid mock doesn't validate the set report data,
	 * so we just verify the ioctl succeeds
	 */
}

/*
 * Test HIDIOCGINPUT ioctl to get input report
 */
TEST_F(hidraw, ioctl_ginput)
{
	__u8 buf[10] = {0};
	int err;

	/* set report ID 1 in first byte */
	buf[0] = 1;

	/* get input report */
	err = ioctl(self->hidraw_fd, HIDIOCGINPUT(sizeof(buf)), buf);
	ASSERT_EQ(err, sizeof(feature_data)) TH_LOG("HIDIOCGINPUT ioctl failed, got %d", err);

	/* verify we got the expected input data */
	ASSERT_EQ(buf[0], feature_data[0])
		TH_LOG("expected feature_data[0] = %d, got %d", feature_data[0], buf[0]);
	ASSERT_EQ(buf[1], feature_data[1])
		TH_LOG("expected feature_data[1] = %d, got %d", feature_data[1], buf[1]);
}

/*
 * Test HIDIOCGINPUT ioctl with invalid report ID
 */
TEST_F(hidraw, ioctl_ginput_invalid)
{
	__u8 buf[10] = {0};
	int err;

	/* set invalid report ID (not 1) */
	buf[0] = 2;

	/* try to get input report */
	err = ioctl(self->hidraw_fd, HIDIOCGINPUT(sizeof(buf)), buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGINPUT should have failed with invalid report ID");
	ASSERT_EQ(errno, EIO) TH_LOG("expected EIO, got errno %d", errno);
}

/*
 * Test HIDIOCSINPUT ioctl to set input report
 */
TEST_F(hidraw, ioctl_sinput)
{
	__u8 buf[10] = {0};
	int err;

	/* prepare input report data */
	buf[0] = 1; /* report ID */
	buf[1] = 0x55;
	buf[2] = 0xAA;

	/* set input report */
	err = ioctl(self->hidraw_fd, HIDIOCSINPUT(3), buf);
	ASSERT_EQ(err, 3) TH_LOG("HIDIOCSINPUT ioctl failed, got %d", err);

	/*
	 * Note: The uhid mock doesn't validate the set report data,
	 * so we just verify the ioctl succeeds
	 */
}

/*
 * Test HIDIOCGOUTPUT ioctl to get output report
 */
TEST_F(hidraw, ioctl_goutput)
{
	__u8 buf[10] = {0};
	int err;

	/* set report ID 1 in first byte */
	buf[0] = 1;

	/* get output report */
	err = ioctl(self->hidraw_fd, HIDIOCGOUTPUT(sizeof(buf)), buf);
	ASSERT_EQ(err, sizeof(feature_data)) TH_LOG("HIDIOCGOUTPUT ioctl failed, got %d", err);

	/* verify we got the expected output data */
	ASSERT_EQ(buf[0], feature_data[0])
		TH_LOG("expected feature_data[0] = %d, got %d", feature_data[0], buf[0]);
	ASSERT_EQ(buf[1], feature_data[1])
		TH_LOG("expected feature_data[1] = %d, got %d", feature_data[1], buf[1]);
}

/*
 * Test HIDIOCGOUTPUT ioctl with invalid report ID
 */
TEST_F(hidraw, ioctl_goutput_invalid)
{
	__u8 buf[10] = {0};
	int err;

	/* set invalid report ID (not 1) */
	buf[0] = 2;

	/* try to get output report */
	err = ioctl(self->hidraw_fd, HIDIOCGOUTPUT(sizeof(buf)), buf);
	ASSERT_LT(err, 0) TH_LOG("HIDIOCGOUTPUT should have failed with invalid report ID");
	ASSERT_EQ(errno, EIO) TH_LOG("expected EIO, got errno %d", errno);
}

/*
 * Test HIDIOCSOUTPUT ioctl to set output report
 */
TEST_F(hidraw, ioctl_soutput)
{
	__u8 buf[10] = {0};
	int err;

	/* prepare output report data */
	buf[0] = 1; /* report ID */
	buf[1] = 0x33;
	buf[2] = 0xCC;

	/* set output report */
	err = ioctl(self->hidraw_fd, HIDIOCSOUTPUT(3), buf);
	ASSERT_EQ(err, 3) TH_LOG("HIDIOCSOUTPUT ioctl failed, got %d", err);

	/*
	 * Note: The uhid mock doesn't validate the set report data,
	 * so we just verify the ioctl succeeds
	 */
}

/*
 * Test HIDIOCGRAWNAME ioctl to get device name string
 */
TEST_F(hidraw, ioctl_rawname)
{
	char name[256] = {0};
	char expected_name[64];
	int err;

	/* get device name */
	err = ioctl(self->hidraw_fd, HIDIOCGRAWNAME(sizeof(name)), name);
	ASSERT_GT(err, 0) TH_LOG("HIDIOCGRAWNAME ioctl failed, got %d", err);

	/* construct expected name based on device id */
	snprintf(expected_name, sizeof(expected_name), "test-uhid-device-%d", self->hid.dev_id);

	/* verify the name matches expected pattern */
	ASSERT_EQ(strcmp(name, expected_name), 0)
		TH_LOG("expected name '%s', got '%s'", expected_name, name);
}

/*
 * Test HIDIOCGRAWPHYS ioctl to get device physical address string
 */
TEST_F(hidraw, ioctl_rawphys)
{
	char phys[256] = {0};
	char expected_phys[64];
	int err;

	/* get device physical address */
	err = ioctl(self->hidraw_fd, HIDIOCGRAWPHYS(sizeof(phys)), phys);
	ASSERT_GT(err, 0) TH_LOG("HIDIOCGRAWPHYS ioctl failed, got %d", err);

	/* construct expected phys based on device id */
	snprintf(expected_phys, sizeof(expected_phys), "%d", self->hid.dev_id);

	/* verify the phys matches expected value */
	ASSERT_EQ(strcmp(phys, expected_phys), 0)
		TH_LOG("expected phys '%s', got '%s'", expected_phys, phys);
}

/*
 * Test HIDIOCGRAWUNIQ ioctl to get device unique identifier string
 */
TEST_F(hidraw, ioctl_rawuniq)
{
	char uniq[256] = {0};
	int err;

	/* get device unique identifier */
	err = ioctl(self->hidraw_fd, HIDIOCGRAWUNIQ(sizeof(uniq)), uniq);
	ASSERT_GE(err, 0) TH_LOG("HIDIOCGRAWUNIQ ioctl failed, got %d", err);

	/* uniq is typically empty in our test setup */
	ASSERT_EQ(strlen(uniq), 0) TH_LOG("expected empty uniq, got '%s'", uniq);
}

/*
 * Test device string ioctls with small buffer sizes
 */
TEST_F(hidraw, ioctl_strings_small_buffer)
{
	char small_buf[8] = {0};
	char expected_name[64];
	int err;

	/* test HIDIOCGRAWNAME with small buffer */
	err = ioctl(self->hidraw_fd, HIDIOCGRAWNAME(sizeof(small_buf)), small_buf);
	ASSERT_EQ(err, sizeof(small_buf))
		TH_LOG("HIDIOCGRAWNAME with small buffer failed, got %d", err);

	/* construct expected truncated name */
	snprintf(expected_name, sizeof(expected_name), "test-uhid-device-%d", self->hid.dev_id);

	/* verify we got truncated name (first 8 chars, no null terminator guaranteed) */
	ASSERT_EQ(strncmp(small_buf, expected_name, sizeof(small_buf)), 0)
		TH_LOG("expected truncated name to match first %zu chars", sizeof(small_buf));

	/* Note: hidraw driver doesn't guarantee null termination when buffer is too small */
}

int main(int argc, char **argv)
{
	return test_harness_run(argc, argv);
}
