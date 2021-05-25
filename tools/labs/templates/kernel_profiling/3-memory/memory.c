#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(int argc, char *argv[]) {

	int src_fd, dst_fd, mode;
	struct stat st;
	unsigned long to_write, size, blk_size;
	char *src_p, *dst_p, *buf;

	if (argc < 3) {
		printf("./memory <mode> <blk_size> <src> <dst>\n");
		return -1;
	}

	mode = atoi(argv[1]);
	blk_size = atoi(argv[2]);

	printf("mode %d blk_size %ld src %s dst %s\n",
		mode, blk_size, argv[3], argv[4]);

	src_fd = open(argv[3], O_RDONLY);
	if (src_fd < 0)
		return src_fd;

	stat(argv[3], &st);
	size = to_write = st.st_size;

	if (mode == 0) {
		src_p = mmap(NULL, size, PROT_READ, MAP_SHARED, src_fd, 0);
		if (src_p < 0)
			return -1;
	}

	dst_fd = open(argv[4], O_CREAT | O_RDWR | O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (dst_fd < 0)
		return -1;

	ftruncate(dst_fd, size);

	if (mode == 0) {
		dst_p = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, dst_fd, 0);
		if (dst_p < 0)
			return -1;
	}

	buf = malloc(blk_size);

	while (to_write > blk_size) {
		if (mode == 0) {
			memcpy(dst_p, src_p, blk_size);
		} else {
			pread(src_fd, buf, blk_size, size - to_write);
			pwrite(dst_fd, buf, blk_size, size - to_write);
		}

		to_write -= blk_size;
		dst_p += blk_size;
		src_p += blk_size;
	}

	if (mode == 0) {
		memcpy(dst_p, src_p, to_write);
		msync(dst_p - size, size, MS_SYNC);
	} else {
		pread(src_fd, buf, to_write, to_write);
		pwrite(dst_fd, buf, blk_size, to_write);
	}


	close(src_fd);
	close(dst_fd);

	return 0;
}
