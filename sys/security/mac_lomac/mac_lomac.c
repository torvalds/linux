/*-
 * Copyright (c) 1999-2002, 2007-2009 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by NAI Labs,
 * the Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Developed by the TrustedBSD Project.
 *
 * Low-watermark floating label mandatory integrity policy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/conf.h>
#include <sys/extattr.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/pipe.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#include <vm/vm.h>

#include <security/mac/mac_policy.h>
#include <security/mac/mac_framework.h>
#include <security/mac_lomac/mac_lomac.h>

struct mac_lomac_proc {
	struct mac_lomac mac_lomac;
	struct mtx mtx;
};

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, lomac, CTLFLAG_RW, 0,
    "TrustedBSD mac_lomac policy controls");

static int	lomac_label_size = sizeof(struct mac_lomac);
SYSCTL_INT(_security_mac_lomac, OID_AUTO, label_size, CTLFLAG_RD,
    &lomac_label_size, 0, "Size of struct mac_lomac");

static int	lomac_enabled = 1;
SYSCTL_INT(_security_mac_lomac, OID_AUTO, enabled, CTLFLAG_RWTUN,
    &lomac_enabled, 0, "Enforce MAC/LOMAC policy");

static int	destroyed_not_inited;
SYSCTL_INT(_security_mac_lomac, OID_AUTO, destroyed_not_inited, CTLFLAG_RD,
    &destroyed_not_inited, 0, "Count of labels destroyed but not inited");

static int	trust_all_interfaces = 0;
SYSCTL_INT(_security_mac_lomac, OID_AUTO, trust_all_interfaces, CTLFLAG_RDTUN,
    &trust_all_interfaces, 0, "Consider all interfaces 'trusted' by MAC/LOMAC");

static char	trusted_interfaces[128];
SYSCTL_STRING(_security_mac_lomac, OID_AUTO, trusted_interfaces, CTLFLAG_RDTUN,
    trusted_interfaces, 0, "Interfaces considered 'trusted' by MAC/LOMAC");

static int	ptys_equal = 0;
SYSCTL_INT(_security_mac_lomac, OID_AUTO, ptys_equal, CTLFLAG_RWTUN,
    &ptys_equal, 0, "Label pty devices as lomac/equal on create");

static int	revocation_enabled = 1;
SYSCTL_INT(_security_mac_lomac, OID_AUTO, revocation_enabled, CTLFLAG_RWTUN,
    &revocation_enabled, 0, "Revoke access to objects on relabel");

static int	lomac_slot;
#define	SLOT(l)	((struct mac_lomac *)mac_label_get((l), lomac_slot))
#define	SLOT_SET(l, val) mac_label_set((l), lomac_slot, (uintptr_t)(val))
#define	PSLOT(l) ((struct mac_lomac_proc *)				\
    mac_label_get((l), lomac_slot))
#define	PSLOT_SET(l, val) mac_label_set((l), lomac_slot, (uintptr_t)(val))

static MALLOC_DEFINE(M_LOMAC, "mac_lomac_label", "MAC/LOMAC labels");

static struct mac_lomac *
lomac_alloc(int flag)
{
	struct mac_lomac *ml;

	ml = malloc(sizeof(*ml), M_LOMAC, M_ZERO | flag);

	return (ml);
}

static void
lomac_free(struct mac_lomac *ml)
{

	if (ml != NULL)
		free(ml, M_LOMAC);
	else
		atomic_add_int(&destroyed_not_inited, 1);
}

static int
lomac_atmostflags(struct mac_lomac *ml, int flags)
{

	if ((ml->ml_flags & flags) != ml->ml_flags)
		return (EINVAL);
	return (0);
}

static int
lomac_dominate_element(struct mac_lomac_element *a,
    struct mac_lomac_element *b)
{

	switch (a->mle_type) {
	case MAC_LOMAC_TYPE_EQUAL:
	case MAC_LOMAC_TYPE_HIGH:
		return (1);

	case MAC_LOMAC_TYPE_LOW:
		switch (b->mle_type) {
		case MAC_LOMAC_TYPE_GRADE:
		case MAC_LOMAC_TYPE_HIGH:
			return (0);

		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_LOW:
			return (1);

		default:
			panic("lomac_dominate_element: b->mle_type invalid");
		}

	case MAC_LOMAC_TYPE_GRADE:
		switch (b->mle_type) {
		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_LOW:
			return (1);

		case MAC_LOMAC_TYPE_HIGH:
			return (0);

		case MAC_LOMAC_TYPE_GRADE:
			return (a->mle_grade >= b->mle_grade);

		default:
			panic("lomac_dominate_element: b->mle_type invalid");
		}

	default:
		panic("lomac_dominate_element: a->mle_type invalid");
	}
}

static int
lomac_range_in_range(struct mac_lomac *rangea, struct mac_lomac *rangeb)
{

	return (lomac_dominate_element(&rangeb->ml_rangehigh,
	    &rangea->ml_rangehigh) &&
	    lomac_dominate_element(&rangea->ml_rangelow,
	    &rangeb->ml_rangelow));
}

static int
lomac_single_in_range(struct mac_lomac *single, struct mac_lomac *range)
{

	KASSERT((single->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_single_in_range: a not single"));
	KASSERT((range->ml_flags & MAC_LOMAC_FLAG_RANGE) != 0,
	    ("lomac_single_in_range: b not range"));

	return (lomac_dominate_element(&range->ml_rangehigh,
	    &single->ml_single) && lomac_dominate_element(&single->ml_single,
	    &range->ml_rangelow));
}

static int
lomac_auxsingle_in_range(struct mac_lomac *single, struct mac_lomac *range)
{

	KASSERT((single->ml_flags & MAC_LOMAC_FLAG_AUX) != 0,
	    ("lomac_single_in_range: a not auxsingle"));
	KASSERT((range->ml_flags & MAC_LOMAC_FLAG_RANGE) != 0,
	    ("lomac_single_in_range: b not range"));

	return (lomac_dominate_element(&range->ml_rangehigh,
	    &single->ml_auxsingle) &&
	    lomac_dominate_element(&single->ml_auxsingle,
	    &range->ml_rangelow));
}

static int
lomac_dominate_single(struct mac_lomac *a, struct mac_lomac *b)
{
	KASSERT((a->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_dominate_single: a not single"));
	KASSERT((b->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_dominate_single: b not single"));

	return (lomac_dominate_element(&a->ml_single, &b->ml_single));
}

static int
lomac_subject_dominate(struct mac_lomac *a, struct mac_lomac *b)
{
	KASSERT((~a->ml_flags &
	    (MAC_LOMAC_FLAG_SINGLE | MAC_LOMAC_FLAG_RANGE)) == 0,
	    ("lomac_dominate_single: a not subject"));
	KASSERT((b->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_dominate_single: b not single"));

	return (lomac_dominate_element(&a->ml_rangehigh, &b->ml_single));
}

static int
lomac_equal_element(struct mac_lomac_element *a, struct mac_lomac_element *b)
{

	if (a->mle_type == MAC_LOMAC_TYPE_EQUAL ||
	    b->mle_type == MAC_LOMAC_TYPE_EQUAL)
		return (1);

	return (a->mle_type == b->mle_type && a->mle_grade == b->mle_grade);
}

static int
lomac_equal_single(struct mac_lomac *a, struct mac_lomac *b)
{

	KASSERT((a->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_equal_single: a not single"));
	KASSERT((b->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_equal_single: b not single"));

	return (lomac_equal_element(&a->ml_single, &b->ml_single));
}

static int
lomac_contains_equal(struct mac_lomac *ml)
{

	if (ml->ml_flags & MAC_LOMAC_FLAG_SINGLE)
		if (ml->ml_single.mle_type == MAC_LOMAC_TYPE_EQUAL)
			return (1);
	if (ml->ml_flags & MAC_LOMAC_FLAG_AUX)
		if (ml->ml_auxsingle.mle_type == MAC_LOMAC_TYPE_EQUAL)
			return (1);

	if (ml->ml_flags & MAC_LOMAC_FLAG_RANGE) {
		if (ml->ml_rangelow.mle_type == MAC_LOMAC_TYPE_EQUAL)
			return (1);
		if (ml->ml_rangehigh.mle_type == MAC_LOMAC_TYPE_EQUAL)
			return (1);
	}

	return (0);
}

static int
lomac_subject_privileged(struct mac_lomac *ml)
{

	KASSERT((ml->ml_flags & MAC_LOMAC_FLAGS_BOTH) ==
	    MAC_LOMAC_FLAGS_BOTH,
	    ("lomac_subject_privileged: subject doesn't have both labels"));

	/* If the single is EQUAL, it's ok. */
	if (ml->ml_single.mle_type == MAC_LOMAC_TYPE_EQUAL)
		return (0);

	/* If either range endpoint is EQUAL, it's ok. */
	if (ml->ml_rangelow.mle_type == MAC_LOMAC_TYPE_EQUAL ||
	    ml->ml_rangehigh.mle_type == MAC_LOMAC_TYPE_EQUAL)
		return (0);

	/* If the range is low-high, it's ok. */
	if (ml->ml_rangelow.mle_type == MAC_LOMAC_TYPE_LOW &&
	    ml->ml_rangehigh.mle_type == MAC_LOMAC_TYPE_HIGH)
		return (0);

	/* It's not ok. */
	return (EPERM);
}

static int
lomac_high_single(struct mac_lomac *ml)
{

	KASSERT((ml->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_high_single: mac_lomac not single"));

	return (ml->ml_single.mle_type == MAC_LOMAC_TYPE_HIGH);
}

static int
lomac_valid(struct mac_lomac *ml)
{

	if (ml->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		switch (ml->ml_single.mle_type) {
		case MAC_LOMAC_TYPE_GRADE:
		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_HIGH:
		case MAC_LOMAC_TYPE_LOW:
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (ml->ml_single.mle_type != MAC_LOMAC_TYPE_UNDEF)
			return (EINVAL);
	}

	if (ml->ml_flags & MAC_LOMAC_FLAG_AUX) {
		switch (ml->ml_auxsingle.mle_type) {
		case MAC_LOMAC_TYPE_GRADE:
		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_HIGH:
		case MAC_LOMAC_TYPE_LOW:
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (ml->ml_auxsingle.mle_type != MAC_LOMAC_TYPE_UNDEF)
			return (EINVAL);
	}

	if (ml->ml_flags & MAC_LOMAC_FLAG_RANGE) {
		switch (ml->ml_rangelow.mle_type) {
		case MAC_LOMAC_TYPE_GRADE:
		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_HIGH:
		case MAC_LOMAC_TYPE_LOW:
			break;

		default:
			return (EINVAL);
		}

		switch (ml->ml_rangehigh.mle_type) {
		case MAC_LOMAC_TYPE_GRADE:
		case MAC_LOMAC_TYPE_EQUAL:
		case MAC_LOMAC_TYPE_HIGH:
		case MAC_LOMAC_TYPE_LOW:
			break;

		default:
			return (EINVAL);
		}
		if (!lomac_dominate_element(&ml->ml_rangehigh,
		    &ml->ml_rangelow))
			return (EINVAL);
	} else {
		if (ml->ml_rangelow.mle_type != MAC_LOMAC_TYPE_UNDEF ||
		    ml->ml_rangehigh.mle_type != MAC_LOMAC_TYPE_UNDEF)
			return (EINVAL);
	}

	return (0);
}

static void
lomac_set_range(struct mac_lomac *ml, u_short typelow, u_short gradelow,
    u_short typehigh, u_short gradehigh)
{

	ml->ml_rangelow.mle_type = typelow;
	ml->ml_rangelow.mle_grade = gradelow;
	ml->ml_rangehigh.mle_type = typehigh;
	ml->ml_rangehigh.mle_grade = gradehigh;
	ml->ml_flags |= MAC_LOMAC_FLAG_RANGE;
}

static void
lomac_set_single(struct mac_lomac *ml, u_short type, u_short grade)
{

	ml->ml_single.mle_type = type;
	ml->ml_single.mle_grade = grade;
	ml->ml_flags |= MAC_LOMAC_FLAG_SINGLE;
}

static void
lomac_copy_range(struct mac_lomac *labelfrom, struct mac_lomac *labelto)
{

	KASSERT((labelfrom->ml_flags & MAC_LOMAC_FLAG_RANGE) != 0,
	    ("lomac_copy_range: labelfrom not range"));

	labelto->ml_rangelow = labelfrom->ml_rangelow;
	labelto->ml_rangehigh = labelfrom->ml_rangehigh;
	labelto->ml_flags |= MAC_LOMAC_FLAG_RANGE;
}

static void
lomac_copy_single(struct mac_lomac *labelfrom, struct mac_lomac *labelto)
{

	KASSERT((labelfrom->ml_flags & MAC_LOMAC_FLAG_SINGLE) != 0,
	    ("lomac_copy_single: labelfrom not single"));

	labelto->ml_single = labelfrom->ml_single;
	labelto->ml_flags |= MAC_LOMAC_FLAG_SINGLE;
}

static void
lomac_copy_auxsingle(struct mac_lomac *labelfrom, struct mac_lomac *labelto)
{

	KASSERT((labelfrom->ml_flags & MAC_LOMAC_FLAG_AUX) != 0,
	    ("lomac_copy_auxsingle: labelfrom not auxsingle"));

	labelto->ml_auxsingle = labelfrom->ml_auxsingle;
	labelto->ml_flags |= MAC_LOMAC_FLAG_AUX;
}

static void
lomac_copy(struct mac_lomac *source, struct mac_lomac *dest)
{

	if (source->ml_flags & MAC_LOMAC_FLAG_SINGLE)
		lomac_copy_single(source, dest);
	if (source->ml_flags & MAC_LOMAC_FLAG_AUX)
		lomac_copy_auxsingle(source, dest);
	if (source->ml_flags & MAC_LOMAC_FLAG_RANGE)
		lomac_copy_range(source, dest);
}

static int	lomac_to_string(struct sbuf *sb, struct mac_lomac *ml);

static int
maybe_demote(struct mac_lomac *subjlabel, struct mac_lomac *objlabel,
    const char *actionname, const char *objname, struct vnode *vp)
{
	struct sbuf subjlabel_sb, subjtext_sb, objlabel_sb;
	char *subjlabeltext, *objlabeltext, *subjtext;
	struct mac_lomac cached_subjlabel;
	struct mac_lomac_proc *subj;
	struct vattr va;
	struct proc *p;
	pid_t pgid;

	subj = PSLOT(curthread->td_proc->p_label);

	p = curthread->td_proc;
	mtx_lock(&subj->mtx);
        if (subj->mac_lomac.ml_flags & MAC_LOMAC_FLAG_UPDATE) {
		/*
		 * Check to see if the pending demotion would be more or less
		 * severe than this one, and keep the more severe.  This can
		 * only happen for a multi-threaded application.
		 */
		if (lomac_dominate_single(objlabel, &subj->mac_lomac)) {
			mtx_unlock(&subj->mtx);
			return (0);
		}
	}
	bzero(&subj->mac_lomac, sizeof(subj->mac_lomac));
	/*
	 * Always demote the single label.
	 */
	lomac_copy_single(objlabel, &subj->mac_lomac);
	/*
	 * Start with the original range, then minimize each side of the
	 * range to the point of not dominating the object.  The high side
	 * will always be demoted, of course.
	 */
	lomac_copy_range(subjlabel, &subj->mac_lomac);
	if (!lomac_dominate_element(&objlabel->ml_single,
	    &subj->mac_lomac.ml_rangelow))
		subj->mac_lomac.ml_rangelow = objlabel->ml_single;
	subj->mac_lomac.ml_rangehigh = objlabel->ml_single;
	subj->mac_lomac.ml_flags |= MAC_LOMAC_FLAG_UPDATE;
	thread_lock(curthread);
	curthread->td_flags |= TDF_ASTPENDING | TDF_MACPEND;
	thread_unlock(curthread);

	/*
	 * Avoid memory allocation while holding a mutex; cache the label.
	 */
	lomac_copy_single(&subj->mac_lomac, &cached_subjlabel);
	mtx_unlock(&subj->mtx);

	sbuf_new(&subjlabel_sb, NULL, 0, SBUF_AUTOEXTEND);
	lomac_to_string(&subjlabel_sb, subjlabel);
	sbuf_finish(&subjlabel_sb);
	subjlabeltext = sbuf_data(&subjlabel_sb);

	sbuf_new(&subjtext_sb, NULL, 0, SBUF_AUTOEXTEND);
	lomac_to_string(&subjtext_sb, &subj->mac_lomac);
	sbuf_finish(&subjtext_sb);
	subjtext = sbuf_data(&subjtext_sb);

	sbuf_new(&objlabel_sb, NULL, 0, SBUF_AUTOEXTEND);
	lomac_to_string(&objlabel_sb, objlabel);
	sbuf_finish(&objlabel_sb);
	objlabeltext = sbuf_data(&objlabel_sb);

	pgid = p->p_pgrp->pg_id;		/* XXX could be stale? */
	if (vp != NULL && VOP_GETATTR(vp, &va, curthread->td_ucred) == 0) {
		log(LOG_INFO, "LOMAC: level-%s subject p%dg%du%d:%s demoted to"
		    " level %s after %s a level-%s %s (inode=%ju, "
		    "mountpount=%s)\n",
		    subjlabeltext, p->p_pid, pgid, curthread->td_ucred->cr_uid,
		    p->p_comm, subjtext, actionname, objlabeltext, objname,
		    (uintmax_t)va.va_fileid, vp->v_mount->mnt_stat.f_mntonname);
	} else {
		log(LOG_INFO, "LOMAC: level-%s subject p%dg%du%d:%s demoted to"
		    " level %s after %s a level-%s %s\n",
		    subjlabeltext, p->p_pid, pgid, curthread->td_ucred->cr_uid,
		    p->p_comm, subjtext, actionname, objlabeltext, objname);
	}

	sbuf_delete(&subjlabel_sb);
	sbuf_delete(&subjtext_sb);
	sbuf_delete(&objlabel_sb);
		
	return (0);
}

/*
 * Relabel "to" to "from" only if "from" is a valid label (contains at least
 * a single), as for a relabel operation which may or may not involve a
 * relevant label.
 */
static void
try_relabel(struct mac_lomac *from, struct mac_lomac *to)
{

	if (from->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		bzero(to, sizeof(*to));
		lomac_copy(from, to);
	}
}

/*
 * Policy module operations.
 */
static void
lomac_init(struct mac_policy_conf *conf)
{

}

/*
 * Label operations.
 */
static void
lomac_init_label(struct label *label)
{

	SLOT_SET(label, lomac_alloc(M_WAITOK));
}

static int
lomac_init_label_waitcheck(struct label *label, int flag)
{

	SLOT_SET(label, lomac_alloc(flag));
	if (SLOT(label) == NULL)
		return (ENOMEM);

	return (0);
}

static void
lomac_destroy_label(struct label *label)
{

	lomac_free(SLOT(label));
	SLOT_SET(label, NULL);
}

static int
lomac_element_to_string(struct sbuf *sb, struct mac_lomac_element *element)
{

	switch (element->mle_type) {
	case MAC_LOMAC_TYPE_HIGH:
		return (sbuf_printf(sb, "high"));

	case MAC_LOMAC_TYPE_LOW:
		return (sbuf_printf(sb, "low"));

	case MAC_LOMAC_TYPE_EQUAL:
		return (sbuf_printf(sb, "equal"));

	case MAC_LOMAC_TYPE_GRADE:
		return (sbuf_printf(sb, "%d", element->mle_grade));

	default:
		panic("lomac_element_to_string: invalid type (%d)",
		    element->mle_type);
	}
}

static int
lomac_to_string(struct sbuf *sb, struct mac_lomac *ml)
{

	if (ml->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		if (lomac_element_to_string(sb, &ml->ml_single) == -1)
			return (EINVAL);
	}

	if (ml->ml_flags & MAC_LOMAC_FLAG_AUX) {
		if (sbuf_putc(sb, '[') == -1)
			return (EINVAL);

		if (lomac_element_to_string(sb, &ml->ml_auxsingle) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, ']') == -1)
			return (EINVAL);
	}

	if (ml->ml_flags & MAC_LOMAC_FLAG_RANGE) {
		if (sbuf_putc(sb, '(') == -1)
			return (EINVAL);

		if (lomac_element_to_string(sb, &ml->ml_rangelow) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, '-') == -1)
			return (EINVAL);

		if (lomac_element_to_string(sb, &ml->ml_rangehigh) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, ')') == -1)
			return (EINVAL);
	}

	return (0);
}

static int
lomac_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{
	struct mac_lomac *ml;

	if (strcmp(MAC_LOMAC_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	ml = SLOT(label);

	return (lomac_to_string(sb, ml));
}

static int
lomac_parse_element(struct mac_lomac_element *element, char *string)
{

	if (strcmp(string, "high") == 0 || strcmp(string, "hi") == 0) {
		element->mle_type = MAC_LOMAC_TYPE_HIGH;
		element->mle_grade = MAC_LOMAC_TYPE_UNDEF;
	} else if (strcmp(string, "low") == 0 || strcmp(string, "lo") == 0) {
		element->mle_type = MAC_LOMAC_TYPE_LOW;
		element->mle_grade = MAC_LOMAC_TYPE_UNDEF;
	} else if (strcmp(string, "equal") == 0 ||
	    strcmp(string, "eq") == 0) {
		element->mle_type = MAC_LOMAC_TYPE_EQUAL;
		element->mle_grade = MAC_LOMAC_TYPE_UNDEF;
	} else {
		char *p0, *p1;
		int d;

		p0 = string;
		d = strtol(p0, &p1, 10);
	
		if (d < 0 || d > 65535)
			return (EINVAL);
		element->mle_type = MAC_LOMAC_TYPE_GRADE;
		element->mle_grade = d;

		if (p1 == p0 || *p1 != '\0')
			return (EINVAL);
	}

	return (0);
}

/*
 * Note: destructively consumes the string, make a local copy before calling
 * if that's a problem.
 */
static int
lomac_parse(struct mac_lomac *ml, char *string)
{
	char *range, *rangeend, *rangehigh, *rangelow, *single, *auxsingle,
	    *auxsingleend;
	int error;

	/* Do we have a range? */
	single = string;
	range = strchr(string, '(');
	if (range == single)
		single = NULL;
	auxsingle = strchr(string, '[');
	if (auxsingle == single)
		single = NULL;
	if (range != NULL && auxsingle != NULL)
		return (EINVAL);
	rangelow = rangehigh = NULL;
	if (range != NULL) {
		/* Nul terminate the end of the single string. */
		*range = '\0';
		range++;
		rangelow = range;
		rangehigh = strchr(rangelow, '-');
		if (rangehigh == NULL)
			return (EINVAL);
		rangehigh++;
		if (*rangelow == '\0' || *rangehigh == '\0')
			return (EINVAL);
		rangeend = strchr(rangehigh, ')');
		if (rangeend == NULL)
			return (EINVAL);
		if (*(rangeend + 1) != '\0')
			return (EINVAL);
		/* Nul terminate the ends of the ranges. */
		*(rangehigh - 1) = '\0';
		*rangeend = '\0';
	}
	KASSERT((rangelow != NULL && rangehigh != NULL) ||
	    (rangelow == NULL && rangehigh == NULL),
	    ("lomac_internalize_label: range mismatch"));
	if (auxsingle != NULL) {
		/* Nul terminate the end of the single string. */
		*auxsingle = '\0';
		auxsingle++;
		auxsingleend = strchr(auxsingle, ']');
		if (auxsingleend == NULL)
			return (EINVAL);
		if (*(auxsingleend + 1) != '\0')
			return (EINVAL);
		/* Nul terminate the end of the auxsingle. */
		*auxsingleend = '\0';
	}

	bzero(ml, sizeof(*ml));
	if (single != NULL) {
		error = lomac_parse_element(&ml->ml_single, single);
		if (error)
			return (error);
		ml->ml_flags |= MAC_LOMAC_FLAG_SINGLE;
	}

	if (auxsingle != NULL) {
		error = lomac_parse_element(&ml->ml_auxsingle, auxsingle);
		if (error)
			return (error);
		ml->ml_flags |= MAC_LOMAC_FLAG_AUX;
	}

	if (rangelow != NULL) {
		error = lomac_parse_element(&ml->ml_rangelow, rangelow);
		if (error)
			return (error);
		error = lomac_parse_element(&ml->ml_rangehigh, rangehigh);
		if (error)
			return (error);
		ml->ml_flags |= MAC_LOMAC_FLAG_RANGE;
	}

	error = lomac_valid(ml);
	if (error)
		return (error);

	return (0);
}

static int
lomac_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{
	struct mac_lomac *ml, ml_temp;
	int error;

	if (strcmp(MAC_LOMAC_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	error = lomac_parse(&ml_temp, element_data);
	if (error)
		return (error);

	ml = SLOT(label);
	*ml = ml_temp;

	return (0);
}

static void
lomac_copy_label(struct label *src, struct label *dest)
{

	*SLOT(dest) = *SLOT(src);
}

/*
 * Object-specific entry point implementations are sorted alphabetically by
 * object type name and then by operation.
 */
static int
lomac_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{
	struct mac_lomac *a, *b;

	if (!lomac_enabled)
		return (0);

	a = SLOT(dlabel);
	b = SLOT(ifplabel);

	if (lomac_equal_single(a, b))
		return (0);
	return (EACCES);
}

static void
lomac_bpfdesc_create(struct ucred *cred, struct bpf_d *d,
    struct label *dlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(dlabel);

	lomac_copy_single(source, dest);
}

static void
lomac_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(dlabel);
	dest = SLOT(mlabel);

	lomac_copy_single(source, dest);
}

static int
lomac_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_lomac *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a LOMAC label update for the credential, it may be an
	 * update of the single, range, or both.
	 */
	error = lomac_atmostflags(new, MAC_LOMAC_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the LOMAC label is to be changed, authorize as appropriate.
	 */
	if (new->ml_flags & MAC_LOMAC_FLAGS_BOTH) {
		/*
		 * Fill in the missing parts from the previous label.
		 */
		if ((new->ml_flags & MAC_LOMAC_FLAG_SINGLE) == 0)
			lomac_copy_single(subj, new);
		if ((new->ml_flags & MAC_LOMAC_FLAG_RANGE) == 0)
			lomac_copy_range(subj, new);

		/*
		 * To change the LOMAC range on a credential, the new range
		 * label must be in the current range.
		 */
		if (!lomac_range_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the LOMAC single label on a credential, the new
		 * single label must be in the new range.  Implicitly from
		 * the previous check, the new single is in the old range.
		 */
		if (!lomac_single_in_range(new, new))
			return (EPERM);

		/*
		 * To have EQUAL in any component of the new credential LOMAC
		 * label, the subject must already have EQUAL in their label.
		 */
		if (lomac_contains_equal(new)) {
			error = lomac_subject_privileged(subj);
			if (error)
				return (error);
		}

		/*
		 * XXXMAC: Additional consistency tests regarding the single
		 * and range of the new label might be performed here.
		 */
	}

	return (0);
}

static int
lomac_cred_check_visible(struct ucred *cr1, struct ucred *cr2)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cr1->cr_label);
	obj = SLOT(cr2->cr_label);

	/* XXX: range */
	if (!lomac_dominate_single(obj, subj))
		return (ESRCH);

	return (0);
}

static void
lomac_cred_create_init(struct ucred *cred)
{
	struct mac_lomac *dest;

	dest = SLOT(cred->cr_label);

	lomac_set_single(dest, MAC_LOMAC_TYPE_HIGH, 0);
	lomac_set_range(dest, MAC_LOMAC_TYPE_LOW, 0, MAC_LOMAC_TYPE_HIGH, 0);
}

static void
lomac_cred_create_swapper(struct ucred *cred)
{
	struct mac_lomac *dest;

	dest = SLOT(cred->cr_label);

	lomac_set_single(dest, MAC_LOMAC_TYPE_EQUAL, 0);
	lomac_set_range(dest, MAC_LOMAC_TYPE_LOW, 0, MAC_LOMAC_TYPE_HIGH, 0);
}

static void
lomac_cred_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(cred->cr_label);

	try_relabel(source, dest);
}

static void
lomac_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_lomac *ml;
	const char *dn;
	int lomac_type;

	ml = SLOT(delabel);
	dn = devtoname(dev);
	if (strcmp(dn, "null") == 0 ||
	    strcmp(dn, "zero") == 0 ||
	    strcmp(dn, "random") == 0 ||
	    strncmp(dn, "fd/", strlen("fd/")) == 0 ||
	    strncmp(dn, "ttyv", strlen("ttyv")) == 0)
		lomac_type = MAC_LOMAC_TYPE_EQUAL;
	else if (ptys_equal &&
	    (strncmp(dn, "ttyp", strlen("ttyp")) == 0 ||
	    strncmp(dn, "pts/", strlen("pts/")) == 0 ||
	    strncmp(dn, "ptyp", strlen("ptyp")) == 0))
		lomac_type = MAC_LOMAC_TYPE_EQUAL;
	else
		lomac_type = MAC_LOMAC_TYPE_HIGH;
	lomac_set_single(ml, lomac_type, 0);
}

static void
lomac_devfs_create_directory(struct mount *mp, char *dirname, int dirnamelen,
    struct devfs_dirent *de, struct label *delabel)
{
	struct mac_lomac *ml;

	ml = SLOT(delabel);
	lomac_set_single(ml, MAC_LOMAC_TYPE_HIGH, 0);
}

static void
lomac_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(delabel);

	lomac_copy_single(source, dest);
}

static void
lomac_devfs_update(struct mount *mp, struct devfs_dirent *de,
    struct label *delabel, struct vnode *vp, struct label *vplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(vplabel);
	dest = SLOT(delabel);

	lomac_copy(source, dest);
}

static void
lomac_devfs_vnode_associate(struct mount *mp, struct label *mplabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(delabel);
	dest = SLOT(vplabel);

	lomac_copy_single(source, dest);
}

static int
lomac_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_lomac *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a LOMAC label update for the interface, it may be an
	 * update of the single, range, or both.
	 */
	error = lomac_atmostflags(new, MAC_LOMAC_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * Relabling network interfaces requires LOMAC privilege.
	 */
	error = lomac_subject_privileged(subj);
	if (error)
		return (error);

	/*
	 * If the LOMAC label is to be changed, authorize as appropriate.
	 */
	if (new->ml_flags & MAC_LOMAC_FLAGS_BOTH) {
		/*
		 * Fill in the missing parts from the previous label.
		 */
		if ((new->ml_flags & MAC_LOMAC_FLAG_SINGLE) == 0)
			lomac_copy_single(subj, new);
		if ((new->ml_flags & MAC_LOMAC_FLAG_RANGE) == 0)
			lomac_copy_range(subj, new);

		/*
		 * Rely on the traditional superuser status for the LOMAC
		 * interface relabel requirements.  XXXMAC: This will go
		 * away.
		 *
		 * XXXRW: This is also redundant to a higher layer check.
		 */
		error = priv_check_cred(cred, PRIV_NET_SETIFMAC);
		if (error)
			return (EPERM);

		/*
		 * XXXMAC: Additional consistency tests regarding the single
		 * and the range of the new label might be performed here.
		 */
	}

	return (0);
}

static int
lomac_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *p, *i;

	if (!lomac_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(ifplabel);

	return (lomac_single_in_range(p, i) ? 0 : EACCES);
}

static void
lomac_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{
	char tifname[IFNAMSIZ], *p, *q;
	char tiflist[sizeof(trusted_interfaces)];
	struct mac_lomac *dest;
	int len, grade;

	dest = SLOT(ifplabel);

	if (ifp->if_type == IFT_LOOP) {
		grade = MAC_LOMAC_TYPE_EQUAL;
		goto set;
	}

	if (trust_all_interfaces) {
		grade = MAC_LOMAC_TYPE_HIGH;
		goto set;
	}

	grade = MAC_LOMAC_TYPE_LOW;

	if (trusted_interfaces[0] == '\0' ||
	    !strvalid(trusted_interfaces, sizeof(trusted_interfaces)))
		goto set;

	bzero(tiflist, sizeof(tiflist));
	for (p = trusted_interfaces, q = tiflist; *p != '\0'; p++, q++)
		if(*p != ' ' && *p != '\t')
			*q = *p;

	for (p = q = tiflist;; p++) {
		if (*p == ',' || *p == '\0') {
			len = p - q;
			if (len < IFNAMSIZ) {
				bzero(tifname, sizeof(tifname));
				bcopy(q, tifname, len);
				if (strcmp(tifname, ifp->if_xname) == 0) {
					grade = MAC_LOMAC_TYPE_HIGH;
					break;
				}
			}
			else {
				*p = '\0';
				printf("MAC/LOMAC warning: interface name "
				    "\"%s\" is too long (must be < %d)\n",
				    q, IFNAMSIZ);
			}
			if (*p == '\0')
				break;
			q = p + 1;
		}
	}
set:
	lomac_set_single(dest, grade, 0);
	lomac_set_range(dest, grade, 0, grade, 0);
}

static void
lomac_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(ifplabel);
	dest = SLOT(mlabel);

	lomac_copy_single(source, dest);
}

static void
lomac_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(ifplabel);

	try_relabel(source, dest);
}

static int
lomac_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *p, *i;

	if (!lomac_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(inplabel);

	return (lomac_equal_single(p, i) ? 0 : EACCES);
}

static int
lomac_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
    struct label *inplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(inplabel);

	if (!lomac_dominate_single(obj, subj))
		return (ENOENT);

	return (0);
}

static void
lomac_inpcb_create(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	lomac_copy_single(source, dest);
}

static void
lomac_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(inplabel);
	dest = SLOT(mlabel);

	lomac_copy_single(source, dest);
}

static void
lomac_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_lomac *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	lomac_copy_single(source, dest);
}

static void
lomac_ip6q_create(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(q6label);

	lomac_copy_single(source, dest);
}

static int
lomac_ip6q_match(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_lomac *a, *b;

	a = SLOT(q6label);
	b = SLOT(mlabel);

	return (lomac_equal_single(a, b));
}

static void
lomac_ip6q_reassemble(struct ip6q *q6, struct label *q6label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(q6label);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	lomac_copy_single(source, dest);
}

static void
lomac_ip6q_update(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static void
lomac_ipq_create(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(qlabel);

	lomac_copy_single(source, dest);
}

static int
lomac_ipq_match(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_lomac *a, *b;

	a = SLOT(qlabel);
	b = SLOT(mlabel);

	return (lomac_equal_single(a, b));
}

static void
lomac_ipq_reassemble(struct ipq *q, struct label *qlabel, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(qlabel);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	lomac_copy_single(source, dest);
}

static void
lomac_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static int
lomac_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (lomac_subject_privileged(subj))
		return (EPERM);

	if (!lomac_high_single(obj))
		return (EACCES);

	return (0);
}

static void
lomac_mount_create(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(mplabel);
	lomac_copy_single(source, dest);
}

static void
lomac_netinet_arp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *dest;

	dest = SLOT(mlabel);

	lomac_set_single(dest, MAC_LOMAC_TYPE_EQUAL, 0);
}

static void
lomac_netinet_firewall_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	lomac_copy_single(source, dest);
}

static void
lomac_netinet_firewall_send(struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *dest;

	dest = SLOT(mlabel);

	/* XXX: where is the label for the firewall really coming from? */
	lomac_set_single(dest, MAC_LOMAC_TYPE_EQUAL, 0);
}

static void
lomac_netinet_fragment(struct mbuf *m, struct label *mlabel,
    struct mbuf *frag, struct label *fraglabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(fraglabel);

	lomac_copy_single(source, dest);
}

static void
lomac_netinet_icmp_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	lomac_copy_single(source, dest);
}

static void
lomac_netinet_igmp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *dest;

	dest = SLOT(mlabel);

	lomac_set_single(dest, MAC_LOMAC_TYPE_EQUAL, 0);
}

static void
lomac_netinet6_nd6_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *dest;

	dest = SLOT(mlabel);

	lomac_set_single(dest, MAC_LOMAC_TYPE_EQUAL, 0);
}

static int
lomac_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	if (!lomac_enabled)
		return (0);

	/* XXX: This will be implemented soon... */

	return (0);
}

static int
lomac_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!lomac_dominate_single(obj, subj))
		return (maybe_demote(subj, obj, "reading", "pipe", NULL));

	return (0);
}

static int
lomac_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_lomac *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	/*
	 * If there is a LOMAC label update for a pipe, it must be a single
	 * update.
	 */
	error = lomac_atmostflags(new, MAC_LOMAC_FLAG_SINGLE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of a pipe (LOMAC label or not), LOMAC must
	 * authorize the relabel.
	 */
	if (!lomac_single_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the LOMAC label is to be changed, authorize as appropriate.
	 */
	if (new->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		/*
		 * To change the LOMAC label on a pipe, the new pipe label
		 * must be in the subject range.
		 */
		if (!lomac_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the LOMAC label on a pipe to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (lomac_contains_equal(new)) {
			error = lomac_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
lomac_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static void
lomac_pipe_create(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(pplabel);

	lomac_copy_single(source, dest);
}

static void
lomac_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(pplabel);

	try_relabel(source, dest);
}

/*
 * Some system privileges are allowed regardless of integrity grade; others
 * are allowed only when running with privilege with respect to the LOMAC 
 * policy as they might otherwise allow bypassing of the integrity policy.
 */
static int
lomac_priv_check(struct ucred *cred, int priv)
{
	struct mac_lomac *subj;
	int error;

	if (!lomac_enabled)
		return (0);

	/*
	 * Exempt only specific privileges from the LOMAC integrity policy.
	 */
	switch (priv) {
	case PRIV_KTRACE:
	case PRIV_MSGBUF:

	/*
	 * Allow processes to manipulate basic process audit properties, and
	 * to submit audit records.
	 */
	case PRIV_AUDIT_GETAUDIT:
	case PRIV_AUDIT_SETAUDIT:
	case PRIV_AUDIT_SUBMIT:

	/*
	 * Allow processes to manipulate their regular UNIX credentials.
	 */
	case PRIV_CRED_SETUID:
	case PRIV_CRED_SETEUID:
	case PRIV_CRED_SETGID:
	case PRIV_CRED_SETEGID:
	case PRIV_CRED_SETGROUPS:
	case PRIV_CRED_SETREUID:
	case PRIV_CRED_SETREGID:
	case PRIV_CRED_SETRESUID:
	case PRIV_CRED_SETRESGID:

	/*
	 * Allow processes to perform system monitoring.
	 */
	case PRIV_SEEOTHERGIDS:
	case PRIV_SEEOTHERUIDS:
		break;

	/*
	 * Allow access to general process debugging facilities.  We
	 * separately control debugging based on MAC label.
	 */
	case PRIV_DEBUG_DIFFCRED:
	case PRIV_DEBUG_SUGID:
	case PRIV_DEBUG_UNPRIV:

	/*
	 * Allow manipulating jails.
	 */
	case PRIV_JAIL_ATTACH:

	/*
	 * Allow privilege with respect to the Partition policy, but not the
	 * Privs policy.
	 */
	case PRIV_MAC_PARTITION:

	/*
	 * Allow privilege with respect to process resource limits and login
	 * context.
	 */
	case PRIV_PROC_LIMIT:
	case PRIV_PROC_SETLOGIN:
	case PRIV_PROC_SETRLIMIT:

	/*
	 * Allow System V and POSIX IPC privileges.
	 */
	case PRIV_IPC_READ:
	case PRIV_IPC_WRITE:
	case PRIV_IPC_ADMIN:
	case PRIV_IPC_MSGSIZE:
	case PRIV_MQ_ADMIN:

	/*
	 * Allow certain scheduler manipulations -- possibly this should be
	 * controlled by more fine-grained policy, as potentially low
	 * integrity processes can deny CPU to higher integrity ones.
	 */
	case PRIV_SCHED_DIFFCRED:
	case PRIV_SCHED_SETPRIORITY:
	case PRIV_SCHED_RTPRIO:
	case PRIV_SCHED_SETPOLICY:
	case PRIV_SCHED_SET:
	case PRIV_SCHED_SETPARAM:

	/*
	 * More IPC privileges.
	 */
	case PRIV_SEM_WRITE:

	/*
	 * Allow signaling privileges subject to integrity policy.
	 */
	case PRIV_SIGNAL_DIFFCRED:
	case PRIV_SIGNAL_SUGID:

	/*
	 * Allow access to only limited sysctls from lower integrity levels;
	 * piggy-back on the Jail definition.
	 */
	case PRIV_SYSCTL_WRITEJAIL:

	/*
	 * Allow TTY-based privileges, subject to general device access using
	 * labels on TTY device nodes, but not console privilege.
	 */
	case PRIV_TTY_DRAINWAIT:
	case PRIV_TTY_DTRWAIT:
	case PRIV_TTY_EXCLUSIVE:
	case PRIV_TTY_STI:
	case PRIV_TTY_SETA:

	/*
	 * Grant most VFS privileges, as almost all are in practice bounded
	 * by more specific checks using labels.
	 */
	case PRIV_VFS_READ:
	case PRIV_VFS_WRITE:
	case PRIV_VFS_ADMIN:
	case PRIV_VFS_EXEC:
	case PRIV_VFS_LOOKUP:
	case PRIV_VFS_CHFLAGS_DEV:
	case PRIV_VFS_CHOWN:
	case PRIV_VFS_CHROOT:
	case PRIV_VFS_RETAINSUGID:
	case PRIV_VFS_EXCEEDQUOTA:
	case PRIV_VFS_FCHROOT:
	case PRIV_VFS_FHOPEN:
	case PRIV_VFS_FHSTATFS:
	case PRIV_VFS_GENERATION:
	case PRIV_VFS_GETFH:
	case PRIV_VFS_GETQUOTA:
	case PRIV_VFS_LINK:
	case PRIV_VFS_MOUNT:
	case PRIV_VFS_MOUNT_OWNER:
	case PRIV_VFS_MOUNT_PERM:
	case PRIV_VFS_MOUNT_SUIDDIR:
	case PRIV_VFS_MOUNT_NONUSER:
	case PRIV_VFS_SETGID:
	case PRIV_VFS_STICKYFILE:
	case PRIV_VFS_SYSFLAGS:
	case PRIV_VFS_UNMOUNT:

	/*
	 * Allow VM privileges; it would be nice if these were subject to
	 * resource limits.
	 */
	case PRIV_VM_MADV_PROTECT:
	case PRIV_VM_MLOCK:
	case PRIV_VM_MUNLOCK:
	case PRIV_VM_SWAP_NOQUOTA:
	case PRIV_VM_SWAP_NORLIMIT:

	/*
	 * Allow some but not all network privileges.  In general, dont allow
	 * reconfiguring the network stack, just normal use.
	 */
	case PRIV_NETINET_RESERVEDPORT:
	case PRIV_NETINET_RAW:
	case PRIV_NETINET_REUSEPORT:
		break;

	/*
	 * All remaining system privileges are allow only if the process
	 * holds privilege with respect to the LOMAC policy.
	 */
	default:
		subj = SLOT(cred->cr_label);
		error = lomac_subject_privileged(subj);
		if (error)
			return (error);
	}
	return (0);
}

static int
lomac_proc_check_debug(struct ucred *cred, struct proc *p)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!lomac_dominate_single(obj, subj))
		return (ESRCH);
	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_proc_check_sched(struct ucred *cred, struct proc *p)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!lomac_dominate_single(obj, subj))
		return (ESRCH);
	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!lomac_dominate_single(obj, subj))
		return (ESRCH);
	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static void
lomac_proc_destroy_label(struct label *label)
{

	mtx_destroy(&PSLOT(label)->mtx);
	free(PSLOT(label), M_LOMAC);
	PSLOT_SET(label, NULL);
}

static void
lomac_proc_init_label(struct label *label)
{

	PSLOT_SET(label, malloc(sizeof(struct mac_lomac_proc), M_LOMAC,
	    M_ZERO | M_WAITOK));
	mtx_init(&PSLOT(label)->mtx, "MAC/Lomac proc lock", NULL, MTX_DEF);
}

static int
lomac_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *p, *s;
	int error;

	if (!lomac_enabled)
		return (0);

	p = SLOT(mlabel);
	s = SLOT(solabel);

	SOCK_LOCK(so);
	error = lomac_equal_single(p, s) ? 0 : EACCES;
	SOCK_UNLOCK(so);
	return (error);
}

static int
lomac_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_lomac *subj, *obj, *new;
	int error;

	SOCK_LOCK_ASSERT(so);

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	/*
	 * If there is a LOMAC label update for the socket, it may be an
	 * update of single.
	 */
	error = lomac_atmostflags(new, MAC_LOMAC_FLAG_SINGLE);
	if (error)
		return (error);

	/*
	 * To relabel a socket, the old socket single must be in the subject
	 * range.
	 */
	if (!lomac_single_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the LOMAC label is to be changed, authorize as appropriate.
	 */
	if (new->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		/*
		 * To relabel a socket, the new socket single must be in the
		 * subject range.
		 */
		if (!lomac_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the LOMAC label on the socket to contain EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (lomac_contains_equal(new)) {
			error = lomac_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
lomac_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	SOCK_LOCK(so);
	if (!lomac_dominate_single(obj, subj)) {
		SOCK_UNLOCK(so);
		return (ENOENT);
	}
	SOCK_UNLOCK(so);

	return (0);
}

static void
lomac_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(solabel);

	lomac_copy_single(source, dest);
}

static void
lomac_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(mlabel);

	SOCK_LOCK(so);
	lomac_copy_single(source, dest);
	SOCK_UNLOCK(so);
}

static void
lomac_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{
	struct mac_lomac source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);

	dest = SLOT(newsolabel);

	SOCK_LOCK(newso);
	lomac_copy_single(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
lomac_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_lomac *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(newlabel);
	dest = SLOT(solabel);

	try_relabel(source, dest);
}

static void
lomac_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(sopeerlabel);

	SOCK_LOCK(so);
	lomac_copy_single(source, dest);
	SOCK_UNLOCK(so);
}

static void
lomac_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{
	struct mac_lomac source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);

	dest = SLOT(newsopeerlabel);

	SOCK_LOCK(newso);
	lomac_copy_single(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
lomac_syncache_create(struct label *label, struct inpcb *inp)
{
	struct mac_lomac *source, *dest;

	source = SLOT(inp->inp_label);
	dest = SLOT(label);
	lomac_copy(source, dest);
}

static void
lomac_syncache_create_mbuf(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(sc_label);
	dest = SLOT(mlabel);
	lomac_copy(source, dest);
}

static int
lomac_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (lomac_subject_privileged(subj))
		return (EPERM);

	if (!lomac_high_single(obj))
		return (EACCES);

	return (0);
}

static int
lomac_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (lomac_subject_privileged(subj))
		return (EPERM);

	if (!lomac_high_single(obj))
		return (EACCES);

	return (0);
}

static int
lomac_system_check_swapoff(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	if (lomac_subject_privileged(subj))
		return (EPERM);

	return (0);
}

static int
lomac_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (lomac_subject_privileged(subj))
		return (EPERM);

	if (!lomac_high_single(obj))
		return (EACCES);

	return (0);
}

static int
lomac_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
    void *arg1, int arg2, struct sysctl_req *req)
{
	struct mac_lomac *subj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	/*
	 * Treat sysctl variables without CTLFLAG_ANYBODY flag as lomac/high,
	 * but also require privilege to change them.
	 */
	if (req->newptr != NULL && (oidp->oid_kind & CTLFLAG_ANYBODY) == 0) {
#ifdef notdef
		if (!lomac_subject_dominate_high(subj))
			return (EACCES);
#endif

		if (lomac_subject_privileged(subj))
			return (EPERM);
	}

	return (0);
}

static void
lomac_thread_userret(struct thread *td)
{
	struct proc *p = td->td_proc;
	struct mac_lomac_proc *subj = PSLOT(p->p_label);
	struct ucred *newcred, *oldcred;
	int dodrop;

	mtx_lock(&subj->mtx);
	if (subj->mac_lomac.ml_flags & MAC_LOMAC_FLAG_UPDATE) {
		dodrop = 0;
		mtx_unlock(&subj->mtx);
		newcred = crget();
		/*
		 * Prevent a lock order reversal in mac_proc_vm_revoke;
		 * ideally, the other user of subj->mtx wouldn't be holding
		 * Giant.
		 */
		mtx_lock(&Giant);
		PROC_LOCK(p);
		mtx_lock(&subj->mtx);
		/*
		 * Check if we lost the race while allocating the cred.
		 */
		if ((subj->mac_lomac.ml_flags & MAC_LOMAC_FLAG_UPDATE) == 0) {
			crfree(newcred);
			goto out;
		}
		oldcred = p->p_ucred;
		crcopy(newcred, oldcred);
		crhold(newcred);
		lomac_copy(&subj->mac_lomac, SLOT(newcred->cr_label));
		proc_set_cred(p, newcred);
		crfree(oldcred);
		dodrop = 1;
	out:
		mtx_unlock(&subj->mtx);
		PROC_UNLOCK(p);
		if (dodrop)
			mac_proc_vm_revoke(curthread);
		mtx_unlock(&Giant);
	} else {
		mtx_unlock(&subj->mtx);
	}
}

static int
lomac_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_lomac ml_temp, *source, *dest;
	int buflen, error;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	buflen = sizeof(ml_temp);
	bzero(&ml_temp, buflen);

	error = vn_extattr_get(vp, IO_NODELOCKED, MAC_LOMAC_EXTATTR_NAMESPACE,
	    MAC_LOMAC_EXTATTR_NAME, &buflen, (char *)&ml_temp, curthread);
	if (error == ENOATTR || error == EOPNOTSUPP) {
		/* Fall back to the mntlabel. */
		lomac_copy_single(source, dest);
		return (0);
	} else if (error)
		return (error);

	if (buflen != sizeof(ml_temp)) {
		if (buflen != sizeof(ml_temp) - sizeof(ml_temp.ml_auxsingle)) {
			printf("lomac_vnode_associate_extattr: bad size %d\n",
			    buflen);
			return (EPERM);
		}
		bzero(&ml_temp.ml_auxsingle, sizeof(ml_temp.ml_auxsingle));
		buflen = sizeof(ml_temp);
		(void)vn_extattr_set(vp, IO_NODELOCKED,
		    MAC_LOMAC_EXTATTR_NAMESPACE, MAC_LOMAC_EXTATTR_NAME,
		    buflen, (char *)&ml_temp, curthread);
	}
	if (lomac_valid(&ml_temp) != 0) {
		printf("lomac_vnode_associate_extattr: invalid\n");
		return (EPERM);
	}
	if ((ml_temp.ml_flags & MAC_LOMAC_FLAGS_BOTH) !=
	    MAC_LOMAC_FLAG_SINGLE) {
		printf("lomac_vnode_associate_extattr: not single\n");
		return (EPERM);
	}

	lomac_copy_single(&ml_temp, dest);
	return (0);
}

static void
lomac_vnode_associate_singlelabel(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	lomac_copy_single(source, dest);
}

static int
lomac_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);
	if (obj->ml_flags & MAC_LOMAC_FLAG_AUX &&
	    !lomac_dominate_element(&subj->ml_single, &obj->ml_auxsingle))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{
	struct mac_lomac *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!lomac_subject_dominate(subj, obj))
			return (EACCES);
	}
	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!lomac_dominate_single(obj, subj))
			return (maybe_demote(subj, obj, "mapping", "file", vp));
	}

	return (0);
}

static void
lomac_vnode_check_mmap_downgrade(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, /* XXX vm_prot_t */ int *prot)
{
	struct mac_lomac *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!lomac_enabled || !revocation_enabled)
		return;

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		*prot &= ~VM_PROT_WRITE;
}

static int
lomac_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	/* XXX privilege override for admin? */
	if (accmode & VMODIFY_PERMS) {
		if (!lomac_subject_dominate(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
lomac_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_dominate_single(obj, subj))
		return (maybe_demote(subj, obj, "reading", "file", vp));

	return (0);
}

static int
lomac_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_lomac *old, *new, *subj;
	int error;

	old = SLOT(vplabel);
	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);

	/*
	 * If there is a LOMAC label update for the vnode, it must be a
	 * single label, with an optional explicit auxiliary single.
	 */
	error = lomac_atmostflags(new,
	    MAC_LOMAC_FLAG_SINGLE | MAC_LOMAC_FLAG_AUX);
	if (error)
		return (error);

	/*
	 * To perform a relabel of the vnode (LOMAC label or not), LOMAC must
	 * authorize the relabel.
	 */
	if (!lomac_single_in_range(old, subj))
		return (EPERM);

	/*
	 * If the LOMAC label is to be changed, authorize as appropriate.
	 */
	if (new->ml_flags & MAC_LOMAC_FLAG_SINGLE) {
		/*
		 * To change the LOMAC label on a vnode, the new vnode label
		 * must be in the subject range.
		 */
		if (!lomac_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the LOMAC label on the vnode to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (lomac_contains_equal(new)) {
			error = lomac_subject_privileged(subj);
			if (error)
				return (error);
		}
	}
	if (new->ml_flags & MAC_LOMAC_FLAG_AUX) {
		/*
		 * Fill in the missing parts from the previous label.
		 */
		if ((new->ml_flags & MAC_LOMAC_FLAG_SINGLE) == 0)
			lomac_copy_single(subj, new);

		/*
		 * To change the auxiliary LOMAC label on a vnode, the new
		 * vnode label must be in the subject range.
		 */
		if (!lomac_auxsingle_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the auxiliary LOMAC label on the vnode to be
		 * EQUAL, the subject must have appropriate privilege.
		 */
		if (lomac_contains_equal(new)) {
			error = lomac_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
lomac_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	if (vp != NULL) {
		obj = SLOT(vplabel);

		if (!lomac_subject_dominate(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
lomac_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	/* XXX: protect the MAC EA in a special way? */

	return (0);
}

static int
lomac_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_check_write(struct ucred *active_cred,
    struct ucred *file_cred, struct vnode *vp, struct label *vplabel)
{
	struct mac_lomac *subj, *obj;

	if (!lomac_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!lomac_subject_dominate(subj, obj))
		return (EACCES);

	return (0);
}

static int
lomac_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mplabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{
	struct mac_lomac *source, *dest, *dir, temp;
	size_t buflen;
	int error;

	buflen = sizeof(temp);
	bzero(&temp, buflen);

	source = SLOT(cred->cr_label);
	dest = SLOT(vplabel);
	dir = SLOT(dvplabel);
	if (dir->ml_flags & MAC_LOMAC_FLAG_AUX) {
		lomac_copy_auxsingle(dir, &temp);
		lomac_set_single(&temp, dir->ml_auxsingle.mle_type,
		    dir->ml_auxsingle.mle_grade);
	} else {
		lomac_copy_single(source, &temp);
	}

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_LOMAC_EXTATTR_NAMESPACE,
	    MAC_LOMAC_EXTATTR_NAME, buflen, (char *)&temp, curthread);
	if (error == 0)
		lomac_copy(&temp, dest);
	return (error);
}

static void
lomac_vnode_execve_transition(struct ucred *old, struct ucred *new,
    struct vnode *vp, struct label *vplabel, struct label *interpvplabel,
    struct image_params *imgp, struct label *execlabel)
{
	struct mac_lomac *source, *dest, *obj, *robj;

	source = SLOT(old->cr_label);
	dest = SLOT(new->cr_label);
	obj = SLOT(vplabel);
	robj = interpvplabel != NULL ? SLOT(interpvplabel) : obj;

	lomac_copy(source, dest);
	/*
	 * If there's an auxiliary label on the real object, respect it and
	 * assume that this level should be assumed immediately if a higher
	 * level is currently in place.
	 */
	if (robj->ml_flags & MAC_LOMAC_FLAG_AUX &&
	    !lomac_dominate_element(&robj->ml_auxsingle, &dest->ml_single)
	    && lomac_auxsingle_in_range(robj, dest))
		lomac_set_single(dest, robj->ml_auxsingle.mle_type,
		    robj->ml_auxsingle.mle_grade);
	/*
	 * Restructuring to use the execve transitioning mechanism instead of
	 * the normal demotion mechanism here would be difficult, so just
	 * copy the label over and perform standard demotion.  This is also
	 * non-optimal because it will result in the intermediate label "new"
	 * being created and immediately recycled.
	 */
	if (lomac_enabled && revocation_enabled &&
	    !lomac_dominate_single(obj, source))
		(void)maybe_demote(source, obj, "executing", "file", vp);
}

static int
lomac_vnode_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *vplabel, struct label *interpvplabel,
    struct image_params *imgp, struct label *execlabel)
{
	struct mac_lomac *subj, *obj, *robj;

	if (!lomac_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(old->cr_label);
	obj = SLOT(vplabel);
	robj = interpvplabel != NULL ? SLOT(interpvplabel) : obj;

	return ((robj->ml_flags & MAC_LOMAC_FLAG_AUX &&
	    !lomac_dominate_element(&robj->ml_auxsingle, &subj->ml_single)
	    && lomac_auxsingle_in_range(robj, subj)) ||
	    !lomac_dominate_single(obj, subj));
}

static void
lomac_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_lomac *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(vplabel);

	try_relabel(source, dest);
}

static int
lomac_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{
	struct mac_lomac *source, temp;
	size_t buflen;
	int error;

	buflen = sizeof(temp);
	bzero(&temp, buflen);

	source = SLOT(intlabel);
	if ((source->ml_flags & MAC_LOMAC_FLAG_SINGLE) == 0)
		return (0);

	lomac_copy_single(source, &temp);
	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_LOMAC_EXTATTR_NAMESPACE,
	    MAC_LOMAC_EXTATTR_NAME, buflen, (char *)&temp, curthread);
	return (error);
}

static struct mac_policy_ops lomac_ops =
{
	.mpo_init = lomac_init,

	.mpo_bpfdesc_check_receive = lomac_bpfdesc_check_receive,
	.mpo_bpfdesc_create = lomac_bpfdesc_create,
	.mpo_bpfdesc_create_mbuf = lomac_bpfdesc_create_mbuf,
	.mpo_bpfdesc_destroy_label = lomac_destroy_label,
	.mpo_bpfdesc_init_label = lomac_init_label,

	.mpo_cred_check_relabel = lomac_cred_check_relabel,
	.mpo_cred_check_visible = lomac_cred_check_visible,
	.mpo_cred_copy_label = lomac_copy_label,
	.mpo_cred_create_swapper = lomac_cred_create_swapper,
	.mpo_cred_create_init = lomac_cred_create_init,
	.mpo_cred_destroy_label = lomac_destroy_label,
	.mpo_cred_externalize_label = lomac_externalize_label,
	.mpo_cred_init_label = lomac_init_label,
	.mpo_cred_internalize_label = lomac_internalize_label,
	.mpo_cred_relabel = lomac_cred_relabel,

	.mpo_devfs_create_device = lomac_devfs_create_device,
	.mpo_devfs_create_directory = lomac_devfs_create_directory,
	.mpo_devfs_create_symlink = lomac_devfs_create_symlink,
	.mpo_devfs_destroy_label = lomac_destroy_label,
	.mpo_devfs_init_label = lomac_init_label,
	.mpo_devfs_update = lomac_devfs_update,
	.mpo_devfs_vnode_associate = lomac_devfs_vnode_associate,

	.mpo_ifnet_check_relabel = lomac_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = lomac_ifnet_check_transmit,
	.mpo_ifnet_copy_label = lomac_copy_label,
	.mpo_ifnet_create = lomac_ifnet_create,
	.mpo_ifnet_create_mbuf = lomac_ifnet_create_mbuf,
	.mpo_ifnet_destroy_label = lomac_destroy_label,
	.mpo_ifnet_externalize_label = lomac_externalize_label,
	.mpo_ifnet_init_label = lomac_init_label,
	.mpo_ifnet_internalize_label = lomac_internalize_label,
	.mpo_ifnet_relabel = lomac_ifnet_relabel,

	.mpo_syncache_create = lomac_syncache_create,
	.mpo_syncache_destroy_label = lomac_destroy_label,
	.mpo_syncache_init_label = lomac_init_label_waitcheck,

	.mpo_inpcb_check_deliver = lomac_inpcb_check_deliver,
	.mpo_inpcb_check_visible = lomac_inpcb_check_visible,
	.mpo_inpcb_create = lomac_inpcb_create,
	.mpo_inpcb_create_mbuf = lomac_inpcb_create_mbuf,
	.mpo_inpcb_destroy_label = lomac_destroy_label,
	.mpo_inpcb_init_label = lomac_init_label_waitcheck,
	.mpo_inpcb_sosetlabel = lomac_inpcb_sosetlabel,

	.mpo_ip6q_create = lomac_ip6q_create,
	.mpo_ip6q_destroy_label = lomac_destroy_label,
	.mpo_ip6q_init_label = lomac_init_label_waitcheck,
	.mpo_ip6q_match = lomac_ip6q_match,
	.mpo_ip6q_reassemble = lomac_ip6q_reassemble,
	.mpo_ip6q_update = lomac_ip6q_update,

	.mpo_ipq_create = lomac_ipq_create,
	.mpo_ipq_destroy_label = lomac_destroy_label,
	.mpo_ipq_init_label = lomac_init_label_waitcheck,
	.mpo_ipq_match = lomac_ipq_match,
	.mpo_ipq_reassemble = lomac_ipq_reassemble,
	.mpo_ipq_update = lomac_ipq_update,

	.mpo_kld_check_load = lomac_kld_check_load,

	.mpo_mbuf_copy_label = lomac_copy_label,
	.mpo_mbuf_destroy_label = lomac_destroy_label,
	.mpo_mbuf_init_label = lomac_init_label_waitcheck,

	.mpo_mount_create = lomac_mount_create,
	.mpo_mount_destroy_label = lomac_destroy_label,
	.mpo_mount_init_label = lomac_init_label,

	.mpo_netinet_arp_send = lomac_netinet_arp_send,
	.mpo_netinet_firewall_reply = lomac_netinet_firewall_reply,
	.mpo_netinet_firewall_send = lomac_netinet_firewall_send,
	.mpo_netinet_fragment = lomac_netinet_fragment,
	.mpo_netinet_icmp_reply = lomac_netinet_icmp_reply,
	.mpo_netinet_igmp_send = lomac_netinet_igmp_send,

	.mpo_netinet6_nd6_send = lomac_netinet6_nd6_send,

	.mpo_pipe_check_ioctl = lomac_pipe_check_ioctl,
	.mpo_pipe_check_read = lomac_pipe_check_read,
	.mpo_pipe_check_relabel = lomac_pipe_check_relabel,
	.mpo_pipe_check_write = lomac_pipe_check_write,
	.mpo_pipe_copy_label = lomac_copy_label,
	.mpo_pipe_create = lomac_pipe_create,
	.mpo_pipe_destroy_label = lomac_destroy_label,
	.mpo_pipe_externalize_label = lomac_externalize_label,
	.mpo_pipe_init_label = lomac_init_label,
	.mpo_pipe_internalize_label = lomac_internalize_label,
	.mpo_pipe_relabel = lomac_pipe_relabel,

	.mpo_priv_check = lomac_priv_check,

	.mpo_proc_check_debug = lomac_proc_check_debug,
	.mpo_proc_check_sched = lomac_proc_check_sched,
	.mpo_proc_check_signal = lomac_proc_check_signal,
	.mpo_proc_destroy_label = lomac_proc_destroy_label,
	.mpo_proc_init_label = lomac_proc_init_label,

	.mpo_socket_check_deliver = lomac_socket_check_deliver,
	.mpo_socket_check_relabel = lomac_socket_check_relabel,
	.mpo_socket_check_visible = lomac_socket_check_visible,
	.mpo_socket_copy_label = lomac_copy_label,
	.mpo_socket_create = lomac_socket_create,
	.mpo_socket_create_mbuf = lomac_socket_create_mbuf,
	.mpo_socket_destroy_label = lomac_destroy_label,
	.mpo_socket_externalize_label = lomac_externalize_label,
	.mpo_socket_init_label = lomac_init_label_waitcheck,
	.mpo_socket_internalize_label = lomac_internalize_label,
	.mpo_socket_newconn = lomac_socket_newconn,
	.mpo_socket_relabel = lomac_socket_relabel,

	.mpo_socketpeer_destroy_label = lomac_destroy_label,
	.mpo_socketpeer_externalize_label = lomac_externalize_label,
	.mpo_socketpeer_init_label = lomac_init_label_waitcheck,
	.mpo_socketpeer_set_from_mbuf = lomac_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = lomac_socketpeer_set_from_socket,

	.mpo_syncache_create_mbuf = lomac_syncache_create_mbuf,

	.mpo_system_check_acct = lomac_system_check_acct,
	.mpo_system_check_auditctl = lomac_system_check_auditctl,
	.mpo_system_check_swapoff = lomac_system_check_swapoff,
	.mpo_system_check_swapon = lomac_system_check_swapon,
	.mpo_system_check_sysctl = lomac_system_check_sysctl,

	.mpo_thread_userret = lomac_thread_userret,

	.mpo_vnode_associate_extattr = lomac_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = lomac_vnode_associate_singlelabel,
	.mpo_vnode_check_access = lomac_vnode_check_open,
	.mpo_vnode_check_create = lomac_vnode_check_create,
	.mpo_vnode_check_deleteacl = lomac_vnode_check_deleteacl,
	.mpo_vnode_check_link = lomac_vnode_check_link,
	.mpo_vnode_check_mmap = lomac_vnode_check_mmap,
	.mpo_vnode_check_mmap_downgrade = lomac_vnode_check_mmap_downgrade,
	.mpo_vnode_check_open = lomac_vnode_check_open,
	.mpo_vnode_check_read = lomac_vnode_check_read,
	.mpo_vnode_check_relabel = lomac_vnode_check_relabel,
	.mpo_vnode_check_rename_from = lomac_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = lomac_vnode_check_rename_to,
	.mpo_vnode_check_revoke = lomac_vnode_check_revoke,
	.mpo_vnode_check_setacl = lomac_vnode_check_setacl,
	.mpo_vnode_check_setextattr = lomac_vnode_check_setextattr,
	.mpo_vnode_check_setflags = lomac_vnode_check_setflags,
	.mpo_vnode_check_setmode = lomac_vnode_check_setmode,
	.mpo_vnode_check_setowner = lomac_vnode_check_setowner,
	.mpo_vnode_check_setutimes = lomac_vnode_check_setutimes,
	.mpo_vnode_check_unlink = lomac_vnode_check_unlink,
	.mpo_vnode_check_write = lomac_vnode_check_write,
	.mpo_vnode_copy_label = lomac_copy_label,
	.mpo_vnode_create_extattr = lomac_vnode_create_extattr,
	.mpo_vnode_destroy_label = lomac_destroy_label,
	.mpo_vnode_execve_transition = lomac_vnode_execve_transition,
	.mpo_vnode_execve_will_transition = lomac_vnode_execve_will_transition,
	.mpo_vnode_externalize_label = lomac_externalize_label,
	.mpo_vnode_init_label = lomac_init_label,
	.mpo_vnode_internalize_label = lomac_internalize_label,
	.mpo_vnode_relabel = lomac_vnode_relabel,
	.mpo_vnode_setlabel_extattr = lomac_vnode_setlabel_extattr,
};

MAC_POLICY_SET(&lomac_ops, mac_lomac, "TrustedBSD MAC/LOMAC",
    MPC_LOADTIME_FLAG_NOTLATE, &lomac_slot);
