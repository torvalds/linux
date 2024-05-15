// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
	struct sockaddr_in saddr = {}, daddr = {};
	int sd, ret, len = sizeof(daddr);
	struct timeval tv = {25, 0};
	char buf[] = "hello";

	if (argc != 6 || (strcmp(argv[1], "server") && strcmp(argv[1], "client"))) {
		printf("%s <server|client> <LOCAL_IP> <LOCAL_PORT> <REMOTE_IP> <REMOTE_PORT>\n",
		       argv[0]);
		return -1;
	}

	sd = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
	if (sd < 0) {
		printf("Failed to create sd\n");
		return -1;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = inet_addr(argv[2]);
	saddr.sin_port = htons(atoi(argv[3]));

	ret = bind(sd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret < 0) {
		printf("Failed to bind to address\n");
		goto out;
	}

	ret = listen(sd, 5);
	if (ret < 0) {
		printf("Failed to listen on port\n");
		goto out;
	}

	daddr.sin_family = AF_INET;
	daddr.sin_addr.s_addr = inet_addr(argv[4]);
	daddr.sin_port = htons(atoi(argv[5]));

	/* make test shorter than 25s */
	ret = setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (ret < 0) {
		printf("Failed to setsockopt SO_RCVTIMEO\n");
		goto out;
	}

	if (!strcmp(argv[1], "server")) {
		sleep(1); /* wait a bit for client's INIT */
		ret = connect(sd, (struct sockaddr *)&daddr, len);
		if (ret < 0) {
			printf("Failed to connect to peer\n");
			goto out;
		}
		ret = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&daddr, &len);
		if (ret < 0) {
			printf("Failed to recv msg %d\n", ret);
			goto out;
		}
		ret = sendto(sd, buf, strlen(buf) + 1, 0, (struct sockaddr *)&daddr, len);
		if (ret < 0) {
			printf("Failed to send msg %d\n", ret);
			goto out;
		}
		printf("Server: sent! %d\n", ret);
	}

	if (!strcmp(argv[1], "client")) {
		usleep(300000); /* wait a bit for server's listening */
		ret = connect(sd, (struct sockaddr *)&daddr, len);
		if (ret < 0) {
			printf("Failed to connect to peer\n");
			goto out;
		}
		sleep(1); /* wait a bit for server's delayed INIT_ACK to reproduce the issue */
		ret = sendto(sd, buf, strlen(buf) + 1, 0, (struct sockaddr *)&daddr, len);
		if (ret < 0) {
			printf("Failed to send msg %d\n", ret);
			goto out;
		}
		ret = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&daddr, &len);
		if (ret < 0) {
			printf("Failed to recv msg %d\n", ret);
			goto out;
		}
		printf("Client: rcvd! %d\n", ret);
	}
	ret = 0;
out:
	close(sd);
	return ret;
}
