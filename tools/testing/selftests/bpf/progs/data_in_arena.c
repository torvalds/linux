// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* global data of the object is in arena */
char data_in_arena SEC(".arena.data");

int counter = 5;
long pair[2] = { 1, 2 };
int x = 42;
long sum;
long table[4];
struct {
	long v[8];
} aligned64 __attribute__((aligned(64)));
const volatile int ro = 7;
const volatile long ro_table[4] = { 10, 20, 30, 40 };

/* const strings stay in a map for helpers and kfuncs, a copy of them is in arena */
const char hello[] SEC(".rodata.str.hello") = "hello";

/* pointers to data that are stored in data */
int *px = &x;
const char *str = hello;
int *const volatile cpx SEC(".data.rel.ro") = &x;

SEC("syscall")
int use_data(void *ctx)
{
	int i;

	for (i = 0; i < 4; i++)
		table[i] = ro_table[i] + i;
	sum = counter + ro + pair[0] + pair[1];
	counter++;
	__sync_fetch_and_add(&pair[1], 3);
	return sum;
}

typedef int (*op_fn)(int);

static __noinline int add1(int v)
{
	return v + 1;
}

/*
 * Pointers to functions and to data in read-only data of a program with callx.
 * Volatile, so that the compiler doesn't replace the pointers with what
 * they point to.
 */
static const volatile struct {
	op_fn fn;
	int *data;
	const char *name;
} ops SEC(".data.rel.ro") = { add1, &x, hello };

SEC("syscall")
int use_ops(void *ctx)
{
	/* a program has the arena when its code refers to it */
	counter++;
#ifdef __clang__
	return ops.fn(*ops.data) + ops.name[1];
#else
	/* gcc doesn't support indirect calls */
	return add1(*ops.data) + ops.name[1];
#endif
}

SEC("syscall")
int use_ptrs(void *ctx)
{
	unsigned long addr = (unsigned long)&aligned64;
	char local[4] = "abc";

	/* hide the address from the compiler, it knows that '& 63' is 0 */
	asm volatile ("" : "+r"(addr));
	if (addr & 63)
		return 1;
	aligned64.v[7] = 7;
	if (*px != 42)
		return 2;
	*px = 43;
	if (x != 43 || *cpx != 43)
		return 3;
	if (str[0] != 'h' || str[4] != 'o' || str[5])
		return 4;
	/* the literal is in a map */
	if (bpf_strncmp(local, sizeof(local), "abc"))
		return 5;
	return 0;
}

char out[16];

/* format strings are in a map */
SEC("syscall")
int use_printk(void *ctx)
{
	char buf[sizeof(out)];
	int i, n;

	bpf_printk("counter %d", counter);
	n = BPF_SNPRINTF(buf, sizeof(buf), "%d-%d", x, ro);
	for (i = 0; i < sizeof(out); i++)
		out[i] = buf[i];
	return n;
}

char _license[] SEC("license") = "GPL";
