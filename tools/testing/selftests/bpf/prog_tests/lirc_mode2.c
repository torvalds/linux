// SPDX-License-Identifier: GPL-2.0
// test ir decoder
//
// Copyright (C) 2018 Sean Young <sean@mess.org>

// A lirc chardev is a device representing a consumer IR (cir) device which
// can receive infrared signals from remote control and/or transmit IR.
//
// IR is sent as a series of pulses and space somewhat like morse code. The
// BPF program can decode this into scancodes so that rc-core can translate
// this into input key codes using the rc keymap.
//
// This test works by sending IR over rc-loopback, so the IR is processed by
// BPF and then decoded into scancodes. The lirc chardev must be the one
// associated with rc-loopback, see the output of ir-keytable(1).
//
// The following CONFIG options must be enabled for the test to succeed:
// CONFIG_RC_CORE=y
// CONFIG_BPF_LIRC_MODE2=y
// CONFIG_RC_LOOPBACK=y
// CONFIG_LIRC=y

#include <linux/input.h>
#include <linux/lirc.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <test_progs.h>
#include "lirc_mode2.skel.h"

/*
 * Read the DEVNAME= line out of the first uevent file that matches
 * pattern, and turn it into a /dev/<name> path.
 */
static bool find_devname(const char *pattern, char *path, size_t path_sz)
{
	glob_t gl = {};
	bool found = false;
	FILE *f;

	if (glob(pattern, 0, NULL, &gl) || gl.gl_pathc == 0)
		goto out;

	f = fopen(gl.gl_pathv[0], "r");
	if (!f)
		goto out;

	char line[256];

	while (fgets(line, sizeof(line), f)) {
		char *val;

		if (strncmp(line, "DEVNAME=", 8))
			continue;

		val = line + 8;
		val[strcspn(val, "\n")] = '\0';
		snprintf(path, path_sz, "/dev/%s", val);
		found = true;
		break;
	}

	fclose(f);
out:
	globfree(&gl);
	return found;
}

/* Load rc-loopback and find the lirc and input chardevs it created. */
static bool find_loopback_devices(char *lirc_path, char *input_path,
				  size_t path_sz)
{
	glob_t gl = {};
	bool found = false;

	/* Ignore failure, we check for the resulting devices below. */
	SYS_NOFAIL("modprobe rc-loopback > /dev/null 2>&1");

	if (glob("/sys/class/rc/rc*", 0, NULL, &gl))
		goto out;

	for (size_t i = 0; i < gl.gl_pathc; i++) {
		const char *rcdir = gl.gl_pathv[i];
		char uevent_path[PATH_MAX];
		char uevent[4096];
		char pattern[PATH_MAX];
		FILE *f;
		size_t n;

		snprintf(uevent_path, sizeof(uevent_path), "%s/uevent", rcdir);
		f = fopen(uevent_path, "r");
		if (!f)
			continue;
		n = fread(uevent, 1, sizeof(uevent) - 1, f);
		fclose(f);
		uevent[n] = '\0';

		if (!strstr(uevent, "DRV_NAME=rc-loopback"))
			continue;

		snprintf(pattern, sizeof(pattern), "%s/lirc*/uevent", rcdir);
		if (!find_devname(pattern, lirc_path, path_sz))
			continue;

		snprintf(pattern, sizeof(pattern), "%s/input*/event*/uevent", rcdir);
		if (!find_devname(pattern, input_path, path_sz))
			continue;

		found = true;
		break;
	}

out:
	if (!found) {
		fprintf(stderr, "No rc devices found\n");
		fprintf(stderr, "Enable CONFIG_RC_LOOPBACK and CONFIG_BPF_LIRC_MODE2\n");
	}

	globfree(&gl);
	return found;
}

void test_lirc_mode2(void)
{
	char lirc_path[PATH_MAX], input_path[PATH_MAX];
	int lircfd = -1, inputfd = -1, progfd, progfd2 = -1;
	struct lirc_mode2 *skel = NULL, *skel2 = NULL;
	__u32 prog_ids[10], prog_flags[10], prog_cnt;
	struct bpf_prog_info info;
	__u32 info_len, prog_id, prog_id2;
	int testir1 = 0x8ead;	/* keydown flag (0x8000) | scancode 0xead */
	int testir2 = 0x4081;	/* pointer_rel flag (0x4000) | rel_x=1 | rel_y=1 */
	struct input_event event;
	struct pollfd pfd = {};
	int ret;

	if (getuid() != 0) {
		test__skip();
		return;
	}

	if (!find_loopback_devices(lirc_path, input_path, sizeof(lirc_path))) {
		test__skip();
		return;
	}

	skel = lirc_mode2__open_and_load();
	if (!ASSERT_OK_PTR(skel, "lirc_mode2__open_and_load"))
		return;

	progfd = bpf_program__fd(skel->progs.bpf_decoder);

	memset(&info, 0, sizeof(info));
	info_len = sizeof(info);
	ret = bpf_prog_get_info_by_fd(progfd, &info, &info_len);
	if (!ASSERT_OK(ret, "get first program's info"))
		goto out;
	prog_id = info.id;

	lircfd = open(lirc_path, O_RDWR | O_NONBLOCK);
	if (!ASSERT_GE(lircfd, 0, "open lirc device"))
		goto out;

	/* Try to detach it before it was ever attached, should fail. */
	ret = bpf_prog_detach2(progfd, lircfd, BPF_LIRC_MODE2);
	if (!ASSERT_EQ(ret, -ENOENT, "detach unattached program"))
		goto out;

	inputfd = open(input_path, O_RDONLY | O_NONBLOCK);
	if (!ASSERT_GE(inputfd, 0, "open input device"))
		goto out;

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs before attach"))
		goto out;
	if (!ASSERT_EQ(prog_cnt, 0, "no programs should be attached yet"))
		goto out;

	/* Invalid attach flags must be rejected, and must not attach. */
	ret = bpf_prog_attach(progfd, lircfd, BPF_LIRC_MODE2, 1);
	if (!ASSERT_EQ(ret, -EINVAL, "attach with invalid flags"))
		goto out;

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs after rejected attach"))
		goto out;
	if (!ASSERT_EQ(prog_cnt, 0, "rejected attach should not attach"))
		goto out;

	ret = bpf_prog_attach(progfd, lircfd, BPF_LIRC_MODE2, 0);
	if (!ASSERT_OK(ret, "attach program to lirc device"))
		goto out;

	/* Invalid query flags must be rejected too, without upsetting state. */
	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 1, prog_flags, prog_ids,
			     &prog_cnt);
	ASSERT_EQ(ret, -EINVAL, "query with invalid flags");

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs after attach"))
		goto out_detach;
	if (!ASSERT_EQ(prog_cnt, 1, "one program should be attached"))
		goto out_detach;
	ASSERT_EQ(prog_ids[0], prog_id, "queried id should match attached program");

	/* Write raw IR */
	ret = write(lircfd, &testir1, sizeof(testir1));
	if (!ASSERT_EQ(ret, sizeof(testir1), "send test IR message 1"))
		goto out_detach;

	pfd.fd = inputfd;
	pfd.events = POLLIN;

	for (;;) {
		poll(&pfd, 1, 100);

		/* Read decoded IR */
		ret = read(inputfd, &event, sizeof(event));
		if (!ASSERT_EQ(ret, sizeof(event), "read decoded IR 1"))
			goto out_detach;

		if (event.type == EV_MSC && event.code == MSC_SCAN &&
		    event.value == 0xead)
			break;
	}

	/* Write raw IR */
	ret = write(lircfd, &testir2, sizeof(testir2));
	if (!ASSERT_EQ(ret, sizeof(testir2), "send test IR message 2"))
		goto out_detach;

	for (;;) {
		poll(&pfd, 1, 100);

		/* Read decoded IR */
		ret = read(inputfd, &event, sizeof(event));
		if (!ASSERT_EQ(ret, sizeof(event), "read decoded IR 2"))
			goto out_detach;

		if (event.type == EV_REL && event.code == REL_Y &&
		    event.value == 1)
			break;
	}

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs after IR was decoded"))
		goto out_detach;
	if (!ASSERT_EQ(prog_cnt, 1, "one program should still be attached"))
		goto out_detach;

	/*
	 * The lirc chardev can hold more than one attached program at once.
	 * Load a second, independent instance and check it can be attached
	 * alongside the first, queried, and then detached on its own
	 * without disturbing the first program's attachment.
	 */
	skel2 = lirc_mode2__open_and_load();
	if (!ASSERT_OK_PTR(skel2, "lirc_mode2__open_and_load (2nd)"))
		goto out_detach;

	progfd2 = bpf_program__fd(skel2->progs.bpf_decoder);

	memset(&info, 0, sizeof(info));
	info_len = sizeof(info);
	ret = bpf_prog_get_info_by_fd(progfd2, &info, &info_len);
	if (!ASSERT_OK(ret, "get second program's info"))
		goto out_detach;
	prog_id2 = info.id;

	ret = bpf_prog_attach(progfd2, lircfd, BPF_LIRC_MODE2, 0);
	if (!ASSERT_OK(ret, "attach second program to lirc device"))
		goto out_detach;

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs after second attach"))
		goto out_detach2;
	if (!ASSERT_EQ(prog_cnt, 2, "two programs should be attached"))
		goto out_detach2;
	ASSERT_TRUE((prog_ids[0] == prog_id && prog_ids[1] == prog_id2) ||
		    (prog_ids[0] == prog_id2 && prog_ids[1] == prog_id),
		    "queried ids should be the two attached programs");

	/* Detach the second program; the first should remain attached. */
	ret = bpf_prog_detach2(progfd2, lircfd, BPF_LIRC_MODE2);
	if (!ASSERT_OK(ret, "detach second program"))
		goto out_detach2;

	/* Detaching an already-detached program should now fail. */
	ret = bpf_prog_detach2(progfd2, lircfd, BPF_LIRC_MODE2);
	ASSERT_EQ(ret, -ENOENT, "detach second program again");

	prog_cnt = ARRAY_SIZE(prog_ids);
	ret = bpf_prog_query(lircfd, BPF_LIRC_MODE2, 0, prog_flags, prog_ids,
			     &prog_cnt);
	if (!ASSERT_OK(ret, "query programs after second detach"))
		goto out_detach;
	if (!ASSERT_EQ(prog_cnt, 1, "one program should remain attached"))
		goto out_detach;
	ASSERT_EQ(prog_ids[0], prog_id, "remaining program should be the first one");

out_detach:
	/* Let's try detaching it now it is actually attached. */
	ret = bpf_prog_detach2(progfd, lircfd, BPF_LIRC_MODE2);
	ASSERT_OK(ret, "detach program from lirc device");

	/* Detaching it again should now fail the same way. */
	ret = bpf_prog_detach2(progfd, lircfd, BPF_LIRC_MODE2);
	ASSERT_EQ(ret, -ENOENT, "detach program from lirc device again");
	goto out;

out_detach2:
	/* Best-effort cleanup of the second program before bailing out. */
	bpf_prog_detach2(progfd2, lircfd, BPF_LIRC_MODE2);
	goto out_detach;

out:
	if (inputfd >= 0)
		close(inputfd);
	if (lircfd >= 0)
		close(lircfd);
	lirc_mode2__destroy(skel2);
	lirc_mode2__destroy(skel);
}
