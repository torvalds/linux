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

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/random.h>

static DEFINE_PER_CPU(struct rnd_state, net_rand_state);

/**
 *	prandom_u32_state - seeded pseudo-random number generator.
 *	@state: pointer to state structure holding seeded state.
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_u32().
 */
u32 prandom_u32_state(struct rnd_state *state)
{
#define TAUSWORTHE(s,a,b,c,d) ((s&c)<<d) ^ (((s <<a) ^ s)>>b)

	state->s1 = TAUSWORTHE(state->s1, 13, 19, 4294967294UL, 12);
	state->s2 = TAUSWORTHE(state->s2, 2, 25, 4294967288UL, 4);
	state->s3 = TAUSWORTHE(state->s3, 3, 11, 4294967280UL, 17);

	return (state->s1 ^ state->s2 ^ state->s3);
}
EXPORT_SYMBOL(prandom_u32_state);

/**
 *	prandom_u32 - pseudo random number generator
 *
 *	A 32 bit pseudo-random number is generated using a fast
 *	algorithm suitable for simulation. This algorithm is NOT
 *	considered safe for cryptographic use.
 */
u32 prandom_u32(void)
{
	unsigned long r;
	struct rnd_state *state = &get_cpu_var(net_rand_state);
	r = prandom_u32_state(state);
	put_cpu_var(state);
	return r;
}
EXPORT_SYMBOL(prandom_u32);

/*
 *	prandom_bytes_state - get the requested number of pseudo-random bytes
 *
 *	@state: pointer to state structure holding seeded state.
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_bytes().
 */
void prandom_bytes_state(struct rnd_state *state, void *buf, int bytes)
{
	unsigned char *p = buf;
	int i;

	for (i = 0; i < round_down(bytes, sizeof(u32)); i += sizeof(u32)) {
		u32 random = prandom_u32_state(state);
		int j;

		for (j = 0; j < sizeof(u32); j++) {
			p[i + j] = random;
			random >>= BITS_PER_BYTE;
		}
	}
	if (i < bytes) {
		u32 random = prandom_u32_state(state);

		for (; i < bytes; i++) {
			p[i] = random;
			random >>= BITS_PER_BYTE;
		}
	}
}
EXPORT_SYMBOL(prandom_bytes_state);

/**
 *	prandom_bytes - get the requested number of pseudo-random bytes
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 */
void prandom_bytes(void *buf, int bytes)
{
	struct rnd_state *state = &get_cpu_var(net_rand_state);

	prandom_bytes_state(state, buf, bytes);
	put_cpu_var(state);
}
EXPORT_SYMBOL(prandom_bytes);

/**
 *	prandom_seed - add entropy to pseudo random number generator
 *	@seed: seed value
 *
 *	Add some additional seeding to the prandom pool.
 */
void prandom_seed(u32 entropy)
{
	int i;
	/*
	 * No locking on the CPUs, but then somewhat random results are, well,
	 * expected.
	 */
	for_each_possible_cpu (i) {
		struct rnd_state *state = &per_cpu(net_rand_state, i);
		state->s1 = __seed(state->s1 ^ entropy, 2);
		prandom_u32_state(state);
	}
}
EXPORT_SYMBOL(prandom_seed);

/*
 *	Generate some initially weak seeding values to allow
 *	to start the prandom_u32() engine.
 */
static int __init prandom_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);

#define LCG(x)	((x) * 69069)	/* super-duper LCG */
		state->s1 = __seed(LCG(i + jiffies), 2);
		state->s2 = __seed(LCG(state->s1), 8);
		state->s3 = __seed(LCG(state->s2), 16);

		/* "warm it up" */
		prandom_u32_state(state);
		prandom_u32_state(state);
		prandom_u32_state(state);
		prandom_u32_state(state);
		prandom_u32_state(state);
		prandom_u32_state(state);
	}
	return 0;
}
core_initcall(prandom_init);

static void __prandom_timer(unsigned long dontcare);
static DEFINE_TIMER(seed_timer, __prandom_timer, 0, 0);

static void __prandom_timer(unsigned long dontcare)
{
	u32 entropy;

	get_random_bytes(&entropy, sizeof(entropy));
	prandom_seed(entropy);
	/* reseed every ~60 seconds, in [40 .. 80) interval with slack */
	seed_timer.expires = jiffies + (40 * HZ + (prandom_u32() % (40 * HZ)));
	add_timer(&seed_timer);
}

static void prandom_start_seed_timer(void)
{
	set_timer_slack(&seed_timer, HZ);
	seed_timer.expires = jiffies + 40 * HZ;
	add_timer(&seed_timer);
}

/*
 *	Generate better values after random number generator
 *	is fully initialized.
 */
static int __init prandom_reseed(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);
		u32 seeds[3];

		get_random_bytes(&seeds, sizeof(seeds));
		state->s1 = __seed(seeds[0], 2);
		state->s2 = __seed(seeds[1], 8);
		state->s3 = __seed(seeds[2], 16);

		/* mix it in */
		prandom_u32_state(state);
	}
	prandom_start_seed_timer();
	return 0;
}
late_initcall(prandom_reseed);
