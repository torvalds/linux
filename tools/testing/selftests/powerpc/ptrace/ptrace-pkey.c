// SPDX-License-Identifier: GPL-2.0+
/*
 * Ptrace test for Memory Protection Key registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 * Copyright (C) 2018 IBM Corporation.
 */
#include "ptrace.h"
#include "child.h"

#ifndef __NR_pkey_alloc
#define __NR_pkey_alloc		384
#endif

#ifndef __NR_pkey_free
#define __NR_pkey_free		385
#endif

#ifndef NT_PPC_PKEY
#define NT_PPC_PKEY		0x110
#endif

#ifndef PKEY_DISABLE_EXECUTE
#define PKEY_DISABLE_EXECUTE	0x4
#endif

#define AMR_BITS_PER_PKEY 2
#define PKEY_REG_BITS (sizeof(u64) * 8)
#define pkeyshift(pkey) (PKEY_REG_BITS - ((pkey + 1) * AMR_BITS_PER_PKEY))

static const char user_read[] = "[User Read (Running)]";
static const char user_write[] = "[User Write (Running)]";
static const char ptrace_read_running[] = "[Ptrace Read (Running)]";
static const char ptrace_write_running[] = "[Ptrace Write (Running)]";

/* Information shared between the parent and the child. */
struct shared_info {
	struct child_sync child_sync;

	/* AMR value the parent expects to read from the child. */
	unsigned long amr1;

	/* AMR value the parent is expected to write to the child. */
	unsigned long amr2;

	/* AMR value that ptrace should refuse to write to the child. */
	unsigned long amr3;

	/* IAMR value the parent expects to read from the child. */
	unsigned long expected_iamr;

	/* UAMOR value the parent expects to read from the child. */
	unsigned long expected_uamor;

	/*
	 * IAMR and UAMOR values that ptrace should refuse to write to the child
	 * (even though they're valid ones) because userspace doesn't have
	 * access to those registers.
	 */
	unsigned long new_iamr;
	unsigned long new_uamor;
};

static int sys_pkey_alloc(unsigned long flags, unsigned long init_access_rights)
{
	return syscall(__NR_pkey_alloc, flags, init_access_rights);
}

static int sys_pkey_free(int pkey)
{
	return syscall(__NR_pkey_free, pkey);
}

static int child(struct shared_info *info)
{
	unsigned long reg;
	bool disable_execute = true;
	int pkey1, pkey2, pkey3;
	int ret;

	/* Wait until parent fills out the initial register values. */
	ret = wait_parent(&info->child_sync);
	if (ret)
		return ret;

	/* Get some pkeys so that we can change their bits in the AMR. */
	pkey1 = sys_pkey_alloc(0, PKEY_DISABLE_EXECUTE);
	if (pkey1 < 0) {
		pkey1 = sys_pkey_alloc(0, 0);
		CHILD_FAIL_IF(pkey1 < 0, &info->child_sync);

		disable_execute = false;
	}

	pkey2 = sys_pkey_alloc(0, 0);
	CHILD_FAIL_IF(pkey2 < 0, &info->child_sync);

	pkey3 = sys_pkey_alloc(0, 0);
	CHILD_FAIL_IF(pkey3 < 0, &info->child_sync);

	info->amr1 |= 3ul << pkeyshift(pkey1);
	info->amr2 |= 3ul << pkeyshift(pkey2);
	info->amr3 |= info->amr2 | 3ul << pkeyshift(pkey3);

	if (disable_execute)
		info->expected_iamr |= 1ul << pkeyshift(pkey1);
	else
		info->expected_iamr &= ~(1ul << pkeyshift(pkey1));

	info->expected_iamr &= ~(1ul << pkeyshift(pkey2) | 1ul << pkeyshift(pkey3));

	info->expected_uamor |= 3ul << pkeyshift(pkey1) |
				3ul << pkeyshift(pkey2);
	info->new_iamr |= 1ul << pkeyshift(pkey1) | 1ul << pkeyshift(pkey2);
	info->new_uamor |= 3ul << pkeyshift(pkey1);

	/*
	 * We won't use pkey3. We just want a plausible but invalid key to test
	 * whether ptrace will let us write to AMR bits we are not supposed to.
	 *
	 * This also tests whether the kernel restores the UAMOR permissions
	 * after a key is freed.
	 */
	sys_pkey_free(pkey3);

	printf("%-30s AMR: %016lx pkey1: %d pkey2: %d pkey3: %d\n",
	       user_write, info->amr1, pkey1, pkey2, pkey3);

	mtspr(SPRN_AMR, info->amr1);

	/* Wait for parent to read our AMR value and write a new one. */
	ret = prod_parent(&info->child_sync);
	CHILD_FAIL_IF(ret, &info->child_sync);

	ret = wait_parent(&info->child_sync);
	if (ret)
		return ret;

	reg = mfspr(SPRN_AMR);

	printf("%-30s AMR: %016lx\n", user_read, reg);

	CHILD_FAIL_IF(reg != info->amr2, &info->child_sync);

	/*
	 * Wait for parent to try to write an invalid AMR value.
	 */
	ret = prod_parent(&info->child_sync);
	CHILD_FAIL_IF(ret, &info->child_sync);

	ret = wait_parent(&info->child_sync);
	if (ret)
		return ret;

	reg = mfspr(SPRN_AMR);

	printf("%-30s AMR: %016lx\n", user_read, reg);

	CHILD_FAIL_IF(reg != info->amr2, &info->child_sync);

	/*
	 * Wait for parent to try to write an IAMR and a UAMOR value. We can't
	 * verify them, but we can verify that the AMR didn't change.
	 */
	ret = prod_parent(&info->child_sync);
	CHILD_FAIL_IF(ret, &info->child_sync);

	ret = wait_parent(&info->child_sync);
	if (ret)
		return ret;

	reg = mfspr(SPRN_AMR);

	printf("%-30s AMR: %016lx\n", user_read, reg);

	CHILD_FAIL_IF(reg != info->amr2, &info->child_sync);

	/* Now let parent now that we are finished. */

	ret = prod_parent(&info->child_sync);
	CHILD_FAIL_IF(ret, &info->child_sync);

	return TEST_PASS;
}

static int parent(struct shared_info *info, pid_t pid)
{
	unsigned long regs[3];
	int ret, status;

	/*
	 * Get the initial values for AMR, IAMR and UAMOR and communicate them
	 * to the child.
	 */
	ret = ptrace_read_regs(pid, NT_PPC_PKEY, regs, 3);
	PARENT_SKIP_IF_UNSUPPORTED(ret, &info->child_sync);
	PARENT_FAIL_IF(ret, &info->child_sync);

	info->amr1 = info->amr2 = info->amr3 = regs[0];
	info->expected_iamr = info->new_iamr = regs[1];
	info->expected_uamor = info->new_uamor = regs[2];

	/* Wake up child so that it can set itself up. */
	ret = prod_child(&info->child_sync);
	PARENT_FAIL_IF(ret, &info->child_sync);

	ret = wait_child(&info->child_sync);
	if (ret)
		return ret;

	/* Verify that we can read the pkey registers from the child. */
	ret = ptrace_read_regs(pid, NT_PPC_PKEY, regs, 3);
	PARENT_FAIL_IF(ret, &info->child_sync);

	printf("%-30s AMR: %016lx IAMR: %016lx UAMOR: %016lx\n",
	       ptrace_read_running, regs[0], regs[1], regs[2]);

	PARENT_FAIL_IF(regs[0] != info->amr1, &info->child_sync);
	PARENT_FAIL_IF(regs[1] != info->expected_iamr, &info->child_sync);
	PARENT_FAIL_IF(regs[2] != info->expected_uamor, &info->child_sync);

	/* Write valid AMR value in child. */
	ret = ptrace_write_regs(pid, NT_PPC_PKEY, &info->amr2, 1);
	PARENT_FAIL_IF(ret, &info->child_sync);

	printf("%-30s AMR: %016lx\n", ptrace_write_running, info->amr2);

	/* Wake up child so that it can verify it changed. */
	ret = prod_child(&info->child_sync);
	PARENT_FAIL_IF(ret, &info->child_sync);

	ret = wait_child(&info->child_sync);
	if (ret)
		return ret;

	/* Write invalid AMR value in child. */
	ret = ptrace_write_regs(pid, NT_PPC_PKEY, &info->amr3, 1);
	PARENT_FAIL_IF(ret, &info->child_sync);

	printf("%-30s AMR: %016lx\n", ptrace_write_running, info->amr3);

	/* Wake up child so that it can verify it didn't change. */
	ret = prod_child(&info->child_sync);
	PARENT_FAIL_IF(ret, &info->child_sync);

	ret = wait_child(&info->child_sync);
	if (ret)
		return ret;

	/* Try to write to IAMR. */
	regs[0] = info->amr1;
	regs[1] = info->new_iamr;
	ret = ptrace_write_regs(pid, NT_PPC_PKEY, regs, 2);
	PARENT_FAIL_IF(!ret, &info->child_sync);

	printf("%-30s AMR: %016lx IAMR: %016lx\n",
	       ptrace_write_running, regs[0], regs[1]);

	/* Try to write to IAMR and UAMOR. */
	regs[2] = info->new_uamor;
	ret = ptrace_write_regs(pid, NT_PPC_PKEY, regs, 3);
	PARENT_FAIL_IF(!ret, &info->child_sync);

	printf("%-30s AMR: %016lx IAMR: %016lx UAMOR: %016lx\n",
	       ptrace_write_running, regs[0], regs[1], regs[2]);

	/* Verify that all registers still have their expected values. */
	ret = ptrace_read_regs(pid, NT_PPC_PKEY, regs, 3);
	PARENT_FAIL_IF(ret, &info->child_sync);

	printf("%-30s AMR: %016lx IAMR: %016lx UAMOR: %016lx\n",
	       ptrace_read_running, regs[0], regs[1], regs[2]);

	PARENT_FAIL_IF(regs[0] != info->amr2, &info->child_sync);
	PARENT_FAIL_IF(regs[1] != info->expected_iamr, &info->child_sync);
	PARENT_FAIL_IF(regs[2] != info->expected_uamor, &info->child_sync);

	/* Wake up child so that it can verify AMR didn't change and wrap up. */
	ret = prod_child(&info->child_sync);
	PARENT_FAIL_IF(ret, &info->child_sync);

	ret = wait(&status);
	if (ret != pid) {
		printf("Child's exit status not captured\n");
		ret = TEST_PASS;
	} else if (!WIFEXITED(status)) {
		printf("Child exited abnormally\n");
		ret = TEST_FAIL;
	} else
		ret = WEXITSTATUS(status) ? TEST_FAIL : TEST_PASS;

	return ret;
}

static int ptrace_pkey(void)
{
	struct shared_info *info;
	int shm_id;
	int ret;
	pid_t pid;

	shm_id = shmget(IPC_PRIVATE, sizeof(*info), 0777 | IPC_CREAT);
	info = shmat(shm_id, NULL, 0);

	ret = init_child_sync(&info->child_sync);
	if (ret)
		return ret;

	pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		ret = TEST_FAIL;
	} else if (pid == 0)
		ret = child(info);
	else
		ret = parent(info, pid);

	shmdt(info);

	if (pid) {
		destroy_child_sync(&info->child_sync);
		shmctl(shm_id, IPC_RMID, NULL);
	}

	return ret;
}

int main(int argc, char *argv[])
{
	return test_harness(ptrace_pkey, "ptrace_pkey");
}
