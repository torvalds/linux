// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit tests for ksmbd security descriptor (DACL) handling.
 *
 * Copyright (C) 2026 Hang Nan <nanx95726@gmail.com>
 *
 * The tests pin the DACL declared-size boundary in smb_check_perm_dacl():
 *
 * - ksmbd_dacl_walk_must_stop_at_declared_size: a pure semantic harness
 *   that models the ACE walk.  Walking to the end of the enclosing
 *   security descriptor (the pre-fix behaviour) selects an ACE that
 *   sits beyond struct smb_acl::size; stopping at the declared DACL
 *   size (the fixed behaviour) rejects it.
 */

#include <kunit/test.h>
#include <linux/slab.h>

#include "../smbacl.h"
#include "../smb_common.h"

struct ksmbd_acl_walk_result {
	bool found;
	bool allowed;
	const struct smb_ace *selected;
};

static const struct smb_sid test_nonmatching_sid = {
	1, 5, {0, 0, 0, 0, 0, 5},
	{ cpu_to_le32(21), cpu_to_le32(1), cpu_to_le32(2),
	  cpu_to_le32(3), cpu_to_le32(9999) }
};

/*
 * S-1-22-1-0: the SID id_to_sid(0, SIDUNIX_USER) resolves to, i.e. what
 * smb_check_perm_dacl() looks for when called with uid == 0.
 */
static const struct smb_sid test_owner_sid = {
	1, 2, {0, 0, 0, 0, 0, 22},
	{ cpu_to_le32(1), cpu_to_le32(0) }
};

static int test_compare_sids(const struct smb_sid *a, const struct smb_sid *b)
{
	int i;

	if (a->revision != b->revision || a->num_subauth != b->num_subauth)
		return 1;
	for (i = 0; i < NUM_AUTHS; i++) {
		if (a->authority[i] != b->authority[i])
			return 1;
	}
	for (i = 0; i < a->num_subauth; i++) {
		if (a->sub_auth[i] != b->sub_auth[i])
			return 1;
	}
	return 0;
}

static u16 test_ace_size(const struct smb_sid *sid)
{
	return offsetof(struct smb_ace, sid) + CIFS_SID_BASE_SIZE +
	       sid->num_subauth * sizeof(__le32);
}

static u16 fill_test_ace(struct smb_ace *ace, const struct smb_sid *sid,
			 u32 access_req)
{
	u16 size = test_ace_size(sid);

	ace->type = ACCESS_ALLOWED_ACE_TYPE;
	ace->flags = 0;
	ace->size = cpu_to_le16(size);
	ace->access_req = cpu_to_le32(access_req);
	memcpy(&ace->sid, sid, size - offsetof(struct smb_ace, sid));
	return size;
}

static struct ksmbd_acl_walk_result test_walk_dacl(struct smb_acl *pdacl,
						    int walk_boundary,
						    const struct smb_sid *target,
						    u32 requested)
{
	struct ksmbd_acl_walk_result result = {};
	struct smb_ace *ace;
	int aces_size;
	int i;

	ace = (struct smb_ace *)((char *)pdacl + sizeof(struct smb_acl));
	aces_size = walk_boundary - sizeof(struct smb_acl);
	for (i = 0; i < le16_to_cpu(pdacl->num_aces); i++) {
		u16 ace_size;

		if (aces_size < offsetof(struct smb_ace, sid) + CIFS_SID_BASE_SIZE)
			break;
		ace_size = le16_to_cpu(ace->size);
		if (ace_size > aces_size ||
		    ace_size < offsetof(struct smb_ace, sid) + CIFS_SID_BASE_SIZE)
			break;
		aces_size -= ace_size;

		if (ace->sid.num_subauth > SID_MAX_SUB_AUTHORITIES ||
		    ace_size < offsetof(struct smb_ace, sid) + CIFS_SID_BASE_SIZE +
			       sizeof(__le32) * ace->sid.num_subauth)
			break;

		if (!test_compare_sids(target, &ace->sid)) {
			result.found = true;
			result.selected = ace;
			result.allowed = !(requested & ~le32_to_cpu(ace->access_req));
			return result;
		}

		ace = (struct smb_ace *)((char *)ace + ace_size);
	}

	return result;
}

static void ksmbd_dacl_walk_must_stop_at_declared_size(struct kunit *test)
{
	struct ksmbd_acl_walk_result declared, enclosing;
	struct smb_acl *acl;
	struct smb_ace *ace1, *fake;
	u16 ace1_size, fake_size;
	u16 pdacl_size;
	u16 acl_size;

	acl = kunit_kzalloc(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, acl);

	acl->revision = cpu_to_le16(2);
	acl->num_aces = cpu_to_le16(2);

	ace1 = (struct smb_ace *)((char *)acl + sizeof(*acl));
	ace1_size = fill_test_ace(ace1, &test_nonmatching_sid, 0);
	fake = (struct smb_ace *)((char *)ace1 + ace1_size);
	fake_size = fill_test_ace(fake, &test_owner_sid, FILE_READ_DATA);

	pdacl_size = sizeof(*acl) + ace1_size;
	acl_size = pdacl_size + fake_size;
	acl->size = cpu_to_le16(pdacl_size);

	declared = test_walk_dacl(acl, pdacl_size, &test_owner_sid,
				  FILE_READ_DATA);
	enclosing = test_walk_dacl(acl, acl_size, &test_owner_sid,
				   FILE_READ_DATA);

	KUNIT_EXPECT_FALSE(test, declared.found);
	KUNIT_EXPECT_FALSE(test, declared.allowed);

	/* Demonstrates that the buggy acl_size boundary selects fake ACE #2. */
	KUNIT_EXPECT_TRUE(test, enclosing.found);
	KUNIT_EXPECT_TRUE(test, enclosing.allowed);
}

static struct kunit_case ksmbd_smbacl_test_cases[] = {
	KUNIT_CASE(ksmbd_dacl_walk_must_stop_at_declared_size),
	{}
};

static struct kunit_suite ksmbd_smbacl_test_suite = {
	.name = "ksmbd-smbacl",
	.test_cases = ksmbd_smbacl_test_cases,
};

kunit_test_suite(ksmbd_smbacl_test_suite);

MODULE_DESCRIPTION("KUnit tests for ksmbd smbacl helpers");
MODULE_LICENSE("GPL");
