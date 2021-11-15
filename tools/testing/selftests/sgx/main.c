// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

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
#include <sys/auxv.h>
#include "defines.h"
#include "../kselftest_harness.h"
#include "main.h"

static const uint64_t MAGIC = 0x1122334455667788ULL;
vdso_sgx_enter_enclave_t vdso_sgx_enter_enclave;

struct vdso_symtab {
	Elf64_Sym *elf_symtab;
	const char *elf_symstrtab;
	Elf64_Word *elf_hashtab;
};

static Elf64_Dyn *vdso_get_dyntab(void *addr)
{
	Elf64_Ehdr *ehdr = addr;
	Elf64_Phdr *phdrtab = addr + ehdr->e_phoff;
	int i;

	for (i = 0; i < ehdr->e_phnum; i++)
		if (phdrtab[i].p_type == PT_DYNAMIC)
			return addr + phdrtab[i].p_offset;

	return NULL;
}

static void *vdso_get_dyn(void *addr, Elf64_Dyn *dyntab, Elf64_Sxword tag)
{
	int i;

	for (i = 0; dyntab[i].d_tag != DT_NULL; i++)
		if (dyntab[i].d_tag == tag)
			return addr + dyntab[i].d_un.d_ptr;

	return NULL;
}

static bool vdso_get_symtab(void *addr, struct vdso_symtab *symtab)
{
	Elf64_Dyn *dyntab = vdso_get_dyntab(addr);

	symtab->elf_symtab = vdso_get_dyn(addr, dyntab, DT_SYMTAB);
	if (!symtab->elf_symtab)
		return false;

	symtab->elf_symstrtab = vdso_get_dyn(addr, dyntab, DT_STRTAB);
	if (!symtab->elf_symstrtab)
		return false;

	symtab->elf_hashtab = vdso_get_dyn(addr, dyntab, DT_HASH);
	if (!symtab->elf_hashtab)
		return false;

	return true;
}

static unsigned long elf_sym_hash(const char *name)
{
	unsigned long h = 0, high;

	while (*name) {
		h = (h << 4) + *name++;
		high = h & 0xf0000000;

		if (high)
			h ^= high >> 24;

		h &= ~high;
	}

	return h;
}

static Elf64_Sym *vdso_symtab_get(struct vdso_symtab *symtab, const char *name)
{
	Elf64_Word bucketnum = symtab->elf_hashtab[0];
	Elf64_Word *buckettab = &symtab->elf_hashtab[2];
	Elf64_Word *chaintab = &symtab->elf_hashtab[2 + bucketnum];
	Elf64_Sym *sym;
	Elf64_Word i;

	for (i = buckettab[elf_sym_hash(name) % bucketnum]; i != STN_UNDEF;
	     i = chaintab[i]) {
		sym = &symtab->elf_symtab[i];
		if (!strcmp(name, &symtab->elf_symstrtab[sym->st_name]))
			return sym;
	}

	return NULL;
}

FIXTURE(enclave) {
	struct encl encl;
	struct sgx_enclave_run run;
};

FIXTURE_SETUP(enclave)
{
	Elf64_Sym *sgx_enter_enclave_sym = NULL;
	struct vdso_symtab symtab;
	struct encl_segment *seg;
	char maps_line[256];
	FILE *maps_file;
	unsigned int i;
	void *addr;

	if (!encl_load("test_encl.elf", &self->encl, ENCL_HEAP_SIZE_DEFAULT)) {
		encl_delete(&self->encl);
		ksft_exit_skip("cannot load enclaves\n");
	}

	if (!encl_measure(&self->encl))
		goto err;

	if (!encl_build(&self->encl))
		goto err;

	/*
	 * An enclave consumer only must do this.
	 */
	for (i = 0; i < self->encl.nr_segments; i++) {
		struct encl_segment *seg = &self->encl.segment_tbl[i];

		addr = mmap((void *)self->encl.encl_base + seg->offset, seg->size,
			    seg->prot, MAP_SHARED | MAP_FIXED, self->encl.fd, 0);
		EXPECT_NE(addr, MAP_FAILED);
		if (addr == MAP_FAILED)
			goto err;
	}

	/* Get vDSO base address */
	addr = (void *)getauxval(AT_SYSINFO_EHDR);
	if (!addr)
		goto err;

	if (!vdso_get_symtab(addr, &symtab))
		goto err;

	sgx_enter_enclave_sym = vdso_symtab_get(&symtab, "__vdso_sgx_enter_enclave");
	if (!sgx_enter_enclave_sym)
		goto err;

	vdso_sgx_enter_enclave = addr + sgx_enter_enclave_sym->st_value;

	memset(&self->run, 0, sizeof(self->run));
	self->run.tcs = self->encl.encl_base;

	return;

err:
	encl_delete(&self->encl);

	for (i = 0; i < self->encl.nr_segments; i++) {
		seg = &self->encl.segment_tbl[i];

		TH_LOG("0x%016lx 0x%016lx 0x%02x", seg->offset, seg->size, seg->prot);
	}

	maps_file = fopen("/proc/self/maps", "r");
	if (maps_file != NULL)  {
		while (fgets(maps_line, sizeof(maps_line), maps_file) != NULL) {
			maps_line[strlen(maps_line) - 1] = '\0';

			if (strstr(maps_line, "/dev/sgx_enclave"))
				TH_LOG("%s", maps_line);
		}

		fclose(maps_file);
	}

	ASSERT_TRUE(false);
}

FIXTURE_TEARDOWN(enclave)
{
	encl_delete(&self->encl);
}

#define ENCL_CALL(op, run, clobbered) \
	({ \
		int ret; \
		if ((clobbered)) \
			ret = vdso_sgx_enter_enclave((unsigned long)(op), 0, 0, \
						     EENTER, 0, 0, (run)); \
		else \
			ret = sgx_enter_enclave((void *)(op), NULL, 0, EENTER, NULL, NULL, \
						(run)); \
		ret; \
	})

#define EXPECT_EEXIT(run) \
	do { \
		EXPECT_EQ((run)->function, EEXIT); \
		if ((run)->function != EEXIT) \
			TH_LOG("0x%02x 0x%02x 0x%016llx", (run)->exception_vector, \
			       (run)->exception_error_code, (run)->exception_addr); \
	} while (0)

TEST_F(enclave, unclobbered_vdso)
{
	struct encl_op op;

	op.type = ENCL_OP_PUT;
	op.buffer = MAGIC;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, false), 0);

	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);

	op.type = ENCL_OP_GET;
	op.buffer = 0;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, false), 0);

	EXPECT_EQ(op.buffer, MAGIC);
	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);
}

TEST_F(enclave, clobbered_vdso)
{
	struct encl_op op;

	op.type = ENCL_OP_PUT;
	op.buffer = MAGIC;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, true), 0);

	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);

	op.type = ENCL_OP_GET;
	op.buffer = 0;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, true), 0);

	EXPECT_EQ(op.buffer, MAGIC);
	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);
}

static int test_handler(long rdi, long rsi, long rdx, long ursp, long r8, long r9,
			struct sgx_enclave_run *run)
{
	run->user_data = 0;

	return 0;
}

TEST_F(enclave, clobbered_vdso_and_user_function)
{
	struct encl_op op;

	self->run.user_handler = (__u64)test_handler;
	self->run.user_data = 0xdeadbeef;

	op.type = ENCL_OP_PUT;
	op.buffer = MAGIC;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, true), 0);

	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);

	op.type = ENCL_OP_GET;
	op.buffer = 0;

	EXPECT_EQ(ENCL_CALL(&op, &self->run, true), 0);

	EXPECT_EQ(op.buffer, MAGIC);
	EXPECT_EEXIT(&self->run);
	EXPECT_EQ(self->run.user_data, 0);
}

TEST_HARNESS_MAIN
