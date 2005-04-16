/*
 *	Generic address resultion entity
 *
 *	Authors:
 *	net_random Alan Cox
 *	net_ratelimit Andy Kleen
 *
 *	Created by Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/percpu.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>


/*
  This is a maximally equidistributed combined Tausworthe generator
  based on code from GNU Scientific Library 1.5 (30 Jun 2004)

   x_n = (s1_n ^ s2_n ^ s3_n) 

   s1_{n+1} = (((s1_n & 4294967294) <<12) ^ (((s1_n <<13) ^ s1_n) >>19))
   s2_{n+1} = (((s2_n & 4294967288) << 4) ^ (((s2_n << 2) ^ s2_n) >>25))
   s3_{n+1} = (((s3_n & 4294967280) <<17) ^ (((s3_n << 3) ^ s3_n) >>11))

   The period of this generator is about 2^88.

   From: P. L'Ecuyer, "Maximally Equidistributed Combined Tausworthe
   Generators", Mathematics of Computation, 65, 213 (1996), 203--213.

   This is available on the net from L'Ecuyer's home page,

   http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme.ps
   ftp://ftp.iro.umontreal.ca/pub/simulation/lecuyer/papers/tausme.ps 

   There is an erratum in the paper "Tables of Maximally
   Equidistributed Combined LFSR Generators", Mathematics of
   Computation, 68, 225 (1999), 261--269:
   http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme2.ps

        ... the k_j most significant bits of z_j must be non-
        zero, for each j. (Note: this restriction also applies to the 
        computer code given in [4], but was mistakenly not mentioned in
        that paper.)
   
   This affects the seeding procedure by imposing the requirement
   s1 > 1, s2 > 7, s3 > 15.

*/
struct nrnd_state {
	u32 s1, s2, s3;
};

static DEFINE_PER_CPU(struct nrnd_state, net_rand_state);

static u32 __net_random(struct nrnd_state *state)
{
#define TAUSWORTHE(s,a,b,c,d) ((s&c)<<d) ^ (((s <<a) ^ s)>>b)

	state->s1 = TAUSWORTHE(state->s1, 13, 19, 4294967294UL, 12);
	state->s2 = TAUSWORTHE(state->s2, 2, 25, 4294967288UL, 4);
	state->s3 = TAUSWORTHE(state->s3, 3, 11, 4294967280UL, 17);

	return (state->s1 ^ state->s2 ^ state->s3);
}

static void __net_srandom(struct nrnd_state *state, unsigned long s)
{
	if (s == 0)
		s = 1;      /* default seed is 1 */

#define LCG(n) (69069 * n)
	state->s1 = LCG(s);
	state->s2 = LCG(state->s1);
	state->s3 = LCG(state->s2);

	/* "warm it up" */
	__net_random(state);
	__net_random(state);
	__net_random(state);
	__net_random(state);
	__net_random(state);
	__net_random(state);
}


unsigned long net_random(void)
{
	unsigned long r;
	struct nrnd_state *state = &get_cpu_var(net_rand_state);
	r = __net_random(state);
	put_cpu_var(state);
	return r;
}


void net_srandom(unsigned long entropy)
{
	struct nrnd_state *state = &get_cpu_var(net_rand_state);
	__net_srandom(state, state->s1^entropy);
	put_cpu_var(state);
}

void __init net_random_init(void)
{
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		struct nrnd_state *state = &per_cpu(net_rand_state,i);
		__net_srandom(state, i+jiffies);
	}
}

static int net_random_reseed(void)
{
	int i;
	unsigned long seed[NR_CPUS];

	get_random_bytes(seed, sizeof(seed));
	for (i = 0; i < NR_CPUS; i++) {
		struct nrnd_state *state = &per_cpu(net_rand_state,i);
		__net_srandom(state, seed[i]);
	}
	return 0;
}
late_initcall(net_random_reseed);

int net_msg_cost = 5*HZ;
int net_msg_burst = 10;

/* 
 * All net warning printk()s should be guarded by this function.
 */ 
int net_ratelimit(void)
{
	return __printk_ratelimit(net_msg_cost, net_msg_burst);
}

EXPORT_SYMBOL(net_random);
EXPORT_SYMBOL(net_ratelimit);
EXPORT_SYMBOL(net_srandom);
