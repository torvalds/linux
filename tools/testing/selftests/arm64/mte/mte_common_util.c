// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <linux/auxvec.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include <asm/hwcap.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#ifndef SA_EXPOSE_TAGBITS
#define SA_EXPOSE_TAGBITS 0x00000800
#endif

#define INIT_BUFFER_SIZE       256

struct mte_fault_cxt cur_mte_cxt;
bool mtefar_support;
bool mtestonly_support;
static unsigned int mte_cur_mode;
static unsigned int mte_cur_pstate_tco;
static bool mte_cur_stonly;

void mte_default_handler(int signum, siginfo_t *si, void *uc)
{
	struct sigaction sa;
	unsigned long addr = (unsigned long)si->si_addr;
	unsigned char si_tag, si_atag;

	sigaction(signum, NULL, &sa);

	if (sa.sa_flags & SA_EXPOSE_TAGBITS) {
		si_tag = MT_FETCH_TAG(addr);
		si_atag = MT_FETCH_ATAG(addr);
		addr = MT_CLEAR_TAGS(addr);
	} else {
		si_tag = 0;
		si_atag = 0;
	}

	if (signum == SIGSEGV) {
#ifdef DEBUG
		ksft_print_msg("INFO: SIGSEGV signal at pc=%lx, fault addr=%lx, si_code=%lx, si_tag=%x, si_atag=%x\n",
				((ucontext_t *)uc)->uc_mcontext.pc, addr, si->si_code, si_tag, si_atag);
#endif
		if (si->si_code == SEGV_MTEAERR) {
			if (cur_mte_cxt.trig_si_code == si->si_code)
				cur_mte_cxt.fault_valid = true;
			else
				ksft_print_msg("Got unexpected SEGV_MTEAERR at pc=%llx, fault addr=%lx\n",
					       ((ucontext_t *)uc)->uc_mcontext.pc,
					       addr);
			return;
		}
		/* Compare the context for precise error */
		else if (si->si_code == SEGV_MTESERR) {
			if ((!mtefar_support && si_atag) || (si_atag != MT_FETCH_ATAG(cur_mte_cxt.trig_addr))) {
				ksft_print_msg("Invalid MTE synchronous exception caught for address tag! si_tag=%x, si_atag: %x\n", si_tag, si_atag);
				exit(KSFT_FAIL);
			}

			if (cur_mte_cxt.trig_si_code == si->si_code &&
			    ((cur_mte_cxt.trig_range >= 0 &&
			      addr >= MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) &&
			      addr <= (MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) + cur_mte_cxt.trig_range)) ||
			     (cur_mte_cxt.trig_range < 0 &&
			      addr <= MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) &&
			      addr >= (MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) + cur_mte_cxt.trig_range)))) {
				cur_mte_cxt.fault_valid = true;
				/* Adjust the pc by 4 */
				((ucontext_t *)uc)->uc_mcontext.pc += 4;
			} else {
				ksft_print_msg("Invalid MTE synchronous exception caught!\n");
				exit(1);
			}
		} else {
			ksft_print_msg("Unknown SIGSEGV exception caught!\n");
			exit(1);
		}
	} else if (signum == SIGBUS) {
		ksft_print_msg("INFO: SIGBUS signal at pc=%llx, fault addr=%lx, si_code=%x\n",
				((ucontext_t *)uc)->uc_mcontext.pc, addr, si->si_code);
		if ((cur_mte_cxt.trig_range >= 0 &&
		     addr >= MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) &&
		     addr <= (MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) + cur_mte_cxt.trig_range)) ||
		    (cur_mte_cxt.trig_range < 0 &&
		     addr <= MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) &&
		     addr >= (MT_CLEAR_TAGS(cur_mte_cxt.trig_addr) + cur_mte_cxt.trig_range))) {
			cur_mte_cxt.fault_valid = true;
			/* Adjust the pc by 4 */
			((ucontext_t *)uc)->uc_mcontext.pc += 4;
		}
	}
}

void mte_register_signal(int signal, void (*handler)(int, siginfo_t *, void *),
			 bool export_tags)
{
	struct sigaction sa;

	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;

	if (export_tags && signal == SIGSEGV)
		sa.sa_flags |= SA_EXPOSE_TAGBITS;

	sigemptyset(&sa.sa_mask);
	sigaction(signal, &sa, NULL);
}

void mte_wait_after_trig(void)
{
	sched_yield();
}

void *mte_insert_tags(void *ptr, size_t size)
{
	void *tag_ptr;
	int align_size;

	if (!ptr || (unsigned long)(ptr) & MT_ALIGN_GRANULE) {
		ksft_print_msg("FAIL: Addr=%p: invalid\n", ptr);
		return NULL;
	}
	align_size = MT_ALIGN_UP(size);
	tag_ptr = mte_insert_random_tag(ptr);
	mte_set_tag_address_range(tag_ptr, align_size);
	return tag_ptr;
}

void mte_clear_tags(void *ptr, size_t size)
{
	if (!ptr || (unsigned long)(ptr) & MT_ALIGN_GRANULE) {
		ksft_print_msg("FAIL: Addr=%p: invalid\n", ptr);
		return;
	}
	size = MT_ALIGN_UP(size);
	ptr = (void *)MT_CLEAR_TAG((unsigned long)ptr);
	mte_clear_tag_address_range(ptr, size);
}

void *mte_insert_atag(void *ptr)
{
	unsigned char atag;

	atag =  mtefar_support ? (random() % MT_ATAG_MASK) + 1 : 0;
	return (void *)MT_SET_ATAG((unsigned long)ptr, atag);
}

void *mte_clear_atag(void *ptr)
{
	return (void *)MT_CLEAR_ATAG((unsigned long)ptr);
}

static void *__mte_allocate_memory_range(size_t size, int mem_type, int mapping,
					 size_t range_before, size_t range_after,
					 bool tags, int fd)
{
	void *ptr;
	int prot_flag, map_flag;
	size_t entire_size = size + range_before + range_after;

	switch (mem_type) {
	case USE_MALLOC:
		return malloc(entire_size) + range_before;
	case USE_MMAP:
	case USE_MPROTECT:
		break;
	default:
		ksft_print_msg("FAIL: Invalid allocate request\n");
		return NULL;
	}

	prot_flag = PROT_READ | PROT_WRITE;
	if (mem_type == USE_MMAP)
		prot_flag |= PROT_MTE;

	map_flag = mapping;
	if (fd == -1)
		map_flag = MAP_ANONYMOUS | map_flag;
	if (!(mapping & MAP_SHARED))
		map_flag |= MAP_PRIVATE;
	ptr = mmap(NULL, entire_size, prot_flag, map_flag, fd, 0);
	if (ptr == MAP_FAILED) {
		ksft_perror("mmap()");
		return NULL;
	}
	if (mem_type == USE_MPROTECT) {
		if (mprotect(ptr, entire_size, prot_flag | PROT_MTE)) {
			ksft_perror("mprotect(PROT_MTE)");
			munmap(ptr, size);
			return NULL;
		}
	}
	if (tags)
		ptr = mte_insert_tags(ptr + range_before, size);
	return ptr;
}

void *mte_allocate_memory_tag_range(size_t size, int mem_type, int mapping,
				    size_t range_before, size_t range_after)
{
	return __mte_allocate_memory_range(size, mem_type, mapping, range_before,
					   range_after, true, -1);
}

void *mte_allocate_memory(size_t size, int mem_type, int mapping, bool tags)
{
	return __mte_allocate_memory_range(size, mem_type, mapping, 0, 0, tags, -1);
}

void *mte_allocate_file_memory(size_t size, int mem_type, int mapping, bool tags, int fd)
{
	int index;
	char buffer[INIT_BUFFER_SIZE];

	if (mem_type != USE_MPROTECT && mem_type != USE_MMAP) {
		ksft_print_msg("FAIL: Invalid mmap file request\n");
		return NULL;
	}
	/* Initialize the file for mappable size */
	lseek(fd, 0, SEEK_SET);
	for (index = INIT_BUFFER_SIZE; index < size; index += INIT_BUFFER_SIZE) {
		if (write(fd, buffer, INIT_BUFFER_SIZE) != INIT_BUFFER_SIZE) {
			ksft_perror("initialising buffer");
			return NULL;
		}
	}
	index -= INIT_BUFFER_SIZE;
	if (write(fd, buffer, size - index) != size - index) {
		ksft_perror("initialising buffer");
		return NULL;
	}
	return __mte_allocate_memory_range(size, mem_type, mapping, 0, 0, tags, fd);
}

void *mte_allocate_file_memory_tag_range(size_t size, int mem_type, int mapping,
					 size_t range_before, size_t range_after, int fd)
{
	int index;
	char buffer[INIT_BUFFER_SIZE];
	int map_size = size + range_before + range_after;

	if (mem_type != USE_MPROTECT && mem_type != USE_MMAP) {
		ksft_print_msg("FAIL: Invalid mmap file request\n");
		return NULL;
	}
	/* Initialize the file for mappable size */
	lseek(fd, 0, SEEK_SET);
	for (index = INIT_BUFFER_SIZE; index < map_size; index += INIT_BUFFER_SIZE)
		if (write(fd, buffer, INIT_BUFFER_SIZE) != INIT_BUFFER_SIZE) {
			ksft_perror("initialising buffer");
			return NULL;
		}
	index -= INIT_BUFFER_SIZE;
	if (write(fd, buffer, map_size - index) != map_size - index) {
		ksft_perror("initialising buffer");
		return NULL;
	}
	return __mte_allocate_memory_range(size, mem_type, mapping, range_before,
					   range_after, true, fd);
}

static void __mte_free_memory_range(void *ptr, size_t size, int mem_type,
				    size_t range_before, size_t range_after, bool tags)
{
	switch (mem_type) {
	case USE_MALLOC:
		free(ptr - range_before);
		break;
	case USE_MMAP:
	case USE_MPROTECT:
		if (tags)
			mte_clear_tags(ptr, size);
		munmap(ptr - range_before, size + range_before + range_after);
		break;
	default:
		ksft_print_msg("FAIL: Invalid free request\n");
		break;
	}
}

void mte_free_memory_tag_range(void *ptr, size_t size, int mem_type,
			       size_t range_before, size_t range_after)
{
	__mte_free_memory_range(ptr, size, mem_type, range_before, range_after, true);
}

void mte_free_memory(void *ptr, size_t size, int mem_type, bool tags)
{
	__mte_free_memory_range(ptr, size, mem_type, 0, 0, tags);
}

void mte_initialize_current_context(int mode, uintptr_t ptr, ssize_t range)
{
	cur_mte_cxt.fault_valid = false;
	cur_mte_cxt.trig_addr = ptr;
	cur_mte_cxt.trig_range = range;
	if (mode == MTE_SYNC_ERR)
		cur_mte_cxt.trig_si_code = SEGV_MTESERR;
	else if (mode == MTE_ASYNC_ERR)
		cur_mte_cxt.trig_si_code = SEGV_MTEAERR;
	else
		cur_mte_cxt.trig_si_code = 0;
}

int mte_switch_mode(int mte_option, unsigned long incl_mask, bool stonly)
{
	unsigned long en = 0;

	switch (mte_option) {
	case MTE_NONE_ERR:
	case MTE_SYNC_ERR:
	case MTE_ASYNC_ERR:
		break;
	default:
		ksft_print_msg("FAIL: Invalid MTE option %x\n", mte_option);
		return -EINVAL;
	}

	if (incl_mask & ~MT_INCLUDE_TAG_MASK) {
		ksft_print_msg("FAIL: Invalid incl_mask %lx\n", incl_mask);
		return -EINVAL;
	}

	en = PR_TAGGED_ADDR_ENABLE;
	switch (mte_option) {
	case MTE_SYNC_ERR:
		en |= PR_MTE_TCF_SYNC;
		break;
	case MTE_ASYNC_ERR:
		en |= PR_MTE_TCF_ASYNC;
		break;
	case MTE_NONE_ERR:
		en |= PR_MTE_TCF_NONE;
		break;
	}

	if (mtestonly_support && stonly)
		en |= PR_MTE_STORE_ONLY;

	en |= (incl_mask << PR_MTE_TAG_SHIFT);
	/* Enable address tagging ABI, mte error reporting mode and tag inclusion mask. */
	if (prctl(PR_SET_TAGGED_ADDR_CTRL, en, 0, 0, 0) != 0) {
		ksft_print_msg("FAIL:prctl PR_SET_TAGGED_ADDR_CTRL for mte mode\n");
		return -EINVAL;
	}
	return 0;
}

int mte_default_setup(void)
{
	unsigned long hwcaps2 = getauxval(AT_HWCAP2);
	unsigned long hwcaps3 = getauxval(AT_HWCAP3);
	unsigned long en = 0;
	int ret;

	/* To generate random address tag */
	srandom(time(NULL));

	if (!(hwcaps2 & HWCAP2_MTE))
		ksft_exit_skip("MTE features unavailable\n");

	mtefar_support = !!(hwcaps3 & HWCAP3_MTE_FAR);

	if (hwcaps3 & HWCAP3_MTE_STORE_ONLY)
		mtestonly_support = true;

	/* Get current mte mode */
	ret = prctl(PR_GET_TAGGED_ADDR_CTRL, en, 0, 0, 0);
	if (ret < 0) {
		ksft_print_msg("FAIL:prctl PR_GET_TAGGED_ADDR_CTRL with error =%d\n", ret);
		return KSFT_FAIL;
	}
	if (ret & PR_MTE_TCF_SYNC)
		mte_cur_mode = MTE_SYNC_ERR;
	else if (ret & PR_MTE_TCF_ASYNC)
		mte_cur_mode = MTE_ASYNC_ERR;
	else if (ret & PR_MTE_TCF_NONE)
		mte_cur_mode = MTE_NONE_ERR;

	mte_cur_stonly = (ret & PR_MTE_STORE_ONLY) ? true : false;

	mte_cur_pstate_tco = mte_get_pstate_tco();
	/* Disable PSTATE.TCO */
	mte_disable_pstate_tco();
	return 0;
}

void mte_restore_setup(void)
{
	mte_switch_mode(mte_cur_mode, MTE_ALLOW_NON_ZERO_TAG, mte_cur_stonly);
	if (mte_cur_pstate_tco == MT_PSTATE_TCO_EN)
		mte_enable_pstate_tco();
	else if (mte_cur_pstate_tco == MT_PSTATE_TCO_DIS)
		mte_disable_pstate_tco();
}

int create_temp_file(void)
{
	int fd;
	char filename[] = "/dev/shm/tmp_XXXXXX";

	/* Create a file in the tmpfs filesystem */
	fd = mkstemp(&filename[0]);
	if (fd == -1) {
		ksft_perror(filename);
		ksft_print_msg("FAIL: Unable to open temporary file\n");
		return 0;
	}
	unlink(&filename[0]);
	return fd;
}
