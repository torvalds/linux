// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sdt.h>

#ifndef MADV_POPULATE_READ
#define MADV_POPULATE_READ 22
#endif

#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

int __attribute__((weak)) uprobe(void)
{
	return 0;
}

#define __PASTE(a, b) a##b
#define PASTE(a, b) __PASTE(a, b)

#define NAME(name, idx) PASTE(name, idx)

#define DEF(name, idx) int __attribute__((weak)) NAME(name, idx)(void) { return 0; }
#define CALL(name, idx) NAME(name, idx)();

#define F(body, name, idx) body(name, idx)

#define F10(body, name, idx) \
	F(body, PASTE(name, idx), 0) F(body, PASTE(name, idx), 1) F(body, PASTE(name, idx), 2) \
	F(body, PASTE(name, idx), 3) F(body, PASTE(name, idx), 4) F(body, PASTE(name, idx), 5) \
	F(body, PASTE(name, idx), 6) F(body, PASTE(name, idx), 7) F(body, PASTE(name, idx), 8) \
	F(body, PASTE(name, idx), 9)

#define F100(body, name, idx) \
	F10(body, PASTE(name, idx), 0) F10(body, PASTE(name, idx), 1) F10(body, PASTE(name, idx), 2) \
	F10(body, PASTE(name, idx), 3) F10(body, PASTE(name, idx), 4) F10(body, PASTE(name, idx), 5) \
	F10(body, PASTE(name, idx), 6) F10(body, PASTE(name, idx), 7) F10(body, PASTE(name, idx), 8) \
	F10(body, PASTE(name, idx), 9)

#define F1000(body, name, idx) \
	F100(body, PASTE(name, idx), 0) F100(body, PASTE(name, idx), 1) F100(body, PASTE(name, idx), 2) \
	F100(body, PASTE(name, idx), 3) F100(body, PASTE(name, idx), 4) F100(body, PASTE(name, idx), 5) \
	F100(body, PASTE(name, idx), 6) F100(body, PASTE(name, idx), 7) F100(body, PASTE(name, idx), 8) \
	F100(body, PASTE(name, idx), 9)

#define F10000(body, name, idx) \
	F1000(body, PASTE(name, idx), 0) F1000(body, PASTE(name, idx), 1) F1000(body, PASTE(name, idx), 2) \
	F1000(body, PASTE(name, idx), 3) F1000(body, PASTE(name, idx), 4) F1000(body, PASTE(name, idx), 5) \
	F1000(body, PASTE(name, idx), 6) F1000(body, PASTE(name, idx), 7) F1000(body, PASTE(name, idx), 8) \
	F1000(body, PASTE(name, idx), 9)

F10000(DEF, uprobe_multi_func_, 0)
F10000(DEF, uprobe_multi_func_, 1)
F10000(DEF, uprobe_multi_func_, 2)
F10000(DEF, uprobe_multi_func_, 3)
F10000(DEF, uprobe_multi_func_, 4)

static int bench(void)
{
	F10000(CALL, uprobe_multi_func_, 0)
	F10000(CALL, uprobe_multi_func_, 1)
	F10000(CALL, uprobe_multi_func_, 2)
	F10000(CALL, uprobe_multi_func_, 3)
	F10000(CALL, uprobe_multi_func_, 4)
	return 0;
}

#define PROBE STAP_PROBE(test, usdt);

#define PROBE10    PROBE PROBE PROBE PROBE PROBE \
		   PROBE PROBE PROBE PROBE PROBE
#define PROBE100   PROBE10 PROBE10 PROBE10 PROBE10 PROBE10 \
		   PROBE10 PROBE10 PROBE10 PROBE10 PROBE10
#define PROBE1000  PROBE100 PROBE100 PROBE100 PROBE100 PROBE100 \
		   PROBE100 PROBE100 PROBE100 PROBE100 PROBE100
#define PROBE10000 PROBE1000 PROBE1000 PROBE1000 PROBE1000 PROBE1000 \
		   PROBE1000 PROBE1000 PROBE1000 PROBE1000 PROBE1000

static int usdt(void)
{
	PROBE10000
	PROBE10000
	PROBE10000
	PROBE10000
	PROBE10000
	return 0;
}

extern char build_id_start[];
extern char build_id_end[];

int __attribute__((weak)) trigger_uprobe(bool build_id_resident)
{
	int page_sz = sysconf(_SC_PAGESIZE);
	void *addr;

	/* page-align build ID start */
	addr = (void *)((uintptr_t)&build_id_start & ~(page_sz - 1));

	/* to guarantee MADV_PAGEOUT work reliably, we need to ensure that
	 * memory range is mapped into current process, so we unconditionally
	 * do MADV_POPULATE_READ, and then MADV_PAGEOUT, if necessary
	 */
	madvise(addr, page_sz, MADV_POPULATE_READ);
	if (!build_id_resident)
		madvise(addr, page_sz, MADV_PAGEOUT);

	(void)uprobe();

	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		goto error;

	if (!strcmp("bench", argv[1]))
		return bench();
	if (!strcmp("usdt", argv[1]))
		return usdt();
	if (!strcmp("uprobe-paged-out", argv[1]))
		return trigger_uprobe(false /* page-out build ID */);
	if (!strcmp("uprobe-paged-in", argv[1]))
		return trigger_uprobe(true /* page-in build ID */);

error:
	fprintf(stderr, "usage: %s <bench|usdt>\n", argv[0]);
	return -1;
}
