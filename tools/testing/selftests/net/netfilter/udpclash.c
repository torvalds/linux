// SPDX-License-Identifier: GPL-2.0

/* Usage: ./udpclash <IP> <PORT>
 *
 * Emit THREAD_COUNT UDP packets sharing the same saddr:daddr pair.
 *
 * This mimics DNS resolver libraries that emit A and AAAA requests
 * in parallel.
 *
 * This exercises conntrack clash resolution logic added and later
 * refined in
 *
 *  71d8c47fc653 ("netfilter: conntrack: introduce clash resolution on insertion race")
 *  ed07d9a021df ("netfilter: nf_conntrack: resolve clash for matching conntracks")
 *  6a757c07e51f ("netfilter: conntrack: allow insertion of clashing entries")
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define THREAD_COUNT 128

struct thread_args {
	const struct sockaddr_in *si_remote;
	int sockfd;
};

static volatile int wait = 1;

static void *thread_main(void *varg)
{
	const struct sockaddr_in *si_remote;
	const struct thread_args *args = varg;
	static const char msg[] = "foo";

	si_remote = args->si_remote;

	while (wait == 1)
		;

	if (sendto(args->sockfd, msg, strlen(msg), MSG_NOSIGNAL,
		   (struct sockaddr *)si_remote, sizeof(*si_remote)) < 0)
		exit(111);

	return varg;
}

static int run_test(int fd, const struct sockaddr_in *si_remote)
{
	struct thread_args thread_args = {
		.si_remote = si_remote,
		.sockfd = fd,
	};
	pthread_t *tid = calloc(THREAD_COUNT, sizeof(pthread_t));
	unsigned int repl_count = 0, timeout = 0;
	int i;

	if (!tid) {
		perror("calloc");
		return 1;
	}

	for (i = 0; i < THREAD_COUNT; i++) {
		int err = pthread_create(&tid[i], NULL, &thread_main, &thread_args);

		if (err != 0) {
			perror("pthread_create");
			exit(1);
		}
	}

	wait = 0;

	for (i = 0; i < THREAD_COUNT; i++)
		pthread_join(tid[i], NULL);

	while (repl_count < THREAD_COUNT) {
		struct sockaddr_in si_repl;
		socklen_t si_repl_len = sizeof(si_repl);
		char repl[512];
		ssize_t ret;

		ret = recvfrom(fd, repl, sizeof(repl), MSG_NOSIGNAL,
			       (struct sockaddr *) &si_repl, &si_repl_len);
		if (ret < 0) {
			if (timeout++ > 5000) {
				fputs("timed out while waiting for reply from thread\n", stderr);
				break;
			}

			/* give reply time to pass though the stack */
			usleep(1000);
			continue;
		}

		if (si_repl_len != sizeof(*si_remote)) {
			fprintf(stderr, "warning: reply has unexpected repl_len %d vs %d\n",
				(int)si_repl_len, (int)sizeof(si_repl));
		} else if (si_remote->sin_addr.s_addr != si_repl.sin_addr.s_addr ||
			si_remote->sin_port != si_repl.sin_port) {
			char a[64], b[64];

			inet_ntop(AF_INET, &si_remote->sin_addr, a, sizeof(a));
			inet_ntop(AF_INET, &si_repl.sin_addr, b, sizeof(b));

			fprintf(stderr, "reply from wrong source: want %s:%d got %s:%d\n",
				a, ntohs(si_remote->sin_port), b, ntohs(si_repl.sin_port));
		}

		repl_count++;
	}

	printf("got %d of %d replies\n", repl_count, THREAD_COUNT);

	free(tid);

	return repl_count == THREAD_COUNT ? 0 : 1;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in si_local = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in si_remote = {
		.sin_family = AF_INET,
	};
	int fd, ret;

	if (argc < 3) {
		fputs("Usage: send_udp <daddr> <dport>\n", stderr);
		return 1;
	}

	si_remote.sin_port = htons(atoi(argv[2]));
	si_remote.sin_addr.s_addr = inet_addr(argv[1]);

	fd = socket(AF_INET, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, IPPROTO_UDP);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	if (bind(fd, (struct sockaddr *)&si_local, sizeof(si_local)) < 0) {
		perror("bind");
		return 1;
	}

	ret = run_test(fd, &si_remote);

	close(fd);

	return ret;
}
