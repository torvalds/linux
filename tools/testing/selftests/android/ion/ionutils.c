#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
//#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "ionutils.h"
#include "ipcsocket.h"


void write_buffer(void *buffer, unsigned long len)
{
	int i;
	unsigned char *ptr = (unsigned char *)buffer;

	if (!ptr) {
		fprintf(stderr, "<%s>: Invalid buffer...\n", __func__);
		return;
	}

	printf("Fill buffer content:\n");
	memset(ptr, 0xfd, len);
	for (i = 0; i < len; i++)
		printf("0x%x ", ptr[i]);
	printf("\n");
}

void read_buffer(void *buffer, unsigned long len)
{
	int i;
	unsigned char *ptr = (unsigned char *)buffer;

	if (!ptr) {
		fprintf(stderr, "<%s>: Invalid buffer...\n", __func__);
		return;
	}

	printf("Read buffer content:\n");
	for (i = 0; i < len; i++)
		printf("0x%x ", ptr[i]);
	printf("\n");
}

int ion_export_buffer_fd(struct ion_buffer_info *ion_info)
{
	int i, ret, ionfd, buffer_fd;
	unsigned int heap_id;
	unsigned long maplen;
	unsigned char *map_buffer;
	struct ion_allocation_data alloc_data;
	struct ion_heap_query query;
	struct ion_heap_data heap_data[MAX_HEAP_COUNT];

	if (!ion_info) {
		fprintf(stderr, "<%s>: Invalid ion info\n", __func__);
		return -1;
	}

	/* Create an ION client */
	ionfd = open(ION_DEVICE, O_RDWR);
	if (ionfd < 0) {
		fprintf(stderr, "<%s>: Failed to open ion client: %s\n",
			__func__, strerror(errno));
		return -1;
	}

	memset(&query, 0, sizeof(query));
	query.cnt = MAX_HEAP_COUNT;
	query.heaps = (unsigned long int)&heap_data[0];
	/* Query ION heap_id_mask from ION heap */
	ret = ioctl(ionfd, ION_IOC_HEAP_QUERY, &query);
	if (ret < 0) {
		fprintf(stderr, "<%s>: Failed: ION_IOC_HEAP_QUERY: %s\n",
			__func__, strerror(errno));
		goto err_query;
	}

	heap_id = MAX_HEAP_COUNT + 1;
	for (i = 0; i < query.cnt; i++) {
		if (heap_data[i].type == ion_info->heap_type) {
			heap_id = heap_data[i].heap_id;
			break;
		}
	}

	if (heap_id > MAX_HEAP_COUNT) {
		fprintf(stderr, "<%s>: ERROR: heap type does not exists\n",
			__func__);
		goto err_heap;
	}

	alloc_data.len = ion_info->heap_size;
	alloc_data.heap_id_mask = 1 << heap_id;
	alloc_data.flags = ion_info->flag_type;

	/* Allocate memory for this ION client as per heap_type */
	ret = ioctl(ionfd, ION_IOC_ALLOC, &alloc_data);
	if (ret < 0) {
		fprintf(stderr, "<%s>: Failed: ION_IOC_ALLOC: %s\n",
			__func__, strerror(errno));
		goto err_alloc;
	}

	/* This will return a valid buffer fd */
	buffer_fd = alloc_data.fd;
	maplen = alloc_data.len;

	if (buffer_fd < 0 || maplen <= 0) {
		fprintf(stderr, "<%s>: Invalid map data, fd: %d, len: %ld\n",
			__func__, buffer_fd, maplen);
		goto err_fd_data;
	}

	/* Create memory mapped buffer for the buffer fd */
	map_buffer = (unsigned char *)mmap(NULL, maplen, PROT_READ|PROT_WRITE,
			MAP_SHARED, buffer_fd, 0);
	if (map_buffer == MAP_FAILED) {
		fprintf(stderr, "<%s>: Failed: mmap: %s\n",
			__func__, strerror(errno));
		goto err_mmap;
	}

	ion_info->ionfd = ionfd;
	ion_info->buffd = buffer_fd;
	ion_info->buffer = map_buffer;
	ion_info->buflen = maplen;

	return 0;

	munmap(map_buffer, maplen);

err_fd_data:
err_mmap:
	/* in case of error: close the buffer fd */
	if (buffer_fd)
		close(buffer_fd);

err_query:
err_heap:
err_alloc:
	/* In case of error: close the ion client fd */
	if (ionfd)
		close(ionfd);

	return -1;
}

int ion_import_buffer_fd(struct ion_buffer_info *ion_info)
{
	int buffd;
	unsigned char *map_buf;
	unsigned long map_len;

	if (!ion_info) {
		fprintf(stderr, "<%s>: Invalid ion info\n", __func__);
		return -1;
	}

	map_len = ion_info->buflen;
	buffd = ion_info->buffd;

	if (buffd < 0 || map_len <= 0) {
		fprintf(stderr, "<%s>: Invalid map data, fd: %d, len: %ld\n",
			__func__, buffd, map_len);
		goto err_buffd;
	}

	map_buf = (unsigned char *)mmap(NULL, map_len, PROT_READ|PROT_WRITE,
			MAP_SHARED, buffd, 0);
	if (map_buf == MAP_FAILED) {
		printf("<%s>: Failed - mmap: %s\n",
			__func__, strerror(errno));
		goto err_mmap;
	}

	ion_info->buffer = map_buf;
	ion_info->buflen = map_len;

	return 0;

err_mmap:
	if (buffd)
		close(buffd);

err_buffd:
	return -1;
}

void ion_close_buffer_fd(struct ion_buffer_info *ion_info)
{
	if (ion_info) {
		/* unmap the buffer properly in the end */
		munmap(ion_info->buffer, ion_info->buflen);
		/* close the buffer fd */
		if (ion_info->buffd > 0)
			close(ion_info->buffd);
		/* Finally, close the client fd */
		if (ion_info->ionfd > 0)
			close(ion_info->ionfd);
	}
}

int socket_send_fd(struct socket_info *info)
{
	int status;
	int fd, sockfd;
	struct socketdata skdata;

	if (!info) {
		fprintf(stderr, "<%s>: Invalid socket info\n", __func__);
		return -1;
	}

	sockfd = info->sockfd;
	fd = info->datafd;
	memset(&skdata, 0, sizeof(skdata));
	skdata.data = fd;
	skdata.len = sizeof(skdata.data);
	status = sendtosocket(sockfd, &skdata);
	if (status < 0) {
		fprintf(stderr, "<%s>: Failed: sendtosocket\n", __func__);
		return -1;
	}

	return 0;
}

int socket_receive_fd(struct socket_info *info)
{
	int status;
	int fd, sockfd;
	struct socketdata skdata;

	if (!info) {
		fprintf(stderr, "<%s>: Invalid socket info\n", __func__);
		return -1;
	}

	sockfd = info->sockfd;
	memset(&skdata, 0, sizeof(skdata));
	status = receivefromsocket(sockfd, &skdata);
	if (status < 0) {
		fprintf(stderr, "<%s>: Failed: receivefromsocket\n", __func__);
		return -1;
	}

	fd = (int)skdata.data;
	info->datafd = fd;

	return status;
}
