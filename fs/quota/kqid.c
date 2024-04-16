// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/quota.h>
#include <linux/export.h>

/**
 *	qid_eq - Test to see if to kquid values are the same
 *	@left: A qid value
 *	@right: Another quid value
 *
 *	Return true if the two qid values are equal and false otherwise.
 */
bool qid_eq(struct kqid left, struct kqid right)
{
	if (left.type != right.type)
		return false;
	switch(left.type) {
	case USRQUOTA:
		return uid_eq(left.uid, right.uid);
	case GRPQUOTA:
		return gid_eq(left.gid, right.gid);
	case PRJQUOTA:
		return projid_eq(left.projid, right.projid);
	default:
		BUG();
	}
}
EXPORT_SYMBOL(qid_eq);

/**
 *	qid_lt - Test to see if one qid value is less than another
 *	@left: The possibly lesser qid value
 *	@right: The possibly greater qid value
 *
 *	Return true if left is less than right and false otherwise.
 */
bool qid_lt(struct kqid left, struct kqid right)
{
	if (left.type < right.type)
		return true;
	if (left.type > right.type)
		return false;
	switch (left.type) {
	case USRQUOTA:
		return uid_lt(left.uid, right.uid);
	case GRPQUOTA:
		return gid_lt(left.gid, right.gid);
	case PRJQUOTA:
		return projid_lt(left.projid, right.projid);
	default:
		BUG();
	}
}
EXPORT_SYMBOL(qid_lt);

/**
 *	from_kqid - Create a qid from a kqid user-namespace pair.
 *	@targ: The user namespace we want a qid in.
 *	@kqid: The kernel internal quota identifier to start with.
 *
 *	Map @kqid into the user-namespace specified by @targ and
 *	return the resulting qid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	If @kqid has no mapping in @targ (qid_t)-1 is returned.
 */
qid_t from_kqid(struct user_namespace *targ, struct kqid kqid)
{
	switch (kqid.type) {
	case USRQUOTA:
		return from_kuid(targ, kqid.uid);
	case GRPQUOTA:
		return from_kgid(targ, kqid.gid);
	case PRJQUOTA:
		return from_kprojid(targ, kqid.projid);
	default:
		BUG();
	}
}
EXPORT_SYMBOL(from_kqid);

/**
 *	from_kqid_munged - Create a qid from a kqid user-namespace pair.
 *	@targ: The user namespace we want a qid in.
 *	@kqid: The kernel internal quota identifier to start with.
 *
 *	Map @kqid into the user-namespace specified by @targ and
 *	return the resulting qid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	Unlike from_kqid from_kqid_munged never fails and always
 *	returns a valid projid.  This makes from_kqid_munged
 *	appropriate for use in places where failing to provide
 *	a qid_t is not a good option.
 *
 *	If @kqid has no mapping in @targ the kqid.type specific
 *	overflow identifier is returned.
 */
qid_t from_kqid_munged(struct user_namespace *targ, struct kqid kqid)
{
	switch (kqid.type) {
	case USRQUOTA:
		return from_kuid_munged(targ, kqid.uid);
	case GRPQUOTA:
		return from_kgid_munged(targ, kqid.gid);
	case PRJQUOTA:
		return from_kprojid_munged(targ, kqid.projid);
	default:
		BUG();
	}
}
EXPORT_SYMBOL(from_kqid_munged);

/**
 *	qid_valid - Report if a valid value is stored in a kqid.
 *	@qid: The kernel internal quota identifier to test.
 */
bool qid_valid(struct kqid qid)
{
	switch (qid.type) {
	case USRQUOTA:
		return uid_valid(qid.uid);
	case GRPQUOTA:
		return gid_valid(qid.gid);
	case PRJQUOTA:
		return projid_valid(qid.projid);
	default:
		BUG();
	}
}
EXPORT_SYMBOL(qid_valid);
