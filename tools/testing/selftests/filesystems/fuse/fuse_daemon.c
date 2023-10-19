// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include "test_fuse.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <linux/unistd.h>

#include <include/uapi/linux/fuse.h>
#include <include/uapi/linux/bpf.h>

bool user_messages;
bool kernel_messages;

static int display_trace(void)
{
	int pid = -1;
	int tp = -1;
	char c;
	ssize_t bytes_read;
	static char line[256] = {0};

	if (!kernel_messages)
		return TEST_SUCCESS;

	TEST(pid = fork(), pid != -1);
	if (pid != 0)
		return pid;

	TESTEQUAL(tracing_on(), 0);
	TEST(tp = s_open(s_path(tracing_folder(), s("trace_pipe")),
			 O_RDONLY | O_CLOEXEC), tp != -1);
	for (;;) {
		TEST(bytes_read = read(tp, &c, sizeof(c)),
		     bytes_read == 1);
		if (c == '\n') {
			printf("%s\n", line);
			line[0] = 0;
		} else
			sprintf(line + strlen(line), "%c", c);
	}
out:
	if (pid == 0) {
		close(tp);
		exit(TEST_FAILURE);
	}
	return pid;
}

static const char *fuse_opcode_to_string(int opcode)
{
	switch (opcode & FUSE_OPCODE_FILTER) {
	case FUSE_LOOKUP:
		return "FUSE_LOOKUP";
	case FUSE_FORGET:
		return "FUSE_FORGET";
	case FUSE_GETATTR:
		return "FUSE_GETATTR";
	case FUSE_SETATTR:
		return "FUSE_SETATTR";
	case FUSE_READLINK:
		return "FUSE_READLINK";
	case FUSE_SYMLINK:
		return "FUSE_SYMLINK";
	case FUSE_MKNOD:
		return "FUSE_MKNOD";
	case FUSE_MKDIR:
		return "FUSE_MKDIR";
	case FUSE_UNLINK:
		return "FUSE_UNLINK";
	case FUSE_RMDIR:
		return "FUSE_RMDIR";
	case FUSE_RENAME:
		return "FUSE_RENAME";
	case FUSE_LINK:
		return "FUSE_LINK";
	case FUSE_OPEN:
		return "FUSE_OPEN";
	case FUSE_READ:
		return "FUSE_READ";
	case FUSE_WRITE:
		return "FUSE_WRITE";
	case FUSE_STATFS:
		return "FUSE_STATFS";
	case FUSE_RELEASE:
		return "FUSE_RELEASE";
	case FUSE_FSYNC:
		return "FUSE_FSYNC";
	case FUSE_SETXATTR:
		return "FUSE_SETXATTR";
	case FUSE_GETXATTR:
		return "FUSE_GETXATTR";
	case FUSE_LISTXATTR:
		return "FUSE_LISTXATTR";
	case FUSE_REMOVEXATTR:
		return "FUSE_REMOVEXATTR";
	case FUSE_FLUSH:
		return "FUSE_FLUSH";
	case FUSE_INIT:
		return "FUSE_INIT";
	case FUSE_OPENDIR:
		return "FUSE_OPENDIR";
	case FUSE_READDIR:
		return "FUSE_READDIR";
	case FUSE_RELEASEDIR:
		return "FUSE_RELEASEDIR";
	case FUSE_FSYNCDIR:
		return "FUSE_FSYNCDIR";
	case FUSE_GETLK:
		return "FUSE_GETLK";
	case FUSE_SETLK:
		return "FUSE_SETLK";
	case FUSE_SETLKW:
		return "FUSE_SETLKW";
	case FUSE_ACCESS:
		return "FUSE_ACCESS";
	case FUSE_CREATE:
		return "FUSE_CREATE";
	case FUSE_INTERRUPT:
		return "FUSE_INTERRUPT";
	case FUSE_BMAP:
		return "FUSE_BMAP";
	case FUSE_DESTROY:
		return "FUSE_DESTROY";
	case FUSE_IOCTL:
		return "FUSE_IOCTL";
	case FUSE_POLL:
		return "FUSE_POLL";
	case FUSE_NOTIFY_REPLY:
		return "FUSE_NOTIFY_REPLY";
	case FUSE_BATCH_FORGET:
		return "FUSE_BATCH_FORGET";
	case FUSE_FALLOCATE:
		return "FUSE_FALLOCATE";
	case FUSE_READDIRPLUS:
		return "FUSE_READDIRPLUS";
	case FUSE_RENAME2:
		return "FUSE_RENAME2";
	case FUSE_LSEEK:
		return "FUSE_LSEEK";
	case FUSE_COPY_FILE_RANGE:
		return "FUSE_COPY_FILE_RANGE";
	case FUSE_SETUPMAPPING:
		return "FUSE_SETUPMAPPING";
	case FUSE_REMOVEMAPPING:
		return "FUSE_REMOVEMAPPING";
	//case FUSE_SYNCFS:
	//	return "FUSE_SYNCFS";
	case CUSE_INIT:
		return "CUSE_INIT";
	case CUSE_INIT_BSWAP_RESERVED:
		return "CUSE_INIT_BSWAP_RESERVED";
	case FUSE_INIT_BSWAP_RESERVED:
		return "FUSE_INIT_BSWAP_RESERVED";
	}
	return "?";
}

static int parse_options(int argc, char *const *argv)
{
	signed char c;

	while ((c = getopt(argc, argv, "kuv")) != -1)
		switch (c) {
		case 'v':
			test_options.verbose = true;
			break;

		case 'u':
			user_messages = true;
			break;

		case 'k':
			kernel_messages = true;
			break;

		default:
			return -EINVAL;
		}

	return 0;
}

int main(int argc, char *argv[])
{
	int result = TEST_FAILURE;
	int trace_pid = -1;
	char *mount_dir = NULL;
	char *src_dir = NULL;
	int bpf_fd = -1;
	int src_fd = -1;
	int fuse_dev = -1;
	struct map_relocation *map_relocations = NULL;
	size_t map_count = 0;
	int i;

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");
	TESTEQUAL(parse_options(argc, argv), 0);

	TEST(trace_pid = display_trace(), trace_pid != -1);

	delete_dir_tree("fd-src", true);
	TEST(src_dir = setup_mount_dir("fd-src"), src_dir);
	delete_dir_tree("fd-dst", true);
	TEST(mount_dir = setup_mount_dir("fd-dst"), mount_dir);

	TESTEQUAL(install_elf_bpf("fd_bpf.bpf", "test_daemon", &bpf_fd,
				  &map_relocations, &map_count), 0);

	TEST(src_fd = open("fd-src", O_DIRECTORY | O_RDONLY | O_CLOEXEC),
	     src_fd != -1);
	TESTSYSCALL(mkdirat(src_fd, "show", 0777));
	TESTSYSCALL(mkdirat(src_fd, "hide", 0777));

	for (i = 0; i < map_count; ++i)
		if (!strcmp(map_relocations[i].name, "test_map")) {
			uint32_t key = 23;
			uint32_t value = 1234;
			union bpf_attr attr = {
				.map_fd = map_relocations[i].fd,
				.key    = ptr_to_u64(&key),
				.value  = ptr_to_u64(&value),
				.flags  = BPF_ANY,
			};
			TESTSYSCALL(syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM,
					    &attr, sizeof(attr)));
		}

	TESTEQUAL(mount_fuse(mount_dir, bpf_fd, src_fd, &fuse_dev), 0);

	if (fork())
		return 0;

	for (;;) {
		uint8_t bytes_in[FUSE_MIN_READ_BUFFER];
		uint8_t bytes_out[FUSE_MIN_READ_BUFFER] __maybe_unused;
		struct fuse_in_header *in_header =
			(struct fuse_in_header *)bytes_in;
		ssize_t res = read(fuse_dev, bytes_in, sizeof(bytes_in));

		if (res == -1)
			break;

		switch (in_header->opcode) {
		case FUSE_LOOKUP | FUSE_PREFILTER: {
			char *name = (char *)(bytes_in + sizeof(*in_header));

			if (user_messages)
				printf("Lookup %s\n", name);
			if (!strcmp(name, "hide"))
				TESTFUSEOUTERROR(-ENOENT);
			else
				TESTFUSEOUTREAD(name, strlen(name) + 1);
			break;
		}
		default:
			if (user_messages) {
				printf("opcode is %d (%s)\n", in_header->opcode,
				       fuse_opcode_to_string(
					       in_header->opcode));
			}
			break;
		}
	}

	result = TEST_SUCCESS;

out:
	for (i = 0; i < map_count; ++i) {
		free(map_relocations[i].name);
		close(map_relocations[i].fd);
	}
	free(map_relocations);
	umount2(mount_dir, MNT_FORCE);
	delete_dir_tree(mount_dir, true);
	free(mount_dir);
	delete_dir_tree(src_dir, true);
	free(src_dir);
	if (trace_pid != -1)
		kill(trace_pid, SIGKILL);
	return result;
}
