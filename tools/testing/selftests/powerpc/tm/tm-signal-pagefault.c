// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020, Gustavo Luiz Duarte, IBM Corp.
 *
 * This test starts a transaction and triggers a signal, forcing a pagefault to
 * happen when the kernel signal handling code touches the user signal stack.
 *
 * In order to avoid pre-faulting the signal stack memory and to force the
 * pagefault to happen precisely in the kernel signal handling code, the
 * pagefault handling is done in userspace using the userfaultfd facility.
 *
 * Further pagefaults are triggered by crafting the signal handler's ucontext
 * to point to additional memory regions managed by the userfaultfd, so using
 * the same mechanism used to avoid pre-faulting the signal stack memory.
 *
 * On failure (bug is present) kernel crashes or never returns control back to
 * userspace. If bug is not present, tests completes almost immediately.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "tm.h"


#define UF_MEM_SIZE 655360	/* 10 x 64k pages */

/* Memory handled by userfaultfd */
static char *uf_mem;
static size_t uf_mem_offset = 0;

/*
 * Data that will be copied into the faulting pages (instead of zero-filled
 * pages). This is used to make the test more reliable and avoid segfaulting
 * when we return from the signal handler. Since we are making the signal
 * handler's ucontext point to newly allocated memory, when that memory is
 * paged-in it will contain the expected content.
 */
static char backing_mem[UF_MEM_SIZE];

static size_t pagesize;

/*
 * Return a chunk of at least 'size' bytes of memory that will be handled by
 * userfaultfd. If 'backing_data' is not NULL, its content will be save to
 * 'backing_mem' and then copied into the faulting pages when the page fault
 * is handled.
 */
void *get_uf_mem(size_t size, void *backing_data)
{
	void *ret;

	if (uf_mem_offset + size > UF_MEM_SIZE) {
		fprintf(stderr, "Requesting more uf_mem than expected!\n");
		exit(EXIT_FAILURE);
	}

	ret = &uf_mem[uf_mem_offset];

	/* Save the data that will be copied into the faulting page */
	if (backing_data != NULL)
		memcpy(&backing_mem[uf_mem_offset], backing_data, size);

	/* Reserve the requested amount of uf_mem */
	uf_mem_offset += size;
	/* Keep uf_mem_offset aligned to the page size (round up) */
	uf_mem_offset = (uf_mem_offset + pagesize - 1) & ~(pagesize - 1);

	return ret;
}

void *fault_handler_thread(void *arg)
{
	struct uffd_msg msg;	/* Data read from userfaultfd */
	long uffd;		/* userfaultfd file descriptor */
	struct uffdio_copy uffdio_copy;
	struct pollfd pollfd;
	ssize_t nread, offset;

	uffd = (long) arg;

	for (;;) {
		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		if (poll(&pollfd, 1, -1) == -1) {
			perror("poll() failed");
			exit(EXIT_FAILURE);
		}

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			fprintf(stderr, "read(): EOF on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1) {
			perror("read() failed");
			exit(EXIT_FAILURE);
		}

		/* We expect only one kind of event */
		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		/*
		 * We need to handle page faults in units of pages(!).
		 * So, round faulting address down to page boundary.
		 */
		uffdio_copy.dst = msg.arg.pagefault.address & ~(pagesize-1);

		offset = (char *) uffdio_copy.dst - uf_mem;
		uffdio_copy.src = (unsigned long) &backing_mem[offset];

		uffdio_copy.len = pagesize;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;
		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
			perror("ioctl-UFFDIO_COPY failed");
			exit(EXIT_FAILURE);
		}
	}
}

void setup_uf_mem(void)
{
	long uffd;		/* userfaultfd file descriptor */
	pthread_t thr;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	int ret;

	pagesize = sysconf(_SC_PAGE_SIZE);

	/* Create and enable userfaultfd object */
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		perror("userfaultfd() failed");
		exit(EXIT_FAILURE);
	}
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		perror("ioctl-UFFDIO_API failed");
		exit(EXIT_FAILURE);
	}

	/*
	 * Create a private anonymous mapping. The memory will be demand-zero
	 * paged, that is, not yet allocated. When we actually touch the memory
	 * the related page will be allocated via the userfaultfd mechanism.
	 */
	uf_mem = mmap(NULL, UF_MEM_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (uf_mem == MAP_FAILED) {
		perror("mmap() failed");
		exit(EXIT_FAILURE);
	}

	/*
	 * Register the memory range of the mapping we've just mapped to be
	 * handled by the userfaultfd object. In 'mode' we request to track
	 * missing pages (i.e. pages that have not yet been faulted-in).
	 */
	uffdio_register.range.start = (unsigned long) uf_mem;
	uffdio_register.range.len = UF_MEM_SIZE;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("ioctl-UFFDIO_REGISTER");
		exit(EXIT_FAILURE);
	}

	/* Create a thread that will process the userfaultfd events */
	ret = pthread_create(&thr, NULL, fault_handler_thread, (void *) uffd);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(): Error. Returned %d\n", ret);
		exit(EXIT_FAILURE);
	}
}

/*
 * Assumption: the signal was delivered while userspace was in transactional or
 * suspended state, i.e. uc->uc_link != NULL.
 */
void signal_handler(int signo, siginfo_t *si, void *uc)
{
	ucontext_t *ucp = uc;

	/* Skip 'trap' after returning, otherwise we get a SIGTRAP again */
	ucp->uc_link->uc_mcontext.regs->nip += 4;

	ucp->uc_mcontext.v_regs =
		get_uf_mem(sizeof(elf_vrreg_t), ucp->uc_mcontext.v_regs);

	ucp->uc_link->uc_mcontext.v_regs =
		get_uf_mem(sizeof(elf_vrreg_t), ucp->uc_link->uc_mcontext.v_regs);

	ucp->uc_link = get_uf_mem(sizeof(ucontext_t), ucp->uc_link);
}

bool have_userfaultfd(void)
{
	long rc;

	errno = 0;
	rc = syscall(__NR_userfaultfd, -1);

	return rc == 0 || errno != ENOSYS;
}

int tm_signal_pagefault(void)
{
	struct sigaction sa;
	stack_t ss;

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());
	SKIP_IF(!have_userfaultfd());

	setup_uf_mem();

	/*
	 * Set an alternative stack that will generate a page fault when the
	 * signal is raised. The page fault will be treated via userfaultfd,
	 * i.e. via fault_handler_thread.
	 */
	ss.ss_sp = get_uf_mem(SIGSTKSZ, NULL);
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) == -1) {
		perror("sigaltstack() failed");
		exit(EXIT_FAILURE);
	}

	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = signal_handler;
	if (sigaction(SIGTRAP, &sa, NULL) == -1) {
		perror("sigaction() failed");
		exit(EXIT_FAILURE);
	}

	/* Trigger a SIGTRAP in transactional state */
	asm __volatile__(
			"tbegin.;"
			"beq    1f;"
			"trap;"
			"1: ;"
			: : : "memory");

	/* Trigger a SIGTRAP in suspended state */
	asm __volatile__(
			"tbegin.;"
			"beq    1f;"
			"tsuspend.;"
			"trap;"
			"tresume.;"
			"1: ;"
			: : : "memory");

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	/*
	 * Depending on kernel config, the TM Bad Thing might not result in a
	 * crash, instead the kernel never returns control back to userspace, so
	 * set a tight timeout. If the test passes it completes almost
	 * immediately.
	 */
	test_harness_set_timeout(2);
	return test_harness(tm_signal_pagefault, "tm_signal_pagefault");
}
