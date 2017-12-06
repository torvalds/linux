/*
 * ionapp_export.c
 *
 * It is a user space utility to create and export android
 * ion memory buffer fd to another process using unix domain socket as IPC.
 * This acts like a server for ionapp_import(client).
 * So, this server has to be started first before the client.
 *
 * Copyright (C) 2017 Pintu Kumar <pintu.ping@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include "ionutils.h"
#include "ipcsocket.h"


void print_usage(int argc, char *argv[])
{
	printf("Usage: %s [-h <help>] [-i <heap id>] [-s <size in bytes>]\n",
		argv[0]);
}

int main(int argc, char *argv[])
{
	int opt, ret, status, heapid;
	int sockfd, client_fd, shared_fd;
	unsigned char *map_buf;
	unsigned long map_len, heap_type, heap_size, flags;
	struct ion_buffer_info info;
	struct socket_info skinfo;

	if (argc < 2) {
		print_usage(argc, argv);
		return -1;
	}

	heap_size = 0;
	flags = 0;

	while ((opt = getopt(argc, argv, "hi:s:")) != -1) {
		switch (opt) {
		case 'h':
			print_usage(argc, argv);
			exit(0);
			break;
		case 'i':
			heapid = atoi(optarg);
			switch (heapid) {
			case 0:
				heap_type = ION_HEAP_TYPE_SYSTEM;
				break;
			case 1:
				heap_type = ION_HEAP_TYPE_SYSTEM_CONTIG;
				break;
			default:
				printf("ERROR: heap type not supported\n");
				exit(1);
			}
			break;
		case 's':
			heap_size = atoi(optarg);
			break;
		default:
			print_usage(argc, argv);
			exit(1);
			break;
		}
	}

	if (heap_size <= 0) {
		printf("heap_size cannot be 0\n");
		print_usage(argc, argv);
		exit(1);
	}

	printf("heap_type: %ld, heap_size: %ld\n", heap_type, heap_size);
	info.heap_type = heap_type;
	info.heap_size = heap_size;
	info.flag_type = flags;

	/* This is server: open the socket connection first */
	/* Here; 1 indicates server or exporter */
	status = opensocket(&sockfd, SOCKET_NAME, 1);
	if (status < 0) {
		fprintf(stderr, "<%s>: Failed opensocket.\n", __func__);
		goto err_socket;
	}
	skinfo.sockfd = sockfd;

	ret = ion_export_buffer_fd(&info);
	if (ret < 0) {
		fprintf(stderr, "FAILED: ion_get_buffer_fd\n");
		goto err_export;
	}
	client_fd = info.ionfd;
	shared_fd = info.buffd;
	map_buf = info.buffer;
	map_len = info.buflen;
	write_buffer(map_buf, map_len);

	/* share ion buf fd with other user process */
	printf("Sharing fd: %d, Client fd: %d\n", shared_fd, client_fd);
	skinfo.datafd = shared_fd;
	skinfo.buflen = map_len;

	ret = socket_send_fd(&skinfo);
	if (ret < 0) {
		fprintf(stderr, "FAILED: socket_send_fd\n");
		goto err_send;
	}

err_send:
err_export:
	ion_close_buffer_fd(&info);

err_socket:
	closesocket(sockfd, SOCKET_NAME);

	return 0;
}
