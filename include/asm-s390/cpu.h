/*
 *  include/asm-s390/cpu.h
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#ifndef _ASM_S390_CPU_H_
#define _ASM_S390_CPU_H_

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

struct s390_idle_data {
	spinlock_t lock;
	unsigned int in_idle;
	unsigned long long idle_count;
	unsigned long long idle_enter;
	unsigned long long idle_time;
};

DECLARE_PER_CPU(struct s390_idle_data, s390_idle);

#endif /* _ASM_S390_CPU_H_ */
