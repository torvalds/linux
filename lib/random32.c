/*
  This is a maximally equidistributed combined Tausworthe generator
  based on code from GNU Scientific Library 1.5 (30 Jun 2004)

  lfsr113 version:

   x_n = (s1_n ^ s2_n ^ s3_n ^ s4_n)

   s1_{n+1} = (((s1_n & 4294967294) << 18) ^ (((s1_n <<  6) ^ s1_n) >> 13))
   s2_{n+1} = (((s2_n & 4294967288) <<  2) ^ (((s2_n <<  2) ^ s2_n) >> 27))
   s3_{n+1} = (((s3_n & 4294967280) <<  7) ^ (((s3_n << 13) ^ s3_n) >> 21))
   s4_{n+1} = (((s4_n & 4294967168) << 13) ^ (((s4_n <<  3) ^ s4_n) >> 12))

   The period of this generator is about 2^113 (see erratum paper).

   From: P. L'Ecuyer, "Maximally Equidistributed Combined Tausworthe
   Generators", Mathematics of Computation, 65, 213 (1996), 203--213:
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
   s1 > 1, s2 > 7, s3 > 15, s4 > 127.

*/

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/sched.h>

#ifdef CONFIG_RANDOM32_SELFTEST
static void __init prandom_state_selftest(void);
#endif

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

	state->s1 = TAUSWORTHE(state->s1,  6U, 13U, 4294967294U, 18U);
	state->s2 = TAUSWORTHE(state->s2,  2U, 27U, 4294967288U,  2U);
	state->s3 = TAUSWORTHE(state->s3, 13U, 21U, 4294967280U,  7U);
	state->s4 = TAUSWORTHE(state->s4,  3U, 12U, 4294967168U, 13U);

	return (state->s1 ^ state->s2 ^ state->s3 ^ state->s4);
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

static void prandom_warmup(struct rnd_state *state)
{
	/* Calling RNG ten times to satify recurrence condition */
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

static void prandom_seed_very_weak(struct rnd_state *state, u32 seed)
{
	/* Note: This sort of seeding is ONLY used in test cases and
	 * during boot at the time from core_initcall until late_initcall
	 * as we don't have a stronger entropy source available yet.
	 * After late_initcall, we reseed entire state, we have to (!),
	 * otherwise an attacker just needs to search 32 bit space to
	 * probe for our internal 128 bit state if he knows a couple
	 * of prandom32 outputs!
	 */
#define LCG(x)	((x) * 69069U)	/* super-duper LCG */
	state->s1 = __seed(LCG(seed),        2U);
	state->s2 = __seed(LCG(state->s1),   8U);
	state->s3 = __seed(LCG(state->s2),  16U);
	state->s4 = __seed(LCG(state->s3), 128U);
}

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

		state->s1 = __seed(state->s1 ^ entropy, 2U);
		prandom_warmup(state);
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

#ifdef CONFIG_RANDOM32_SELFTEST
	prandom_state_selftest();
#endif

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);

		prandom_seed_very_weak(state, (i + jiffies) ^ random_get_entropy());
		prandom_warmup(state);
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

static void __init __prandom_start_seed_timer(void)
{
	set_timer_slack(&seed_timer, HZ);
	seed_timer.expires = jiffies + 40 * HZ;
	add_timer(&seed_timer);
}

/*
 *	Generate better values after random number generator
 *	is fully initialized.
 */
static void __prandom_reseed(bool late)
{
	int i;
	unsigned long flags;
	static bool latch = false;
	static DEFINE_SPINLOCK(lock);

	/* only allow initial seeding (late == false) once */
	spin_lock_irqsave(&lock, flags);
	if (latch && !late)
		goto out;
	latch = true;

	for_each_possible_cpu(i) {
		struct rnd_state *state = &per_cpu(net_rand_state,i);
		u32 seeds[4];

		get_random_bytes(&seeds, sizeof(seeds));
		state->s1 = __seed(seeds[0],   2U);
		state->s2 = __seed(seeds[1],   8U);
		state->s3 = __seed(seeds[2],  16U);
		state->s4 = __seed(seeds[3], 128U);

		prandom_warmup(state);
	}
out:
	spin_unlock_irqrestore(&lock, flags);
}

void prandom_reseed_late(void)
{
	__prandom_reseed(true);
}

static int __init prandom_reseed(void)
{
	__prandom_reseed(false);
	__prandom_start_seed_timer();
	return 0;
}
late_initcall(prandom_reseed);

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

static void __init prandom_state_selftest(void)
{
	int i, j, errors = 0, runs = 0;
	bool error = false;

	for (i = 0; i < ARRAY_SIZE(test1); i++) {
		struct rnd_state state;

		prandom_seed_very_weak(&state, test1[i].seed);
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

		prandom_seed_very_weak(&state, test2[i].seed);
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
}
#endif
