// SPDX-License-Identifier: GPL-2.0
/*
 * This is a maximally equidistributed combined Tausworthe generator
 * based on code from GNU Scientific Library 1.5 (30 Jun 2004)
 *
 * lfsr113 version:
 *
 * x_n = (s1_n ^ s2_n ^ s3_n ^ s4_n)
 *
 * s1_{n+1} = (((s1_n & 4294967294) << 18) ^ (((s1_n <<  6) ^ s1_n) >> 13))
 * s2_{n+1} = (((s2_n & 4294967288) <<  2) ^ (((s2_n <<  2) ^ s2_n) >> 27))
 * s3_{n+1} = (((s3_n & 4294967280) <<  7) ^ (((s3_n << 13) ^ s3_n) >> 21))
 * s4_{n+1} = (((s4_n & 4294967168) << 13) ^ (((s4_n <<  3) ^ s4_n) >> 12))
 *
 * The period of this generator is about 2^113 (see erratum paper).
 *
 * From: P. L'Ecuyer, "Maximally Equidistributed Combined Tausworthe
 * Generators", Mathematics of Computation, 65, 213 (1996), 203--213:
 * http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme.ps
 * ftp://ftp.iro.umontreal.ca/pub/simulation/lecuyer/papers/tausme.ps
 *
 * There is an erratum in the paper "Tables of Maximally Equidistributed
 * Combined LFSR Generators", Mathematics of Computation, 68, 225 (1999),
 * 261--269: http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme2.ps
 *
 *      ... the k_j most significant bits of z_j must be non-zero,
 *      for each j. (Note: this restriction also applies to the
 *      computer code given in [4], but was mistakenly not mentioned
 *      in that paper.)
 *
 * This affects the seeding procedure by imposing the requirement
 * s1 > 1, s2 > 7, s3 > 15, s4 > 127.
 */

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <trace/events/random.h>

/**
 *	prandom_u32_state - seeded pseudo-random number generator.
 *	@state: pointer to state structure holding seeded state.
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_u32().
 */
u32 prandom_u32_state(struct rnd_state *state)
{
#define TAUSWORTHE(s, a, b, c, d) ((s & c) << d) ^ (((s << a) ^ s) >> b)
	state->s1 = TAUSWORTHE(state->s1,  6U, 13U, 4294967294U, 18U);
	state->s2 = TAUSWORTHE(state->s2,  2U, 27U, 4294967288U,  2U);
	state->s3 = TAUSWORTHE(state->s3, 13U, 21U, 4294967280U,  7U);
	state->s4 = TAUSWORTHE(state->s4,  3U, 12U, 4294967168U, 13U);

	return (state->s1 ^ state->s2 ^ state->s3 ^ state->s4);
}
EXPORT_SYMBOL(prandom_u32_state);

/**
 *	prandom_bytes_state - get the requested number of pseudo-random bytes
 *
 *	@state: pointer to state structure holding seeded state.
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 *
 *	This is used for pseudo-randomness with no outside seeding.
 *	For more random results, use prandom_bytes().
 */
void prandom_bytes_state(struct rnd_state *state, void *buf, size_t bytes)
{
	u8 *ptr = buf;

	while (bytes >= sizeof(u32)) {
		put_unaligned(prandom_u32_state(state), (u32 *) ptr);
		ptr += sizeof(u32);
		bytes -= sizeof(u32);
	}

	if (bytes > 0) {
		u32 rem = prandom_u32_state(state);
		do {
			*ptr++ = (u8) rem;
			bytes--;
			rem >>= BITS_PER_BYTE;
		} while (bytes > 0);
	}
}
EXPORT_SYMBOL(prandom_bytes_state);

static void prandom_warmup(struct rnd_state *state)
{
	/* Calling RNG ten times to satisfy recurrence condition */
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
	prandom_u32_state(state);
}

void prandom_seed_full_state(struct rnd_state __percpu *pcpu_state)
{
	int i;

	for_each_possible_cpu(i) {
		struct rnd_state *state = per_cpu_ptr(pcpu_state, i);
		u32 seeds[4];

		get_random_bytes(&seeds, sizeof(seeds));
		state->s1 = __seed(seeds[0],   2U);
		state->s2 = __seed(seeds[1],   8U);
		state->s3 = __seed(seeds[2],  16U);
		state->s4 = __seed(seeds[3], 128U);

		prandom_warmup(state);
	}
}
EXPORT_SYMBOL(prandom_seed_full_state);

#ifdef CONFIG_RANDOM32_SELFTEST
static struct prandom_test1 {
	u32 seed;
	u32 result;
} test1[] = {
	{ 1U, 3484351685U },
	{ 2U, 2623130059U },
	{ 3U, 3125133893U },
	{ 4U,  984847254U },
};

static struct prandom_test2 {
	u32 seed;
	u32 iteration;
	u32 result;
} test2[] = {
	/* Test cases against taus113 from GSL library. */
	{  931557656U, 959U, 2975593782U },
	{ 1339693295U, 876U, 3887776532U },
	{ 1545556285U, 961U, 1615538833U },
	{  601730776U, 723U, 1776162651U },
	{ 1027516047U, 687U,  511983079U },
	{  416526298U, 700U,  916156552U },
	{ 1395522032U, 652U, 2222063676U },
	{  366221443U, 617U, 2992857763U },
	{ 1539836965U, 714U, 3783265725U },
	{  556206671U, 994U,  799626459U },
	{  684907218U, 799U,  367789491U },
	{ 2121230701U, 931U, 2115467001U },
	{ 1668516451U, 644U, 3620590685U },
	{  768046066U, 883U, 2034077390U },
	{ 1989159136U, 833U, 1195767305U },
	{  536585145U, 996U, 3577259204U },
	{ 1008129373U, 642U, 1478080776U },
	{ 1740775604U, 939U, 1264980372U },
	{ 1967883163U, 508U,   10734624U },
	{ 1923019697U, 730U, 3821419629U },
	{  442079932U, 560U, 3440032343U },
	{ 1961302714U, 845U,  841962572U },
	{ 2030205964U, 962U, 1325144227U },
	{ 1160407529U, 507U,  240940858U },
	{  635482502U, 779U, 4200489746U },
	{ 1252788931U, 699U,  867195434U },
	{ 1961817131U, 719U,  668237657U },
	{ 1071468216U, 983U,  917876630U },
	{ 1281848367U, 932U, 1003100039U },
	{  582537119U, 780U, 1127273778U },
	{ 1973672777U, 853U, 1071368872U },
	{ 1896756996U, 762U, 1127851055U },
	{  847917054U, 500U, 1717499075U },
	{ 1240520510U, 951U, 2849576657U },
	{ 1685071682U, 567U, 1961810396U },
	{ 1516232129U, 557U,    3173877U },
	{ 1208118903U, 612U, 1613145022U },
	{ 1817269927U, 693U, 4279122573U },
	{ 1510091701U, 717U,  638191229U },
	{  365916850U, 807U,  600424314U },
	{  399324359U, 702U, 1803598116U },
	{ 1318480274U, 779U, 2074237022U },
	{  697758115U, 840U, 1483639402U },
	{ 1696507773U, 840U,  577415447U },
	{ 2081979121U, 981U, 3041486449U },
	{  955646687U, 742U, 3846494357U },
	{ 1250683506U, 749U,  836419859U },
	{  595003102U, 534U,  366794109U },
	{   47485338U, 558U, 3521120834U },
	{  619433479U, 610U, 3991783875U },
	{  704096520U, 518U, 4139493852U },
	{ 1712224984U, 606U, 2393312003U },
	{ 1318233152U, 922U, 3880361134U },
	{  855572992U, 761U, 1472974787U },
	{   64721421U, 703U,  683860550U },
	{  678931758U, 840U,  380616043U },
	{  692711973U, 778U, 1382361947U },
	{  677703619U, 530U, 2826914161U },
	{   92393223U, 586U, 1522128471U },
	{ 1222592920U, 743U, 3466726667U },
	{  358288986U, 695U, 1091956998U },
	{ 1935056945U, 958U,  514864477U },
	{  735675993U, 990U, 1294239989U },
	{ 1560089402U, 897U, 2238551287U },
	{   70616361U, 829U,   22483098U },
	{  368234700U, 731U, 2913875084U },
	{   20221190U, 879U, 1564152970U },
	{  539444654U, 682U, 1835141259U },
	{ 1314987297U, 840U, 1801114136U },
	{ 2019295544U, 645U, 3286438930U },
	{  469023838U, 716U, 1637918202U },
	{ 1843754496U, 653U, 2562092152U },
	{  400672036U, 809U, 4264212785U },
	{  404722249U, 965U, 2704116999U },
	{  600702209U, 758U,  584979986U },
	{  519953954U, 667U, 2574436237U },
	{ 1658071126U, 694U, 2214569490U },
	{  420480037U, 749U, 3430010866U },
	{  690103647U, 969U, 3700758083U },
	{ 1029424799U, 937U, 3787746841U },
	{ 2012608669U, 506U, 3362628973U },
	{ 1535432887U, 998U,   42610943U },
	{ 1330635533U, 857U, 3040806504U },
	{ 1223800550U, 539U, 3954229517U },
	{ 1322411537U, 680U, 3223250324U },
	{ 1877847898U, 945U, 2915147143U },
	{ 1646356099U, 874U,  965988280U },
	{  805687536U, 744U, 4032277920U },
	{ 1948093210U, 633U, 1346597684U },
	{  392609744U, 783U, 1636083295U },
	{  690241304U, 770U, 1201031298U },
	{ 1360302965U, 696U, 1665394461U },
	{ 1220090946U, 780U, 1316922812U },
	{  447092251U, 500U, 3438743375U },
	{ 1613868791U, 592U,  828546883U },
	{  523430951U, 548U, 2552392304U },
	{  726692899U, 810U, 1656872867U },
	{ 1364340021U, 836U, 3710513486U },
	{ 1986257729U, 931U,  935013962U },
	{  407983964U, 921U,  728767059U },
};

static u32 __extract_hwseed(void)
{
	unsigned int val = 0;

	(void)(arch_get_random_seed_int(&val) ||
	       arch_get_random_int(&val));

	return val;
}

static void prandom_seed_early(struct rnd_state *state, u32 seed,
			       bool mix_with_hwseed)
{
#define LCG(x)	 ((x) * 69069U)	/* super-duper LCG */
#define HWSEED() (mix_with_hwseed ? __extract_hwseed() : 0)
	state->s1 = __seed(HWSEED() ^ LCG(seed),        2U);
	state->s2 = __seed(HWSEED() ^ LCG(state->s1),   8U);
	state->s3 = __seed(HWSEED() ^ LCG(state->s2),  16U);
	state->s4 = __seed(HWSEED() ^ LCG(state->s3), 128U);
}

static int __init prandom_state_selftest(void)
{
	int i, j, errors = 0, runs = 0;
	bool error = false;

	for (i = 0; i < ARRAY_SIZE(test1); i++) {
		struct rnd_state state;

		prandom_seed_early(&state, test1[i].seed, false);
		prandom_warmup(&state);

		if (test1[i].result != prandom_u32_state(&state))
			error = true;
	}

	if (error)
		pr_warn("prandom: seed boundary self test failed\n");
	else
		pr_info("prandom: seed boundary self test passed\n");

	for (i = 0; i < ARRAY_SIZE(test2); i++) {
		struct rnd_state state;

		prandom_seed_early(&state, test2[i].seed, false);
		prandom_warmup(&state);

		for (j = 0; j < test2[i].iteration - 1; j++)
			prandom_u32_state(&state);

		if (test2[i].result != prandom_u32_state(&state))
			errors++;

		runs++;
		cond_resched();
	}

	if (errors)
		pr_warn("prandom: %d/%d self tests failed\n", errors, runs);
	else
		pr_info("prandom: %d self tests passed\n", runs);
	return 0;
}
core_initcall(prandom_state_selftest);
#endif

/*
 * The prandom_u32() implementation is now completely separate from the
 * prandom_state() functions, which are retained (for now) for compatibility.
 *
 * Because of (ab)use in the networking code for choosing random TCP/UDP port
 * numbers, which open DoS possibilities if guessable, we want something
 * stronger than a standard PRNG.  But the performance requirements of
 * the network code do not allow robust crypto for this application.
 *
 * So this is a homebrew Junior Spaceman implementation, based on the
 * lowest-latency trustworthy crypto primitive available, SipHash.
 * (The authors of SipHash have not been consulted about this abuse of
 * their work.)
 *
 * Standard SipHash-2-4 uses 2n+4 rounds to hash n words of input to
 * one word of output.  This abbreviated version uses 2 rounds per word
 * of output.
 */

struct siprand_state {
	unsigned long v0;
	unsigned long v1;
	unsigned long v2;
	unsigned long v3;
};

static DEFINE_PER_CPU(struct siprand_state, net_rand_state) __latent_entropy;
DEFINE_PER_CPU(unsigned long, net_rand_noise);
EXPORT_PER_CPU_SYMBOL(net_rand_noise);

/*
 * This is the core CPRNG function.  As "pseudorandom", this is not used
 * for truly valuable things, just intended to be a PITA to guess.
 * For maximum speed, we do just two SipHash rounds per word.  This is
 * the same rate as 4 rounds per 64 bits that SipHash normally uses,
 * so hopefully it's reasonably secure.
 *
 * There are two changes from the official SipHash finalization:
 * - We omit some constants XORed with v2 in the SipHash spec as irrelevant;
 *   they are there only to make the output rounds distinct from the input
 *   rounds, and this application has no input rounds.
 * - Rather than returning v0^v1^v2^v3, return v1+v3.
 *   If you look at the SipHash round, the last operation on v3 is
 *   "v3 ^= v0", so "v0 ^ v3" just undoes that, a waste of time.
 *   Likewise "v1 ^= v2".  (The rotate of v2 makes a difference, but
 *   it still cancels out half of the bits in v2 for no benefit.)
 *   Second, since the last combining operation was xor, continue the
 *   pattern of alternating xor/add for a tiny bit of extra non-linearity.
 */
static inline u32 siprand_u32(struct siprand_state *s)
{
	unsigned long v0 = s->v0, v1 = s->v1, v2 = s->v2, v3 = s->v3;
	unsigned long n = raw_cpu_read(net_rand_noise);

	v3 ^= n;
	PRND_SIPROUND(v0, v1, v2, v3);
	PRND_SIPROUND(v0, v1, v2, v3);
	v0 ^= n;
	s->v0 = v0;  s->v1 = v1;  s->v2 = v2;  s->v3 = v3;
	return v1 + v3;
}


/**
 *	prandom_u32 - pseudo random number generator
 *
 *	A 32 bit pseudo-random number is generated using a fast
 *	algorithm suitable for simulation. This algorithm is NOT
 *	considered safe for cryptographic use.
 */
u32 prandom_u32(void)
{
	struct siprand_state *state = get_cpu_ptr(&net_rand_state);
	u32 res = siprand_u32(state);

	trace_prandom_u32(res);
	put_cpu_ptr(&net_rand_state);
	return res;
}
EXPORT_SYMBOL(prandom_u32);

/**
 *	prandom_bytes - get the requested number of pseudo-random bytes
 *	@buf: where to copy the pseudo-random bytes to
 *	@bytes: the requested number of bytes
 */
void prandom_bytes(void *buf, size_t bytes)
{
	struct siprand_state *state = get_cpu_ptr(&net_rand_state);
	u8 *ptr = buf;

	while (bytes >= sizeof(u32)) {
		put_unaligned(siprand_u32(state), (u32 *)ptr);
		ptr += sizeof(u32);
		bytes -= sizeof(u32);
	}

	if (bytes > 0) {
		u32 rem = siprand_u32(state);

		do {
			*ptr++ = (u8)rem;
			rem >>= BITS_PER_BYTE;
		} while (--bytes > 0);
	}
	put_cpu_ptr(&net_rand_state);
}
EXPORT_SYMBOL(prandom_bytes);

/**
 *	prandom_seed - add entropy to pseudo random number generator
 *	@entropy: entropy value
 *
 *	Add some additional seed material to the prandom pool.
 *	The "entropy" is actually our IP address (the only caller is
 *	the network code), not for unpredictability, but to ensure that
 *	different machines are initialized differently.
 */
void prandom_seed(u32 entropy)
{
	int i;

	add_device_randomness(&entropy, sizeof(entropy));

	for_each_possible_cpu(i) {
		struct siprand_state *state = per_cpu_ptr(&net_rand_state, i);
		unsigned long v0 = state->v0, v1 = state->v1;
		unsigned long v2 = state->v2, v3 = state->v3;

		do {
			v3 ^= entropy;
			PRND_SIPROUND(v0, v1, v2, v3);
			PRND_SIPROUND(v0, v1, v2, v3);
			v0 ^= entropy;
		} while (unlikely(!v0 || !v1 || !v2 || !v3));

		WRITE_ONCE(state->v0, v0);
		WRITE_ONCE(state->v1, v1);
		WRITE_ONCE(state->v2, v2);
		WRITE_ONCE(state->v3, v3);
	}
}
EXPORT_SYMBOL(prandom_seed);

/*
 *	Generate some initially weak seeding values to allow
 *	the prandom_u32() engine to be started.
 */
static int __init prandom_init_early(void)
{
	int i;
	unsigned long v0, v1, v2, v3;

	if (!arch_get_random_long(&v0))
		v0 = jiffies;
	if (!arch_get_random_long(&v1))
		v1 = random_get_entropy();
	v2 = v0 ^ PRND_K0;
	v3 = v1 ^ PRND_K1;

	for_each_possible_cpu(i) {
		struct siprand_state *state;

		v3 ^= i;
		PRND_SIPROUND(v0, v1, v2, v3);
		PRND_SIPROUND(v0, v1, v2, v3);
		v0 ^= i;

		state = per_cpu_ptr(&net_rand_state, i);
		state->v0 = v0;  state->v1 = v1;
		state->v2 = v2;  state->v3 = v3;
	}

	return 0;
}
core_initcall(prandom_init_early);


/* Stronger reseeding when available, and periodically thereafter. */
static void prandom_reseed(struct timer_list *unused);

static DEFINE_TIMER(seed_timer, prandom_reseed);

static void prandom_reseed(struct timer_list *unused)
{
	unsigned long expires;
	int i;

	/*
	 * Reinitialize each CPU's PRNG with 128 bits of key.
	 * No locking on the CPUs, but then somewhat random results are,
	 * well, expected.
	 */
	for_each_possible_cpu(i) {
		struct siprand_state *state;
		unsigned long v0 = get_random_long(), v2 = v0 ^ PRND_K0;
		unsigned long v1 = get_random_long(), v3 = v1 ^ PRND_K1;
#if BITS_PER_LONG == 32
		int j;

		/*
		 * On 32-bit machines, hash in two extra words to
		 * approximate 128-bit key length.  Not that the hash
		 * has that much security, but this prevents a trivial
		 * 64-bit brute force.
		 */
		for (j = 0; j < 2; j++) {
			unsigned long m = get_random_long();

			v3 ^= m;
			PRND_SIPROUND(v0, v1, v2, v3);
			PRND_SIPROUND(v0, v1, v2, v3);
			v0 ^= m;
		}
#endif
		/*
		 * Probably impossible in practice, but there is a
		 * theoretical risk that a race between this reseeding
		 * and the target CPU writing its state back could
		 * create the all-zero SipHash fixed point.
		 *
		 * To ensure that never happens, ensure the state
		 * we write contains no zero words.
		 */
		state = per_cpu_ptr(&net_rand_state, i);
		WRITE_ONCE(state->v0, v0 ? v0 : -1ul);
		WRITE_ONCE(state->v1, v1 ? v1 : -1ul);
		WRITE_ONCE(state->v2, v2 ? v2 : -1ul);
		WRITE_ONCE(state->v3, v3 ? v3 : -1ul);
	}

	/* reseed every ~60 seconds, in [40 .. 80) interval with slack */
	expires = round_jiffies(jiffies + 40 * HZ + prandom_u32_max(40 * HZ));
	mod_timer(&seed_timer, expires);
}

/*
 * The random ready callback can be called from almost any interrupt.
 * To avoid worrying about whether it's safe to delay that interrupt
 * long enough to seed all CPUs, just schedule an immediate timer event.
 */
static void prandom_timer_start(struct random_ready_callback *unused)
{
	mod_timer(&seed_timer, jiffies);
}

#ifdef CONFIG_RANDOM32_SELFTEST
/* Principle: True 32-bit random numbers will all have 16 differing bits on
 * average. For each 32-bit number, there are 601M numbers differing by 16
 * bits, and 89% of the numbers differ by at least 12 bits. Note that more
 * than 16 differing bits also implies a correlation with inverted bits. Thus
 * we take 1024 random numbers and compare each of them to the other ones,
 * counting the deviation of correlated bits to 16. Constants report 32,
 * counters 32-log2(TEST_SIZE), and pure randoms, around 6 or lower. With the
 * u32 total, TEST_SIZE may be as large as 4096 samples.
 */
#define TEST_SIZE 1024
static int __init prandom32_state_selftest(void)
{
	unsigned int x, y, bits, samples;
	u32 xor, flip;
	u32 total;
	u32 *data;

	data = kmalloc(sizeof(*data) * TEST_SIZE, GFP_KERNEL);
	if (!data)
		return 0;

	for (samples = 0; samples < TEST_SIZE; samples++)
		data[samples] = prandom_u32();

	flip = total = 0;
	for (x = 0; x < samples; x++) {
		for (y = 0; y < samples; y++) {
			if (x == y)
				continue;
			xor = data[x] ^ data[y];
			flip |= xor;
			bits = hweight32(xor);
			total += (bits - 16) * (bits - 16);
		}
	}

	/* We'll return the average deviation as 2*sqrt(corr/samples), which
	 * is also sqrt(4*corr/samples) which provides a better resolution.
	 */
	bits = int_sqrt(total / (samples * (samples - 1)) * 4);
	if (bits > 6)
		pr_warn("prandom32: self test failed (at least %u bits"
			" correlated, fixed_mask=%#x fixed_value=%#x\n",
			bits, ~flip, data[0] & ~flip);
	else
		pr_info("prandom32: self test passed (less than %u bits"
			" correlated)\n",
			bits+1);
	kfree(data);
	return 0;
}
core_initcall(prandom32_state_selftest);
#endif /*  CONFIG_RANDOM32_SELFTEST */

/*
 * Start periodic full reseeding as soon as strong
 * random numbers are available.
 */
static int __init prandom_init_late(void)
{
	static struct random_ready_callback random_ready = {
		.func = prandom_timer_start
	};
	int ret = add_random_ready_callback(&random_ready);

	if (ret == -EALREADY) {
		prandom_timer_start(&random_ready);
		ret = 0;
	}
	return ret;
}
late_initcall(prandom_init_late);
