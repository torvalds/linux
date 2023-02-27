// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/uhid.h>
#include <linux/major.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <lkl/linux/time.h>
#include <lkl/linux/uhid.h>

#include <lkl.h>
#include <lkl_host.h>

#include "hid-fuzzer.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_EVENT_SIZE 128

#define LKL_CALL(op)	lkl_sys_##op

#define LOG(fmt, ...) \
	do { \
		if (g_log_enabled) {\
			printf(fmt, ##__VA_ARGS__); \
		} \
	} while (0)

#define LOG_BYTES(title, data, size) \
	do { \
		if (g_log_enabled) {\
			dump_bytes(title, data, size); \
		} \
	} while (0)

bool g_log_enabled = true;

void dump_bytes(const char *title, const void *data, size_t size)
{
	const int kBytesPerLine = 16;   // 16 bytes per line
	const int kCharPerByte = 3;     // format string: " %02X"
	const int kAddrWidth = 9;       // format string:  "%08X:"

	const uint8_t *p = (const uint8_t *)data;
	char line[kAddrWidth + kCharPerByte * kBytesPerLine + kBytesPerLine + 2];

	printf("%s:size=%zu\n", title, size);
	for (size_t i = 0; i < size; i++) {
		size_t col = i % kBytesPerLine;

		if (col == 0) {
			memset(line, ' ', sizeof(line));
			snprintf(line, sizeof(line), "%08zX: ", i);
		}

		// Hex code display
		snprintf(&line[kAddrWidth + col * kCharPerByte],
			sizeof(line) - (kAddrWidth + col * kCharPerByte), " %02X", *p);

		// Printable display
		line[kAddrWidth + kCharPerByte * kBytesPerLine + col + 1] = isprint(*p) ? *p : '.';

		// Line ending
		if (col == (kBytesPerLine - 1) || i == (size - 1)) {
			// This erases the '\0' added by snprintf right after hex code
			line[kAddrWidth + (col + 1) * kCharPerByte] = ' ';

			// This adds the '\0' at the end of entire line
			line[kAddrWidth + kCharPerByte * kBytesPerLine + kBytesPerLine + 1] = '\0';
			printf("%s\n", line);
		}

		p++;
	}
	printf("\n");
}

static int uhid_write(int fd, const struct lkl_uhid_event *ev)
{
	int size = sizeof(*ev);
	ssize_t ret = LKL_CALL(write)(fd, (const char *)ev, size);

	if (ret < 0) {
		LOG("Cannot write to uhid: %d\n", errno);
		return ret;
	} else if (ret != sizeof(*ev)) {
		LOG("Wrong size written to uhid: %ld != %lu\n", ret, sizeof(ev));
		return -EFAULT;
	} else {
		return 0;
	}
}

static int uhid_create(int fd, uint16_t vid, uint16_t pid,
	const void *data, size_t size)
{
	struct lkl_uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = LKL_UHID_CREATE;
	strcpy((char *)ev.u.create.name, "test-uhid-device");
	ev.u.create.rd_data = (void *)data;
	ev.u.create.rd_size = size;
	ev.u.create.bus = BUS_USB;
	ev.u.create.vendor = vid;
	ev.u.create.product = pid;
	ev.u.create.version = 0;
	ev.u.create.country = 0;

	return uhid_write(fd, &ev);
}

static uint32_t fix_uhid_event_type(uint32_t type)
{
	static uint32_t event_map[] = {
		LKL_UHID_INPUT,
		LKL_UHID_INPUT2,
		LKL_UHID_GET_REPORT_REPLY,
		LKL_UHID_SET_REPORT_REPLY
	};

	int index = type % ARRAY_SIZE(event_map);
	return event_map[index];
}

static const char *event_type_to_name(uint32_t type)
{
	if (type == LKL_UHID_INPUT)
		return "UHID_INPUT";

	if (type == LKL_UHID_INPUT2)
		return "UHID_INPUT2";

	if (type == LKL_UHID_GET_REPORT_REPLY)
		return "UHID_GET_REPORT_REPLY";

	if (type == LKL_UHID_SET_REPORT_REPLY)
		return "UHID_SET_REPORT_REPLY";

	return "UNKNOWN";
}

// return consumed data size
static int init_uhid_message(const uint8_t *data, size_t size, struct lkl_uhid_event *ev)
{
	memset(ev, 0, sizeof(*ev));

	size_t ev_size = MIN(MAX_EVENT_SIZE, sizeof(*ev));

	ev_size = MIN(ev_size, size);

	if (ev_size < sizeof(ev->type)) // no enough data
		return 0;

	memcpy(ev, data, ev_size);
	ev->type = fix_uhid_event_type(ev->type);

	return ev_size;
}

static void uhid_destroy(int fd)
{
	struct lkl_uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_DESTROY;

	uhid_write(fd, &ev);
}

struct fuzz_data_t {
	uint8_t  rdesc_sz;
	uint32_t msg_sz;
	uint16_t vid;
	uint16_t pid;
	uint8_t  payload[0];
};

static int fuzz_data_fixup(struct fuzz_data_t *data, size_t data_sz)
{
	if (data_sz < sizeof(struct fuzz_data_t))
		return 0;

	if (data->rdesc_sz > 255)
		data->rdesc_sz = 255;

	size_t sz = data_sz - sizeof(struct fuzz_data_t);

	if (data->rdesc_sz > sz)
		data->rdesc_sz = sz;

	sz -= data->rdesc_sz;

	if (data->msg_sz > sz)
		data->msg_sz = sz;

	return 1;
}

static int uhid_fuzz(struct fuzz_data_t *data)
{
	if (data->rdesc_sz == 0) {
		LOG("Empty fuzz dat\n");
		return 0;
	}

	LOG("VID=%04X, PID=%04X, RDESC: %u bytes, msg: %u byetes\n",
		data->vid, data->pid, data->rdesc_sz, data->msg_sz);

	LOG_BYTES("RDESC:", data->payload, data->rdesc_sz);

	int fd = LKL_CALL(open)("/dev/uhid", O_RDWR | O_CLOEXEC, 0);

	if (fd < 0) {
		LOG("Cannot open /dev/uhid\n");
		return -1;
	}

	int ret = uhid_create(fd, data->vid, data->pid,
		data->payload, data->rdesc_sz);

	if (ret) {
		close(fd);
		LOG("Creating uhid device failed, %d\n", ret);
		return -1;
	}

	uint8_t  *msg_data = data->payload + data->rdesc_sz;
	uint32_t msg_data_size = data->msg_sz;

	while (msg_data_size > 0) {
		struct lkl_uhid_event ev;
		int consume = init_uhid_message(msg_data, msg_data_size, &ev);

		if (consume == 0) {  // no more data
			break;
		}

		LOG("TYPE: %s ", event_type_to_name(ev.type));
		LOG_BYTES("DATA:", msg_data, consume);
		msg_data += consume;
		msg_data_size -= consume;

		uhid_write(fd, &ev);
	}

	uhid_destroy(fd);
	LKL_CALL(close)(fd);
	return 0;
}

static int initialize_lkl(void)
{
	if (!g_log_enabled)
		lkl_host_ops.print = NULL;

	int ret = lkl_init(&lkl_host_ops);

	if (ret) {
		LOG("lkl_init failed\n");
		return -1;
	}

	ret = lkl_start_kernel("mem=50M kasan.fault=panic");
	if (ret) {
		LOG("lkl_start_kernel failed\n");
		lkl_cleanup();
		return -1;
	}

	lkl_mount_fs("sysfs");
	lkl_mount_fs("proc");
	lkl_mount_fs("dev");

	// This is defined in miscdevice.h which is, however, not visible to
	// userspace code.
	#define UHID_MINOR  239

	dev_t dev = makedev(MISC_MAJOR, UHID_MINOR);
	int mknod_result = LKL_CALL(mknodat)(AT_FDCWD, "/dev/uhid",
		S_IFCHR | 0600 /* S_IRUSR | S_IWUSR */, dev);

	if (mknod_result != 0) {
		LOG("Create device file failed\n");
		return -1;
	}

	return 0;
}

void flush_coverage(void)
{
	LOG("Flushing coverage data...\n");
	__llvm_profile_write_file();
	LOG("Done...\n");
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	for (int i = 0; i < *argc; i++) {
		if (strcmp((*argv)[i], "-quiet=1") == 0) {
			g_log_enabled = false;
			break;
		}
	}

	initialize_lkl();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);

	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	static int iter;
	uint8_t data[sizeof(struct fuzz_data_t) + 128] = {0};

	if (Size > sizeof(data))
		Size = sizeof(data);

	memcpy(data, Data, Size);

	struct fuzz_data_t *fuzz_data = (struct fuzz_data_t *)data;
	int success = fuzz_data_fixup(fuzz_data, Size);

	if (success) {
		uhid_fuzz(fuzz_data);

		iter++;
		if (iter > 1000) {
			flush_coverage();
			iter = 0;
		}
	}

	return 0;
}

