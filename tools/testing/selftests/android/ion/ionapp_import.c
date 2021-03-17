// SPDX-License-Identifier: GPL-2.0-only
/*
 * ionapp_import.c
 *
 * It is a user space utility to receive android ion memory buffer fd
 * over unix domain socket IPC that can be exported by ionapp_export.
 * This acts like a client for ionapp_export.
 *
 * Copyright (C) 2017 Pintu Kumar <pintu.ping@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ionutils.h"
#include "ipcsocket.h"


int main(void)
{
	int ret, status;
	int sockfd, shared_fd;
	unsigned char *map_buf;
	unsigned long map_len;
	struct ion_buffer_info info;
	struct socket_info skinfo;

	/* This is the client part. Here 0 means client or importer */
	status = opensocket(&sockfd, SOCKET_NAME, 0);
	if (status < 0) {
		fprintf(stderr, "No exporter exists...\n");
		ret = status;
		goto err_socket;
	}

	skinfo.sockfd = sockfd;

	ret = socket_receive_fd(&skinfo);
	if (ret < 0) {
		fprintf(stderr, "Failed: socket_receive_fd\n");
		goto err_recv;
	}

	shared_fd = skinfo.datafd;
	printf("Received buffer fd: %d\n", shared_fd);
	if (shared_fd <= 0) {
		fprintf(stderr, "ERROR: improper buf fd\n");
		ret = -1;
		goto err_fd;
	}

	memset(&info, 0, sizeof(info));
	info.buffd = shared_fd;
	info.buflen = ION_BUFFER_LEN;

	ret = ion_import_buffer_fd(&info);
	if (ret < 0) {
		fprintf(stderr, "Failed: ion_use_buffer_fd\n");
		goto err_import;
	}

	map_buf = info.buffer;
	map_len = info.buflen;
	read_buffer(map_buf, map_len);

	/* Write probably new data to the same buffer again */
	map_len = ION_BUFFER_LEN;
	write_buffer(map_buf, map_len);

err_import:
	ion_close_buffer_fd(&info);
err_fd:
err_recv:
err_socket:
	closesocket(sockfd, SOCKET_NAME);

	return ret;
}
