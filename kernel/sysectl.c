#ifdef CONFIG_SYSECTL
#include <linux/sysectl.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/types.h>

static void sysectl_exit(char *msg)
{
	printk(msg);
	do_exit(-EFAULT); // TODO - Which is the right way to exit?
}

static struct sysectlmap *
sysectl_lookup (struct sysectltable *table, unsigned long ip)
{
	// syscall instructions cannot be right next to each other	
	unsigned long lindex = ip >> 3;
	// Index range = 2048
	lindex &= 0x00000000000007FF;

	printk("Lookup table index:%ld\n", lindex);
	// Get the index of PC from lookup table
	short index = table->index[lindex];
	if (index < 0) {
		// Index < 0 = Index is empty
		sysectl_exit("sysectl: invalid modification interrupted\n");
	}

	do {
		struct sysectlmap *map = &table->maps[index];
		if ((unsigned long)map->pc == ip) {
			printk("sysectl: ip/syscall [%ld] found at index [%d]\n", ip, index);
			return map;
		}

		// In most cases, we don't arrive to this loop.
		// 	The loop is only run on conflict.
		index = map->next;
	} while(index >= 0);

	sysectl_exit("sysectl: syscall at ip was not found\n");
	return NULL;
}

/*
static void sysectl_xor(struct sysectlmap *target, struct sysectlmap *source)
{
	unsigned long *t = (unsigned long*)target->filter.bitmap->bits;
	unsigned long *s = (unsigned long*)source->filter.bitmap->bits;

	#pragma GCC unroll 8
	for (int i = 0; i < 8; i++) {
        	t[i] ^= s[i];
	}	
}
*/

static void sysectl_allow(struct sysectl *sysectl, struct sysectlmap *source)
{
	if (&sysectl->top[1] >= sysectl->stackend)
		sysectl_exit("Sysectl stack overflow\n");

	sysectl->top[1] = sysectl->top[0];
	sysectl->top++;

	sysectl->top->map = source;

	unsigned long *t = (unsigned long*)&sysectl->top->bitmap.bits;
	unsigned long *s = (unsigned long*)source->filter.bitmap->bits;

	#pragma GCC unroll 8
	for (int i = 0; i < 8; i++) {
        	t[i] |= s[i];
	}	
}

static void sysectl_deny(struct sysectl *sysectl, struct sysectlmap *source)
{
	if (&sysectl->top[1] >= sysectl->stackend)
		sysectl_exit("Sysectl stack overflow\n");

	sysectl->top[1] = sysectl->top[0];
	sysectl->top = &sysectl->top[1];

	sysectl->top->map = source;

	unsigned long *t = (unsigned long*)&sysectl->top->bitmap.bits;
	unsigned long *s = (unsigned long*)source->filter.bitmap->bits;

	#pragma GCC unroll 8
	for (int i = 0; i < 8; i++) {
        	t[i] &= ~s[i];
	}	
}

static void sysectl_add(struct sysectl *sysectl, struct sysectlmap *source)
{
	struct sysectlstack *top = sysectl->top;
	struct sysectlstack *dest = sysectl->stack;
	unsigned long *s = (unsigned long*)source->filter.bitmap->bits;

	do {
		unsigned long *d = (unsigned long*)&dest->bitmap.bits;

		#pragma GCC unroll 8
		for (int i=0; i<8; i++) {
			d[i] |= s[i];
		}

		dest++;
	} while (dest <= top);
}

static void sysectl_del(struct sysectl *sysectl, struct sysectlmap *source)
{
	struct sysectlstack *top = sysectl->top;
	struct sysectlstack *dest = sysectl->stack;
	unsigned long *s = (unsigned long*)source->filter.bitmap->bits;

	do {
		unsigned long *d = (unsigned long*)&dest->bitmap.bits;

		#pragma GCC unroll 8
		for (int i=0; i<8; i++) {
			d[i] &= ~s[i];
		}

		dest++;
	} while (dest <= top);
}
/*
 * TODO - REMOVED FOR NOW, NOT THE CORE FEATURES ATM
static void sysectl_add4(struct sysectlmap *target, struct sysectlmap *source)
{
	unsigned short *syscalls = source->filter.syscalls;
	unsigned long *t = (unsigned long*)target->filter.bitmap->bits;

	for (int i=0; i<4; i++) {
		if (syscalls[i] == 0)
			return;

		unsigned short index = syscalls[i] / 8;
		unsigned short bit = syscalls[i] % 8;
		t[index] |= (1 << bit);
	}
}

static void sysectl_del4(struct sysectlmap *target, struct sysectlmap *source)
{
	unsigned short *syscalls = source->filter.syscalls;
	unsigned long *t = (unsigned long*)target->filter.bitmap->bits;

	for (int i=0; i<4; i++) {
		if (syscalls[i] == 0)
			return;

		unsigned short index = syscalls[i] / 8;
		unsigned short bit = syscalls[i] % 8;
		t[index] &= ~(1 << bit);
	}
}
*/
// Modify sysectl filter
SYSCALL_DEFINE0(sysectl)
{
	// TODO - 2 = x86_64, 4 = arm, 1=x86?   Make to work over architectures.
	static const unsigned long syscall_instruction_size = 2;
// TODO
// - Find the correct sysectlmap
// - switch case to operation defined in the map
// - If paired operation, handle pairing, add/verify pair
// - Modify the current bitmap based on the operation
// - 
//
// - In case something is incorrect, kill binary
	struct sysectl *sysectl = &current->sysectl;

	struct pt_regs *regs = task_pt_regs(current);
	unsigned long ip = instruction_pointer(regs);
	ip -= syscall_instruction_size;

	// _lookup will exit() if ip not found, no return
	struct sysectlmap *map = sysectl_lookup(&sysectl->ltable, ip);

	// TODO - Fix numbers to defines or something similar
	// 	once we know how lib/kmod/kernel code work together
	switch(map->opcode) {
	case 1: // SEC_FILTER_SET
		sysectl->top = sysectl->stack;
		sysectl->top->map = map;
		sysectl->top->bitmap = *map->filter.bitmap;
		printk("Filter set to %px\n", map);
		break;

	case 2: // SEC_FILTER_RESTORE
		printk("sysectl:Restore: %px - %px - %px - %px\n", sysectl->top, sysectl->stack, sysectl->top->map->filter.bitmap, map->filter.bitmap);
		if (sysectl->top == sysectl->stack ||
		    sysectl->top->map->filter.bitmap != map->filter.bitmap)
		{
			sysectl_exit("sysectl:Restore - Pair not matching\n");
		}
		printk("Filter restore %px\n", map);
		sysectl->top--;
		break;

	case 3: // SEC_FILTER_DENY
		printk("Filter deny %px\n", map);
		sysectl_deny(sysectl, map);
		break;

	case 4: // SEC_FILTER_ALLOW
		printk("Filter allow %px\n", map);
		sysectl_allow(sysectl, map);
		break;

	case 5: // SEC_FILTER_ADD
		printk("Filter add %px\n", map);
		sysectl_add(sysectl, map);
		break;

	case 6: // SEC_FILTER_DEL
		printk("Filter del %px\n", map);
		sysectl_del(sysectl, map);
		break;

// TODO - All below
#if 0
	case 7: // SEC_FILTER_ADD4
		printk("Filter add4 %px\n", map);
		sysectl_add4(sysectl->top, map);
		break;

	case 8: // SEC_FILTER_DEL4
		printk("Filter del4 %px\n", map);
		sysectl_del4(sysectl->top, map);
		break;
#endif
	default:
		sysectl_exit("sysectl: Invalid opcode\n");
	};

	printk("FILTER:");
	for (int i=0; i<64; i++) {
		printk("%02x\n", sysectl->top->bitmap.bits[i]);
	}

	return 0;
}

/* sysectl_entry is the entry to filter syscall
 * noinline = Function returns 1 ~always, except in case of an error.
 * 	Hoping the branch predictor will make it a nop.
 * nbr = system call number */
long sysectl_entry(long nbr)
{
        long index = nbr / 8;
        long shift = nbr % 8;
        long bit = 0x1 << shift;
	printk("syscall:%ld [%ld]\n ", nbr, (sysectl_bitmap(current)[index] & bit));
        if (sysectl_bitmap(current)[index] & bit)
		return 1;
	printk("Syscall:%ld access denied\n", nbr);
	do_exit(-EFAULT);
	return -1L;
}

void sysectl_release(struct sysectl *sysectl)
{
	if (sysectl->ltable.bitmaps != NULL) {
		kfree(sysectl->ltable.bitmaps);
		sysectl->ltable.bitmaps = NULL;
	}
	if (sysectl->ltable.maps != NULL) {
		kfree(sysectl->ltable.maps);
		sysectl->ltable.maps = NULL;
	}
	if (sysectl->ltable.index != NULL) {
		kfree(sysectl->ltable.index);
		sysectl->ltable.index = NULL;
	}
	if (sysectl->stack != NULL) {
		kfree(sysectl->stack);
		sysectl->stack = NULL;
		sysectl->stackend = NULL;
		sysectl->top = NULL;
	}
}

#endif

