/*-
 * Copyright (C) 2010-2014 Nathan Whitehorn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <fdt_platform.h>

#define _KERNEL
#include <machine/cpufunc.h>
#include "bootstrap.h"
#include "host_syscall.h"


struct arch_switch	archsw;
extern void *_end;

int kboot_getdev(void **vdev, const char *devspec, const char **path);
ssize_t kboot_copyin(const void *src, vm_offset_t dest, const size_t len);
ssize_t kboot_copyout(vm_offset_t src, void *dest, const size_t len);
ssize_t kboot_readin(const int fd, vm_offset_t dest, const size_t len);
int kboot_autoload(void);
uint64_t kboot_loadaddr(u_int type, void *data, uint64_t addr);
int kboot_setcurrdev(struct env_var *ev, int flags, const void *value);
static void kboot_kseg_get(int *nseg, void **ptr);

extern int command_fdt_internal(int argc, char *argv[]);

struct region_desc {
	uint64_t start;
	uint64_t end;
};

static uint64_t
kboot_get_phys_load_segment(void)
{
	int fd;
	uint64_t entry[2];
	static uint64_t load_segment = ~(0UL);
	uint64_t val_64;
	uint32_t val_32;
	struct region_desc rsvd_reg[32];
	int rsvd_reg_cnt = 0;
	int ret, a, b;
	uint64_t start, end;

	if (load_segment == ~(0UL)) {

		/* Default load address is 0x00000000 */
		load_segment = 0UL;

		/* Read reserved regions */
		fd = host_open("/proc/device-tree/reserved-ranges", O_RDONLY, 0);
		if (fd >= 0) {
			while (host_read(fd, &entry[0], sizeof(entry)) == sizeof(entry)) {
				rsvd_reg[rsvd_reg_cnt].start = be64toh(entry[0]);
				rsvd_reg[rsvd_reg_cnt].end =
				    be64toh(entry[1]) + rsvd_reg[rsvd_reg_cnt].start - 1;
				rsvd_reg_cnt++;
			}
			host_close(fd);
		}
		/* Read where the kernel ends */
		fd = host_open("/proc/device-tree/chosen/linux,kernel-end", O_RDONLY, 0);
		if (fd >= 0) {
			ret = host_read(fd, &val_64, sizeof(val_64));

			if (ret == sizeof(uint64_t)) {
				rsvd_reg[rsvd_reg_cnt].start = 0;
				rsvd_reg[rsvd_reg_cnt].end = be64toh(val_64) - 1;
			} else {
				memcpy(&val_32, &val_64, sizeof(val_32));
				rsvd_reg[rsvd_reg_cnt].start = 0;
				rsvd_reg[rsvd_reg_cnt].end = be32toh(val_32) - 1;
			}
			rsvd_reg_cnt++;

			host_close(fd);
		}
		/* Read memory size (SOCKET0 only) */
		fd = host_open("/proc/device-tree/memory@0/reg", O_RDONLY, 0);
		if (fd < 0)
			fd = host_open("/proc/device-tree/memory/reg", O_RDONLY, 0);
		if (fd >= 0) {
			ret = host_read(fd, &entry, sizeof(entry));

			/* Memory range in start:length format */
			entry[0] = be64toh(entry[0]);
			entry[1] = be64toh(entry[1]);

			/* Reserve everything what is before start */
			if (entry[0] != 0) {
				rsvd_reg[rsvd_reg_cnt].start = 0;
				rsvd_reg[rsvd_reg_cnt].end = entry[0] - 1;
				rsvd_reg_cnt++;
			}
			/* Reserve everything what is after end */
			if (entry[1] != 0xffffffffffffffffUL) {
				rsvd_reg[rsvd_reg_cnt].start = entry[0] + entry[1];
				rsvd_reg[rsvd_reg_cnt].end = 0xffffffffffffffffUL;
				rsvd_reg_cnt++;
			}

			host_close(fd);
		}

		/* Sort entries in ascending order (bubble) */
		for (a = rsvd_reg_cnt - 1; a > 0; a--) {
			for (b = 0; b < a; b++) {
				if (rsvd_reg[b].start > rsvd_reg[b + 1].start) {
					struct region_desc tmp;
					tmp = rsvd_reg[b];
					rsvd_reg[b] = rsvd_reg[b + 1];
					rsvd_reg[b + 1] = tmp;
				}
			}
		}

		/* Join overlapping/adjacent regions */
		for (a = 0; a < rsvd_reg_cnt - 1; ) {

			if ((rsvd_reg[a + 1].start >= rsvd_reg[a].start) &&
			    ((rsvd_reg[a + 1].start - 1) <= rsvd_reg[a].end)) {
				/* We have overlapping/adjacent regions! */
				rsvd_reg[a].end =
				    MAX(rsvd_reg[a].end, rsvd_reg[a + a].end);

				for (b = a + 1; b < rsvd_reg_cnt - 1; b++)
					rsvd_reg[b] = rsvd_reg[b + 1];
				rsvd_reg_cnt--;
			} else
				a++;
		}

		/* Find the first free region */
		if (rsvd_reg_cnt > 0) {
			start = 0;
			end = rsvd_reg[0].start;
			for (a = 0; a < rsvd_reg_cnt - 1; a++) {
				if ((start >= rsvd_reg[a].start) &&
				    (start <= rsvd_reg[a].end)) {
					start = rsvd_reg[a].end + 1;
					end = rsvd_reg[a + 1].start;
				} else
					break;
			}

			if (start != end) {
				uint64_t align = 64UL*1024UL*1024UL;

				/* Align both to 64MB boundary */
				start = (start + align - 1UL) & ~(align - 1UL);
				end = ((end + 1UL) & ~(align - 1UL)) - 1UL;

				if (start < end)
					load_segment = start;
			}
		}
	}

	return (load_segment);
}

uint8_t
kboot_get_kernel_machine_bits(void)
{
	static uint8_t bits = 0;
	struct old_utsname utsname;
	int ret;

	if (bits == 0) {
		/* Default is 32-bit kernel */
		bits = 32;

		/* Try to get system type */
		memset(&utsname, 0, sizeof(utsname));
		ret = host_uname(&utsname);
		if (ret == 0) {
			if (strcmp(utsname.machine, "ppc64") == 0)
				bits = 64;
			else if (strcmp(utsname.machine, "ppc64le") == 0)
				bits = 64;
		}
	}

	return (bits);
}

int
kboot_getdev(void **vdev, const char *devspec, const char **path)
{
	int i;
	const char *devpath, *filepath;
	struct devsw *dv;
	struct devdesc *desc;

	if (strchr(devspec, ':') != NULL) {
		devpath = devspec;
		filepath = strchr(devspec, ':') + 1;
	} else {
		devpath = getenv("currdev");
		filepath = devspec;
	}

	for (i = 0; (dv = devsw[i]) != NULL; i++) {
		if (strncmp(dv->dv_name, devpath, strlen(dv->dv_name)) == 0)
			goto found;
	}
	return (ENOENT);

found:
	if (path != NULL && filepath != NULL)
		*path = filepath;
	else if (path != NULL)
		*path = strchr(devspec, ':') + 1;

	if (vdev != NULL) {
		desc = malloc(sizeof(*desc));
		desc->d_dev = dv;
		desc->d_unit = 0;
		desc->d_opendata = strdup(devpath);
		*vdev = desc;
	}

	return (0);
}

int
main(int argc, const char **argv)
{
	void *heapbase;
	const size_t heapsize = 15*1024*1024;
	const char *bootdev;

	/*
	 * Set the heap to one page after the end of the loader.
	 */
	heapbase = host_getmem(heapsize);
	setheap(heapbase, heapbase + heapsize);

	/*
	 * Set up console.
	 */
	cons_probe();

	/* Choose bootdev if provided */
	if (argc > 1)
		bootdev = argv[1];
	else
		bootdev = "";

	printf("Boot device: %s\n", bootdev);

	archsw.arch_getdev = kboot_getdev;
	archsw.arch_copyin = kboot_copyin;
	archsw.arch_copyout = kboot_copyout;
	archsw.arch_readin = kboot_readin;
	archsw.arch_autoload = kboot_autoload;
	archsw.arch_loadaddr = kboot_loadaddr;
	archsw.arch_kexec_kseg_get = kboot_kseg_get;

	printf("\n%s", bootprog_info);

	setenv("currdev", bootdev, 1);
	setenv("loaddev", bootdev, 1);
	setenv("LINES", "24", 1);
	setenv("usefdt", "1", 1);

	interact();			/* doesn't return */

	return (0);
}

void
exit(int code)
{
	while (1); /* XXX: host_exit */
	__unreachable();
}

void
delay(int usecs)
{
	struct host_timeval tvi, tv;
	uint64_t ti, t;
	host_gettimeofday(&tvi, NULL);
	ti = tvi.tv_sec*1000000 + tvi.tv_usec;
	do {
		host_gettimeofday(&tv, NULL);
		t = tv.tv_sec*1000000 + tv.tv_usec;
	} while (t < ti + usecs);
}

time_t
getsecs(void)
{
	struct host_timeval tv;
	host_gettimeofday(&tv, NULL);
	return (tv.tv_sec);
}

time_t
time(time_t *tloc)
{
	time_t rv;
	
	rv = getsecs();
	if (tloc != NULL)
		*tloc = rv;

	return (rv);
}

struct kexec_segment {
	void *buf;
	int bufsz;
	void *mem;
	int memsz;
};

struct kexec_segment loaded_segments[128];
int nkexec_segments = 0;

static ssize_t
get_phys_buffer(vm_offset_t dest, const size_t len, void **buf)
{
	int i = 0;
	const size_t segsize = 4*1024*1024;

	for (i = 0; i < nkexec_segments; i++) {
		if (dest >= (vm_offset_t)loaded_segments[i].mem &&
		    dest < (vm_offset_t)loaded_segments[i].mem +
		    loaded_segments[i].memsz)
			goto out;
	}

	loaded_segments[nkexec_segments].buf = host_getmem(segsize);
	loaded_segments[nkexec_segments].bufsz = segsize;
	loaded_segments[nkexec_segments].mem = (void *)rounddown2(dest,segsize);
	loaded_segments[nkexec_segments].memsz = segsize;

	i = nkexec_segments;
	nkexec_segments++;

out:
	*buf = loaded_segments[i].buf + (dest -
	    (vm_offset_t)loaded_segments[i].mem);
	return (min(len,loaded_segments[i].bufsz - (dest -
	    (vm_offset_t)loaded_segments[i].mem)));
}

ssize_t
kboot_copyin(const void *src, vm_offset_t dest, const size_t len)
{
	ssize_t segsize, remainder;
	void *destbuf;

	remainder = len;
	do {
		segsize = get_phys_buffer(dest, remainder, &destbuf);
		bcopy(src, destbuf, segsize);
		remainder -= segsize;
		src += segsize;
		dest += segsize;
	} while (remainder > 0);

	return (len);
}

ssize_t
kboot_copyout(vm_offset_t src, void *dest, const size_t len)
{
	ssize_t segsize, remainder;
	void *srcbuf;

	remainder = len;
	do {
		segsize = get_phys_buffer(src, remainder, &srcbuf);
		bcopy(srcbuf, dest, segsize);
		remainder -= segsize;
		src += segsize;
		dest += segsize;
	} while (remainder > 0);

	return (len);
}

ssize_t
kboot_readin(const int fd, vm_offset_t dest, const size_t len)
{
	void            *buf;
	size_t          resid, chunk, get;
	ssize_t         got;
	vm_offset_t     p;

	p = dest;

	chunk = min(PAGE_SIZE, len);
	buf = malloc(chunk);
	if (buf == NULL) {
		printf("kboot_readin: buf malloc failed\n");
		return (0);
	}

	for (resid = len; resid > 0; resid -= got, p += got) {
		get = min(chunk, resid);
		got = read(fd, buf, get);
		if (got <= 0) {
			if (got < 0)
				printf("kboot_readin: read failed\n");
			break;
		}

		kboot_copyin(buf, p, got);
	}

	free (buf);
	return (len - resid);
}

int
kboot_autoload(void)
{

	return (0);
}

uint64_t
kboot_loadaddr(u_int type, void *data, uint64_t addr)
{

	if (type == LOAD_ELF)
		addr = roundup(addr, PAGE_SIZE);
	else
		addr += kboot_get_phys_load_segment();

	return (addr);
}

static void
kboot_kseg_get(int *nseg, void **ptr)
{
#if 0
	int a;

	for (a = 0; a < nkexec_segments; a++) {
		printf("kseg_get: %jx %jx %jx %jx\n",
			(uintmax_t)loaded_segments[a].buf,
			(uintmax_t)loaded_segments[a].bufsz,
			(uintmax_t)loaded_segments[a].mem,
			(uintmax_t)loaded_segments[a].memsz);
	}
#endif

	*nseg = nkexec_segments;
	*ptr = &loaded_segments[0];
}

void
_start(int argc, const char **argv, char **env)
{
	register volatile void **sp asm("r1");
	main((int)sp[0], (const char **)&sp[1]);
}

/*
 * Since proper fdt command handling function is defined in fdt_loader_cmd.c,
 * and declaring it as extern is in contradiction with COMMAND_SET() macro
 * (which uses static pointer), we're defining wrapper function, which
 * calls the proper fdt handling routine.
 */
static int
command_fdt(int argc, char *argv[])
{

	return (command_fdt_internal(argc, argv));
}
        
COMMAND_SET(fdt, "fdt", "flattened device tree handling", command_fdt);

