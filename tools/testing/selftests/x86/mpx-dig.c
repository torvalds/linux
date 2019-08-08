// SPDX-License-Identifier: GPL-2.0
/*
 * Written by Dave Hansen <dave.hansen@intel.com>
 */

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include "mpx-debug.h"
#include "mpx-mm.h"
#include "mpx-hw.h"

unsigned long bounds_dir_global;

#define mpx_dig_abort()	__mpx_dig_abort(__FILE__, __func__, __LINE__)
static void inline __mpx_dig_abort(const char *file, const char *func, int line)
{
	fprintf(stderr, "MPX dig abort @ %s::%d in %s()\n", file, line, func);
	printf("MPX dig abort @ %s::%d in %s()\n", file, line, func);
	abort();
}

/*
 * run like this (BDIR finds the probably bounds directory):
 *
 *	BDIR="$(cat /proc/$pid/smaps | grep -B1 2097152 \
 *		| head -1 | awk -F- '{print $1}')";
 *	./mpx-dig $pid 0x$BDIR
 *
 * NOTE:
 *	assumes that the only 2097152-kb VMA is the bounds dir
 */

long nr_incore(void *ptr, unsigned long size_bytes)
{
	int i;
	long ret = 0;
	long vec_len = size_bytes / PAGE_SIZE;
	unsigned char *vec = malloc(vec_len);
	int incore_ret;

	if (!vec)
		mpx_dig_abort();

	incore_ret = mincore(ptr, size_bytes, vec);
	if (incore_ret) {
		printf("mincore ret: %d\n", incore_ret);
		perror("mincore");
		mpx_dig_abort();
	}
	for (i = 0; i < vec_len; i++)
		ret += vec[i];
	free(vec);
	return ret;
}

int open_proc(int pid, char *file)
{
	static char buf[100];
	int fd;

	snprintf(&buf[0], sizeof(buf), "/proc/%d/%s", pid, file);
	fd = open(&buf[0], O_RDONLY);
	if (fd < 0)
		perror(buf);

	return fd;
}

struct vaddr_range {
	unsigned long start;
	unsigned long end;
};
struct vaddr_range *ranges;
int nr_ranges_allocated;
int nr_ranges_populated;
int last_range = -1;

int __pid_load_vaddrs(int pid)
{
	int ret = 0;
	int proc_maps_fd = open_proc(pid, "maps");
	char linebuf[10000];
	unsigned long start;
	unsigned long end;
	char rest[1000];
	FILE *f = fdopen(proc_maps_fd, "r");

	if (!f)
		mpx_dig_abort();
	nr_ranges_populated = 0;
	while (!feof(f)) {
		char *readret = fgets(linebuf, sizeof(linebuf), f);
		int parsed;

		if (readret == NULL) {
			if (feof(f))
				break;
			mpx_dig_abort();
		}

		parsed = sscanf(linebuf, "%lx-%lx%s", &start, &end, rest);
		if (parsed != 3)
			mpx_dig_abort();

		dprintf4("result[%d]: %lx-%lx<->%s\n", parsed, start, end, rest);
		if (nr_ranges_populated >= nr_ranges_allocated) {
			ret = -E2BIG;
			break;
		}
		ranges[nr_ranges_populated].start = start;
		ranges[nr_ranges_populated].end = end;
		nr_ranges_populated++;
	}
	last_range = -1;
	fclose(f);
	close(proc_maps_fd);
	return ret;
}

int pid_load_vaddrs(int pid)
{
	int ret;

	dprintf2("%s(%d)\n", __func__, pid);
	if (!ranges) {
		nr_ranges_allocated = 4;
		ranges = malloc(nr_ranges_allocated * sizeof(ranges[0]));
		dprintf2("%s(%d) allocated %d ranges @ %p\n", __func__, pid,
			 nr_ranges_allocated, ranges);
		assert(ranges != NULL);
	}
	do {
		ret = __pid_load_vaddrs(pid);
		if (!ret)
			break;
		if (ret == -E2BIG) {
			dprintf2("%s(%d) need to realloc\n", __func__, pid);
			nr_ranges_allocated *= 2;
			ranges = realloc(ranges,
					nr_ranges_allocated * sizeof(ranges[0]));
			dprintf2("%s(%d) allocated %d ranges @ %p\n", __func__,
					pid, nr_ranges_allocated, ranges);
			assert(ranges != NULL);
			dprintf1("reallocating to hold %d ranges\n", nr_ranges_allocated);
		}
	} while (1);

	dprintf2("%s(%d) done\n", __func__, pid);

	return ret;
}

static inline int vaddr_in_range(unsigned long vaddr, struct vaddr_range *r)
{
	if (vaddr < r->start)
		return 0;
	if (vaddr >= r->end)
		return 0;
	return 1;
}

static inline int vaddr_mapped_by_range(unsigned long vaddr)
{
	int i;

	if (last_range > 0 && vaddr_in_range(vaddr, &ranges[last_range]))
		return 1;

	for (i = 0; i < nr_ranges_populated; i++) {
		struct vaddr_range *r = &ranges[i];

		if (vaddr_in_range(vaddr, r))
			continue;
		last_range = i;
		return 1;
	}
	return 0;
}

const int bt_entry_size_bytes = sizeof(unsigned long) * 4;

void *read_bounds_table_into_buf(unsigned long table_vaddr)
{
#ifdef MPX_DIG_STANDALONE
	static char bt_buf[MPX_BOUNDS_TABLE_SIZE_BYTES];
	off_t seek_ret = lseek(fd, table_vaddr, SEEK_SET);
	if (seek_ret != table_vaddr)
		mpx_dig_abort();

	int read_ret = read(fd, &bt_buf, sizeof(bt_buf));
	if (read_ret != sizeof(bt_buf))
		mpx_dig_abort();
	return &bt_buf;
#else
	return (void *)table_vaddr;
#endif
}

int dump_table(unsigned long table_vaddr, unsigned long base_controlled_vaddr,
		unsigned long bde_vaddr)
{
	unsigned long offset_inside_bt;
	int nr_entries = 0;
	int do_abort = 0;
	char *bt_buf;

	dprintf3("%s() base_controlled_vaddr: 0x%012lx bde_vaddr: 0x%012lx\n",
			__func__, base_controlled_vaddr, bde_vaddr);

	bt_buf = read_bounds_table_into_buf(table_vaddr);

	dprintf4("%s() read done\n", __func__);

	for (offset_inside_bt = 0;
	     offset_inside_bt < MPX_BOUNDS_TABLE_SIZE_BYTES;
	     offset_inside_bt += bt_entry_size_bytes) {
		unsigned long bt_entry_index;
		unsigned long bt_entry_controls;
		unsigned long this_bt_entry_for_vaddr;
		unsigned long *bt_entry_buf;
		int i;

		dprintf4("%s() offset_inside_bt: 0x%lx of 0x%llx\n", __func__,
			offset_inside_bt, MPX_BOUNDS_TABLE_SIZE_BYTES);
		bt_entry_buf = (void *)&bt_buf[offset_inside_bt];
		if (!bt_buf) {
			printf("null bt_buf\n");
			mpx_dig_abort();
		}
		if (!bt_entry_buf) {
			printf("null bt_entry_buf\n");
			mpx_dig_abort();
		}
		dprintf4("%s() reading *bt_entry_buf @ %p\n", __func__,
				bt_entry_buf);
		if (!bt_entry_buf[0] &&
		    !bt_entry_buf[1] &&
		    !bt_entry_buf[2] &&
		    !bt_entry_buf[3])
			continue;

		nr_entries++;

		bt_entry_index = offset_inside_bt/bt_entry_size_bytes;
		bt_entry_controls = sizeof(void *);
		this_bt_entry_for_vaddr =
			base_controlled_vaddr + bt_entry_index*bt_entry_controls;
		/*
		 * We sign extend vaddr bits 48->63 which effectively
		 * creates a hole in the virtual address space.
		 * This calculation corrects for the hole.
		 */
		if (this_bt_entry_for_vaddr > 0x00007fffffffffffUL)
			this_bt_entry_for_vaddr |= 0xffff800000000000;

		if (!vaddr_mapped_by_range(this_bt_entry_for_vaddr)) {
			printf("bt_entry_buf: %p\n", bt_entry_buf);
			printf("there is a bte for %lx but no mapping\n",
					this_bt_entry_for_vaddr);
			printf("	  bde   vaddr: %016lx\n", bde_vaddr);
			printf("base_controlled_vaddr: %016lx\n", base_controlled_vaddr);
			printf("	  table_vaddr: %016lx\n", table_vaddr);
			printf("	  entry vaddr: %016lx @ offset %lx\n",
				table_vaddr + offset_inside_bt, offset_inside_bt);
			do_abort = 1;
			mpx_dig_abort();
		}
		if (DEBUG_LEVEL < 4)
			continue;

		printf("table entry[%lx]: ", offset_inside_bt);
		for (i = 0; i < bt_entry_size_bytes; i += sizeof(unsigned long))
			printf("0x%016lx ", bt_entry_buf[i]);
		printf("\n");
	}
	if (do_abort)
		mpx_dig_abort();
	dprintf4("%s() done\n",  __func__);
	return nr_entries;
}

int search_bd_buf(char *buf, int len_bytes, unsigned long bd_offset_bytes,
		int *nr_populated_bdes)
{
	unsigned long i;
	int total_entries = 0;

	dprintf3("%s(%p, %x, %lx, ...) buf end: %p\n", __func__, buf,
			len_bytes, bd_offset_bytes, buf + len_bytes);

	for (i = 0; i < len_bytes; i += sizeof(unsigned long)) {
		unsigned long bd_index = (bd_offset_bytes + i) / sizeof(unsigned long);
		unsigned long *bounds_dir_entry_ptr = (unsigned long *)&buf[i];
		unsigned long bounds_dir_entry;
		unsigned long bd_for_vaddr;
		unsigned long bt_start;
		unsigned long bt_tail;
		int nr_entries;

		dprintf4("%s() loop i: %ld bounds_dir_entry_ptr: %p\n", __func__, i,
				bounds_dir_entry_ptr);

		bounds_dir_entry = *bounds_dir_entry_ptr;
		if (!bounds_dir_entry) {
			dprintf4("no bounds dir at index 0x%lx / 0x%lx "
				 "start at offset:%lx %lx\n", bd_index, bd_index,
					bd_offset_bytes, i);
			continue;
		}
		dprintf3("found bounds_dir_entry: 0x%lx @ "
			 "index 0x%lx buf ptr: %p\n", bounds_dir_entry, i,
					&buf[i]);
		/* mask off the enable bit: */
		bounds_dir_entry &= ~0x1;
		(*nr_populated_bdes)++;
		dprintf4("nr_populated_bdes: %p\n", nr_populated_bdes);
		dprintf4("*nr_populated_bdes: %d\n", *nr_populated_bdes);

		bt_start = bounds_dir_entry;
		bt_tail = bounds_dir_entry + MPX_BOUNDS_TABLE_SIZE_BYTES - 1;
		if (!vaddr_mapped_by_range(bt_start)) {
			printf("bounds directory 0x%lx points to nowhere\n",
					bounds_dir_entry);
			mpx_dig_abort();
		}
		if (!vaddr_mapped_by_range(bt_tail)) {
			printf("bounds directory end 0x%lx points to nowhere\n",
					bt_tail);
			mpx_dig_abort();
		}
		/*
		 * Each bounds directory entry controls 1MB of virtual address
		 * space.  This variable is the virtual address in the process
		 * of the beginning of the area controlled by this bounds_dir.
		 */
		bd_for_vaddr = bd_index * (1UL<<20);

		nr_entries = dump_table(bounds_dir_entry, bd_for_vaddr,
				bounds_dir_global+bd_offset_bytes+i);
		total_entries += nr_entries;
		dprintf5("dir entry[%4ld @ %p]: 0x%lx %6d entries "
			 "total this buf: %7d bd_for_vaddrs: 0x%lx -> 0x%lx\n",
				bd_index, buf+i,
				bounds_dir_entry, nr_entries, total_entries,
				bd_for_vaddr, bd_for_vaddr + (1UL<<20));
	}
	dprintf3("%s(%p, %x, %lx, ...) done\n", __func__, buf, len_bytes,
			bd_offset_bytes);
	return total_entries;
}

int proc_pid_mem_fd = -1;

void *fill_bounds_dir_buf_other(long byte_offset_inside_bounds_dir,
			   long buffer_size_bytes, void *buffer)
{
	unsigned long seekto = bounds_dir_global + byte_offset_inside_bounds_dir;
	int read_ret;
	off_t seek_ret = lseek(proc_pid_mem_fd, seekto, SEEK_SET);

	if (seek_ret != seekto)
		mpx_dig_abort();

	read_ret = read(proc_pid_mem_fd, buffer, buffer_size_bytes);
	/* there shouldn't practically be short reads of /proc/$pid/mem */
	if (read_ret != buffer_size_bytes)
		mpx_dig_abort();

	return buffer;
}
void *fill_bounds_dir_buf_self(long byte_offset_inside_bounds_dir,
			   long buffer_size_bytes, void *buffer)

{
	unsigned char vec[buffer_size_bytes / PAGE_SIZE];
	char *dig_bounds_dir_ptr =
		(void *)(bounds_dir_global + byte_offset_inside_bounds_dir);
	/*
	 * use mincore() to quickly find the areas of the bounds directory
	 * that have memory and thus will be worth scanning.
	 */
	int incore_ret;

	int incore = 0;
	int i;

	dprintf4("%s() dig_bounds_dir_ptr: %p\n", __func__, dig_bounds_dir_ptr);

	incore_ret = mincore(dig_bounds_dir_ptr, buffer_size_bytes, &vec[0]);
	if (incore_ret) {
		printf("mincore ret: %d\n", incore_ret);
		perror("mincore");
		mpx_dig_abort();
	}
	for (i = 0; i < sizeof(vec); i++)
		incore += vec[i];
	dprintf4("%s() total incore: %d\n", __func__, incore);
	if (!incore)
		return NULL;
	dprintf3("%s() total incore: %d\n", __func__, incore);
	return dig_bounds_dir_ptr;
}

int inspect_pid(int pid)
{
	static int dig_nr;
	long offset_inside_bounds_dir;
	char bounds_dir_buf[sizeof(unsigned long) * (1UL << 15)];
	char *dig_bounds_dir_ptr;
	int total_entries = 0;
	int nr_populated_bdes = 0;
	int inspect_self;

	if (getpid() == pid) {
		dprintf4("inspecting self\n");
		inspect_self = 1;
	} else {
		dprintf4("inspecting pid %d\n", pid);
		mpx_dig_abort();
	}

	for (offset_inside_bounds_dir = 0;
	     offset_inside_bounds_dir < MPX_BOUNDS_TABLE_SIZE_BYTES;
	     offset_inside_bounds_dir += sizeof(bounds_dir_buf)) {
		static int bufs_skipped;
		int this_entries;

		if (inspect_self) {
			dig_bounds_dir_ptr =
				fill_bounds_dir_buf_self(offset_inside_bounds_dir,
							 sizeof(bounds_dir_buf),
							 &bounds_dir_buf[0]);
		} else {
			dig_bounds_dir_ptr =
				fill_bounds_dir_buf_other(offset_inside_bounds_dir,
							  sizeof(bounds_dir_buf),
							  &bounds_dir_buf[0]);
		}
		if (!dig_bounds_dir_ptr) {
			bufs_skipped++;
			continue;
		}
		this_entries = search_bd_buf(dig_bounds_dir_ptr,
					sizeof(bounds_dir_buf),
					offset_inside_bounds_dir,
					&nr_populated_bdes);
		total_entries += this_entries;
	}
	printf("mpx dig (%3d) complete, SUCCESS (%8d / %4d)\n", ++dig_nr,
			total_entries, nr_populated_bdes);
	return total_entries + nr_populated_bdes;
}

#ifdef MPX_DIG_REMOTE
int main(int argc, char **argv)
{
	int err;
	char *c;
	unsigned long bounds_dir_entry;
	int pid;

	printf("mpx-dig starting...\n");
	err = sscanf(argv[1], "%d", &pid);
	printf("parsing: '%s', err: %d\n", argv[1], err);
	if (err != 1)
		mpx_dig_abort();

	err = sscanf(argv[2], "%lx", &bounds_dir_global);
	printf("parsing: '%s': %d\n", argv[2], err);
	if (err != 1)
		mpx_dig_abort();

	proc_pid_mem_fd = open_proc(pid, "mem");
	if (proc_pid_mem_fd < 0)
		mpx_dig_abort();

	inspect_pid(pid);
	return 0;
}
#endif

long inspect_me(struct mpx_bounds_dir *bounds_dir)
{
	int pid = getpid();

	pid_load_vaddrs(pid);
	bounds_dir_global = (unsigned long)bounds_dir;
	dprintf4("enter %s() bounds dir: %p\n", __func__, bounds_dir);
	return inspect_pid(pid);
}
