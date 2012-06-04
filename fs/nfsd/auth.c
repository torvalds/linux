/* Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de> */

#include <linux/sched.h>
#include <linux/user_namespace.h>
#include "nfsd.h"
#include "auth.h"

int nfsexp_flags(struct svc_rqst *rqstp, struct svc_export *exp)
{
	struct exp_flavor_info *f;
	struct exp_flavor_info *end = exp->ex_flavors + exp->ex_nflavors;

	for (f = exp->ex_flavors; f < end; f++) {
		if (f->pseudoflavor == rqstp->rq_cred.cr_flavor)
			return f->flags;
	}
	return exp->ex_flags;

}

int nfsd_setuser(struct svc_rqst *rqstp, struct svc_export *exp)
{
	struct group_info *rqgi;
	struct group_info *gi;
	struct cred *new;
	int i;
	int flags = nfsexp_flags(rqstp, exp);
	int ret;

	validate_process_creds();

	/* discard any old override before preparing the new set */
	revert_creds(get_cred(current->real_cred));
	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	new->fsuid = rqstp->rq_cred.cr_uid;
	new->fsgid = rqstp->rq_cred.cr_gid;

	rqgi = rqstp->rq_cred.cr_group_info;

	if (flags & NFSEXP_ALLSQUASH) {
		new->fsuid = exp->ex_anon_uid;
		new->fsgid = exp->ex_anon_gid;
		gi = groups_alloc(0);
		if (!gi)
			goto oom;
	} else if (flags & NFSEXP_ROOTSQUASH) {
		if (!new->fsuid)
			new->fsuid = exp->ex_anon_uid;
		if (!new->fsgid)
			new->fsgid = exp->ex_anon_gid;

		gi = groups_alloc(rqgi->ngroups);
		if (!gi)
			goto oom;

		for (i = 0; i < rqgi->ngroups; i++) {
			if (gid_eq(GLOBAL_ROOT_GID, GROUP_AT(rqgi, i)))
				GROUP_AT(gi, i) = make_kgid(&init_user_ns, exp->ex_anon_gid);
			else
				GROUP_AT(gi, i) = GROUP_AT(rqgi, i);
		}
	} else {
		gi = get_group_info(rqgi);
	}

	if (new->fsuid == (uid_t) -1)
		new->fsuid = exp->ex_anon_uid;
	if (new->fsgid == (gid_t) -1)
		new->fsgid = exp->ex_anon_gid;

	ret = set_groups(new, gi);
	put_group_info(gi);
	if (ret < 0)
		goto error;

	if (new->fsuid)
		new->cap_effective = cap_drop_nfsd_set(new->cap_effective);
	else
		new->cap_effective = cap_raise_nfsd_set(new->cap_effective,
							new->cap_permitted);
	validate_process_creds();
	put_cred(override_creds(new));
	put_cred(new);
	validate_process_creds();
	return 0;

oom:
	ret = -ENOMEM;
error:
	abort_creds(new);
	return ret;
}

