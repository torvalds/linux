// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "defines.h"
#include "main.h"

void encl_delete(struct encl *encl)
{
	if (encl->encl_base)
		munmap((void *)encl->encl_base, encl->encl_size);

	if (encl->bin)
		munmap(encl->bin, encl->bin_size);

	if (encl->fd)
		close(encl->fd);

	if (encl->segment_tbl)
		free(encl->segment_tbl);

	memset(encl, 0, sizeof(*encl));
}

static bool encl_map_bin(const char *path, struct encl *encl)
{
	struct stat sb;
	void *bin;
	int ret;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)  {
		perror("open()");
		return false;
	}

	ret = stat(path, &sb);
	if (ret) {
		perror("stat()");
		goto err;
	}

	bin = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (bin == MAP_FAILED) {
		perror("mmap()");
		goto err;
	}

	encl->bin = bin;
	encl->bin_size = sb.st_size;

	close(fd);
	return true;

err:
	close(fd);
	return false;
}

static bool encl_ioc_create(struct encl *encl)
{
	struct sgx_secs *secs = &encl->secs;
	struct sgx_enclave_create ioc;
	int rc;

	assert(encl->encl_base != 0);

	memset(secs, 0, sizeof(*secs));
	secs->ssa_frame_size = 1;
	secs->attributes = SGX_ATTR_MODE64BIT;
	secs->xfrm = 3;
	secs->base = encl->encl_base;
	secs->size = encl->encl_size;

	ioc.src = (unsigned long)secs;
	rc = ioctl(encl->fd, SGX_IOC_ENCLAVE_CREATE, &ioc);
	if (rc) {
		fprintf(stderr, "SGX_IOC_ENCLAVE_CREATE failed: errno=%d\n",
			errno);
		munmap((void *)secs->base, encl->encl_size);
		return false;
	}

	return true;
}

static bool encl_ioc_add_pages(struct encl *encl, struct encl_segment *seg)
{
	struct sgx_enclave_add_pages ioc;
	struct sgx_secinfo secinfo;
	int rc;

	memset(&secinfo, 0, sizeof(secinfo));
	secinfo.flags = seg->flags;

	ioc.src = (uint64_t)encl->src + seg->offset;
	ioc.offset = seg->offset;
	ioc.length = seg->size;
	ioc.secinfo = (unsigned long)&secinfo;
	ioc.flags = SGX_PAGE_MEASURE;

	rc = ioctl(encl->fd, SGX_IOC_ENCLAVE_ADD_PAGES, &ioc);
	if (rc < 0) {
		fprintf(stderr, "SGX_IOC_ENCLAVE_ADD_PAGES failed: errno=%d.\n",
			errno);
		return false;
	}

	return true;
}

bool encl_load(const char *path, struct encl *encl)
{
	Elf64_Phdr *phdr_tbl;
	off_t src_offset;
	Elf64_Ehdr *ehdr;
	int i, j;
	int ret;

	memset(encl, 0, sizeof(*encl));

	ret = open("/dev/sgx_enclave", O_RDWR);
	if (ret < 0) {
		fprintf(stderr, "Unable to open /dev/sgx_enclave\n");
		goto err;
	}

	encl->fd = ret;

	if (!encl_map_bin(path, encl))
		goto err;

	ehdr = encl->bin;
	phdr_tbl = encl->bin + ehdr->e_phoff;

	for (i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *phdr = &phdr_tbl[i];

		if (phdr->p_type == PT_LOAD)
			encl->nr_segments++;
	}

	encl->segment_tbl = calloc(encl->nr_segments,
				   sizeof(struct encl_segment));
	if (!encl->segment_tbl)
		goto err;

	for (i = 0, j = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *phdr = &phdr_tbl[i];
		unsigned int flags = phdr->p_flags;
		struct encl_segment *seg;

		if (phdr->p_type != PT_LOAD)
			continue;

		seg = &encl->segment_tbl[j];

		if (!!(flags & ~(PF_R | PF_W | PF_X))) {
			fprintf(stderr,
				"%d has invalid segment flags 0x%02x.\n", i,
				phdr->p_flags);
			goto err;
		}

		if (j == 0 && flags != (PF_R | PF_W)) {
			fprintf(stderr,
				"TCS has invalid segment flags 0x%02x.\n",
				phdr->p_flags);
			goto err;
		}

		if (j == 0) {
			src_offset = phdr->p_offset & PAGE_MASK;

			seg->prot = PROT_READ | PROT_WRITE;
			seg->flags = SGX_PAGE_TYPE_TCS << 8;
		} else  {
			seg->prot = (phdr->p_flags & PF_R) ? PROT_READ : 0;
			seg->prot |= (phdr->p_flags & PF_W) ? PROT_WRITE : 0;
			seg->prot |= (phdr->p_flags & PF_X) ? PROT_EXEC : 0;
			seg->flags = (SGX_PAGE_TYPE_REG << 8) | seg->prot;
		}

		seg->offset = (phdr->p_offset & PAGE_MASK) - src_offset;
		seg->size = (phdr->p_filesz + PAGE_SIZE - 1) & PAGE_MASK;

		printf("0x%016lx 0x%016lx 0x%02x\n", seg->offset, seg->size,
		       seg->prot);

		j++;
	}

	assert(j == encl->nr_segments);

	encl->src = encl->bin + src_offset;
	encl->src_size = encl->segment_tbl[j - 1].offset +
			 encl->segment_tbl[j - 1].size;

	for (encl->encl_size = 4096; encl->encl_size < encl->src_size; )
		encl->encl_size <<= 1;

	return true;

err:
	encl_delete(encl);
	return false;
}

static bool encl_map_area(struct encl *encl)
{
	size_t encl_size = encl->encl_size;
	void *area;

	area = mmap(NULL, encl_size * 2, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == MAP_FAILED) {
		perror("mmap");
		return false;
	}

	encl->encl_base = ((uint64_t)area + encl_size - 1) & ~(encl_size - 1);

	munmap(area, encl->encl_base - (uint64_t)area);
	munmap((void *)(encl->encl_base + encl_size),
	       (uint64_t)area + encl_size - encl->encl_base);

	return true;
}

bool encl_build(struct encl *encl)
{
	struct sgx_enclave_init ioc;
	int ret;
	int i;

	if (!encl_map_area(encl))
		return false;

	if (!encl_ioc_create(encl))
		return false;

	/*
	 * Pages must be added before mapping VMAs because their permissions
	 * cap the VMA permissions.
	 */
	for (i = 0; i < encl->nr_segments; i++) {
		struct encl_segment *seg = &encl->segment_tbl[i];

		if (!encl_ioc_add_pages(encl, seg))
			return false;
	}

	ioc.sigstruct = (uint64_t)&encl->sigstruct;
	ret = ioctl(encl->fd, SGX_IOC_ENCLAVE_INIT, &ioc);
	if (ret) {
		fprintf(stderr, "SGX_IOC_ENCLAVE_INIT failed: errno=%d\n",
			errno);
		return false;
	}

	return true;
}
