// SPDX-License-Identifier: GPL-2.0-only
/*
 * soc-topology-test.c  --  ALSA SoC Topology Kernel Unit Tests
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/random.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-topology.h>
#include <kunit/test.h>

/* ===== HELPER FUNCTIONS =================================================== */

/*
 * snd_soc_component needs device to operate on (primarily for prints), create
 * fake one, as we don't register with PCI or anything else
 * device_driver name is used in some of the prints (fmt_single_name) so
 * we also mock up minimal one
 */
static struct device *test_dev;

static struct device_driver test_drv = {
	.name = "sound-soc-topology-test-driver",
};

static int snd_soc_tplg_test_init(struct kunit *test)
{
	test_dev = root_device_register("sound-soc-topology-test");
	test_dev = get_device(test_dev);
	if (!test_dev)
		return -ENODEV;

	test_dev->driver = &test_drv;

	return 0;
}

static void snd_soc_tplg_test_exit(struct kunit *test)
{
	put_device(test_dev);
	root_device_unregister(test_dev);
}

/*
 * helper struct we use when registering component, as we load topology during
 * component probe, we need to pass struct kunit somehow to probe function, so
 * we can report test result
 */
struct kunit_soc_component {
	struct kunit *kunit;
	int expect; /* what result we expect when loading topology */
	struct snd_soc_component comp;
	struct snd_soc_card card;
	struct firmware fw;
};

static int d_probe(struct snd_soc_component *component)
{
	struct kunit_soc_component *kunit_comp =
			container_of(component, struct kunit_soc_component, comp);
	int ret;

	ret = snd_soc_tplg_component_load(component, NULL, &kunit_comp->fw);
	KUNIT_EXPECT_EQ_MSG(kunit_comp->kunit, kunit_comp->expect, ret,
			    "Failed topology load");

	return 0;
}

static void d_remove(struct snd_soc_component *component)
{
	struct kunit_soc_component *kunit_comp =
			container_of(component, struct kunit_soc_component, comp);
	int ret;

	ret = snd_soc_tplg_component_remove(component);
	KUNIT_EXPECT_EQ(kunit_comp->kunit, 0, ret);
}

/*
 * ASoC minimal boiler plate
 */
SND_SOC_DAILINK_DEF(dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(platform, DAILINK_COMP_ARRAY(COMP_PLATFORM("sound-soc-topology-test")));

static struct snd_soc_dai_link kunit_dai_links[] = {
	{
		.name = "KUNIT Audio Port",
		.id = 0,
		.stream_name = "Audio Playback/Capture",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(dummy, dummy, platform),
	},
};

static const struct snd_soc_component_driver test_component = {
	.name = "sound-soc-topology-test",
	.probe = d_probe,
	.remove = d_remove,
	.non_legacy_dai_naming = 1,
};

/* ===== TEST CASES ========================================================= */

// TEST CASE
// Test passing NULL component as parameter to snd_soc_tplg_component_load

/*
 * need to override generic probe function with one using NULL when calling
 * topology load during component initialization, we don't need .remove
 * handler as load should fail
 */
static int d_probe_null_comp(struct snd_soc_component *component)
{
	struct kunit_soc_component *kunit_comp =
			container_of(component, struct kunit_soc_component, comp);
	int ret;

	/* instead of passing component pointer as first argument, pass NULL here */
	ret = snd_soc_tplg_component_load(NULL, NULL, &kunit_comp->fw);
	KUNIT_EXPECT_EQ_MSG(kunit_comp->kunit, kunit_comp->expect, ret,
			    "Failed topology load");

	return 0;
}

static const struct snd_soc_component_driver test_component_null_comp = {
	.name = "sound-soc-topology-test",
	.probe = d_probe_null_comp,
	.non_legacy_dai_naming = 1,
};

static void snd_soc_tplg_test_load_with_null_comp(struct kunit *test)
{
	struct kunit_soc_component *kunit_comp;
	int ret;

	/* prepare */
	kunit_comp = kunit_kzalloc(test, sizeof(*kunit_comp), GFP_KERNEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, kunit_comp);
	kunit_comp->kunit = test;
	kunit_comp->expect = -EINVAL; /* expect failure */

	kunit_comp->card.dev = test_dev,
	kunit_comp->card.name = "kunit-card",
	kunit_comp->card.owner = THIS_MODULE,
	kunit_comp->card.dai_link = kunit_dai_links,
	kunit_comp->card.num_links = ARRAY_SIZE(kunit_dai_links),
	kunit_comp->card.fully_routed = true,

	/* run test */
	ret = snd_soc_register_card(&kunit_comp->card);
	if (ret != 0 && ret != -EPROBE_DEFER)
		KUNIT_FAIL(test, "Failed to register card");

	ret = snd_soc_component_initialize(&kunit_comp->comp, &test_component_null_comp, test_dev);
	KUNIT_EXPECT_EQ(test, 0, ret);

	ret = snd_soc_add_component(&kunit_comp->comp, NULL, 0);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* cleanup */
	ret = snd_soc_unregister_card(&kunit_comp->card);
	KUNIT_EXPECT_EQ(test, 0, ret);

	snd_soc_unregister_component(test_dev);
}

// TEST CASE
// Test passing NULL ops as parameter to snd_soc_tplg_component_load

/*
 * NULL ops is default case, we pass empty topology (fw), so we don't have
 * anything to parse and just do nothing, which results in return 0; from
 * calling soc_tplg_dapm_complete in soc_tplg_process_headers
 */
static void snd_soc_tplg_test_load_with_null_ops(struct kunit *test)
{
	struct kunit_soc_component *kunit_comp;
	int ret;

	/* prepare */
	kunit_comp = kunit_kzalloc(test, sizeof(*kunit_comp), GFP_KERNEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, kunit_comp);
	kunit_comp->kunit = test;
	kunit_comp->expect = 0; /* expect success */

	kunit_comp->card.dev = test_dev,
	kunit_comp->card.name = "kunit-card",
	kunit_comp->card.owner = THIS_MODULE,
	kunit_comp->card.dai_link = kunit_dai_links,
	kunit_comp->card.num_links = ARRAY_SIZE(kunit_dai_links),
	kunit_comp->card.fully_routed = true,

	/* run test */
	ret = snd_soc_register_card(&kunit_comp->card);
	if (ret != 0 && ret != -EPROBE_DEFER)
		KUNIT_FAIL(test, "Failed to register card");

	ret = snd_soc_component_initialize(&kunit_comp->comp, &test_component, test_dev);
	KUNIT_EXPECT_EQ(test, 0, ret);

	ret = snd_soc_add_component(&kunit_comp->comp, NULL, 0);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* cleanup */
	ret = snd_soc_unregister_card(&kunit_comp->card);
	KUNIT_EXPECT_EQ(test, 0, ret);

	snd_soc_unregister_component(test_dev);
}

// TEST CASE
// Test passing NULL fw as parameter to snd_soc_tplg_component_load

/*
 * need to override generic probe function with one using NULL pointer to fw
 * when calling topology load during component initialization, we don't need
 * .remove handler as load should fail
 */
static int d_probe_null_fw(struct snd_soc_component *component)
{
	struct kunit_soc_component *kunit_comp =
			container_of(component, struct kunit_soc_component, comp);
	int ret;

	/* instead of passing fw pointer as third argument, pass NULL here */
	ret = snd_soc_tplg_component_load(component, NULL, NULL);
	KUNIT_EXPECT_EQ_MSG(kunit_comp->kunit, kunit_comp->expect, ret,
			    "Failed topology load");

	return 0;
}

static const struct snd_soc_component_driver test_component_null_fw = {
	.name = "sound-soc-topology-test",
	.probe = d_probe_null_fw,
	.non_legacy_dai_naming = 1,
};

static void snd_soc_tplg_test_load_with_null_fw(struct kunit *test)
{
	struct kunit_soc_component *kunit_comp;
	int ret;

	/* prepare */
	kunit_comp = kunit_kzalloc(test, sizeof(*kunit_comp), GFP_KERNEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, kunit_comp);
	kunit_comp->kunit = test;
	kunit_comp->expect = -EINVAL; /* expect failure */

	kunit_comp->card.dev = test_dev,
	kunit_comp->card.name = "kunit-card",
	kunit_comp->card.owner = THIS_MODULE,
	kunit_comp->card.dai_link = kunit_dai_links,
	kunit_comp->card.num_links = ARRAY_SIZE(kunit_dai_links),
	kunit_comp->card.fully_routed = true,

	/* run test */
	ret = snd_soc_register_card(&kunit_comp->card);
	if (ret != 0 && ret != -EPROBE_DEFER)
		KUNIT_FAIL(test, "Failed to register card");

	ret = snd_soc_component_initialize(&kunit_comp->comp, &test_component_null_fw, test_dev);
	KUNIT_EXPECT_EQ(test, 0, ret);

	ret = snd_soc_add_component(&kunit_comp->comp, NULL, 0);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* cleanup */
	ret = snd_soc_unregister_card(&kunit_comp->card);
	KUNIT_EXPECT_EQ(test, 0, ret);

	snd_soc_unregister_component(test_dev);
}

/* ===== KUNIT MODULE DEFINITIONS =========================================== */

static struct kunit_case snd_soc_tplg_test_cases[] = {
	KUNIT_CASE(snd_soc_tplg_test_load_with_null_comp),
	KUNIT_CASE(snd_soc_tplg_test_load_with_null_ops),
	KUNIT_CASE(snd_soc_tplg_test_load_with_null_fw),
	{}
};

static struct kunit_suite snd_soc_tplg_test_suite = {
	.name = "snd_soc_tplg_test",
	.init = snd_soc_tplg_test_init,
	.exit = snd_soc_tplg_test_exit,
	.test_cases = snd_soc_tplg_test_cases,
};

kunit_test_suites(&snd_soc_tplg_test_suite);

MODULE_LICENSE("GPL");
