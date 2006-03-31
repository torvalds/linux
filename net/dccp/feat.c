/*
 *  net/dccp/feat.c
 *
 *  An implementation of the DCCP protocol
 *  Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>

#include "dccp.h"
#include "ccid.h"
#include "feat.h"

#define DCCP_FEAT_SP_NOAGREE (-123)

int dccp_feat_change(struct dccp_minisock *dmsk, u8 type, u8 feature,
		     u8 *val, u8 len, gfp_t gfp)
{
	struct dccp_opt_pend *opt;

	dccp_pr_debug("feat change type=%d feat=%d\n", type, feature);

	/* XXX sanity check feat change request */

	/* check if that feature is already being negotiated */
	list_for_each_entry(opt, &dmsk->dccpms_pending, dccpop_node) {
		/* ok we found a negotiation for this option already */
		if (opt->dccpop_feat == feature && opt->dccpop_type == type) {
			dccp_pr_debug("Replacing old\n");
			/* replace */
			BUG_ON(opt->dccpop_val == NULL);
			kfree(opt->dccpop_val);
			opt->dccpop_val	 = val;
			opt->dccpop_len	 = len;
			opt->dccpop_conf = 0;
			return 0;
		}
	}

	/* negotiation for a new feature */
	opt = kmalloc(sizeof(*opt), gfp);
	if (opt == NULL)
		return -ENOMEM;

	opt->dccpop_type = type;
	opt->dccpop_feat = feature;
	opt->dccpop_len	 = len;
	opt->dccpop_val	 = val;
	opt->dccpop_conf = 0;
	opt->dccpop_sc	 = NULL;

	BUG_ON(opt->dccpop_val == NULL);

	list_add_tail(&opt->dccpop_node, &dmsk->dccpms_pending);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_feat_change);

static int dccp_feat_update_ccid(struct sock *sk, u8 type, u8 new_ccid_nr)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_minisock *dmsk = dccp_msk(sk);
	/* figure out if we are changing our CCID or the peer's */
	const int rx = type == DCCPO_CHANGE_R;
	const u8 ccid_nr = rx ? dmsk->dccpms_rx_ccid : dmsk->dccpms_tx_ccid;
	struct ccid *new_ccid;

	/* Check if nothing is being changed. */
	if (ccid_nr == new_ccid_nr)
		return 0;

	new_ccid = ccid_new(new_ccid_nr, sk, rx, GFP_ATOMIC);
	if (new_ccid == NULL)
		return -ENOMEM;

	if (rx) {
		ccid_hc_rx_delete(dp->dccps_hc_rx_ccid, sk);
		dp->dccps_hc_rx_ccid = new_ccid;
		dmsk->dccpms_rx_ccid = new_ccid_nr;
	} else {
		ccid_hc_tx_delete(dp->dccps_hc_tx_ccid, sk);
		dp->dccps_hc_tx_ccid = new_ccid;
		dmsk->dccpms_tx_ccid = new_ccid_nr;
	}

	return 0;
}

/* XXX taking only u8 vals */
static int dccp_feat_update(struct sock *sk, u8 type, u8 feat, u8 val)
{
	dccp_pr_debug("changing [%d] feat %d to %d\n", type, feat, val);

	switch (feat) {
	case DCCPF_CCID:
		return dccp_feat_update_ccid(sk, type, val);
	default:
		dccp_pr_debug("IMPLEMENT changing [%d] feat %d to %d\n",
			      type, feat, val);
		break;
	}
	return 0;
}

static int dccp_feat_reconcile(struct sock *sk, struct dccp_opt_pend *opt,
			       u8 *rpref, u8 rlen)
{
	struct dccp_sock *dp = dccp_sk(sk);
	u8 *spref, slen, *res = NULL;
	int i, j, rc, agree = 1;

	BUG_ON(rpref == NULL);

	/* check if we are the black sheep */
	if (dp->dccps_role == DCCP_ROLE_CLIENT) {
		spref = rpref;
		slen  = rlen;
		rpref = opt->dccpop_val;
		rlen  = opt->dccpop_len;
	} else {
		spref = opt->dccpop_val;
		slen  = opt->dccpop_len;
	}
	/*
	 * Now we have server preference list in spref and client preference in
	 * rpref
	 */
	BUG_ON(spref == NULL);
	BUG_ON(rpref == NULL);

	/* FIXME sanity check vals */

	/* Are values in any order?  XXX Lame "algorithm" here */
	/* XXX assume values are 1 byte */
	for (i = 0; i < slen; i++) {
		for (j = 0; j < rlen; j++) {
			if (spref[i] == rpref[j]) {
				res = &spref[i];
				break;
			}
		}
		if (res)
			break;
	}

	/* we didn't agree on anything */
	if (res == NULL) {
		/* confirm previous value */
		switch (opt->dccpop_feat) {
		case DCCPF_CCID:
			/* XXX did i get this right? =P */
			if (opt->dccpop_type == DCCPO_CHANGE_L)
				res = &dccp_msk(sk)->dccpms_tx_ccid;
			else
				res = &dccp_msk(sk)->dccpms_rx_ccid;
			break;

		default:
			WARN_ON(1); /* XXX implement res */
			return -EFAULT;
		}

		dccp_pr_debug("Don't agree... reconfirming %d\n", *res);
		agree = 0; /* this is used for mandatory options... */
	}

	/* need to put result and our preference list */
	/* XXX assume 1 byte vals */
	rlen = 1 + opt->dccpop_len;
	rpref = kmalloc(rlen, GFP_ATOMIC);
	if (rpref == NULL)
		return -ENOMEM;

	*rpref = *res;
	memcpy(&rpref[1], opt->dccpop_val, opt->dccpop_len);

	/* put it in the "confirm queue" */
	if (opt->dccpop_sc == NULL) {
		opt->dccpop_sc = kmalloc(sizeof(*opt->dccpop_sc), GFP_ATOMIC);
		if (opt->dccpop_sc == NULL) {
			kfree(rpref);
			return -ENOMEM;
		}
	} else {
		/* recycle the confirm slot */
		BUG_ON(opt->dccpop_sc->dccpoc_val == NULL);
		kfree(opt->dccpop_sc->dccpoc_val);
		dccp_pr_debug("recycling confirm slot\n");
	}
	memset(opt->dccpop_sc, 0, sizeof(*opt->dccpop_sc));

	opt->dccpop_sc->dccpoc_val = rpref;
	opt->dccpop_sc->dccpoc_len = rlen;

	/* update the option on our side [we are about to send the confirm] */
	rc = dccp_feat_update(sk, opt->dccpop_type, opt->dccpop_feat, *res);
	if (rc) {
		kfree(opt->dccpop_sc->dccpoc_val);
		kfree(opt->dccpop_sc);
		opt->dccpop_sc = 0;
		return rc;
	}

	dccp_pr_debug("Will confirm %d\n", *rpref);

	/* say we want to change to X but we just got a confirm X, suppress our
	 * change
	 */
	if (!opt->dccpop_conf) {
		if (*opt->dccpop_val == *res)
			opt->dccpop_conf = 1;
		dccp_pr_debug("won't ask for change of same feature\n");
	}

	return agree ? 0 : DCCP_FEAT_SP_NOAGREE; /* used for mandatory opts */
}

static int dccp_feat_sp(struct sock *sk, u8 type, u8 feature, u8 *val, u8 len)
{
	struct dccp_minisock *dmsk = dccp_msk(sk);
	struct dccp_opt_pend *opt;
	int rc = 1;
	u8 t;

	/*
	 * We received a CHANGE.  We gotta match it against our own preference
	 * list.  If we got a CHANGE_R it means it's a change for us, so we need
	 * to compare our CHANGE_L list.
	 */
	if (type == DCCPO_CHANGE_L)
		t = DCCPO_CHANGE_R;
	else
		t = DCCPO_CHANGE_L;

	/* find our preference list for this feature */
	list_for_each_entry(opt, &dmsk->dccpms_pending, dccpop_node) {
		if (opt->dccpop_type != t || opt->dccpop_feat != feature)
			continue;

		/* find the winner from the two preference lists */
		rc = dccp_feat_reconcile(sk, opt, val, len);
		break;
	}

	/* We didn't deal with the change.  This can happen if we have no
	 * preference list for the feature.  In fact, it just shouldn't
	 * happen---if we understand a feature, we should have a preference list
	 * with at least the default value.
	 */
	BUG_ON(rc == 1);

	return rc;
}

static int dccp_feat_nn(struct sock *sk, u8 type, u8 feature, u8 *val, u8 len)
{
	struct dccp_opt_pend *opt;
	struct dccp_minisock *dmsk = dccp_msk(sk);
	u8 *copy;
	int rc;

	/* NN features must be change L */
	if (type == DCCPO_CHANGE_R) {
		dccp_pr_debug("received CHANGE_R %d for NN feat %d\n",
			      type, feature);
		return -EFAULT;
	}

	/* XXX sanity check opt val */

	/* copy option so we can confirm it */
	opt = kzalloc(sizeof(*opt), GFP_ATOMIC);
	if (opt == NULL)
		return -ENOMEM;

	copy = kmalloc(len, GFP_ATOMIC);
	if (copy == NULL) {
		kfree(opt);
		return -ENOMEM;
	}
	memcpy(copy, val, len);

	opt->dccpop_type = DCCPO_CONFIRM_R; /* NN can only confirm R */
	opt->dccpop_feat = feature;
	opt->dccpop_val	 = copy;
	opt->dccpop_len	 = len;

	/* change feature */
	rc = dccp_feat_update(sk, type, feature, *val);
	if (rc) {
		kfree(opt->dccpop_val);
		kfree(opt);
		return rc;
	}

	dccp_pr_debug("Confirming NN feature %d (val=%d)\n", feature, *copy);
	list_add_tail(&opt->dccpop_node, &dmsk->dccpms_conf);

	return 0;
}

static void dccp_feat_empty_confirm(struct dccp_minisock *dmsk,
				    u8 type, u8 feature)
{
	/* XXX check if other confirms for that are queued and recycle slot */
	struct dccp_opt_pend *opt = kzalloc(sizeof(*opt), GFP_ATOMIC);

	if (opt == NULL) {
		/* XXX what do we do?  Ignoring should be fine.  It's a change
		 * after all =P
		 */
		return;
	}

	opt->dccpop_type = type == DCCPO_CHANGE_L ? DCCPO_CONFIRM_R :
						    DCCPO_CONFIRM_L;
	opt->dccpop_feat = feature;
	opt->dccpop_val	 = 0;
	opt->dccpop_len	 = 0;

	/* change feature */
	dccp_pr_debug("Empty confirm feature %d type %d\n", feature, type);
	list_add_tail(&opt->dccpop_node, &dmsk->dccpms_conf);
}

static void dccp_feat_flush_confirm(struct sock *sk)
{
	struct dccp_minisock *dmsk = dccp_msk(sk);
	/* Check if there is anything to confirm in the first place */
	int yes = !list_empty(&dmsk->dccpms_conf);

	if (!yes) {
		struct dccp_opt_pend *opt;

		list_for_each_entry(opt, &dmsk->dccpms_pending, dccpop_node) {
			if (opt->dccpop_conf) {
				yes = 1;
				break;
			}
		}
	}

	if (!yes)
		return;

	/* OK there is something to confirm... */
	/* XXX check if packet is in flight?  Send delayed ack?? */
	if (sk->sk_state == DCCP_OPEN)
		dccp_send_ack(sk);
}

int dccp_feat_change_recv(struct sock *sk, u8 type, u8 feature, u8 *val, u8 len)
{
	int rc;

	dccp_pr_debug("got feat change type=%d feat=%d\n", type, feature);

	/* figure out if it's SP or NN feature */
	switch (feature) {
	/* deal with SP features */
	case DCCPF_CCID:
		rc = dccp_feat_sp(sk, type, feature, val, len);
		break;

	/* deal with NN features */
	case DCCPF_ACK_RATIO:
		rc = dccp_feat_nn(sk, type, feature, val, len);
		break;

	/* XXX implement other features */
	default:
		rc = -EFAULT;
		break;
	}

	/* check if there were problems changing features */
	if (rc) {
		/* If we don't agree on SP, we sent a confirm for old value.
		 * However we propagate rc to caller in case option was
		 * mandatory
		 */
		if (rc != DCCP_FEAT_SP_NOAGREE)
			dccp_feat_empty_confirm(dccp_msk(sk), type, feature);
	}

	/* generate the confirm [if required] */
	dccp_feat_flush_confirm(sk);

	return rc;
}

EXPORT_SYMBOL_GPL(dccp_feat_change_recv);

int dccp_feat_confirm_recv(struct sock *sk, u8 type, u8 feature,
			   u8 *val, u8 len)
{
	u8 t;
	struct dccp_opt_pend *opt;
	struct dccp_minisock *dmsk = dccp_msk(sk);
	int rc = 1;
	int all_confirmed = 1;

	dccp_pr_debug("got feat confirm type=%d feat=%d\n", type, feature);

	/* XXX sanity check type & feat */

	/* locate our change request */
	t = type == DCCPO_CONFIRM_L ? DCCPO_CHANGE_R : DCCPO_CHANGE_L;

	list_for_each_entry(opt, &dmsk->dccpms_pending, dccpop_node) {
		if (!opt->dccpop_conf && opt->dccpop_type == t &&
		    opt->dccpop_feat == feature) {
			/* we found it */
			/* XXX do sanity check */

			opt->dccpop_conf = 1;

			/* We got a confirmation---change the option */
			dccp_feat_update(sk, opt->dccpop_type,
					 opt->dccpop_feat, *val);

			dccp_pr_debug("feat %d type %d confirmed %d\n",
				      feature, type, *val);
			rc = 0;
			break;
		}

		if (!opt->dccpop_conf)
			all_confirmed = 0;
	}

	/* fix re-transmit timer */
	/* XXX gotta make sure that no option negotiation occurs during
	 * connection shutdown.  Consider that the CLOSEREQ is sent and timer is
	 * on.  if all options are confirmed it might kill timer which should
	 * remain alive until close is received.
	 */
	if (all_confirmed) {
		dccp_pr_debug("clear feat negotiation timer %p\n", sk);
		inet_csk_clear_xmit_timer(sk, ICSK_TIME_RETRANS);
	}

	if (rc)
		dccp_pr_debug("feat %d type %d never requested\n",
			      feature, type);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_feat_confirm_recv);

void dccp_feat_clean(struct dccp_minisock *dmsk)
{
	struct dccp_opt_pend *opt, *next;

	list_for_each_entry_safe(opt, next, &dmsk->dccpms_pending,
				 dccpop_node) {
                BUG_ON(opt->dccpop_val == NULL);
                kfree(opt->dccpop_val);

		if (opt->dccpop_sc != NULL) {
			BUG_ON(opt->dccpop_sc->dccpoc_val == NULL);
			kfree(opt->dccpop_sc->dccpoc_val);
			kfree(opt->dccpop_sc);
		}

                kfree(opt);
        }
	INIT_LIST_HEAD(&dmsk->dccpms_pending);

	list_for_each_entry_safe(opt, next, &dmsk->dccpms_conf, dccpop_node) {
		BUG_ON(opt == NULL);
		if (opt->dccpop_val != NULL)
			kfree(opt->dccpop_val);
		kfree(opt);
	}
	INIT_LIST_HEAD(&dmsk->dccpms_conf);
}

EXPORT_SYMBOL_GPL(dccp_feat_clean);

/* this is to be called only when a listening sock creates its child.  It is
 * assumed by the function---the confirm is not duplicated, but rather it is
 * "passed on".
 */
int dccp_feat_clone(struct sock *oldsk, struct sock *newsk)
{
	struct dccp_minisock *olddmsk = dccp_msk(oldsk);
	struct dccp_minisock *newdmsk = dccp_msk(newsk);
	struct dccp_opt_pend *opt;
	int rc = 0;

	INIT_LIST_HEAD(&newdmsk->dccpms_pending);
	INIT_LIST_HEAD(&newdmsk->dccpms_conf);

	list_for_each_entry(opt, &olddmsk->dccpms_pending, dccpop_node) {
		struct dccp_opt_pend *newopt;
		/* copy the value of the option */
		u8 *val = kmalloc(opt->dccpop_len, GFP_ATOMIC);

		if (val == NULL)
			goto out_clean;
		memcpy(val, opt->dccpop_val, opt->dccpop_len);

		newopt = kmalloc(sizeof(*newopt), GFP_ATOMIC);
		if (newopt == NULL) {
			kfree(val);
			goto out_clean;
		}

		/* insert the option */
		memcpy(newopt, opt, sizeof(*newopt));
		newopt->dccpop_val = val;
		list_add_tail(&newopt->dccpop_node, &newdmsk->dccpms_pending);

		/* XXX what happens with backlogs and multiple connections at
		 * once...
		 */
		/* the master socket no longer needs to worry about confirms */
		opt->dccpop_sc = 0; /* it's not a memleak---new socket has it */

		/* reset state for a new socket */
		opt->dccpop_conf = 0;
	}

	/* XXX not doing anything about the conf queue */

out:
	return rc;

out_clean:
	dccp_feat_clean(newdmsk);
	rc = -ENOMEM;
	goto out;
}

EXPORT_SYMBOL_GPL(dccp_feat_clone);

static int __dccp_feat_init(struct dccp_minisock *dmsk, u8 type, u8 feat,
			    u8 *val, u8 len)
{
	int rc = -ENOMEM;
	u8 *copy = kmalloc(len, GFP_KERNEL);

	if (copy != NULL) {
		memcpy(copy, val, len);
		rc = dccp_feat_change(dmsk, type, feat, copy, len, GFP_KERNEL);
		if (rc)
			kfree(copy);
	}
	return rc;
}

int dccp_feat_init(struct dccp_minisock *dmsk)
{
	int rc;

	INIT_LIST_HEAD(&dmsk->dccpms_pending);
	INIT_LIST_HEAD(&dmsk->dccpms_conf);

	/* CCID L */
	rc = __dccp_feat_init(dmsk, DCCPO_CHANGE_L, DCCPF_CCID,
			      &dmsk->dccpms_tx_ccid, 1);
	if (rc)
		goto out;

	/* CCID R */
	rc = __dccp_feat_init(dmsk, DCCPO_CHANGE_R, DCCPF_CCID,
			      &dmsk->dccpms_rx_ccid, 1);
	if (rc)
		goto out;

	/* Ack ratio */
	rc = __dccp_feat_init(dmsk, DCCPO_CHANGE_L, DCCPF_ACK_RATIO,
			      &dmsk->dccpms_ack_ratio, 1);
out:
	return rc;
}

EXPORT_SYMBOL_GPL(dccp_feat_init);
