// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int ac, char **argv)
{
	struct sockaddr_in *serv_addr_in, *mapped_addr_in, *tmp_addr_in;
	struct sockaddr serv_addr, mapped_addr, tmp_addr;
	int serverfd, serverconnfd, clientfd, map_fd;
	struct bpf_link *link = NULL;
	struct bpf_program *prog;
	struct bpf_object *obj;
	socklen_t sockaddr_len;
	char filename[256];
	char *ip;

	serv_addr_in = (struct sockaddr_in *)&serv_addr;
	mapped_addr_in = (struct sockaddr_in *)&mapped_addr;
	tmp_addr_in = (struct sockaddr_in *)&tmp_addr;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	prog = bpf_object__find_program_by_name(obj, "bpf_prog1");
	if (libbpf_get_error(prog)) {
		fprintf(stderr, "ERROR: finding a prog in obj file failed\n");
		goto cleanup;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	map_fd = bpf_object__find_map_fd_by_name(obj, "dnat_map");
	if (map_fd < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	link = bpf_program__attach(prog);
	if (libbpf_get_error(link)) {
		fprintf(stderr, "ERROR: bpf_program__attach failed\n");
		link = NULL;
		goto cleanup;
	}

	assert((serverfd = socket(AF_INET, SOCK_STREAM, 0)) > 0);
	assert((clientfd = socket(AF_INET, SOCK_STREAM, 0)) > 0);

	/* Bind server to ephemeral port on lo */
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr_in->sin_family = AF_INET;
	serv_addr_in->sin_port = 0;
	serv_addr_in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	assert(bind(serverfd, &serv_addr, sizeof(serv_addr)) == 0);

	sockaddr_len = sizeof(serv_addr);
	assert(getsockname(serverfd, &serv_addr, &sockaddr_len) == 0);
	ip = inet_ntoa(serv_addr_in->sin_addr);
	printf("Server bound to: %s:%d\n", ip, ntohs(serv_addr_in->sin_port));

	memset(&mapped_addr, 0, sizeof(mapped_addr));
	mapped_addr_in->sin_family = AF_INET;
	mapped_addr_in->sin_port = htons(5555);
	mapped_addr_in->sin_addr.s_addr = inet_addr("255.255.255.255");

	assert(!bpf_map_update_elem(map_fd, &mapped_addr, &serv_addr, BPF_ANY));

	assert(listen(serverfd, 5) == 0);

	ip = inet_ntoa(mapped_addr_in->sin_addr);
	printf("Client connecting to: %s:%d\n",
	       ip, ntohs(mapped_addr_in->sin_port));
	assert(connect(clientfd, &mapped_addr, sizeof(mapped_addr)) == 0);

	sockaddr_len = sizeof(tmp_addr);
	ip = inet_ntoa(tmp_addr_in->sin_addr);
	assert((serverconnfd = accept(serverfd, &tmp_addr, &sockaddr_len)) > 0);
	printf("Server received connection from: %s:%d\n",
	       ip, ntohs(tmp_addr_in->sin_port));

	sockaddr_len = sizeof(tmp_addr);
	assert(getpeername(clientfd, &tmp_addr, &sockaddr_len) == 0);
	ip = inet_ntoa(tmp_addr_in->sin_addr);
	printf("Client's peer address: %s:%d\n",
	       ip, ntohs(tmp_addr_in->sin_port));

	/* Is the server's getsockname = the socket getpeername */
	assert(memcmp(&serv_addr, &tmp_addr, sizeof(struct sockaddr_in)) == 0);

cleanup:
	bpf_link__destroy(link);
	bpf_object__close(obj);
	return 0;
}
