/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2008-2011	Luis R. Rodriguez <mcgrof@qca.qualcomm.com>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright      2017  Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


/**
 * DOC: Wireless regulatory infrastructure
 *
 * The usual implementation is for a driver to read a device EEPROM to
 * determine which regulatory domain it should be operating under, then
 * looking up the allowable channels in a driver-local table and finally
 * registering those channels in the wiphy structure.
 *
 * Another set of compliance enforcement is for drivers to use their
 * own compliance limits which can be stored on the EEPROM. The host
 * driver or firmware may ensure these are used.
 *
 * In addition to all this we provide an extra layer of regulatory
 * conformance. For drivers which do not have any regulatory
 * information CRDA provides the complete regulatory solution.
 * For others it provides a community effort on further restrictions
 * to enhance compliance.
 *
 * Note: When number of rules --> infinity we will not be able to
 * index on alpha2 any more, instead we'll probably have to
 * rely on some SHA1 checksum of the regdomain for example.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <linux/verification.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <net/cfg80211.h>
#include "core.h"
#include "reg.h"
#include "rdev-ops.h"
#include "nl80211.h"

/*
 * Grace period we give before making sure all current interfaces reside on
 * channels allowed by the current regulatory domain.
 */
#define REG_ENFORCE_GRACE_MS 60000

/**
 * enum reg_request_treatment - regulatory request treatment
 *
 * @REG_REQ_OK: continue processing the regulatory request
 * @REG_REQ_IGNORE: ignore the regulatory request
 * @REG_REQ_INTERSECT: the regulatory domain resulting from this request should
 *	be intersected with the current one.
 * @REG_REQ_ALREADY_SET: the regulatory request will not change the current
 *	regulatory settings, and no further processing is required.
 */
enum reg_request_treatment {
	REG_REQ_OK,
	REG_REQ_IGNORE,
	REG_REQ_INTERSECT,
	REG_REQ_ALREADY_SET,
};

static struct regulatory_request core_request_world = {
	.initiator = NL80211_REGDOM_SET_BY_CORE,
	.alpha2[0] = '0',
	.alpha2[1] = '0',
	.intersect = false,
	.processed = true,
	.country_ie_env = ENVIRON_ANY,
};

/*
 * Receipt of information from last regulatory request,
 * protected by RTNL (and can be accessed with RCU protection)
 */
static struct regulatory_request __rcu *last_request =
	(void __force __rcu *)&core_request_world;

/* To trigger userspace events and load firmware */
static struct platform_device *reg_pdev;

/*
 * Central wireless core regulatory domains, we only need two,
 * the current one and a world regulatory domain in case we have no
 * information to give us an alpha2.
 * (protected by RTNL, can be read under RCU)
 */
const struct ieee80211_regdomain __rcu *cfg80211_regdomain;

/*
 * Number of devices that registered to the core
 * that support cellular base station regulatory hints
 * (protected by RTNL)
 */
static int reg_num_devs_support_basehint;

/*
 * State variable indicating if the platform on which the devices
 * are attached is operating in an indoor environment. The state variable
 * is relevant for all registered devices.
 */
static bool reg_is_indoor;
static spinlock_t reg_indoor_lock;

/* Used to track the userspace process controlling the indoor setting */
static u32 reg_is_indoor_portid;

static void restore_regulatory_settings(bool reset_user);

static const struct ieee80211_regdomain *get_cfg80211_regdom(void)
{
	return rcu_dereference_rtnl(cfg80211_regdomain);
}

const struct ieee80211_regdomain *get_wiphy_regdom(struct wiphy *wiphy)
{
	return rcu_dereference_rtnl(wiphy->regd);
}

static const char *reg_dfs_region_str(enum nl80211_dfs_regions dfs_region)
{
	switch (dfs_region) {
	case NL80211_DFS_UNSET:
		return "unset";
	case NL80211_DFS_FCC:
		return "FCC";
	case NL80211_DFS_ETSI:
		return "ETSI";
	case NL80211_DFS_JP:
		return "JP";
	}
	return "Unknown";
}

enum nl80211_dfs_regions reg_get_dfs_region(struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *regd = NULL;
	const struct ieee80211_regdomain *wiphy_regd = NULL;

	regd = get_cfg80211_regdom();
	if (!wiphy)
		goto out;

	wiphy_regd = get_wiphy_regdom(wiphy);
	if (!wiphy_regd)
		goto out;

	if (wiphy_regd->dfs_region == regd->dfs_region)
		goto out;

	pr_debug("%s: device specific dfs_region (%s) disagrees with cfg80211's central dfs_region (%s)\n",
		 dev_name(&wiphy->dev),
		 reg_dfs_region_str(wiphy_regd->dfs_region),
		 reg_dfs_region_str(regd->dfs_region));

out:
	return regd->dfs_region;
}

static void rcu_free_regdom(const struct ieee80211_regdomain *r)
{
	if (!r)
		return;
	kfree_rcu((struct ieee80211_regdomain *)r, rcu_head);
}

static struct regulatory_request *get_last_request(void)
{
	return rcu_dereference_rtnl(last_request);
}

/* Used to queue up regulatory hints */
static LIST_HEAD(reg_requests_list);
static spinlock_t reg_requests_lock;

/* Used to queue up beacon hints for review */
static LIST_HEAD(reg_pending_beacons);
static spinlock_t reg_pending_beacons_lock;

/* Used to keep track of processed beacon hints */
static LIST_HEAD(reg_beacon_list);

struct reg_beacon {
	struct list_head list;
	struct ieee80211_channel chan;
};

static void reg_check_chans_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(reg_check_chans, reg_check_chans_work);

static void reg_todo(struct work_struct *work);
static DECLARE_WORK(reg_work, reg_todo);

/* We keep a static world regulatory domain in case of the absence of CRDA */
static const struct ieee80211_regdomain world_regdom = {
	.n_reg_rules = 8,
	.alpha2 =  "00",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2462+10, 40, 6, 20, 0),
		/* IEEE 802.11b/g, channels 12..13. */
		REG_RULE(2467-10, 2472+10, 20, 6, 20,
			NL80211_RRF_NO_IR | NL80211_RRF_AUTO_BW),
		/* IEEE 802.11 channel 14 - Only JP enables
		 * this and for 802.11b only */
		REG_RULE(2484-10, 2484+10, 20, 6, 20,
			NL80211_RRF_NO_IR |
			NL80211_RRF_NO_OFDM),
		/* IEEE 802.11a, channel 36..48 */
		REG_RULE(5180-10, 5240+10, 80, 6, 20,
                        NL80211_RRF_NO_IR |
                        NL80211_RRF_AUTO_BW),

		/* IEEE 802.11a, channel 52..64 - DFS required */
		REG_RULE(5260-10, 5320+10, 80, 6, 20,
			NL80211_RRF_NO_IR |
			NL80211_RRF_AUTO_BW |
			NL80211_RRF_DFS),

		/* IEEE 802.11a, channel 100..144 - DFS required */
		REG_RULE(5500-10, 5720+10, 160, 6, 20,
			NL80211_RRF_NO_IR |
			NL80211_RRF_DFS),

		/* IEEE 802.11a, channel 149..165 */
		REG_RULE(5745-10, 5825+10, 80, 6, 20,
			NL80211_RRF_NO_IR),

		/* IEEE 802.11ad (60GHz), channels 1..3 */
		REG_RULE(56160+2160*1-1080, 56160+2160*3+1080, 2160, 0, 0, 0),
	}
};

/* protected by RTNL */
static const struct ieee80211_regdomain *cfg80211_world_regdom =
	&world_regdom;

static char *ieee80211_regdom = "00";
static char user_alpha2[2];

module_param(ieee80211_regdom, charp, 0444);
MODULE_PARM_DESC(ieee80211_regdom, "IEEE 802.11 regulatory domain code");

static void reg_free_request(struct regulatory_request *request)
{
	if (request == &core_request_world)
		return;

	if (request != get_last_request())
		kfree(request);
}

static void reg_free_last_request(void)
{
	struct regulatory_request *lr = get_last_request();

	if (lr != &core_request_world && lr)
		kfree_rcu(lr, rcu_head);
}

static void reg_update_last_request(struct regulatory_request *request)
{
	struct regulatory_request *lr;

	lr = get_last_request();
	if (lr == request)
		return;

	reg_free_last_request();
	rcu_assign_pointer(last_request, request);
}

static void reset_regdomains(bool full_reset,
			     const struct ieee80211_regdomain *new_regdom)
{
	const struct ieee80211_regdomain *r;

	ASSERT_RTNL();

	r = get_cfg80211_regdom();

	/* avoid freeing static information or freeing something twice */
	if (r == cfg80211_world_regdom)
		r = NULL;
	if (cfg80211_world_regdom == &world_regdom)
		cfg80211_world_regdom = NULL;
	if (r == &world_regdom)
		r = NULL;

	rcu_free_regdom(r);
	rcu_free_regdom(cfg80211_world_regdom);

	cfg80211_world_regdom = &world_regdom;
	rcu_assign_pointer(cfg80211_regdomain, new_regdom);

	if (!full_reset)
		return;

	reg_update_last_request(&core_request_world);
}

/*
 * Dynamic world regulatory domain requested by the wireless
 * core upon initialization
 */
static void update_world_regdomain(const struct ieee80211_regdomain *rd)
{
	struct regulatory_request *lr;

	lr = get_last_request();

	WARN_ON(!lr);

	reset_regdomains(false, rd);

	cfg80211_world_regdom = rd;
}

bool is_world_regdom(const char *alpha2)
{
	if (!alpha2)
		return false;
	return alpha2[0] == '0' && alpha2[1] == '0';
}

static bool is_alpha2_set(const char *alpha2)
{
	if (!alpha2)
		return false;
	return alpha2[0] && alpha2[1];
}

static bool is_unknown_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/*
	 * Special case where regulatory domain was built by driver
	 * but a specific alpha2 cannot be determined
	 */
	return alpha2[0] == '9' && alpha2[1] == '9';
}

static bool is_intersected_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	/*
	 * Special case where regulatory domain is the
	 * result of an intersection between two regulatory domain
	 * structures
	 */
	return alpha2[0] == '9' && alpha2[1] == '8';
}

static bool is_an_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;
	return isalpha(alpha2[0]) && isalpha(alpha2[1]);
}

static bool alpha2_equal(const char *alpha2_x, const char *alpha2_y)
{
	if (!alpha2_x || !alpha2_y)
		return false;
	return alpha2_x[0] == alpha2_y[0] && alpha2_x[1] == alpha2_y[1];
}

static bool regdom_changes(const char *alpha2)
{
	const struct ieee80211_regdomain *r = get_cfg80211_regdom();

	if (!r)
		return true;
	return !alpha2_equal(r->alpha2, alpha2);
}

/*
 * The NL80211_REGDOM_SET_BY_USER regdom alpha2 is cached, this lets
 * you know if a valid regulatory hint with NL80211_REGDOM_SET_BY_USER
 * has ever been issued.
 */
static bool is_user_regdom_saved(void)
{
	if (user_alpha2[0] == '9' && user_alpha2[1] == '7')
		return false;

	/* This would indicate a mistake on the design */
	if (WARN(!is_world_regdom(user_alpha2) && !is_an_alpha2(user_alpha2),
		 "Unexpected user alpha2: %c%c\n",
		 user_alpha2[0], user_alpha2[1]))
		return false;

	return true;
}

static const struct ieee80211_regdomain *
reg_copy_regd(const struct ieee80211_regdomain *src_regd)
{
	struct ieee80211_regdomain *regd;
	int size_of_regd;
	unsigned int i;

	size_of_regd =
		sizeof(struct ieee80211_regdomain) +
		src_regd->n_reg_rules * sizeof(struct ieee80211_reg_rule);

	regd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!regd)
		return ERR_PTR(-ENOMEM);

	memcpy(regd, src_regd, sizeof(struct ieee80211_regdomain));

	for (i = 0; i < src_regd->n_reg_rules; i++)
		memcpy(&regd->reg_rules[i], &src_regd->reg_rules[i],
		       sizeof(struct ieee80211_reg_rule));

	return regd;
}

struct reg_regdb_apply_request {
	struct list_head list;
	const struct ieee80211_regdomain *regdom;
};

static LIST_HEAD(reg_regdb_apply_list);
static DEFINE_MUTEX(reg_regdb_apply_mutex);

static void reg_regdb_apply(struct work_struct *work)
{
	struct reg_regdb_apply_request *request;

	rtnl_lock();

	mutex_lock(&reg_regdb_apply_mutex);
	while (!list_empty(&reg_regdb_apply_list)) {
		request = list_first_entry(&reg_regdb_apply_list,
					   struct reg_regdb_apply_request,
					   list);
		list_del(&request->list);

		set_regdom(request->regdom, REGD_SOURCE_INTERNAL_DB);
		kfree(request);
	}
	mutex_unlock(&reg_regdb_apply_mutex);

	rtnl_unlock();
}

static DECLARE_WORK(reg_regdb_work, reg_regdb_apply);

static int reg_schedule_apply(const struct ieee80211_regdomain *regdom)
{
	struct reg_regdb_apply_request *request;

	request = kzalloc(sizeof(struct reg_regdb_apply_request), GFP_KERNEL);
	if (!request) {
		kfree(regdom);
		return -ENOMEM;
	}

	request->regdom = regdom;

	mutex_lock(&reg_regdb_apply_mutex);
	list_add_tail(&request->list, &reg_regdb_apply_list);
	mutex_unlock(&reg_regdb_apply_mutex);

	schedule_work(&reg_regdb_work);
	return 0;
}

#ifdef CONFIG_CFG80211_CRDA_SUPPORT
/* Max number of consecutive attempts to communicate with CRDA  */
#define REG_MAX_CRDA_TIMEOUTS 10

static u32 reg_crda_timeouts;

static void crda_timeout_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(crda_timeout, crda_timeout_work);

static void crda_timeout_work(struct work_struct *work)
{
	pr_debug("Timeout while waiting for CRDA to reply, restoring regulatory settings\n");
	rtnl_lock();
	reg_crda_timeouts++;
	restore_regulatory_settings(true);
	rtnl_unlock();
}

static void cancel_crda_timeout(void)
{
	cancel_delayed_work(&crda_timeout);
}

static void cancel_crda_timeout_sync(void)
{
	cancel_delayed_work_sync(&crda_timeout);
}

static void reset_crda_timeouts(void)
{
	reg_crda_timeouts = 0;
}

/*
 * This lets us keep regulatory code which is updated on a regulatory
 * basis in userspace.
 */
static int call_crda(const char *alpha2)
{
	char country[12];
	char *env[] = { country, NULL };
	int ret;

	snprintf(country, sizeof(country), "COUNTRY=%c%c",
		 alpha2[0], alpha2[1]);

	if (reg_crda_timeouts > REG_MAX_CRDA_TIMEOUTS) {
		pr_debug("Exceeded CRDA call max attempts. Not calling CRDA\n");
		return -EINVAL;
	}

	if (!is_world_regdom((char *) alpha2))
		pr_debug("Calling CRDA for country: %c%c\n",
			 alpha2[0], alpha2[1]);
	else
		pr_debug("Calling CRDA to update world regulatory domain\n");

	ret = kobject_uevent_env(&reg_pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		return ret;

	queue_delayed_work(system_power_efficient_wq,
			   &crda_timeout, msecs_to_jiffies(3142));
	return 0;
}
#else
static inline void cancel_crda_timeout(void) {}
static inline void cancel_crda_timeout_sync(void) {}
static inline void reset_crda_timeouts(void) {}
static inline int call_crda(const char *alpha2)
{
	return -ENODATA;
}
#endif /* CONFIG_CFG80211_CRDA_SUPPORT */

/* code to directly load a firmware database through request_firmware */
static const struct fwdb_header *regdb;

struct fwdb_country {
	u8 alpha2[2];
	__be16 coll_ptr;
	/* this struct cannot be extended */
} __packed __aligned(4);

struct fwdb_collection {
	u8 len;
	u8 n_rules;
	u8 dfs_region;
	/* no optional data yet */
	/* aligned to 2, then followed by __be16 array of rule pointers */
} __packed __aligned(4);

enum fwdb_flags {
	FWDB_FLAG_NO_OFDM	= BIT(0),
	FWDB_FLAG_NO_OUTDOOR	= BIT(1),
	FWDB_FLAG_DFS		= BIT(2),
	FWDB_FLAG_NO_IR		= BIT(3),
	FWDB_FLAG_AUTO_BW	= BIT(4),
};

struct fwdb_wmm_ac {
	u8 ecw;
	u8 aifsn;
	__be16 cot;
} __packed;

struct fwdb_wmm_rule {
	struct fwdb_wmm_ac client[IEEE80211_NUM_ACS];
	struct fwdb_wmm_ac ap[IEEE80211_NUM_ACS];
} __packed;

struct fwdb_rule {
	u8 len;
	u8 flags;
	__be16 max_eirp;
	__be32 start, end, max_bw;
	/* start of optional data */
	__be16 cac_timeout;
	__be16 wmm_ptr;
} __packed __aligned(4);

#define FWDB_MAGIC 0x52474442
#define FWDB_VERSION 20

struct fwdb_header {
	__be32 magic;
	__be32 version;
	struct fwdb_country country[];
} __packed __aligned(4);

static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}

static bool valid_wmm(struct fwdb_wmm_rule *rule)
{
	struct fwdb_wmm_ac *ac = (struct fwdb_wmm_ac *)rule;
	int i;

	for (i = 0; i < IEEE80211_NUM_ACS * 2; i++) {
		u16 cw_min = ecw2cw((ac[i].ecw & 0xf0) >> 4);
		u16 cw_max = ecw2cw(ac[i].ecw & 0x0f);
		u8 aifsn = ac[i].aifsn;

		if (cw_min >= cw_max)
			return false;

		if (aifsn < 1)
			return false;
	}

	return true;
}

static bool valid_rule(const u8 *data, unsigned int size, u16 rule_ptr)
{
	struct fwdb_rule *rule = (void *)(data + (rule_ptr << 2));

	if ((u8 *)rule + sizeof(rule->len) > data + size)
		return false;

	/* mandatory fields */
	if (rule->len < offsetofend(struct fwdb_rule, max_bw))
		return false;
	if (rule->len >= offsetofend(struct fwdb_rule, wmm_ptr)) {
		u32 wmm_ptr = be16_to_cpu(rule->wmm_ptr) << 2;
		struct fwdb_wmm_rule *wmm;

		if (wmm_ptr + sizeof(struct fwdb_wmm_rule) > size)
			return false;

		wmm = (void *)(data + wmm_ptr);

		if (!valid_wmm(wmm))
			return false;
	}
	return true;
}

static bool valid_country(const u8 *data, unsigned int size,
			  const struct fwdb_country *country)
{
	unsigned int ptr = be16_to_cpu(country->coll_ptr) << 2;
	struct fwdb_collection *coll = (void *)(data + ptr);
	__be16 *rules_ptr;
	unsigned int i;

	/* make sure we can read len/n_rules */
	if ((u8 *)coll + offsetofend(typeof(*coll), n_rules) > data + size)
		return false;

	/* make sure base struct and all rules fit */
	if ((u8 *)coll + ALIGN(coll->len, 2) +
	    (coll->n_rules * 2) > data + size)
		return false;

	/* mandatory fields must exist */
	if (coll->len < offsetofend(struct fwdb_collection, dfs_region))
		return false;

	rules_ptr = (void *)((u8 *)coll + ALIGN(coll->len, 2));

	for (i = 0; i < coll->n_rules; i++) {
		u16 rule_ptr = be16_to_cpu(rules_ptr[i]);

		if (!valid_rule(data, size, rule_ptr))
			return false;
	}

	return true;
}

#ifdef CONFIG_CFG80211_REQUIRE_SIGNED_REGDB
static struct key *builtin_regdb_keys;

static void __init load_keys_from_buffer(const u8 *p, unsigned int buflen)
{
	const u8 *end = p + buflen;
	size_t plen;
	key_ref_t key;

	while (p < end) {
		/* Each cert begins with an ASN.1 SEQUENCE tag and must be more
		 * than 256 bytes in size.
		 */
		if (end - p < 4)
			goto dodgy_cert;
		if (p[0] != 0x30 &&
		    p[1] != 0x82)
			goto dodgy_cert;
		plen = (p[2] << 8) | p[3];
		plen += 4;
		if (plen > end - p)
			goto dodgy_cert;

		key = key_create_or_update(make_key_ref(builtin_regdb_keys, 1),
					   "asymmetric", NULL, p, plen,
					   ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
					    KEY_USR_VIEW | KEY_USR_READ),
					   KEY_ALLOC_NOT_IN_QUOTA |
					   KEY_ALLOC_BUILT_IN |
					   KEY_ALLOC_BYPASS_RESTRICTION);
		if (IS_ERR(key)) {
			pr_err("Problem loading in-kernel X.509 certificate (%ld)\n",
			       PTR_ERR(key));
		} else {
			pr_notice("Loaded X.509 cert '%s'\n",
				  key_ref_to_ptr(key)->description);
			key_ref_put(key);
		}
		p += plen;
	}

	return;

dodgy_cert:
	pr_err("Problem parsing in-kernel X.509 certificate list\n");
}

static int __init load_builtin_regdb_keys(void)
{
	builtin_regdb_keys =
		keyring_alloc(".builtin_regdb_keys",
			      KUIDT_INIT(0), KGIDT_INIT(0), current_cred(),
			      ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
			      KEY_USR_VIEW | KEY_USR_READ | KEY_USR_SEARCH),
			      KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(builtin_regdb_keys))
		return PTR_ERR(builtin_regdb_keys);

	pr_notice("Loading compiled-in X.509 certificates for regulatory database\n");

#ifdef CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS
	load_keys_from_buffer(shipped_regdb_certs, shipped_regdb_certs_len);
#endif
#ifdef CONFIG_CFG80211_EXTRA_REGDB_KEYDIR
	if (CONFIG_CFG80211_EXTRA_REGDB_KEYDIR[0] != '\0')
		load_keys_from_buffer(extra_regdb_certs, extra_regdb_certs_len);
#endif

	return 0;
}

static bool regdb_has_valid_signature(const u8 *data, unsigned int size)
{
	const struct firmware *sig;
	bool result;

	if (request_firmware(&sig, "regulatory.db.p7s", &reg_pdev->dev))
		return false;

	result = verify_pkcs7_signature(data, size, sig->data, sig->size,
					builtin_regdb_keys,
					VERIFYING_UNSPECIFIED_SIGNATURE,
					NULL, NULL) == 0;

	release_firmware(sig);

	return result;
}

static void free_regdb_keyring(void)
{
	key_put(builtin_regdb_keys);
}
#else
static int load_builtin_regdb_keys(void)
{
	return 0;
}

static bool regdb_has_valid_signature(const u8 *data, unsigned int size)
{
	return true;
}

static void free_regdb_keyring(void)
{
}
#endif /* CONFIG_CFG80211_REQUIRE_SIGNED_REGDB */

static bool valid_regdb(const u8 *data, unsigned int size)
{
	const struct fwdb_header *hdr = (void *)data;
	const struct fwdb_country *country;

	if (size < sizeof(*hdr))
		return false;

	if (hdr->magic != cpu_to_be32(FWDB_MAGIC))
		return false;

	if (hdr->version != cpu_to_be32(FWDB_VERSION))
		return false;

	if (!regdb_has_valid_signature(data, size))
		return false;

	country = &hdr->country[0];
	while ((u8 *)(country + 1) <= data + size) {
		if (!country->coll_ptr)
			break;
		if (!valid_country(data, size, country))
			return false;
		country++;
	}

	return true;
}

static void set_wmm_rule(const struct fwdb_header *db,
			 const struct fwdb_country *country,
			 const struct fwdb_rule *rule,
			 struct ieee80211_reg_rule *rrule)
{
	struct ieee80211_wmm_rule *wmm_rule = &rrule->wmm_rule;
	struct fwdb_wmm_rule *wmm;
	unsigned int i, wmm_ptr;

	wmm_ptr = be16_to_cpu(rule->wmm_ptr) << 2;
	wmm = (void *)((u8 *)db + wmm_ptr);

	if (!valid_wmm(wmm)) {
		pr_err("Invalid regulatory WMM rule %u-%u in domain %c%c\n",
		       be32_to_cpu(rule->start), be32_to_cpu(rule->end),
		       country->alpha2[0], country->alpha2[1]);
		return;
	}

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		wmm_rule->client[i].cw_min =
			ecw2cw((wmm->client[i].ecw & 0xf0) >> 4);
		wmm_rule->client[i].cw_max = ecw2cw(wmm->client[i].ecw & 0x0f);
		wmm_rule->client[i].aifsn =  wmm->client[i].aifsn;
		wmm_rule->client[i].cot =
			1000 * be16_to_cpu(wmm->client[i].cot);
		wmm_rule->ap[i].cw_min = ecw2cw((wmm->ap[i].ecw & 0xf0) >> 4);
		wmm_rule->ap[i].cw_max = ecw2cw(wmm->ap[i].ecw & 0x0f);
		wmm_rule->ap[i].aifsn = wmm->ap[i].aifsn;
		wmm_rule->ap[i].cot = 1000 * be16_to_cpu(wmm->ap[i].cot);
	}

	rrule->has_wmm = true;
}

static int __regdb_query_wmm(const struct fwdb_header *db,
			     const struct fwdb_country *country, int freq,
			     struct ieee80211_reg_rule *rrule)
{
	unsigned int ptr = be16_to_cpu(country->coll_ptr) << 2;
	struct fwdb_collection *coll = (void *)((u8 *)db + ptr);
	int i;

	for (i = 0; i < coll->n_rules; i++) {
		__be16 *rules_ptr = (void *)((u8 *)coll + ALIGN(coll->len, 2));
		unsigned int rule_ptr = be16_to_cpu(rules_ptr[i]) << 2;
		struct fwdb_rule *rule = (void *)((u8 *)db + rule_ptr);

		if (rule->len < offsetofend(struct fwdb_rule, wmm_ptr))
			continue;

		if (freq >= KHZ_TO_MHZ(be32_to_cpu(rule->start)) &&
		    freq <= KHZ_TO_MHZ(be32_to_cpu(rule->end))) {
			set_wmm_rule(db, country, rule, rrule);
			return 0;
		}
	}

	return -ENODATA;
}

int reg_query_regdb_wmm(char *alpha2, int freq, struct ieee80211_reg_rule *rule)
{
	const struct fwdb_header *hdr = regdb;
	const struct fwdb_country *country;

	if (!regdb)
		return -ENODATA;

	if (IS_ERR(regdb))
		return PTR_ERR(regdb);

	country = &hdr->country[0];
	while (country->coll_ptr) {
		if (alpha2_equal(alpha2, country->alpha2))
			return __regdb_query_wmm(regdb, country, freq, rule);

		country++;
	}

	return -ENODATA;
}
EXPORT_SYMBOL(reg_query_regdb_wmm);

static int regdb_query_country(const struct fwdb_header *db,
			       const struct fwdb_country *country)
{
	unsigned int ptr = be16_to_cpu(country->coll_ptr) << 2;
	struct fwdb_collection *coll = (void *)((u8 *)db + ptr);
	struct ieee80211_regdomain *regdom;
	unsigned int size_of_regd, i;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		coll->n_rules * sizeof(struct ieee80211_reg_rule);

	regdom = kzalloc(size_of_regd, GFP_KERNEL);
	if (!regdom)
		return -ENOMEM;

	regdom->n_reg_rules = coll->n_rules;
	regdom->alpha2[0] = country->alpha2[0];
	regdom->alpha2[1] = country->alpha2[1];
	regdom->dfs_region = coll->dfs_region;

	for (i = 0; i < regdom->n_reg_rules; i++) {
		__be16 *rules_ptr = (void *)((u8 *)coll + ALIGN(coll->len, 2));
		unsigned int rule_ptr = be16_to_cpu(rules_ptr[i]) << 2;
		struct fwdb_rule *rule = (void *)((u8 *)db + rule_ptr);
		struct ieee80211_reg_rule *rrule = &regdom->reg_rules[i];

		rrule->freq_range.start_freq_khz = be32_to_cpu(rule->start);
		rrule->freq_range.end_freq_khz = be32_to_cpu(rule->end);
		rrule->freq_range.max_bandwidth_khz = be32_to_cpu(rule->max_bw);

		rrule->power_rule.max_antenna_gain = 0;
		rrule->power_rule.max_eirp = be16_to_cpu(rule->max_eirp);

		rrule->flags = 0;
		if (rule->flags & FWDB_FLAG_NO_OFDM)
			rrule->flags |= NL80211_RRF_NO_OFDM;
		if (rule->flags & FWDB_FLAG_NO_OUTDOOR)
			rrule->flags |= NL80211_RRF_NO_OUTDOOR;
		if (rule->flags & FWDB_FLAG_DFS)
			rrule->flags |= NL80211_RRF_DFS;
		if (rule->flags & FWDB_FLAG_NO_IR)
			rrule->flags |= NL80211_RRF_NO_IR;
		if (rule->flags & FWDB_FLAG_AUTO_BW)
			rrule->flags |= NL80211_RRF_AUTO_BW;

		rrule->dfs_cac_ms = 0;

		/* handle optional data */
		if (rule->len >= offsetofend(struct fwdb_rule, cac_timeout))
			rrule->dfs_cac_ms =
				1000 * be16_to_cpu(rule->cac_timeout);
		if (rule->len >= offsetofend(struct fwdb_rule, wmm_ptr))
			set_wmm_rule(db, country, rule, rrule);
	}

	return reg_schedule_apply(regdom);
}

static int query_regdb(const char *alpha2)
{
	const struct fwdb_header *hdr = regdb;
	const struct fwdb_country *country;

	ASSERT_RTNL();

	if (IS_ERR(regdb))
		return PTR_ERR(regdb);

	country = &hdr->country[0];
	while (country->coll_ptr) {
		if (alpha2_equal(alpha2, country->alpha2))
			return regdb_query_country(regdb, country);
		country++;
	}

	return -ENODATA;
}

static void regdb_fw_cb(const struct firmware *fw, void *context)
{
	int set_error = 0;
	bool restore = true;
	void *db;

	if (!fw) {
		pr_info("failed to load regulatory.db\n");
		set_error = -ENODATA;
	} else if (!valid_regdb(fw->data, fw->size)) {
		pr_info("loaded regulatory.db is malformed or signature is missing/invalid\n");
		set_error = -EINVAL;
	}

	rtnl_lock();
	if (WARN_ON(regdb && !IS_ERR(regdb))) {
		/* just restore and free new db */
	} else if (set_error) {
		regdb = ERR_PTR(set_error);
	} else if (fw) {
		db = kmemdup(fw->data, fw->size, GFP_KERNEL);
		if (db) {
			regdb = db;
			restore = context && query_regdb(context);
		} else {
			restore = true;
		}
	}

	if (restore)
		restore_regulatory_settings(true);

	rtnl_unlock();

	kfree(context);

	release_firmware(fw);
}

static int query_regdb_file(const char *alpha2)
{
	ASSERT_RTNL();

	if (regdb)
		return query_regdb(alpha2);

	alpha2 = kmemdup(alpha2, 2, GFP_KERNEL);
	if (!alpha2)
		return -ENOMEM;

	return request_firmware_nowait(THIS_MODULE, true, "regulatory.db",
				       &reg_pdev->dev, GFP_KERNEL,
				       (void *)alpha2, regdb_fw_cb);
}

int reg_reload_regdb(void)
{
	const struct firmware *fw;
	void *db;
	int err;

	err = request_firmware(&fw, "regulatory.db", &reg_pdev->dev);
	if (err)
		return err;

	if (!valid_regdb(fw->data, fw->size)) {
		err = -ENODATA;
		goto out;
	}

	db = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (!db) {
		err = -ENOMEM;
		goto out;
	}

	rtnl_lock();
	if (!IS_ERR_OR_NULL(regdb))
		kfree(regdb);
	regdb = db;
	rtnl_unlock();

 out:
	release_firmware(fw);
	return err;
}

static bool reg_query_database(struct regulatory_request *request)
{
	if (query_regdb_file(request->alpha2) == 0)
		return true;

	if (call_crda(request->alpha2) == 0)
		return true;

	return false;
}

bool reg_is_valid_request(const char *alpha2)
{
	struct regulatory_request *lr = get_last_request();

	if (!lr || lr->processed)
		return false;

	return alpha2_equal(lr->alpha2, alpha2);
}

static const struct ieee80211_regdomain *reg_get_regdomain(struct wiphy *wiphy)
{
	struct regulatory_request *lr = get_last_request();

	/*
	 * Follow the driver's regulatory domain, if present, unless a country
	 * IE has been processed or a user wants to help complaince further
	 */
	if (lr->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    lr->initiator != NL80211_REGDOM_SET_BY_USER &&
	    wiphy->regd)
		return get_wiphy_regdom(wiphy);

	return get_cfg80211_regdom();
}

static unsigned int
reg_get_max_bandwidth_from_range(const struct ieee80211_regdomain *rd,
				 const struct ieee80211_reg_rule *rule)
{
	const struct ieee80211_freq_range *freq_range = &rule->freq_range;
	const struct ieee80211_freq_range *freq_range_tmp;
	const struct ieee80211_reg_rule *tmp;
	u32 start_freq, end_freq, idx, no;

	for (idx = 0; idx < rd->n_reg_rules; idx++)
		if (rule == &rd->reg_rules[idx])
			break;

	if (idx == rd->n_reg_rules)
		return 0;

	/* get start_freq */
	no = idx;

	while (no) {
		tmp = &rd->reg_rules[--no];
		freq_range_tmp = &tmp->freq_range;

		if (freq_range_tmp->end_freq_khz < freq_range->start_freq_khz)
			break;

		freq_range = freq_range_tmp;
	}

	start_freq = freq_range->start_freq_khz;

	/* get end_freq */
	freq_range = &rule->freq_range;
	no = idx;

	while (no < rd->n_reg_rules - 1) {
		tmp = &rd->reg_rules[++no];
		freq_range_tmp = &tmp->freq_range;

		if (freq_range_tmp->start_freq_khz > freq_range->end_freq_khz)
			break;

		freq_range = freq_range_tmp;
	}

	end_freq = freq_range->end_freq_khz;

	return end_freq - start_freq;
}

unsigned int reg_get_max_bandwidth(const struct ieee80211_regdomain *rd,
				   const struct ieee80211_reg_rule *rule)
{
	unsigned int bw = reg_get_max_bandwidth_from_range(rd, rule);

	if (rule->flags & NL80211_RRF_NO_160MHZ)
		bw = min_t(unsigned int, bw, MHZ_TO_KHZ(80));
	if (rule->flags & NL80211_RRF_NO_80MHZ)
		bw = min_t(unsigned int, bw, MHZ_TO_KHZ(40));

	/*
	 * HT40+/HT40- limits are handled per-channel. Only limit BW if both
	 * are not allowed.
	 */
	if (rule->flags & NL80211_RRF_NO_HT40MINUS &&
	    rule->flags & NL80211_RRF_NO_HT40PLUS)
		bw = min_t(unsigned int, bw, MHZ_TO_KHZ(20));

	return bw;
}

/* Sanity check on a regulatory rule */
static bool is_valid_reg_rule(const struct ieee80211_reg_rule *rule)
{
	const struct ieee80211_freq_range *freq_range = &rule->freq_range;
	u32 freq_diff;

	if (freq_range->start_freq_khz <= 0 || freq_range->end_freq_khz <= 0)
		return false;

	if (freq_range->start_freq_khz > freq_range->end_freq_khz)
		return false;

	freq_diff = freq_range->end_freq_khz - freq_range->start_freq_khz;

	if (freq_range->end_freq_khz <= freq_range->start_freq_khz ||
	    freq_range->max_bandwidth_khz > freq_diff)
		return false;

	return true;
}

static bool is_valid_rd(const struct ieee80211_regdomain *rd)
{
	const struct ieee80211_reg_rule *reg_rule = NULL;
	unsigned int i;

	if (!rd->n_reg_rules)
		return false;

	if (WARN_ON(rd->n_reg_rules > NL80211_MAX_SUPP_REG_RULES))
		return false;

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		if (!is_valid_reg_rule(reg_rule))
			return false;
	}

	return true;
}

/**
 * freq_in_rule_band - tells us if a frequency is in a frequency band
 * @freq_range: frequency rule we want to query
 * @freq_khz: frequency we are inquiring about
 *
 * This lets us know if a specific frequency rule is or is not relevant to
 * a specific frequency's band. Bands are device specific and artificial
 * definitions (the "2.4 GHz band", the "5 GHz band" and the "60GHz band"),
 * however it is safe for now to assume that a frequency rule should not be
 * part of a frequency's band if the start freq or end freq are off by more
 * than 2 GHz for the 2.4 and 5 GHz bands, and by more than 20 GHz for the
 * 60 GHz band.
 * This resolution can be lowered and should be considered as we add
 * regulatory rule support for other "bands".
 **/
static bool freq_in_rule_band(const struct ieee80211_freq_range *freq_range,
			      u32 freq_khz)
{
#define ONE_GHZ_IN_KHZ	1000000
	/*
	 * From 802.11ad: directional multi-gigabit (DMG):
	 * Pertaining to operation in a frequency band containing a channel
	 * with the Channel starting frequency above 45 GHz.
	 */
	u32 limit = freq_khz > 45 * ONE_GHZ_IN_KHZ ?
			20 * ONE_GHZ_IN_KHZ : 2 * ONE_GHZ_IN_KHZ;
	if (abs(freq_khz - freq_range->start_freq_khz) <= limit)
		return true;
	if (abs(freq_khz - freq_range->end_freq_khz) <= limit)
		return true;
	return false;
#undef ONE_GHZ_IN_KHZ
}

/*
 * Later on we can perhaps use the more restrictive DFS
 * region but we don't have information for that yet so
 * for now simply disallow conflicts.
 */
static enum nl80211_dfs_regions
reg_intersect_dfs_region(const enum nl80211_dfs_regions dfs_region1,
			 const enum nl80211_dfs_regions dfs_region2)
{
	if (dfs_region1 != dfs_region2)
		return NL80211_DFS_UNSET;
	return dfs_region1;
}

static void reg_wmm_rules_intersect(const struct ieee80211_wmm_ac *wmm_ac1,
				    const struct ieee80211_wmm_ac *wmm_ac2,
				    struct ieee80211_wmm_ac *intersect)
{
	intersect->cw_min = max_t(u16, wmm_ac1->cw_min, wmm_ac2->cw_min);
	intersect->cw_max = max_t(u16, wmm_ac1->cw_max, wmm_ac2->cw_max);
	intersect->cot = min_t(u16, wmm_ac1->cot, wmm_ac2->cot);
	intersect->aifsn = max_t(u8, wmm_ac1->aifsn, wmm_ac2->aifsn);
}

/*
 * Helper for regdom_intersect(), this does the real
 * mathematical intersection fun
 */
static int reg_rules_intersect(const struct ieee80211_regdomain *rd1,
			       const struct ieee80211_regdomain *rd2,
			       const struct ieee80211_reg_rule *rule1,
			       const struct ieee80211_reg_rule *rule2,
			       struct ieee80211_reg_rule *intersected_rule)
{
	const struct ieee80211_freq_range *freq_range1, *freq_range2;
	struct ieee80211_freq_range *freq_range;
	const struct ieee80211_power_rule *power_rule1, *power_rule2;
	struct ieee80211_power_rule *power_rule;
	const struct ieee80211_wmm_rule *wmm_rule1, *wmm_rule2;
	struct ieee80211_wmm_rule *wmm_rule;
	u32 freq_diff, max_bandwidth1, max_bandwidth2;

	freq_range1 = &rule1->freq_range;
	freq_range2 = &rule2->freq_range;
	freq_range = &intersected_rule->freq_range;

	power_rule1 = &rule1->power_rule;
	power_rule2 = &rule2->power_rule;
	power_rule = &intersected_rule->power_rule;

	wmm_rule1 = &rule1->wmm_rule;
	wmm_rule2 = &rule2->wmm_rule;
	wmm_rule = &intersected_rule->wmm_rule;

	freq_range->start_freq_khz = max(freq_range1->start_freq_khz,
					 freq_range2->start_freq_khz);
	freq_range->end_freq_khz = min(freq_range1->end_freq_khz,
				       freq_range2->end_freq_khz);

	max_bandwidth1 = freq_range1->max_bandwidth_khz;
	max_bandwidth2 = freq_range2->max_bandwidth_khz;

	if (rule1->flags & NL80211_RRF_AUTO_BW)
		max_bandwidth1 = reg_get_max_bandwidth(rd1, rule1);
	if (rule2->flags & NL80211_RRF_AUTO_BW)
		max_bandwidth2 = reg_get_max_bandwidth(rd2, rule2);

	freq_range->max_bandwidth_khz = min(max_bandwidth1, max_bandwidth2);

	intersected_rule->flags = rule1->flags | rule2->flags;

	/*
	 * In case NL80211_RRF_AUTO_BW requested for both rules
	 * set AUTO_BW in intersected rule also. Next we will
	 * calculate BW correctly in handle_channel function.
	 * In other case remove AUTO_BW flag while we calculate
	 * maximum bandwidth correctly and auto calculation is
	 * not required.
	 */
	if ((rule1->flags & NL80211_RRF_AUTO_BW) &&
	    (rule2->flags & NL80211_RRF_AUTO_BW))
		intersected_rule->flags |= NL80211_RRF_AUTO_BW;
	else
		intersected_rule->flags &= ~NL80211_RRF_AUTO_BW;

	freq_diff = freq_range->end_freq_khz - freq_range->start_freq_khz;
	if (freq_range->max_bandwidth_khz > freq_diff)
		freq_range->max_bandwidth_khz = freq_diff;

	power_rule->max_eirp = min(power_rule1->max_eirp,
		power_rule2->max_eirp);
	power_rule->max_antenna_gain = min(power_rule1->max_antenna_gain,
		power_rule2->max_antenna_gain);

	intersected_rule->dfs_cac_ms = max(rule1->dfs_cac_ms,
					   rule2->dfs_cac_ms);

	if (rule1->has_wmm && rule2->has_wmm) {
		u8 ac;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			reg_wmm_rules_intersect(&wmm_rule1->client[ac],
						&wmm_rule2->client[ac],
						&wmm_rule->client[ac]);
			reg_wmm_rules_intersect(&wmm_rule1->ap[ac],
						&wmm_rule2->ap[ac],
						&wmm_rule->ap[ac]);
		}

		intersected_rule->has_wmm = true;
	} else if (rule1->has_wmm) {
		*wmm_rule = *wmm_rule1;
		intersected_rule->has_wmm = true;
	} else if (rule2->has_wmm) {
		*wmm_rule = *wmm_rule2;
		intersected_rule->has_wmm = true;
	} else {
		intersected_rule->has_wmm = false;
	}

	if (!is_valid_reg_rule(intersected_rule))
		return -EINVAL;

	return 0;
}

/* check whether old rule contains new rule */
static bool rule_contains(struct ieee80211_reg_rule *r1,
			  struct ieee80211_reg_rule *r2)
{
	/* for simplicity, currently consider only same flags */
	if (r1->flags != r2->flags)
		return false;

	/* verify r1 is more restrictive */
	if ((r1->power_rule.max_antenna_gain >
	     r2->power_rule.max_antenna_gain) ||
	    r1->power_rule.max_eirp > r2->power_rule.max_eirp)
		return false;

	/* make sure r2's range is contained within r1 */
	if (r1->freq_range.start_freq_khz > r2->freq_range.start_freq_khz ||
	    r1->freq_range.end_freq_khz < r2->freq_range.end_freq_khz)
		return false;

	/* and finally verify that r1.max_bw >= r2.max_bw */
	if (r1->freq_range.max_bandwidth_khz <
	    r2->freq_range.max_bandwidth_khz)
		return false;

	return true;
}

/* add or extend current rules. do nothing if rule is already contained */
static void add_rule(struct ieee80211_reg_rule *rule,
		     struct ieee80211_reg_rule *reg_rules, u32 *n_rules)
{
	struct ieee80211_reg_rule *tmp_rule;
	int i;

	for (i = 0; i < *n_rules; i++) {
		tmp_rule = &reg_rules[i];
		/* rule is already contained - do nothing */
		if (rule_contains(tmp_rule, rule))
			return;

		/* extend rule if possible */
		if (rule_contains(rule, tmp_rule)) {
			memcpy(tmp_rule, rule, sizeof(*rule));
			return;
		}
	}

	memcpy(&reg_rules[*n_rules], rule, sizeof(*rule));
	(*n_rules)++;
}

/**
 * regdom_intersect - do the intersection between two regulatory domains
 * @rd1: first regulatory domain
 * @rd2: second regulatory domain
 *
 * Use this function to get the intersection between two regulatory domains.
 * Once completed we will mark the alpha2 for the rd as intersected, "98",
 * as no one single alpha2 can represent this regulatory domain.
 *
 * Returns a pointer to the regulatory domain structure which will hold the
 * resulting intersection of rules between rd1 and rd2. We will
 * kzalloc() this structure for you.
 */
static struct ieee80211_regdomain *
regdom_intersect(const struct ieee80211_regdomain *rd1,
		 const struct ieee80211_regdomain *rd2)
{
	int r, size_of_regd;
	unsigned int x, y;
	unsigned int num_rules = 0;
	const struct ieee80211_reg_rule *rule1, *rule2;
	struct ieee80211_reg_rule intersected_rule;
	struct ieee80211_regdomain *rd;

	if (!rd1 || !rd2)
		return NULL;

	/*
	 * First we get a count of the rules we'll need, then we actually
	 * build them. This is to so we can malloc() and free() a
	 * regdomain once. The reason we use reg_rules_intersect() here
	 * is it will return -EINVAL if the rule computed makes no sense.
	 * All rules that do check out OK are valid.
	 */

	for (x = 0; x < rd1->n_reg_rules; x++) {
		rule1 = &rd1->reg_rules[x];
		for (y = 0; y < rd2->n_reg_rules; y++) {
			rule2 = &rd2->reg_rules[y];
			if (!reg_rules_intersect(rd1, rd2, rule1, rule2,
						 &intersected_rule))
				num_rules++;
		}
	}

	if (!num_rules)
		return NULL;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		       num_rules * sizeof(struct ieee80211_reg_rule);

	rd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!rd)
		return NULL;

	for (x = 0; x < rd1->n_reg_rules; x++) {
		rule1 = &rd1->reg_rules[x];
		for (y = 0; y < rd2->n_reg_rules; y++) {
			rule2 = &rd2->reg_rules[y];
			r = reg_rules_intersect(rd1, rd2, rule1, rule2,
						&intersected_rule);
			/*
			 * No need to memset here the intersected rule here as
			 * we're not using the stack anymore
			 */
			if (r)
				continue;

			add_rule(&intersected_rule, rd->reg_rules,
				 &rd->n_reg_rules);
		}
	}

	rd->alpha2[0] = '9';
	rd->alpha2[1] = '8';
	rd->dfs_region = reg_intersect_dfs_region(rd1->dfs_region,
						  rd2->dfs_region);

	return rd;
}

/*
 * XXX: add support for the rest of enum nl80211_reg_rule_flags, we may
 * want to just have the channel structure use these
 */
static u32 map_regdom_flags(u32 rd_flags)
{
	u32 channel_flags = 0;
	if (rd_flags & NL80211_RRF_NO_IR_ALL)
		channel_flags |= IEEE80211_CHAN_NO_IR;
	if (rd_flags & NL80211_RRF_DFS)
		channel_flags |= IEEE80211_CHAN_RADAR;
	if (rd_flags & NL80211_RRF_NO_OFDM)
		channel_flags |= IEEE80211_CHAN_NO_OFDM;
	if (rd_flags & NL80211_RRF_NO_OUTDOOR)
		channel_flags |= IEEE80211_CHAN_INDOOR_ONLY;
	if (rd_flags & NL80211_RRF_IR_CONCURRENT)
		channel_flags |= IEEE80211_CHAN_IR_CONCURRENT;
	if (rd_flags & NL80211_RRF_NO_HT40MINUS)
		channel_flags |= IEEE80211_CHAN_NO_HT40MINUS;
	if (rd_flags & NL80211_RRF_NO_HT40PLUS)
		channel_flags |= IEEE80211_CHAN_NO_HT40PLUS;
	if (rd_flags & NL80211_RRF_NO_80MHZ)
		channel_flags |= IEEE80211_CHAN_NO_80MHZ;
	if (rd_flags & NL80211_RRF_NO_160MHZ)
		channel_flags |= IEEE80211_CHAN_NO_160MHZ;
	return channel_flags;
}

static const struct ieee80211_reg_rule *
freq_reg_info_regd(u32 center_freq,
		   const struct ieee80211_regdomain *regd, u32 bw)
{
	int i;
	bool band_rule_found = false;
	bool bw_fits = false;

	if (!regd)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < regd->n_reg_rules; i++) {
		const struct ieee80211_reg_rule *rr;
		const struct ieee80211_freq_range *fr = NULL;

		rr = &regd->reg_rules[i];
		fr = &rr->freq_range;

		/*
		 * We only need to know if one frequency rule was
		 * was in center_freq's band, that's enough, so lets
		 * not overwrite it once found
		 */
		if (!band_rule_found)
			band_rule_found = freq_in_rule_band(fr, center_freq);

		bw_fits = cfg80211_does_bw_fit_range(fr, center_freq, bw);

		if (band_rule_found && bw_fits)
			return rr;
	}

	if (!band_rule_found)
		return ERR_PTR(-ERANGE);

	return ERR_PTR(-EINVAL);
}

static const struct ieee80211_reg_rule *
__freq_reg_info(struct wiphy *wiphy, u32 center_freq, u32 min_bw)
{
	const struct ieee80211_regdomain *regd = reg_get_regdomain(wiphy);
	const struct ieee80211_reg_rule *reg_rule = NULL;
	u32 bw;

	for (bw = MHZ_TO_KHZ(20); bw >= min_bw; bw = bw / 2) {
		reg_rule = freq_reg_info_regd(center_freq, regd, bw);
		if (!IS_ERR(reg_rule))
			return reg_rule;
	}

	return reg_rule;
}

const struct ieee80211_reg_rule *freq_reg_info(struct wiphy *wiphy,
					       u32 center_freq)
{
	return __freq_reg_info(wiphy, center_freq, MHZ_TO_KHZ(20));
}
EXPORT_SYMBOL(freq_reg_info);

const char *reg_initiator_name(enum nl80211_reg_initiator initiator)
{
	switch (initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		return "core";
	case NL80211_REGDOM_SET_BY_USER:
		return "user";
	case NL80211_REGDOM_SET_BY_DRIVER:
		return "driver";
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		return "country element";
	default:
		WARN_ON(1);
		return "bug";
	}
}
EXPORT_SYMBOL(reg_initiator_name);

static uint32_t reg_rule_to_chan_bw_flags(const struct ieee80211_regdomain *regd,
					  const struct ieee80211_reg_rule *reg_rule,
					  const struct ieee80211_channel *chan)
{
	const struct ieee80211_freq_range *freq_range = NULL;
	u32 max_bandwidth_khz, bw_flags = 0;

	freq_range = &reg_rule->freq_range;

	max_bandwidth_khz = freq_range->max_bandwidth_khz;
	/* Check if auto calculation requested */
	if (reg_rule->flags & NL80211_RRF_AUTO_BW)
		max_bandwidth_khz = reg_get_max_bandwidth(regd, reg_rule);

	/* If we get a reg_rule we can assume that at least 5Mhz fit */
	if (!cfg80211_does_bw_fit_range(freq_range,
					MHZ_TO_KHZ(chan->center_freq),
					MHZ_TO_KHZ(10)))
		bw_flags |= IEEE80211_CHAN_NO_10MHZ;
	if (!cfg80211_does_bw_fit_range(freq_range,
					MHZ_TO_KHZ(chan->center_freq),
					MHZ_TO_KHZ(20)))
		bw_flags |= IEEE80211_CHAN_NO_20MHZ;

	if (max_bandwidth_khz < MHZ_TO_KHZ(10))
		bw_flags |= IEEE80211_CHAN_NO_10MHZ;
	if (max_bandwidth_khz < MHZ_TO_KHZ(20))
		bw_flags |= IEEE80211_CHAN_NO_20MHZ;
	if (max_bandwidth_khz < MHZ_TO_KHZ(40))
		bw_flags |= IEEE80211_CHAN_NO_HT40;
	if (max_bandwidth_khz < MHZ_TO_KHZ(80))
		bw_flags |= IEEE80211_CHAN_NO_80MHZ;
	if (max_bandwidth_khz < MHZ_TO_KHZ(160))
		bw_flags |= IEEE80211_CHAN_NO_160MHZ;
	return bw_flags;
}

/*
 * Note that right now we assume the desired channel bandwidth
 * is always 20 MHz for each individual channel (HT40 uses 20 MHz
 * per channel, the primary and the extension channel).
 */
static void handle_channel(struct wiphy *wiphy,
			   enum nl80211_reg_initiator initiator,
			   struct ieee80211_channel *chan)
{
	u32 flags, bw_flags = 0;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	struct wiphy *request_wiphy = NULL;
	struct regulatory_request *lr = get_last_request();
	const struct ieee80211_regdomain *regd;

	request_wiphy = wiphy_idx_to_wiphy(lr->wiphy_idx);

	flags = chan->orig_flags;

	reg_rule = freq_reg_info(wiphy, MHZ_TO_KHZ(chan->center_freq));
	if (IS_ERR(reg_rule)) {
		/*
		 * We will disable all channels that do not match our
		 * received regulatory rule unless the hint is coming
		 * from a Country IE and the Country IE had no information
		 * about a band. The IEEE 802.11 spec allows for an AP
		 * to send only a subset of the regulatory rules allowed,
		 * so an AP in the US that only supports 2.4 GHz may only send
		 * a country IE with information for the 2.4 GHz band
		 * while 5 GHz is still supported.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE &&
		    PTR_ERR(reg_rule) == -ERANGE)
			return;

		if (lr->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
		    request_wiphy && request_wiphy == wiphy &&
		    request_wiphy->regulatory_flags & REGULATORY_STRICT_REG) {
			pr_debug("Disabling freq %d MHz for good\n",
				 chan->center_freq);
			chan->orig_flags |= IEEE80211_CHAN_DISABLED;
			chan->flags = chan->orig_flags;
		} else {
			pr_debug("Disabling freq %d MHz\n",
				 chan->center_freq);
			chan->flags |= IEEE80211_CHAN_DISABLED;
		}
		return;
	}

	regd = reg_get_regdomain(wiphy);

	power_rule = &reg_rule->power_rule;
	bw_flags = reg_rule_to_chan_bw_flags(regd, reg_rule, chan);

	if (lr->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    request_wiphy && request_wiphy == wiphy &&
	    request_wiphy->regulatory_flags & REGULATORY_STRICT_REG) {
		/*
		 * This guarantees the driver's requested regulatory domain
		 * will always be used as a base for further regulatory
		 * settings
		 */
		chan->flags = chan->orig_flags =
			map_regdom_flags(reg_rule->flags) | bw_flags;
		chan->max_antenna_gain = chan->orig_mag =
			(int) MBI_TO_DBI(power_rule->max_antenna_gain);
		chan->max_reg_power = chan->max_power = chan->orig_mpwr =
			(int) MBM_TO_DBM(power_rule->max_eirp);

		if (chan->flags & IEEE80211_CHAN_RADAR) {
			chan->dfs_cac_ms = IEEE80211_DFS_MIN_CAC_TIME_MS;
			if (reg_rule->dfs_cac_ms)
				chan->dfs_cac_ms = reg_rule->dfs_cac_ms;
		}

		return;
	}

	chan->dfs_state = NL80211_DFS_USABLE;
	chan->dfs_state_entered = jiffies;

	chan->beacon_found = false;
	chan->flags = flags | bw_flags | map_regdom_flags(reg_rule->flags);
	chan->max_antenna_gain =
		min_t(int, chan->orig_mag,
		      MBI_TO_DBI(power_rule->max_antenna_gain));
	chan->max_reg_power = (int) MBM_TO_DBM(power_rule->max_eirp);

	if (chan->flags & IEEE80211_CHAN_RADAR) {
		if (reg_rule->dfs_cac_ms)
			chan->dfs_cac_ms = reg_rule->dfs_cac_ms;
		else
			chan->dfs_cac_ms = IEEE80211_DFS_MIN_CAC_TIME_MS;
	}

	if (chan->orig_mpwr) {
		/*
		 * Devices that use REGULATORY_COUNTRY_IE_FOLLOW_POWER
		 * will always follow the passed country IE power settings.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE &&
		    wiphy->regulatory_flags & REGULATORY_COUNTRY_IE_FOLLOW_POWER)
			chan->max_power = chan->max_reg_power;
		else
			chan->max_power = min(chan->orig_mpwr,
					      chan->max_reg_power);
	} else
		chan->max_power = chan->max_reg_power;
}

static void handle_band(struct wiphy *wiphy,
			enum nl80211_reg_initiator initiator,
			struct ieee80211_supported_band *sband)
{
	unsigned int i;

	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++)
		handle_channel(wiphy, initiator, &sband->channels[i]);
}

static bool reg_request_cell_base(struct regulatory_request *request)
{
	if (request->initiator != NL80211_REGDOM_SET_BY_USER)
		return false;
	return request->user_reg_hint_type == NL80211_USER_REG_HINT_CELL_BASE;
}

bool reg_last_request_cell_base(void)
{
	return reg_request_cell_base(get_last_request());
}

#ifdef CONFIG_CFG80211_REG_CELLULAR_HINTS
/* Core specific check */
static enum reg_request_treatment
reg_ignore_cell_hint(struct regulatory_request *pending_request)
{
	struct regulatory_request *lr = get_last_request();

	if (!reg_num_devs_support_basehint)
		return REG_REQ_IGNORE;

	if (reg_request_cell_base(lr) &&
	    !regdom_changes(pending_request->alpha2))
		return REG_REQ_ALREADY_SET;

	return REG_REQ_OK;
}

/* Device specific check */
static bool reg_dev_ignore_cell_hint(struct wiphy *wiphy)
{
	return !(wiphy->features & NL80211_FEATURE_CELL_BASE_REG_HINTS);
}
#else
static enum reg_request_treatment
reg_ignore_cell_hint(struct regulatory_request *pending_request)
{
	return REG_REQ_IGNORE;
}

static bool reg_dev_ignore_cell_hint(struct wiphy *wiphy)
{
	return true;
}
#endif

static bool wiphy_strict_alpha2_regd(struct wiphy *wiphy)
{
	if (wiphy->regulatory_flags & REGULATORY_STRICT_REG &&
	    !(wiphy->regulatory_flags & REGULATORY_CUSTOM_REG))
		return true;
	return false;
}

static bool ignore_reg_update(struct wiphy *wiphy,
			      enum nl80211_reg_initiator initiator)
{
	struct regulatory_request *lr = get_last_request();

	if (wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED)
		return true;

	if (!lr) {
		pr_debug("Ignoring regulatory request set by %s since last_request is not set\n",
			 reg_initiator_name(initiator));
		return true;
	}

	if (initiator == NL80211_REGDOM_SET_BY_CORE &&
	    wiphy->regulatory_flags & REGULATORY_CUSTOM_REG) {
		pr_debug("Ignoring regulatory request set by %s since the driver uses its own custom regulatory domain\n",
			 reg_initiator_name(initiator));
		return true;
	}

	/*
	 * wiphy->regd will be set once the device has its own
	 * desired regulatory domain set
	 */
	if (wiphy_strict_alpha2_regd(wiphy) && !wiphy->regd &&
	    initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    !is_world_regdom(lr->alpha2)) {
		pr_debug("Ignoring regulatory request set by %s since the driver requires its own regulatory domain to be set first\n",
			 reg_initiator_name(initiator));
		return true;
	}

	if (reg_request_cell_base(lr))
		return reg_dev_ignore_cell_hint(wiphy);

	return false;
}

static bool reg_is_world_roaming(struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *cr = get_cfg80211_regdom();
	const struct ieee80211_regdomain *wr = get_wiphy_regdom(wiphy);
	struct regulatory_request *lr = get_last_request();

	if (is_world_regdom(cr->alpha2) || (wr && is_world_regdom(wr->alpha2)))
		return true;

	if (lr && lr->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    wiphy->regulatory_flags & REGULATORY_CUSTOM_REG)
		return true;

	return false;
}

static void handle_reg_beacon(struct wiphy *wiphy, unsigned int chan_idx,
			      struct reg_beacon *reg_beacon)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	bool channel_changed = false;
	struct ieee80211_channel chan_before;

	sband = wiphy->bands[reg_beacon->chan.band];
	chan = &sband->channels[chan_idx];

	if (likely(chan->center_freq != reg_beacon->chan.center_freq))
		return;

	if (chan->beacon_found)
		return;

	chan->beacon_found = true;

	if (!reg_is_world_roaming(wiphy))
		return;

	if (wiphy->regulatory_flags & REGULATORY_DISABLE_BEACON_HINTS)
		return;

	chan_before = *chan;

	if (chan->flags & IEEE80211_CHAN_NO_IR) {
		chan->flags &= ~IEEE80211_CHAN_NO_IR;
		channel_changed = true;
	}

	if (channel_changed)
		nl80211_send_beacon_hint_event(wiphy, &chan_before, chan);
}

/*
 * Called when a scan on a wiphy finds a beacon on
 * new channel
 */
static void wiphy_update_new_beacon(struct wiphy *wiphy,
				    struct reg_beacon *reg_beacon)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;

	if (!wiphy->bands[reg_beacon->chan.band])
		return;

	sband = wiphy->bands[reg_beacon->chan.band];

	for (i = 0; i < sband->n_channels; i++)
		handle_reg_beacon(wiphy, i, reg_beacon);
}

/*
 * Called upon reg changes or a new wiphy is added
 */
static void wiphy_update_beacon_reg(struct wiphy *wiphy)
{
	unsigned int i;
	struct ieee80211_supported_band *sband;
	struct reg_beacon *reg_beacon;

	list_for_each_entry(reg_beacon, &reg_beacon_list, list) {
		if (!wiphy->bands[reg_beacon->chan.band])
			continue;
		sband = wiphy->bands[reg_beacon->chan.band];
		for (i = 0; i < sband->n_channels; i++)
			handle_reg_beacon(wiphy, i, reg_beacon);
	}
}

/* Reap the advantages of previously found beacons */
static void reg_process_beacons(struct wiphy *wiphy)
{
	/*
	 * Means we are just firing up cfg80211, so no beacons would
	 * have been processed yet.
	 */
	if (!last_request)
		return;
	wiphy_update_beacon_reg(wiphy);
}

static bool is_ht40_allowed(struct ieee80211_channel *chan)
{
	if (!chan)
		return false;
	if (chan->flags & IEEE80211_CHAN_DISABLED)
		return false;
	/* This would happen when regulatory rules disallow HT40 completely */
	if ((chan->flags & IEEE80211_CHAN_NO_HT40) == IEEE80211_CHAN_NO_HT40)
		return false;
	return true;
}

static void reg_process_ht_flags_channel(struct wiphy *wiphy,
					 struct ieee80211_channel *channel)
{
	struct ieee80211_supported_band *sband = wiphy->bands[channel->band];
	struct ieee80211_channel *channel_before = NULL, *channel_after = NULL;
	const struct ieee80211_regdomain *regd;
	unsigned int i;
	u32 flags;

	if (!is_ht40_allowed(channel)) {
		channel->flags |= IEEE80211_CHAN_NO_HT40;
		return;
	}

	/*
	 * We need to ensure the extension channels exist to
	 * be able to use HT40- or HT40+, this finds them (or not)
	 */
	for (i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *c = &sband->channels[i];

		if (c->center_freq == (channel->center_freq - 20))
			channel_before = c;
		if (c->center_freq == (channel->center_freq + 20))
			channel_after = c;
	}

	flags = 0;
	regd = get_wiphy_regdom(wiphy);
	if (regd) {
		const struct ieee80211_reg_rule *reg_rule =
			freq_reg_info_regd(MHZ_TO_KHZ(channel->center_freq),
					   regd, MHZ_TO_KHZ(20));

		if (!IS_ERR(reg_rule))
			flags = reg_rule->flags;
	}

	/*
	 * Please note that this assumes target bandwidth is 20 MHz,
	 * if that ever changes we also need to change the below logic
	 * to include that as well.
	 */
	if (!is_ht40_allowed(channel_before) ||
	    flags & NL80211_RRF_NO_HT40MINUS)
		channel->flags |= IEEE80211_CHAN_NO_HT40MINUS;
	else
		channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;

	if (!is_ht40_allowed(channel_after) ||
	    flags & NL80211_RRF_NO_HT40PLUS)
		channel->flags |= IEEE80211_CHAN_NO_HT40PLUS;
	else
		channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
}

static void reg_process_ht_flags_band(struct wiphy *wiphy,
				      struct ieee80211_supported_band *sband)
{
	unsigned int i;

	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++)
		reg_process_ht_flags_channel(wiphy, &sband->channels[i]);
}

static void reg_process_ht_flags(struct wiphy *wiphy)
{
	enum nl80211_band band;

	if (!wiphy)
		return;

	for (band = 0; band < NUM_NL80211_BANDS; band++)
		reg_process_ht_flags_band(wiphy, wiphy->bands[band]);
}

static void reg_call_notifier(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	if (wiphy->reg_notifier)
		wiphy->reg_notifier(wiphy, request);
}

static bool reg_wdev_chan_valid(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct cfg80211_chan_def chandef = {};
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	enum nl80211_iftype iftype;

	wdev_lock(wdev);
	iftype = wdev->iftype;

	/* make sure the interface is active */
	if (!wdev->netdev || !netif_running(wdev->netdev))
		goto wdev_inactive_unlock;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		if (!wdev->beacon_interval)
			goto wdev_inactive_unlock;
		chandef = wdev->chandef;
		break;
	case NL80211_IFTYPE_ADHOC:
		if (!wdev->ssid_len)
			goto wdev_inactive_unlock;
		chandef = wdev->chandef;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (!wdev->current_bss ||
		    !wdev->current_bss->pub.channel)
			goto wdev_inactive_unlock;

		if (!rdev->ops->get_channel ||
		    rdev_get_channel(rdev, wdev, &chandef))
			cfg80211_chandef_create(&chandef,
						wdev->current_bss->pub.channel,
						NL80211_CHAN_NO_HT);
		break;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_DEVICE:
		/* no enforcement required */
		break;
	default:
		/* others not implemented for now */
		WARN_ON(1);
		break;
	}

	wdev_unlock(wdev);

	switch (iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_reg_can_beacon_relax(wiphy, &chandef, iftype);
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		return cfg80211_chandef_usable(wiphy, &chandef,
					       IEEE80211_CHAN_DISABLED);
	default:
		break;
	}

	return true;

wdev_inactive_unlock:
	wdev_unlock(wdev);
	return true;
}

static void reg_leave_invalid_chans(struct wiphy *wiphy)
{
	struct wireless_dev *wdev;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	ASSERT_RTNL();

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list)
		if (!reg_wdev_chan_valid(wiphy, wdev))
			cfg80211_leave(rdev, wdev);
}

static void reg_check_chans_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev;

	pr_debug("Verifying active interfaces after reg change\n");
	rtnl_lock();

	list_for_each_entry(rdev, &cfg80211_rdev_list, list)
		if (!(rdev->wiphy.regulatory_flags &
		      REGULATORY_IGNORE_STALE_KICKOFF))
			reg_leave_invalid_chans(&rdev->wiphy);

	rtnl_unlock();
}

static void reg_check_channels(void)
{
	/*
	 * Give usermode a chance to do something nicer (move to another
	 * channel, orderly disconnection), before forcing a disconnection.
	 */
	mod_delayed_work(system_power_efficient_wq,
			 &reg_check_chans,
			 msecs_to_jiffies(REG_ENFORCE_GRACE_MS));
}

static void wiphy_update_regulatory(struct wiphy *wiphy,
				    enum nl80211_reg_initiator initiator)
{
	enum nl80211_band band;
	struct regulatory_request *lr = get_last_request();

	if (ignore_reg_update(wiphy, initiator)) {
		/*
		 * Regulatory updates set by CORE are ignored for custom
		 * regulatory cards. Let us notify the changes to the driver,
		 * as some drivers used this to restore its orig_* reg domain.
		 */
		if (initiator == NL80211_REGDOM_SET_BY_CORE &&
		    wiphy->regulatory_flags & REGULATORY_CUSTOM_REG &&
		    !(wiphy->regulatory_flags &
		      REGULATORY_WIPHY_SELF_MANAGED))
			reg_call_notifier(wiphy, lr);
		return;
	}

	lr->dfs_region = get_cfg80211_regdom()->dfs_region;

	for (band = 0; band < NUM_NL80211_BANDS; band++)
		handle_band(wiphy, initiator, wiphy->bands[band]);

	reg_process_beacons(wiphy);
	reg_process_ht_flags(wiphy);
	reg_call_notifier(wiphy, lr);
}

static void update_all_wiphy_regulatory(enum nl80211_reg_initiator initiator)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;

	ASSERT_RTNL();

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		wiphy = &rdev->wiphy;
		wiphy_update_regulatory(wiphy, initiator);
	}

	reg_check_channels();
}

static void handle_channel_custom(struct wiphy *wiphy,
				  struct ieee80211_channel *chan,
				  const struct ieee80211_regdomain *regd)
{
	u32 bw_flags = 0;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	u32 bw;

	for (bw = MHZ_TO_KHZ(20); bw >= MHZ_TO_KHZ(5); bw = bw / 2) {
		reg_rule = freq_reg_info_regd(MHZ_TO_KHZ(chan->center_freq),
					      regd, bw);
		if (!IS_ERR(reg_rule))
			break;
	}

	if (IS_ERR(reg_rule)) {
		pr_debug("Disabling freq %d MHz as custom regd has no rule that fits it\n",
			 chan->center_freq);
		if (wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED) {
			chan->flags |= IEEE80211_CHAN_DISABLED;
		} else {
			chan->orig_flags |= IEEE80211_CHAN_DISABLED;
			chan->flags = chan->orig_flags;
		}
		return;
	}

	power_rule = &reg_rule->power_rule;
	bw_flags = reg_rule_to_chan_bw_flags(regd, reg_rule, chan);

	chan->dfs_state_entered = jiffies;
	chan->dfs_state = NL80211_DFS_USABLE;

	chan->beacon_found = false;

	if (wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED)
		chan->flags = chan->orig_flags | bw_flags |
			      map_regdom_flags(reg_rule->flags);
	else
		chan->flags |= map_regdom_flags(reg_rule->flags) | bw_flags;

	chan->max_antenna_gain = (int) MBI_TO_DBI(power_rule->max_antenna_gain);
	chan->max_reg_power = chan->max_power =
		(int) MBM_TO_DBM(power_rule->max_eirp);

	if (chan->flags & IEEE80211_CHAN_RADAR) {
		if (reg_rule->dfs_cac_ms)
			chan->dfs_cac_ms = reg_rule->dfs_cac_ms;
		else
			chan->dfs_cac_ms = IEEE80211_DFS_MIN_CAC_TIME_MS;
	}

	chan->max_power = chan->max_reg_power;
}

static void handle_band_custom(struct wiphy *wiphy,
			       struct ieee80211_supported_band *sband,
			       const struct ieee80211_regdomain *regd)
{
	unsigned int i;

	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++)
		handle_channel_custom(wiphy, &sband->channels[i], regd);
}

/* Used by drivers prior to wiphy registration */
void wiphy_apply_custom_regulatory(struct wiphy *wiphy,
				   const struct ieee80211_regdomain *regd)
{
	enum nl80211_band band;
	unsigned int bands_set = 0;

	WARN(!(wiphy->regulatory_flags & REGULATORY_CUSTOM_REG),
	     "wiphy should have REGULATORY_CUSTOM_REG\n");
	wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!wiphy->bands[band])
			continue;
		handle_band_custom(wiphy, wiphy->bands[band], regd);
		bands_set++;
	}

	/*
	 * no point in calling this if it won't have any effect
	 * on your device's supported bands.
	 */
	WARN_ON(!bands_set);
}
EXPORT_SYMBOL(wiphy_apply_custom_regulatory);

static void reg_set_request_processed(void)
{
	bool need_more_processing = false;
	struct regulatory_request *lr = get_last_request();

	lr->processed = true;

	spin_lock(&reg_requests_lock);
	if (!list_empty(&reg_requests_list))
		need_more_processing = true;
	spin_unlock(&reg_requests_lock);

	cancel_crda_timeout();

	if (need_more_processing)
		schedule_work(&reg_work);
}

/**
 * reg_process_hint_core - process core regulatory requests
 * @pending_request: a pending core regulatory request
 *
 * The wireless subsystem can use this function to process
 * a regulatory request issued by the regulatory core.
 */
static enum reg_request_treatment
reg_process_hint_core(struct regulatory_request *core_request)
{
	if (reg_query_database(core_request)) {
		core_request->intersect = false;
		core_request->processed = false;
		reg_update_last_request(core_request);
		return REG_REQ_OK;
	}

	return REG_REQ_IGNORE;
}

static enum reg_request_treatment
__reg_process_hint_user(struct regulatory_request *user_request)
{
	struct regulatory_request *lr = get_last_request();

	if (reg_request_cell_base(user_request))
		return reg_ignore_cell_hint(user_request);

	if (reg_request_cell_base(lr))
		return REG_REQ_IGNORE;

	if (lr->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE)
		return REG_REQ_INTERSECT;
	/*
	 * If the user knows better the user should set the regdom
	 * to their country before the IE is picked up
	 */
	if (lr->initiator == NL80211_REGDOM_SET_BY_USER &&
	    lr->intersect)
		return REG_REQ_IGNORE;
	/*
	 * Process user requests only after previous user/driver/core
	 * requests have been processed
	 */
	if ((lr->initiator == NL80211_REGDOM_SET_BY_CORE ||
	     lr->initiator == NL80211_REGDOM_SET_BY_DRIVER ||
	     lr->initiator == NL80211_REGDOM_SET_BY_USER) &&
	    regdom_changes(lr->alpha2))
		return REG_REQ_IGNORE;

	if (!regdom_changes(user_request->alpha2))
		return REG_REQ_ALREADY_SET;

	return REG_REQ_OK;
}

/**
 * reg_process_hint_user - process user regulatory requests
 * @user_request: a pending user regulatory request
 *
 * The wireless subsystem can use this function to process
 * a regulatory request initiated by userspace.
 */
static enum reg_request_treatment
reg_process_hint_user(struct regulatory_request *user_request)
{
	enum reg_request_treatment treatment;

	treatment = __reg_process_hint_user(user_request);
	if (treatment == REG_REQ_IGNORE ||
	    treatment == REG_REQ_ALREADY_SET)
		return REG_REQ_IGNORE;

	user_request->intersect = treatment == REG_REQ_INTERSECT;
	user_request->processed = false;

	if (reg_query_database(user_request)) {
		reg_update_last_request(user_request);
		user_alpha2[0] = user_request->alpha2[0];
		user_alpha2[1] = user_request->alpha2[1];
		return REG_REQ_OK;
	}

	return REG_REQ_IGNORE;
}

static enum reg_request_treatment
__reg_process_hint_driver(struct regulatory_request *driver_request)
{
	struct regulatory_request *lr = get_last_request();

	if (lr->initiator == NL80211_REGDOM_SET_BY_CORE) {
		if (regdom_changes(driver_request->alpha2))
			return REG_REQ_OK;
		return REG_REQ_ALREADY_SET;
	}

	/*
	 * This would happen if you unplug and plug your card
	 * back in or if you add a new device for which the previously
	 * loaded card also agrees on the regulatory domain.
	 */
	if (lr->initiator == NL80211_REGDOM_SET_BY_DRIVER &&
	    !regdom_changes(driver_request->alpha2))
		return REG_REQ_ALREADY_SET;

	return REG_REQ_INTERSECT;
}

/**
 * reg_process_hint_driver - process driver regulatory requests
 * @driver_request: a pending driver regulatory request
 *
 * The wireless subsystem can use this function to process
 * a regulatory request issued by an 802.11 driver.
 *
 * Returns one of the different reg request treatment values.
 */
static enum reg_request_treatment
reg_process_hint_driver(struct wiphy *wiphy,
			struct regulatory_request *driver_request)
{
	const struct ieee80211_regdomain *regd, *tmp;
	enum reg_request_treatment treatment;

	treatment = __reg_process_hint_driver(driver_request);

	switch (treatment) {
	case REG_REQ_OK:
		break;
	case REG_REQ_IGNORE:
		return REG_REQ_IGNORE;
	case REG_REQ_INTERSECT:
	case REG_REQ_ALREADY_SET:
		regd = reg_copy_regd(get_cfg80211_regdom());
		if (IS_ERR(regd))
			return REG_REQ_IGNORE;

		tmp = get_wiphy_regdom(wiphy);
		rcu_assign_pointer(wiphy->regd, regd);
		rcu_free_regdom(tmp);
	}


	driver_request->intersect = treatment == REG_REQ_INTERSECT;
	driver_request->processed = false;

	/*
	 * Since CRDA will not be called in this case as we already
	 * have applied the requested regulatory domain before we just
	 * inform userspace we have processed the request
	 */
	if (treatment == REG_REQ_ALREADY_SET) {
		nl80211_send_reg_change_event(driver_request);
		reg_update_last_request(driver_request);
		reg_set_request_processed();
		return REG_REQ_ALREADY_SET;
	}

	if (reg_query_database(driver_request)) {
		reg_update_last_request(driver_request);
		return REG_REQ_OK;
	}

	return REG_REQ_IGNORE;
}

static enum reg_request_treatment
__reg_process_hint_country_ie(struct wiphy *wiphy,
			      struct regulatory_request *country_ie_request)
{
	struct wiphy *last_wiphy = NULL;
	struct regulatory_request *lr = get_last_request();

	if (reg_request_cell_base(lr)) {
		/* Trust a Cell base station over the AP's country IE */
		if (regdom_changes(country_ie_request->alpha2))
			return REG_REQ_IGNORE;
		return REG_REQ_ALREADY_SET;
	} else {
		if (wiphy->regulatory_flags & REGULATORY_COUNTRY_IE_IGNORE)
			return REG_REQ_IGNORE;
	}

	if (unlikely(!is_an_alpha2(country_ie_request->alpha2)))
		return -EINVAL;

	if (lr->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE)
		return REG_REQ_OK;

	last_wiphy = wiphy_idx_to_wiphy(lr->wiphy_idx);

	if (last_wiphy != wiphy) {
		/*
		 * Two cards with two APs claiming different
		 * Country IE alpha2s. We could
		 * intersect them, but that seems unlikely
		 * to be correct. Reject second one for now.
		 */
		if (regdom_changes(country_ie_request->alpha2))
			return REG_REQ_IGNORE;
		return REG_REQ_ALREADY_SET;
	}

	if (regdom_changes(country_ie_request->alpha2))
		return REG_REQ_OK;
	return REG_REQ_ALREADY_SET;
}

/**
 * reg_process_hint_country_ie - process regulatory requests from country IEs
 * @country_ie_request: a regulatory request from a country IE
 *
 * The wireless subsystem can use this function to process
 * a regulatory request issued by a country Information Element.
 *
 * Returns one of the different reg request treatment values.
 */
static enum reg_request_treatment
reg_process_hint_country_ie(struct wiphy *wiphy,
			    struct regulatory_request *country_ie_request)
{
	enum reg_request_treatment treatment;

	treatment = __reg_process_hint_country_ie(wiphy, country_ie_request);

	switch (treatment) {
	case REG_REQ_OK:
		break;
	case REG_REQ_IGNORE:
		return REG_REQ_IGNORE;
	case REG_REQ_ALREADY_SET:
		reg_free_request(country_ie_request);
		return REG_REQ_ALREADY_SET;
	case REG_REQ_INTERSECT:
		/*
		 * This doesn't happen yet, not sure we
		 * ever want to support it for this case.
		 */
		WARN_ONCE(1, "Unexpected intersection for country elements");
		return REG_REQ_IGNORE;
	}

	country_ie_request->intersect = false;
	country_ie_request->processed = false;

	if (reg_query_database(country_ie_request)) {
		reg_update_last_request(country_ie_request);
		return REG_REQ_OK;
	}

	return REG_REQ_IGNORE;
}

bool reg_dfs_domain_same(struct wiphy *wiphy1, struct wiphy *wiphy2)
{
	const struct ieee80211_regdomain *wiphy1_regd = NULL;
	const struct ieee80211_regdomain *wiphy2_regd = NULL;
	const struct ieee80211_regdomain *cfg80211_regd = NULL;
	bool dfs_domain_same;

	rcu_read_lock();

	cfg80211_regd = rcu_dereference(cfg80211_regdomain);
	wiphy1_regd = rcu_dereference(wiphy1->regd);
	if (!wiphy1_regd)
		wiphy1_regd = cfg80211_regd;

	wiphy2_regd = rcu_dereference(wiphy2->regd);
	if (!wiphy2_regd)
		wiphy2_regd = cfg80211_regd;

	dfs_domain_same = wiphy1_regd->dfs_region == wiphy2_regd->dfs_region;

	rcu_read_unlock();

	return dfs_domain_same;
}

static void reg_copy_dfs_chan_state(struct ieee80211_channel *dst_chan,
				    struct ieee80211_channel *src_chan)
{
	if (!(dst_chan->flags & IEEE80211_CHAN_RADAR) ||
	    !(src_chan->flags & IEEE80211_CHAN_RADAR))
		return;

	if (dst_chan->flags & IEEE80211_CHAN_DISABLED ||
	    src_chan->flags & IEEE80211_CHAN_DISABLED)
		return;

	if (src_chan->center_freq == dst_chan->center_freq &&
	    dst_chan->dfs_state == NL80211_DFS_USABLE) {
		dst_chan->dfs_state = src_chan->dfs_state;
		dst_chan->dfs_state_entered = src_chan->dfs_state_entered;
	}
}

static void wiphy_share_dfs_chan_state(struct wiphy *dst_wiphy,
				       struct wiphy *src_wiphy)
{
	struct ieee80211_supported_band *src_sband, *dst_sband;
	struct ieee80211_channel *src_chan, *dst_chan;
	int i, j, band;

	if (!reg_dfs_domain_same(dst_wiphy, src_wiphy))
		return;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		dst_sband = dst_wiphy->bands[band];
		src_sband = src_wiphy->bands[band];
		if (!dst_sband || !src_sband)
			continue;

		for (i = 0; i < dst_sband->n_channels; i++) {
			dst_chan = &dst_sband->channels[i];
			for (j = 0; j < src_sband->n_channels; j++) {
				src_chan = &src_sband->channels[j];
				reg_copy_dfs_chan_state(dst_chan, src_chan);
			}
		}
	}
}

static void wiphy_all_share_dfs_chan_state(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev;

	ASSERT_RTNL();

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (wiphy == &rdev->wiphy)
			continue;
		wiphy_share_dfs_chan_state(wiphy, &rdev->wiphy);
	}
}

/* This processes *all* regulatory hints */
static void reg_process_hint(struct regulatory_request *reg_request)
{
	struct wiphy *wiphy = NULL;
	enum reg_request_treatment treatment;
	enum nl80211_reg_initiator initiator = reg_request->initiator;

	if (reg_request->wiphy_idx != WIPHY_IDX_INVALID)
		wiphy = wiphy_idx_to_wiphy(reg_request->wiphy_idx);

	switch (initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		treatment = reg_process_hint_core(reg_request);
		break;
	case NL80211_REGDOM_SET_BY_USER:
		treatment = reg_process_hint_user(reg_request);
		break;
	case NL80211_REGDOM_SET_BY_DRIVER:
		if (!wiphy)
			goto out_free;
		treatment = reg_process_hint_driver(wiphy, reg_request);
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		if (!wiphy)
			goto out_free;
		treatment = reg_process_hint_country_ie(wiphy, reg_request);
		break;
	default:
		WARN(1, "invalid initiator %d\n", initiator);
		goto out_free;
	}

	if (treatment == REG_REQ_IGNORE)
		goto out_free;

	WARN(treatment != REG_REQ_OK && treatment != REG_REQ_ALREADY_SET,
	     "unexpected treatment value %d\n", treatment);

	/* This is required so that the orig_* parameters are saved.
	 * NOTE: treatment must be set for any case that reaches here!
	 */
	if (treatment == REG_REQ_ALREADY_SET && wiphy &&
	    wiphy->regulatory_flags & REGULATORY_STRICT_REG) {
		wiphy_update_regulatory(wiphy, initiator);
		wiphy_all_share_dfs_chan_state(wiphy);
		reg_check_channels();
	}

	return;

out_free:
	reg_free_request(reg_request);
}

static void notify_self_managed_wiphys(struct regulatory_request *request)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		wiphy = &rdev->wiphy;
		if (wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED &&
		    request->initiator == NL80211_REGDOM_SET_BY_USER &&
		    request->user_reg_hint_type ==
				NL80211_USER_REG_HINT_CELL_BASE)
			reg_call_notifier(wiphy, request);
	}
}

/*
 * Processes regulatory hints, this is all the NL80211_REGDOM_SET_BY_*
 * Regulatory hints come on a first come first serve basis and we
 * must process each one atomically.
 */
static void reg_process_pending_hints(void)
{
	struct regulatory_request *reg_request, *lr;

	lr = get_last_request();

	/* When last_request->processed becomes true this will be rescheduled */
	if (lr && !lr->processed) {
		pr_debug("Pending regulatory request, waiting for it to be processed...\n");
		return;
	}

	spin_lock(&reg_requests_lock);

	if (list_empty(&reg_requests_list)) {
		spin_unlock(&reg_requests_lock);
		return;
	}

	reg_request = list_first_entry(&reg_requests_list,
				       struct regulatory_request,
				       list);
	list_del_init(&reg_request->list);

	spin_unlock(&reg_requests_lock);

	notify_self_managed_wiphys(reg_request);

	reg_process_hint(reg_request);

	lr = get_last_request();

	spin_lock(&reg_requests_lock);
	if (!list_empty(&reg_requests_list) && lr && lr->processed)
		schedule_work(&reg_work);
	spin_unlock(&reg_requests_lock);
}

/* Processes beacon hints -- this has nothing to do with country IEs */
static void reg_process_pending_beacon_hints(void)
{
	struct cfg80211_registered_device *rdev;
	struct reg_beacon *pending_beacon, *tmp;

	/* This goes through the _pending_ beacon list */
	spin_lock_bh(&reg_pending_beacons_lock);

	list_for_each_entry_safe(pending_beacon, tmp,
				 &reg_pending_beacons, list) {
		list_del_init(&pending_beacon->list);

		/* Applies the beacon hint to current wiphys */
		list_for_each_entry(rdev, &cfg80211_rdev_list, list)
			wiphy_update_new_beacon(&rdev->wiphy, pending_beacon);

		/* Remembers the beacon hint for new wiphys or reg changes */
		list_add_tail(&pending_beacon->list, &reg_beacon_list);
	}

	spin_unlock_bh(&reg_pending_beacons_lock);
}

static void reg_process_self_managed_hints(void)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;
	const struct ieee80211_regdomain *tmp;
	const struct ieee80211_regdomain *regd;
	enum nl80211_band band;
	struct regulatory_request request = {};

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		wiphy = &rdev->wiphy;

		spin_lock(&reg_requests_lock);
		regd = rdev->requested_regd;
		rdev->requested_regd = NULL;
		spin_unlock(&reg_requests_lock);

		if (regd == NULL)
			continue;

		tmp = get_wiphy_regdom(wiphy);
		rcu_assign_pointer(wiphy->regd, regd);
		rcu_free_regdom(tmp);

		for (band = 0; band < NUM_NL80211_BANDS; band++)
			handle_band_custom(wiphy, wiphy->bands[band], regd);

		reg_process_ht_flags(wiphy);

		request.wiphy_idx = get_wiphy_idx(wiphy);
		request.alpha2[0] = regd->alpha2[0];
		request.alpha2[1] = regd->alpha2[1];
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;

		nl80211_send_wiphy_reg_change_event(&request);
	}

	reg_check_channels();
}

static void reg_todo(struct work_struct *work)
{
	rtnl_lock();
	reg_process_pending_hints();
	reg_process_pending_beacon_hints();
	reg_process_self_managed_hints();
	rtnl_unlock();
}

static void queue_regulatory_request(struct regulatory_request *request)
{
	request->alpha2[0] = toupper(request->alpha2[0]);
	request->alpha2[1] = toupper(request->alpha2[1]);

	spin_lock(&reg_requests_lock);
	list_add_tail(&request->list, &reg_requests_list);
	spin_unlock(&reg_requests_lock);

	schedule_work(&reg_work);
}

/*
 * Core regulatory hint -- happens during cfg80211_init()
 * and when we restore regulatory settings.
 */
static int regulatory_hint_core(const char *alpha2)
{
	struct regulatory_request *request;

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_CORE;
	request->wiphy_idx = WIPHY_IDX_INVALID;

	queue_regulatory_request(request);

	return 0;
}

/* User hints */
int regulatory_hint_user(const char *alpha2,
			 enum nl80211_user_reg_hint_type user_reg_hint_type)
{
	struct regulatory_request *request;

	if (WARN_ON(!alpha2))
		return -EINVAL;

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->wiphy_idx = WIPHY_IDX_INVALID;
	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_USER;
	request->user_reg_hint_type = user_reg_hint_type;

	/* Allow calling CRDA again */
	reset_crda_timeouts();

	queue_regulatory_request(request);

	return 0;
}

int regulatory_hint_indoor(bool is_indoor, u32 portid)
{
	spin_lock(&reg_indoor_lock);

	/* It is possible that more than one user space process is trying to
	 * configure the indoor setting. To handle such cases, clear the indoor
	 * setting in case that some process does not think that the device
	 * is operating in an indoor environment. In addition, if a user space
	 * process indicates that it is controlling the indoor setting, save its
	 * portid, i.e., make it the owner.
	 */
	reg_is_indoor = is_indoor;
	if (reg_is_indoor) {
		if (!reg_is_indoor_portid)
			reg_is_indoor_portid = portid;
	} else {
		reg_is_indoor_portid = 0;
	}

	spin_unlock(&reg_indoor_lock);

	if (!is_indoor)
		reg_check_channels();

	return 0;
}

void regulatory_netlink_notify(u32 portid)
{
	spin_lock(&reg_indoor_lock);

	if (reg_is_indoor_portid != portid) {
		spin_unlock(&reg_indoor_lock);
		return;
	}

	reg_is_indoor = false;
	reg_is_indoor_portid = 0;

	spin_unlock(&reg_indoor_lock);

	reg_check_channels();
}

/* Driver hints */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
	struct regulatory_request *request;

	if (WARN_ON(!alpha2 || !wiphy))
		return -EINVAL;

	wiphy->regulatory_flags &= ~REGULATORY_CUSTOM_REG;

	request = kzalloc(sizeof(struct regulatory_request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->wiphy_idx = get_wiphy_idx(wiphy);

	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_DRIVER;

	/* Allow calling CRDA again */
	reset_crda_timeouts();

	queue_regulatory_request(request);

	return 0;
}
EXPORT_SYMBOL(regulatory_hint);

void regulatory_hint_country_ie(struct wiphy *wiphy, enum nl80211_band band,
				const u8 *country_ie, u8 country_ie_len)
{
	char alpha2[2];
	enum environment_cap env = ENVIRON_ANY;
	struct regulatory_request *request = NULL, *lr;

	/* IE len must be evenly divisible by 2 */
	if (country_ie_len & 0x01)
		return;

	if (country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN)
		return;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return;

	alpha2[0] = country_ie[0];
	alpha2[1] = country_ie[1];

	if (country_ie[2] == 'I')
		env = ENVIRON_INDOOR;
	else if (country_ie[2] == 'O')
		env = ENVIRON_OUTDOOR;

	rcu_read_lock();
	lr = get_last_request();

	if (unlikely(!lr))
		goto out;

	/*
	 * We will run this only upon a successful connection on cfg80211.
	 * We leave conflict resolution to the workqueue, where can hold
	 * the RTNL.
	 */
	if (lr->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE &&
	    lr->wiphy_idx != WIPHY_IDX_INVALID)
		goto out;

	request->wiphy_idx = get_wiphy_idx(wiphy);
	request->alpha2[0] = alpha2[0];
	request->alpha2[1] = alpha2[1];
	request->initiator = NL80211_REGDOM_SET_BY_COUNTRY_IE;
	request->country_ie_env = env;

	/* Allow calling CRDA again */
	reset_crda_timeouts();

	queue_regulatory_request(request);
	request = NULL;
out:
	kfree(request);
	rcu_read_unlock();
}

static void restore_alpha2(char *alpha2, bool reset_user)
{
	/* indicates there is no alpha2 to consider for restoration */
	alpha2[0] = '9';
	alpha2[1] = '7';

	/* The user setting has precedence over the module parameter */
	if (is_user_regdom_saved()) {
		/* Unless we're asked to ignore it and reset it */
		if (reset_user) {
			pr_debug("Restoring regulatory settings including user preference\n");
			user_alpha2[0] = '9';
			user_alpha2[1] = '7';

			/*
			 * If we're ignoring user settings, we still need to
			 * check the module parameter to ensure we put things
			 * back as they were for a full restore.
			 */
			if (!is_world_regdom(ieee80211_regdom)) {
				pr_debug("Keeping preference on module parameter ieee80211_regdom: %c%c\n",
					 ieee80211_regdom[0], ieee80211_regdom[1]);
				alpha2[0] = ieee80211_regdom[0];
				alpha2[1] = ieee80211_regdom[1];
			}
		} else {
			pr_debug("Restoring regulatory settings while preserving user preference for: %c%c\n",
				 user_alpha2[0], user_alpha2[1]);
			alpha2[0] = user_alpha2[0];
			alpha2[1] = user_alpha2[1];
		}
	} else if (!is_world_regdom(ieee80211_regdom)) {
		pr_debug("Keeping preference on module parameter ieee80211_regdom: %c%c\n",
			 ieee80211_regdom[0], ieee80211_regdom[1]);
		alpha2[0] = ieee80211_regdom[0];
		alpha2[1] = ieee80211_regdom[1];
	} else
		pr_debug("Restoring regulatory settings\n");
}

static void restore_custom_reg_settings(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	enum nl80211_band band;
	struct ieee80211_channel *chan;
	int i;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++) {
			chan = &sband->channels[i];
			chan->flags = chan->orig_flags;
			chan->max_antenna_gain = chan->orig_mag;
			chan->max_power = chan->orig_mpwr;
			chan->beacon_found = false;
		}
	}
}

/*
 * Restoring regulatory settings involves ingoring any
 * possibly stale country IE information and user regulatory
 * settings if so desired, this includes any beacon hints
 * learned as we could have traveled outside to another country
 * after disconnection. To restore regulatory settings we do
 * exactly what we did at bootup:
 *
 *   - send a core regulatory hint
 *   - send a user regulatory hint if applicable
 *
 * Device drivers that send a regulatory hint for a specific country
 * keep their own regulatory domain on wiphy->regd so that does does
 * not need to be remembered.
 */
static void restore_regulatory_settings(bool reset_user)
{
	char alpha2[2];
	char world_alpha2[2];
	struct reg_beacon *reg_beacon, *btmp;
	LIST_HEAD(tmp_reg_req_list);
	struct cfg80211_registered_device *rdev;

	ASSERT_RTNL();

	/*
	 * Clear the indoor setting in case that it is not controlled by user
	 * space, as otherwise there is no guarantee that the device is still
	 * operating in an indoor environment.
	 */
	spin_lock(&reg_indoor_lock);
	if (reg_is_indoor && !reg_is_indoor_portid) {
		reg_is_indoor = false;
		reg_check_channels();
	}
	spin_unlock(&reg_indoor_lock);

	reset_regdomains(true, &world_regdom);
	restore_alpha2(alpha2, reset_user);

	/*
	 * If there's any pending requests we simply
	 * stash them to a temporary pending queue and
	 * add then after we've restored regulatory
	 * settings.
	 */
	spin_lock(&reg_requests_lock);
	list_splice_tail_init(&reg_requests_list, &tmp_reg_req_list);
	spin_unlock(&reg_requests_lock);

	/* Clear beacon hints */
	spin_lock_bh(&reg_pending_beacons_lock);
	list_for_each_entry_safe(reg_beacon, btmp, &reg_pending_beacons, list) {
		list_del(&reg_beacon->list);
		kfree(reg_beacon);
	}
	spin_unlock_bh(&reg_pending_beacons_lock);

	list_for_each_entry_safe(reg_beacon, btmp, &reg_beacon_list, list) {
		list_del(&reg_beacon->list);
		kfree(reg_beacon);
	}

	/* First restore to the basic regulatory settings */
	world_alpha2[0] = cfg80211_world_regdom->alpha2[0];
	world_alpha2[1] = cfg80211_world_regdom->alpha2[1];

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (rdev->wiphy.regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED)
			continue;
		if (rdev->wiphy.regulatory_flags & REGULATORY_CUSTOM_REG)
			restore_custom_reg_settings(&rdev->wiphy);
	}

	regulatory_hint_core(world_alpha2);

	/*
	 * This restores the ieee80211_regdom module parameter
	 * preference or the last user requested regulatory
	 * settings, user regulatory settings takes precedence.
	 */
	if (is_an_alpha2(alpha2))
		regulatory_hint_user(alpha2, NL80211_USER_REG_HINT_USER);

	spin_lock(&reg_requests_lock);
	list_splice_tail_init(&tmp_reg_req_list, &reg_requests_list);
	spin_unlock(&reg_requests_lock);

	pr_debug("Kicking the queue\n");

	schedule_work(&reg_work);
}

static bool is_wiphy_all_set_reg_flag(enum ieee80211_regulatory_flags flag)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
			wdev_lock(wdev);
			if (!(wdev->wiphy->regulatory_flags & flag)) {
				wdev_unlock(wdev);
				return false;
			}
			wdev_unlock(wdev);
		}
	}

	return true;
}

void regulatory_hint_disconnect(void)
{
	/* Restore of regulatory settings is not required when wiphy(s)
	 * ignore IE from connected access point but clearance of beacon hints
	 * is required when wiphy(s) supports beacon hints.
	 */
	if (is_wiphy_all_set_reg_flag(REGULATORY_COUNTRY_IE_IGNORE)) {
		struct reg_beacon *reg_beacon, *btmp;

		if (is_wiphy_all_set_reg_flag(REGULATORY_DISABLE_BEACON_HINTS))
			return;

		spin_lock_bh(&reg_pending_beacons_lock);
		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_pending_beacons, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}
		spin_unlock_bh(&reg_pending_beacons_lock);

		list_for_each_entry_safe(reg_beacon, btmp,
					 &reg_beacon_list, list) {
			list_del(&reg_beacon->list);
			kfree(reg_beacon);
		}

		return;
	}

	pr_debug("All devices are disconnected, going to restore regulatory settings\n");
	restore_regulatory_settings(false);
}

static bool freq_is_chan_12_13_14(u16 freq)
{
	if (freq == ieee80211_channel_to_frequency(12, NL80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(13, NL80211_BAND_2GHZ) ||
	    freq == ieee80211_channel_to_frequency(14, NL80211_BAND_2GHZ))
		return true;
	return false;
}

static bool pending_reg_beacon(struct ieee80211_channel *beacon_chan)
{
	struct reg_beacon *pending_beacon;

	list_for_each_entry(pending_beacon, &reg_pending_beacons, list)
		if (beacon_chan->center_freq ==
		    pending_beacon->chan.center_freq)
			return true;
	return false;
}

int regulatory_hint_found_beacon(struct wiphy *wiphy,
				 struct ieee80211_channel *beacon_chan,
				 gfp_t gfp)
{
	struct reg_beacon *reg_beacon;
	bool processing;

	if (beacon_chan->beacon_found ||
	    beacon_chan->flags & IEEE80211_CHAN_RADAR ||
	    (beacon_chan->band == NL80211_BAND_2GHZ &&
	     !freq_is_chan_12_13_14(beacon_chan->center_freq)))
		return 0;

	spin_lock_bh(&reg_pending_beacons_lock);
	processing = pending_reg_beacon(beacon_chan);
	spin_unlock_bh(&reg_pending_beacons_lock);

	if (processing)
		return 0;

	reg_beacon = kzalloc(sizeof(struct reg_beacon), gfp);
	if (!reg_beacon)
		return -ENOMEM;

	pr_debug("Found new beacon on frequency: %d MHz (Ch %d) on %s\n",
		 beacon_chan->center_freq,
		 ieee80211_frequency_to_channel(beacon_chan->center_freq),
		 wiphy_name(wiphy));

	memcpy(&reg_beacon->chan, beacon_chan,
	       sizeof(struct ieee80211_channel));

	/*
	 * Since we can be called from BH or and non-BH context
	 * we must use spin_lock_bh()
	 */
	spin_lock_bh(&reg_pending_beacons_lock);
	list_add_tail(&reg_beacon->list, &reg_pending_beacons);
	spin_unlock_bh(&reg_pending_beacons_lock);

	schedule_work(&reg_work);

	return 0;
}

static void print_rd_rules(const struct ieee80211_regdomain *rd)
{
	unsigned int i;
	const struct ieee80211_reg_rule *reg_rule = NULL;
	const struct ieee80211_freq_range *freq_range = NULL;
	const struct ieee80211_power_rule *power_rule = NULL;
	char bw[32], cac_time[32];

	pr_debug("  (start_freq - end_freq @ bandwidth), (max_antenna_gain, max_eirp), (dfs_cac_time)\n");

	for (i = 0; i < rd->n_reg_rules; i++) {
		reg_rule = &rd->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		if (reg_rule->flags & NL80211_RRF_AUTO_BW)
			snprintf(bw, sizeof(bw), "%d KHz, %d KHz AUTO",
				 freq_range->max_bandwidth_khz,
				 reg_get_max_bandwidth(rd, reg_rule));
		else
			snprintf(bw, sizeof(bw), "%d KHz",
				 freq_range->max_bandwidth_khz);

		if (reg_rule->flags & NL80211_RRF_DFS)
			scnprintf(cac_time, sizeof(cac_time), "%u s",
				  reg_rule->dfs_cac_ms/1000);
		else
			scnprintf(cac_time, sizeof(cac_time), "N/A");


		/*
		 * There may not be documentation for max antenna gain
		 * in certain regions
		 */
		if (power_rule->max_antenna_gain)
			pr_debug("  (%d KHz - %d KHz @ %s), (%d mBi, %d mBm), (%s)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				bw,
				power_rule->max_antenna_gain,
				power_rule->max_eirp,
				cac_time);
		else
			pr_debug("  (%d KHz - %d KHz @ %s), (N/A, %d mBm), (%s)\n",
				freq_range->start_freq_khz,
				freq_range->end_freq_khz,
				bw,
				power_rule->max_eirp,
				cac_time);
	}
}

bool reg_supported_dfs_region(enum nl80211_dfs_regions dfs_region)
{
	switch (dfs_region) {
	case NL80211_DFS_UNSET:
	case NL80211_DFS_FCC:
	case NL80211_DFS_ETSI:
	case NL80211_DFS_JP:
		return true;
	default:
		pr_debug("Ignoring unknown DFS master region: %d\n", dfs_region);
		return false;
	}
}

static void print_regdomain(const struct ieee80211_regdomain *rd)
{
	struct regulatory_request *lr = get_last_request();

	if (is_intersected_alpha2(rd->alpha2)) {
		if (lr->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
			struct cfg80211_registered_device *rdev;
			rdev = cfg80211_rdev_by_wiphy_idx(lr->wiphy_idx);
			if (rdev) {
				pr_debug("Current regulatory domain updated by AP to: %c%c\n",
					rdev->country_ie_alpha2[0],
					rdev->country_ie_alpha2[1]);
			} else
				pr_debug("Current regulatory domain intersected:\n");
		} else
			pr_debug("Current regulatory domain intersected:\n");
	} else if (is_world_regdom(rd->alpha2)) {
		pr_debug("World regulatory domain updated:\n");
	} else {
		if (is_unknown_alpha2(rd->alpha2))
			pr_debug("Regulatory domain changed to driver built-in settings (unknown country)\n");
		else {
			if (reg_request_cell_base(lr))
				pr_debug("Regulatory domain changed to country: %c%c by Cell Station\n",
					rd->alpha2[0], rd->alpha2[1]);
			else
				pr_debug("Regulatory domain changed to country: %c%c\n",
					rd->alpha2[0], rd->alpha2[1]);
		}
	}

	pr_debug(" DFS Master region: %s", reg_dfs_region_str(rd->dfs_region));
	print_rd_rules(rd);
}

static void print_regdomain_info(const struct ieee80211_regdomain *rd)
{
	pr_debug("Regulatory domain: %c%c\n", rd->alpha2[0], rd->alpha2[1]);
	print_rd_rules(rd);
}

static int reg_set_rd_core(const struct ieee80211_regdomain *rd)
{
	if (!is_world_regdom(rd->alpha2))
		return -EINVAL;
	update_world_regdomain(rd);
	return 0;
}

static int reg_set_rd_user(const struct ieee80211_regdomain *rd,
			   struct regulatory_request *user_request)
{
	const struct ieee80211_regdomain *intersected_rd = NULL;

	if (!regdom_changes(rd->alpha2))
		return -EALREADY;

	if (!is_valid_rd(rd)) {
		pr_err("Invalid regulatory domain detected: %c%c\n",
		       rd->alpha2[0], rd->alpha2[1]);
		print_regdomain_info(rd);
		return -EINVAL;
	}

	if (!user_request->intersect) {
		reset_regdomains(false, rd);
		return 0;
	}

	intersected_rd = regdom_intersect(rd, get_cfg80211_regdom());
	if (!intersected_rd)
		return -EINVAL;

	kfree(rd);
	rd = NULL;
	reset_regdomains(false, intersected_rd);

	return 0;
}

static int reg_set_rd_driver(const struct ieee80211_regdomain *rd,
			     struct regulatory_request *driver_request)
{
	const struct ieee80211_regdomain *regd;
	const struct ieee80211_regdomain *intersected_rd = NULL;
	const struct ieee80211_regdomain *tmp;
	struct wiphy *request_wiphy;

	if (is_world_regdom(rd->alpha2))
		return -EINVAL;

	if (!regdom_changes(rd->alpha2))
		return -EALREADY;

	if (!is_valid_rd(rd)) {
		pr_err("Invalid regulatory domain detected: %c%c\n",
		       rd->alpha2[0], rd->alpha2[1]);
		print_regdomain_info(rd);
		return -EINVAL;
	}

	request_wiphy = wiphy_idx_to_wiphy(driver_request->wiphy_idx);
	if (!request_wiphy)
		return -ENODEV;

	if (!driver_request->intersect) {
		if (request_wiphy->regd)
			return -EALREADY;

		regd = reg_copy_regd(rd);
		if (IS_ERR(regd))
			return PTR_ERR(regd);

		rcu_assign_pointer(request_wiphy->regd, regd);
		reset_regdomains(false, rd);
		return 0;
	}

	intersected_rd = regdom_intersect(rd, get_cfg80211_regdom());
	if (!intersected_rd)
		return -EINVAL;

	/*
	 * We can trash what CRDA provided now.
	 * However if a driver requested this specific regulatory
	 * domain we keep it for its private use
	 */
	tmp = get_wiphy_regdom(request_wiphy);
	rcu_assign_pointer(request_wiphy->regd, rd);
	rcu_free_regdom(tmp);

	rd = NULL;

	reset_regdomains(false, intersected_rd);

	return 0;
}

static int reg_set_rd_country_ie(const struct ieee80211_regdomain *rd,
				 struct regulatory_request *country_ie_request)
{
	struct wiphy *request_wiphy;

	if (!is_alpha2_set(rd->alpha2) && !is_an_alpha2(rd->alpha2) &&
	    !is_unknown_alpha2(rd->alpha2))
		return -EINVAL;

	/*
	 * Lets only bother proceeding on the same alpha2 if the current
	 * rd is non static (it means CRDA was present and was used last)
	 * and the pending request came in from a country IE
	 */

	if (!is_valid_rd(rd)) {
		pr_err("Invalid regulatory domain detected: %c%c\n",
		       rd->alpha2[0], rd->alpha2[1]);
		print_regdomain_info(rd);
		return -EINVAL;
	}

	request_wiphy = wiphy_idx_to_wiphy(country_ie_request->wiphy_idx);
	if (!request_wiphy)
		return -ENODEV;

	if (country_ie_request->intersect)
		return -EINVAL;

	reset_regdomains(false, rd);
	return 0;
}

/*
 * Use this call to set the current regulatory domain. Conflicts with
 * multiple drivers can be ironed out later. Caller must've already
 * kmalloc'd the rd structure.
 */
int set_regdom(const struct ieee80211_regdomain *rd,
	       enum ieee80211_regd_source regd_src)
{
	struct regulatory_request *lr;
	bool user_reset = false;
	int r;

	if (!reg_is_valid_request(rd->alpha2)) {
		kfree(rd);
		return -EINVAL;
	}

	if (regd_src == REGD_SOURCE_CRDA)
		reset_crda_timeouts();

	lr = get_last_request();

	/* Note that this doesn't update the wiphys, this is done below */
	switch (lr->initiator) {
	case NL80211_REGDOM_SET_BY_CORE:
		r = reg_set_rd_core(rd);
		break;
	case NL80211_REGDOM_SET_BY_USER:
		r = reg_set_rd_user(rd, lr);
		user_reset = true;
		break;
	case NL80211_REGDOM_SET_BY_DRIVER:
		r = reg_set_rd_driver(rd, lr);
		break;
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		r = reg_set_rd_country_ie(rd, lr);
		break;
	default:
		WARN(1, "invalid initiator %d\n", lr->initiator);
		kfree(rd);
		return -EINVAL;
	}

	if (r) {
		switch (r) {
		case -EALREADY:
			reg_set_request_processed();
			break;
		default:
			/* Back to world regulatory in case of errors */
			restore_regulatory_settings(user_reset);
		}

		kfree(rd);
		return r;
	}

	/* This would make this whole thing pointless */
	if (WARN_ON(!lr->intersect && rd != get_cfg80211_regdom()))
		return -EINVAL;

	/* update all wiphys now with the new established regulatory domain */
	update_all_wiphy_regulatory(lr->initiator);

	print_regdomain(get_cfg80211_regdom());

	nl80211_send_reg_change_event(lr);

	reg_set_request_processed();

	return 0;
}

static int __regulatory_set_wiphy_regd(struct wiphy *wiphy,
				       struct ieee80211_regdomain *rd)
{
	const struct ieee80211_regdomain *regd;
	const struct ieee80211_regdomain *prev_regd;
	struct cfg80211_registered_device *rdev;

	if (WARN_ON(!wiphy || !rd))
		return -EINVAL;

	if (WARN(!(wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED),
		 "wiphy should have REGULATORY_WIPHY_SELF_MANAGED\n"))
		return -EPERM;

	if (WARN(!is_valid_rd(rd), "Invalid regulatory domain detected\n")) {
		print_regdomain_info(rd);
		return -EINVAL;
	}

	regd = reg_copy_regd(rd);
	if (IS_ERR(regd))
		return PTR_ERR(regd);

	rdev = wiphy_to_rdev(wiphy);

	spin_lock(&reg_requests_lock);
	prev_regd = rdev->requested_regd;
	rdev->requested_regd = regd;
	spin_unlock(&reg_requests_lock);

	kfree(prev_regd);
	return 0;
}

int regulatory_set_wiphy_regd(struct wiphy *wiphy,
			      struct ieee80211_regdomain *rd)
{
	int ret = __regulatory_set_wiphy_regd(wiphy, rd);

	if (ret)
		return ret;

	schedule_work(&reg_work);
	return 0;
}
EXPORT_SYMBOL(regulatory_set_wiphy_regd);

int regulatory_set_wiphy_regd_sync_rtnl(struct wiphy *wiphy,
					struct ieee80211_regdomain *rd)
{
	int ret;

	ASSERT_RTNL();

	ret = __regulatory_set_wiphy_regd(wiphy, rd);
	if (ret)
		return ret;

	/* process the request immediately */
	reg_process_self_managed_hints();
	return 0;
}
EXPORT_SYMBOL(regulatory_set_wiphy_regd_sync_rtnl);

void wiphy_regulatory_register(struct wiphy *wiphy)
{
	struct regulatory_request *lr = get_last_request();

	/* self-managed devices ignore beacon hints and country IE */
	if (wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED) {
		wiphy->regulatory_flags |= REGULATORY_DISABLE_BEACON_HINTS |
					   REGULATORY_COUNTRY_IE_IGNORE;

		/*
		 * The last request may have been received before this
		 * registration call. Call the driver notifier if
		 * initiator is USER and user type is CELL_BASE.
		 */
		if (lr->initiator == NL80211_REGDOM_SET_BY_USER &&
		    lr->user_reg_hint_type == NL80211_USER_REG_HINT_CELL_BASE)
			reg_call_notifier(wiphy, lr);
	}

	if (!reg_dev_ignore_cell_hint(wiphy))
		reg_num_devs_support_basehint++;

	wiphy_update_regulatory(wiphy, lr->initiator);
	wiphy_all_share_dfs_chan_state(wiphy);
}

void wiphy_regulatory_deregister(struct wiphy *wiphy)
{
	struct wiphy *request_wiphy = NULL;
	struct regulatory_request *lr;

	lr = get_last_request();

	if (!reg_dev_ignore_cell_hint(wiphy))
		reg_num_devs_support_basehint--;

	rcu_free_regdom(get_wiphy_regdom(wiphy));
	RCU_INIT_POINTER(wiphy->regd, NULL);

	if (lr)
		request_wiphy = wiphy_idx_to_wiphy(lr->wiphy_idx);

	if (!request_wiphy || request_wiphy != wiphy)
		return;

	lr->wiphy_idx = WIPHY_IDX_INVALID;
	lr->country_ie_env = ENVIRON_ANY;
}

/*
 * See http://www.fcc.gov/document/5-ghz-unlicensed-spectrum-unii, for
 * UNII band definitions
 */
int cfg80211_get_unii(int freq)
{
	/* UNII-1 */
	if (freq >= 5150 && freq <= 5250)
		return 0;

	/* UNII-2A */
	if (freq > 5250 && freq <= 5350)
		return 1;

	/* UNII-2B */
	if (freq > 5350 && freq <= 5470)
		return 2;

	/* UNII-2C */
	if (freq > 5470 && freq <= 5725)
		return 3;

	/* UNII-3 */
	if (freq > 5725 && freq <= 5825)
		return 4;

	return -EINVAL;
}

bool regulatory_indoor_allowed(void)
{
	return reg_is_indoor;
}

bool regulatory_pre_cac_allowed(struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *regd = NULL;
	const struct ieee80211_regdomain *wiphy_regd = NULL;
	bool pre_cac_allowed = false;

	rcu_read_lock();

	regd = rcu_dereference(cfg80211_regdomain);
	wiphy_regd = rcu_dereference(wiphy->regd);
	if (!wiphy_regd) {
		if (regd->dfs_region == NL80211_DFS_ETSI)
			pre_cac_allowed = true;

		rcu_read_unlock();

		return pre_cac_allowed;
	}

	if (regd->dfs_region == wiphy_regd->dfs_region &&
	    wiphy_regd->dfs_region == NL80211_DFS_ETSI)
		pre_cac_allowed = true;

	rcu_read_unlock();

	return pre_cac_allowed;
}

void regulatory_propagate_dfs_state(struct wiphy *wiphy,
				    struct cfg80211_chan_def *chandef,
				    enum nl80211_dfs_state dfs_state,
				    enum nl80211_radar_event event)
{
	struct cfg80211_registered_device *rdev;

	ASSERT_RTNL();

	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return;

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (wiphy == &rdev->wiphy)
			continue;

		if (!reg_dfs_domain_same(wiphy, &rdev->wiphy))
			continue;

		if (!ieee80211_get_channel(&rdev->wiphy,
					   chandef->chan->center_freq))
			continue;

		cfg80211_set_dfs_state(&rdev->wiphy, chandef, dfs_state);

		if (event == NL80211_RADAR_DETECTED ||
		    event == NL80211_RADAR_CAC_FINISHED)
			cfg80211_sched_dfs_chan_update(rdev);

		nl80211_radar_notify(rdev, chandef, event, NULL, GFP_KERNEL);
	}
}

static int __init regulatory_init_db(void)
{
	int err;

	/*
	 * It's possible that - due to other bugs/issues - cfg80211
	 * never called regulatory_init() below, or that it failed;
	 * in that case, don't try to do any further work here as
	 * it's doomed to lead to crashes.
	 */
	if (IS_ERR_OR_NULL(reg_pdev))
		return -EINVAL;

	err = load_builtin_regdb_keys();
	if (err)
		return err;

	/* We always try to get an update for the static regdomain */
	err = regulatory_hint_core(cfg80211_world_regdom->alpha2);
	if (err) {
		if (err == -ENOMEM) {
			platform_device_unregister(reg_pdev);
			return err;
		}
		/*
		 * N.B. kobject_uevent_env() can fail mainly for when we're out
		 * memory which is handled and propagated appropriately above
		 * but it can also fail during a netlink_broadcast() or during
		 * early boot for call_usermodehelper(). For now treat these
		 * errors as non-fatal.
		 */
		pr_err("kobject_uevent_env() was unable to call CRDA during init\n");
	}

	/*
	 * Finally, if the user set the module parameter treat it
	 * as a user hint.
	 */
	if (!is_world_regdom(ieee80211_regdom))
		regulatory_hint_user(ieee80211_regdom,
				     NL80211_USER_REG_HINT_USER);

	return 0;
}
#ifndef MODULE
late_initcall(regulatory_init_db);
#endif

int __init regulatory_init(void)
{
	reg_pdev = platform_device_register_simple("regulatory", 0, NULL, 0);
	if (IS_ERR(reg_pdev))
		return PTR_ERR(reg_pdev);

	spin_lock_init(&reg_requests_lock);
	spin_lock_init(&reg_pending_beacons_lock);
	spin_lock_init(&reg_indoor_lock);

	rcu_assign_pointer(cfg80211_regdomain, cfg80211_world_regdom);

	user_alpha2[0] = '9';
	user_alpha2[1] = '7';

#ifdef MODULE
	return regulatory_init_db();
#else
	return 0;
#endif
}

void regulatory_exit(void)
{
	struct regulatory_request *reg_request, *tmp;
	struct reg_beacon *reg_beacon, *btmp;

	cancel_work_sync(&reg_work);
	cancel_crda_timeout_sync();
	cancel_delayed_work_sync(&reg_check_chans);

	/* Lock to suppress warnings */
	rtnl_lock();
	reset_regdomains(true, NULL);
	rtnl_unlock();

	dev_set_uevent_suppress(&reg_pdev->dev, true);

	platform_device_unregister(reg_pdev);

	list_for_each_entry_safe(reg_beacon, btmp, &reg_pending_beacons, list) {
		list_del(&reg_beacon->list);
		kfree(reg_beacon);
	}

	list_for_each_entry_safe(reg_beacon, btmp, &reg_beacon_list, list) {
		list_del(&reg_beacon->list);
		kfree(reg_beacon);
	}

	list_for_each_entry_safe(reg_request, tmp, &reg_requests_list, list) {
		list_del(&reg_request->list);
		kfree(reg_request);
	}

	if (!IS_ERR_OR_NULL(regdb))
		kfree(regdb);

	free_regdb_keyring();
}
