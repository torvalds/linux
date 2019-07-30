#include <linux/unistd.h>
#include <linux/bpf.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <bpf/bpf.h>

#include "bpf/libbpf.h"
#include "bpf_insn.h"
#include "sock_example.h"

#define BPF_F_PIN	(1 << 0)
#define BPF_F_GET	(1 << 1)
#define BPF_F_PIN_GET	(BPF_F_PIN | BPF_F_GET)

#define BPF_F_KEY	(1 << 2)
#define BPF_F_VAL	(1 << 3)
#define BPF_F_KEY_VAL	(BPF_F_KEY | BPF_F_VAL)

#define BPF_M_UNSPEC	0
#define BPF_M_MAP	1
#define BPF_M_PROG	2

static void usage(void)
{
	printf("Usage: fds_example [...]\n");
	printf("       -F <file>   File to pin/get object\n");
	printf("       -P          |- pin object\n");
	printf("       -G          `- get object\n");
	printf("       -m          eBPF map mode\n");
	printf("       -k <key>    |- map key\n");
	printf("       -v <value>  `- map value\n");
	printf("       -p          eBPF prog mode\n");
	printf("       -o <object> `- object file\n");
	printf("       -h          Display this help.\n");
}

static int bpf_map_create(void)
{
	return bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
			      sizeof(uint32_t), 1024, 0);
}

static int bpf_prog_create(const char *object)
{
	static struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};
	size_t insns_cnt = sizeof(insns) / sizeof(struct bpf_insn);
	char bpf_log_buf[BPF_LOG_BUF_SIZE];
	struct bpf_object *obj;
	int prog_fd;

	if (object) {
		assert(!bpf_prog_load(object, BPF_PROG_TYPE_UNSPEC,
				      &obj, &prog_fd));
		return prog_fd;
	} else {
		return bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER,
					insns, insns_cnt, "GPL", 0,
					bpf_log_buf, BPF_LOG_BUF_SIZE);
	}
}

static int bpf_do_map(const char *file, uint32_t flags, uint32_t key,
		      uint32_t value)
{
	int fd, ret;

	if (flags & BPF_F_PIN) {
		fd = bpf_map_create();
		printf("bpf: map fd:%d (%s)\n", fd, strerror(errno));
		assert(fd > 0);

		ret = bpf_obj_pin(fd, file);
		printf("bpf: pin ret:(%d,%s)\n", ret, strerror(errno));
		assert(ret == 0);
	} else {
		fd = bpf_obj_get(file);
		printf("bpf: get fd:%d (%s)\n", fd, strerror(errno));
		assert(fd > 0);
	}

	if ((flags & BPF_F_KEY_VAL) == BPF_F_KEY_VAL) {
		ret = bpf_map_update_elem(fd, &key, &value, 0);
		printf("bpf: fd:%d u->(%u:%u) ret:(%d,%s)\n", fd, key, value,
		       ret, strerror(errno));
		assert(ret == 0);
	} else if (flags & BPF_F_KEY) {
		ret = bpf_map_lookup_elem(fd, &key, &value);
		printf("bpf: fd:%d l->(%u):%u ret:(%d,%s)\n", fd, key, value,
		       ret, strerror(errno));
		assert(ret == 0);
	}

	return 0;
}

static int bpf_do_prog(const char *file, uint32_t flags, const char *object)
{
	int fd, sock, ret;

	if (flags & BPF_F_PIN) {
		fd = bpf_prog_create(object);
		printf("bpf: prog fd:%d (%s)\n", fd, strerror(errno));
		assert(fd > 0);

		ret = bpf_obj_pin(fd, file);
		printf("bpf: pin ret:(%d,%s)\n", ret, strerror(errno));
		assert(ret == 0);
	} else {
		fd = bpf_obj_get(file);
		printf("bpf: get fd:%d (%s)\n", fd, strerror(errno));
		assert(fd > 0);
	}

	sock = open_raw_sock("lo");
	assert(sock > 0);

	ret = setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &fd, sizeof(fd));
	printf("bpf: sock:%d <- fd:%d attached ret:(%d,%s)\n", sock, fd,
	       ret, strerror(errno));
	assert(ret == 0);

	return 0;
}

int main(int argc, char **argv)
{
	const char *file = NULL, *object = NULL;
	uint32_t key = 0, value = 0, flags = 0;
	int opt, mode = BPF_M_UNSPEC;

	while ((opt = getopt(argc, argv, "F:PGmk:v:po:")) != -1) {
		switch (opt) {
		/* General args */
		case 'F':
			file = optarg;
			break;
		case 'P':
			flags |= BPF_F_PIN;
			break;
		case 'G':
			flags |= BPF_F_GET;
			break;
		/* Map-related args */
		case 'm':
			mode = BPF_M_MAP;
			break;
		case 'k':
			key = strtoul(optarg, NULL, 0);
			flags |= BPF_F_KEY;
			break;
		case 'v':
			value = strtoul(optarg, NULL, 0);
			flags |= BPF_F_VAL;
			break;
		/* Prog-related args */
		case 'p':
			mode = BPF_M_PROG;
			break;
		case 'o':
			object = optarg;
			break;
		default:
			goto out;
		}
	}

	if (!(flags & BPF_F_PIN_GET) || !file)
		goto out;

	switch (mode) {
	case BPF_M_MAP:
		return bpf_do_map(file, flags, key, value);
	case BPF_M_PROG:
		return bpf_do_prog(file, flags, object);
	}
out:
	usage();
	return -1;
}
