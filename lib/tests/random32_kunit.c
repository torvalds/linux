// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for random32 functions.
 */

#include <linux/prandom.h>
#include <kunit/test.h>

/* prandom_warmup() is static in lib/random32.c; exposed for testing only. */
void prandom_warmup(struct rnd_state *state);

static const struct prandom_test1 {
	u32 seed;
	u32 result;
} test1[] = {
	{ 1U, 3484351685U },
	{ 2U, 2623130059U },
	{ 3U, 3125133893U },
	{ 4U,  984847254U },
};

static const struct prandom_test2 {
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

static void prandom_state_test_seed(struct rnd_state *state, u32 seed)
{
#define LCG(x)	 ((x) * 69069U)	/* super-duper LCG */
	state->s1 = __seed(LCG(seed),        2U);
	state->s2 = __seed(LCG(state->s1),   8U);
	state->s3 = __seed(LCG(state->s2),  16U);
	state->s4 = __seed(LCG(state->s3), 128U);
}

static void test_prandom_seed_boundary(struct kunit *test)
{
	int i;
	struct rnd_state state;

	for (i = 0; i < ARRAY_SIZE(test1); i++) {
		prandom_state_test_seed(&state, test1[i].seed);
		prandom_warmup(&state);
		KUNIT_EXPECT_EQ(test, test1[i].result, prandom_u32_state(&state));
	}
}

static void test_prandom_taus113(struct kunit *test)
{
	int i, j;
	struct rnd_state state;

	for (i = 0; i < ARRAY_SIZE(test2); i++) {
		prandom_state_test_seed(&state, test2[i].seed);
		prandom_warmup(&state);

		for (j = 0; j < test2[i].iteration - 1; j++)
			prandom_u32_state(&state);

		KUNIT_EXPECT_EQ(test, test2[i].result, prandom_u32_state(&state));
	}
}

static struct kunit_case prandom_test_cases[] = {
	KUNIT_CASE(test_prandom_seed_boundary),
	KUNIT_CASE(test_prandom_taus113),
	{}
};

static struct kunit_suite prandom_test_suite = {
	.name = "prandom",
	.test_cases = prandom_test_cases,
};

kunit_test_suite(prandom_test_suite);

MODULE_DESCRIPTION("KUnit test for prandom");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
