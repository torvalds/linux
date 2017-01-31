#include <stdio.h>
#include <assert.h>
#include <linux/bpf.h>
#include <unistd.h>
#include "libbpf.h"
#include "bpf_load.h"
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int ac, char **argv)
{
	int serverfd, serverconnfd, clientfd;
	socklen_t sockaddr_len;
	struct sockaddr serv_addr, mapped_addr, tmp_addr;
	struct sockaddr_in *serv_addr_in, *mapped_addr_in, *tmp_addr_in;
	char filename[256];
	char *ip;

	serv_addr_in = (struct sockaddr_in *)&serv_addr;
	mapped_addr_in = (struct sockaddr_in *)&mapped_addr;
	tmp_addr_in = (struct sockaddr_in *)&tmp_addr;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
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

	assert(!bpf_map_update_elem(map_fd[0], &mapped_addr, &serv_addr, BPF_ANY));

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

	return 0;
}
