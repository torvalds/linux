/*-
 * Copyright (c) 1999-2002, 2007-2011 Robert N. M. Watson
 * Copyright (c) 2001-2005 McAfee, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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
 * MLS fixed label mandatory confidentiality policy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/conf.h>
#include <sys/extattr.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
#include <sys/mman.h>
#include <sys/malloc.h>
#include <sys/mount.h>
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
#include <sys/pipe.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#include <vm/uma.h>
#include <vm/vm.h>

#include <security/mac/mac_policy.h>
#include <security/mac_mls/mac_mls.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, mls, CTLFLAG_RW, 0,
    "TrustedBSD mac_mls policy controls");

static int	mls_label_size = sizeof(struct mac_mls);
SYSCTL_INT(_security_mac_mls, OID_AUTO, label_size, CTLFLAG_RD,
    &mls_label_size, 0, "Size of struct mac_mls");

static int	mls_enabled = 1;
SYSCTL_INT(_security_mac_mls, OID_AUTO, enabled, CTLFLAG_RWTUN, &mls_enabled, 0,
    "Enforce MAC/MLS policy");

static int	destroyed_not_inited;
SYSCTL_INT(_security_mac_mls, OID_AUTO, destroyed_not_inited, CTLFLAG_RD,
    &destroyed_not_inited, 0, "Count of labels destroyed but not inited");

static int	ptys_equal = 0;
SYSCTL_INT(_security_mac_mls, OID_AUTO, ptys_equal, CTLFLAG_RWTUN,
    &ptys_equal, 0, "Label pty devices as mls/equal on create");

static int	revocation_enabled = 0;
SYSCTL_INT(_security_mac_mls, OID_AUTO, revocation_enabled, CTLFLAG_RWTUN,
    &revocation_enabled, 0, "Revoke access to objects on relabel");

static int	max_compartments = MAC_MLS_MAX_COMPARTMENTS;
SYSCTL_INT(_security_mac_mls, OID_AUTO, max_compartments, CTLFLAG_RD,
    &max_compartments, 0, "Maximum compartments the policy supports");

static int	mls_slot;
#define	SLOT(l)	((struct mac_mls *)mac_label_get((l), mls_slot))
#define	SLOT_SET(l, val) mac_label_set((l), mls_slot, (uintptr_t)(val))

static uma_zone_t	zone_mls;

static __inline int
mls_bit_set_empty(u_char *set) {
	int i;

	for (i = 0; i < MAC_MLS_MAX_COMPARTMENTS >> 3; i++)
		if (set[i] != 0)
			return (0);
	return (1);
}

static struct mac_mls *
mls_alloc(int flag)
{

	return (uma_zalloc(zone_mls, flag | M_ZERO));
}

static void
mls_free(struct mac_mls *mm)
{

	if (mm != NULL)
		uma_zfree(zone_mls, mm);
	else
		atomic_add_int(&destroyed_not_inited, 1);
}

static int
mls_atmostflags(struct mac_mls *mm, int flags)
{

	if ((mm->mm_flags & flags) != mm->mm_flags)
		return (EINVAL);
	return (0);
}

static int
mls_dominate_element(struct mac_mls_element *a, struct mac_mls_element *b)
{
	int bit;

	switch (a->mme_type) {
	case MAC_MLS_TYPE_EQUAL:
	case MAC_MLS_TYPE_HIGH:
		return (1);

	case MAC_MLS_TYPE_LOW:
		switch (b->mme_type) {
		case MAC_MLS_TYPE_LEVEL:
		case MAC_MLS_TYPE_HIGH:
			return (0);

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_LOW:
			return (1);

		default:
			panic("mls_dominate_element: b->mme_type invalid");
		}

	case MAC_MLS_TYPE_LEVEL:
		switch (b->mme_type) {
		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_LOW:
			return (1);

		case MAC_MLS_TYPE_HIGH:
			return (0);

		case MAC_MLS_TYPE_LEVEL:
			for (bit = 1; bit <= MAC_MLS_MAX_COMPARTMENTS; bit++)
				if (!MAC_MLS_BIT_TEST(bit,
				    a->mme_compartments) &&
				    MAC_MLS_BIT_TEST(bit, b->mme_compartments))
					return (0);
			return (a->mme_level >= b->mme_level);

		default:
			panic("mls_dominate_element: b->mme_type invalid");
		}

	default:
		panic("mls_dominate_element: a->mme_type invalid");
	}

	return (0);
}

static int
mls_range_in_range(struct mac_mls *rangea, struct mac_mls *rangeb)
{

	return (mls_dominate_element(&rangeb->mm_rangehigh,
	    &rangea->mm_rangehigh) &&
	    mls_dominate_element(&rangea->mm_rangelow,
	    &rangeb->mm_rangelow));
}

static int
mls_effective_in_range(struct mac_mls *effective, struct mac_mls *range)
{

	KASSERT((effective->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_effective_in_range: a not effective"));
	KASSERT((range->mm_flags & MAC_MLS_FLAG_RANGE) != 0,
	    ("mls_effective_in_range: b not range"));

	return (mls_dominate_element(&range->mm_rangehigh,
	    &effective->mm_effective) &&
	    mls_dominate_element(&effective->mm_effective,
	    &range->mm_rangelow));

	return (1);
}

static int
mls_dominate_effective(struct mac_mls *a, struct mac_mls *b)
{
	KASSERT((a->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_dominate_effective: a not effective"));
	KASSERT((b->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_dominate_effective: b not effective"));

	return (mls_dominate_element(&a->mm_effective, &b->mm_effective));
}

static int
mls_equal_element(struct mac_mls_element *a, struct mac_mls_element *b)
{

	if (a->mme_type == MAC_MLS_TYPE_EQUAL ||
	    b->mme_type == MAC_MLS_TYPE_EQUAL)
		return (1);

	return (a->mme_type == b->mme_type && a->mme_level == b->mme_level);
}

static int
mls_equal_effective(struct mac_mls *a, struct mac_mls *b)
{

	KASSERT((a->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_equal_effective: a not effective"));
	KASSERT((b->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_equal_effective: b not effective"));

	return (mls_equal_element(&a->mm_effective, &b->mm_effective));
}

static int
mls_contains_equal(struct mac_mls *mm)
{

	if (mm->mm_flags & MAC_MLS_FLAG_EFFECTIVE)
		if (mm->mm_effective.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);

	if (mm->mm_flags & MAC_MLS_FLAG_RANGE) {
		if (mm->mm_rangelow.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);
		if (mm->mm_rangehigh.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);
	}

	return (0);
}

static int
mls_subject_privileged(struct mac_mls *mm)
{

	KASSERT((mm->mm_flags & MAC_MLS_FLAGS_BOTH) == MAC_MLS_FLAGS_BOTH,
	    ("mls_subject_privileged: subject doesn't have both labels"));

	/* If the effective is EQUAL, it's ok. */
	if (mm->mm_effective.mme_type == MAC_MLS_TYPE_EQUAL)
		return (0);

	/* If either range endpoint is EQUAL, it's ok. */
	if (mm->mm_rangelow.mme_type == MAC_MLS_TYPE_EQUAL ||
	    mm->mm_rangehigh.mme_type == MAC_MLS_TYPE_EQUAL)
		return (0);

	/* If the range is low-high, it's ok. */
	if (mm->mm_rangelow.mme_type == MAC_MLS_TYPE_LOW &&
	    mm->mm_rangehigh.mme_type == MAC_MLS_TYPE_HIGH)
		return (0);

	/* It's not ok. */
	return (EPERM);
}

static int
mls_valid(struct mac_mls *mm)
{

	if (mm->mm_flags & MAC_MLS_FLAG_EFFECTIVE) {
		switch (mm->mm_effective.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mm->mm_effective.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mm->mm_effective.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (mm->mm_effective.mme_type != MAC_MLS_TYPE_UNDEF)
			return (EINVAL);
	}

	if (mm->mm_flags & MAC_MLS_FLAG_RANGE) {
		switch (mm->mm_rangelow.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mm->mm_rangelow.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mm->mm_rangelow.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}

		switch (mm->mm_rangehigh.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mm->mm_rangehigh.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mm->mm_rangehigh.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
		if (!mls_dominate_element(&mm->mm_rangehigh,
		    &mm->mm_rangelow))
			return (EINVAL);
	} else {
		if (mm->mm_rangelow.mme_type != MAC_MLS_TYPE_UNDEF ||
		    mm->mm_rangehigh.mme_type != MAC_MLS_TYPE_UNDEF)
			return (EINVAL);
	}

	return (0);
}

static void
mls_set_range(struct mac_mls *mm, u_short typelow, u_short levellow,
    u_char *compartmentslow, u_short typehigh, u_short levelhigh,
    u_char *compartmentshigh)
{

	mm->mm_rangelow.mme_type = typelow;
	mm->mm_rangelow.mme_level = levellow;
	if (compartmentslow != NULL)
		memcpy(mm->mm_rangelow.mme_compartments, compartmentslow,
		    sizeof(mm->mm_rangelow.mme_compartments));
	mm->mm_rangehigh.mme_type = typehigh;
	mm->mm_rangehigh.mme_level = levelhigh;
	if (compartmentshigh != NULL)
		memcpy(mm->mm_rangehigh.mme_compartments, compartmentshigh,
		    sizeof(mm->mm_rangehigh.mme_compartments));
	mm->mm_flags |= MAC_MLS_FLAG_RANGE;
}

static void
mls_set_effective(struct mac_mls *mm, u_short type, u_short level,
    u_char *compartments)
{

	mm->mm_effective.mme_type = type;
	mm->mm_effective.mme_level = level;
	if (compartments != NULL)
		memcpy(mm->mm_effective.mme_compartments, compartments,
		    sizeof(mm->mm_effective.mme_compartments));
	mm->mm_flags |= MAC_MLS_FLAG_EFFECTIVE;
}

static void
mls_copy_range(struct mac_mls *labelfrom, struct mac_mls *labelto)
{

	KASSERT((labelfrom->mm_flags & MAC_MLS_FLAG_RANGE) != 0,
	    ("mls_copy_range: labelfrom not range"));

	labelto->mm_rangelow = labelfrom->mm_rangelow;
	labelto->mm_rangehigh = labelfrom->mm_rangehigh;
	labelto->mm_flags |= MAC_MLS_FLAG_RANGE;
}

static void
mls_copy_effective(struct mac_mls *labelfrom, struct mac_mls *labelto)
{

	KASSERT((labelfrom->mm_flags & MAC_MLS_FLAG_EFFECTIVE) != 0,
	    ("mls_copy_effective: labelfrom not effective"));

	labelto->mm_effective = labelfrom->mm_effective;
	labelto->mm_flags |= MAC_MLS_FLAG_EFFECTIVE;
}

static void
mls_copy(struct mac_mls *source, struct mac_mls *dest)
{

	if (source->mm_flags & MAC_MLS_FLAG_EFFECTIVE)
		mls_copy_effective(source, dest);
	if (source->mm_flags & MAC_MLS_FLAG_RANGE)
		mls_copy_range(source, dest);
}

/*
 * Policy module operations.
 */
static void
mls_init(struct mac_policy_conf *conf)
{

	zone_mls = uma_zcreate("mac_mls", sizeof(struct mac_mls), NULL,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
}

/*
 * Label operations.
 */
static void
mls_init_label(struct label *label)
{

	SLOT_SET(label, mls_alloc(M_WAITOK));
}

static int
mls_init_label_waitcheck(struct label *label, int flag)
{

	SLOT_SET(label, mls_alloc(flag));
	if (SLOT(label) == NULL)
		return (ENOMEM);

	return (0);
}

static void
mls_destroy_label(struct label *label)
{

	mls_free(SLOT(label));
	SLOT_SET(label, NULL);
}

/*
 * mls_element_to_string() accepts an sbuf and MLS element.  It converts the
 * MLS element to a string and stores the result in the sbuf; if there isn't
 * space in the sbuf, -1 is returned.
 */
static int
mls_element_to_string(struct sbuf *sb, struct mac_mls_element *element)
{
	int i, first;

	switch (element->mme_type) {
	case MAC_MLS_TYPE_HIGH:
		return (sbuf_printf(sb, "high"));

	case MAC_MLS_TYPE_LOW:
		return (sbuf_printf(sb, "low"));

	case MAC_MLS_TYPE_EQUAL:
		return (sbuf_printf(sb, "equal"));

	case MAC_MLS_TYPE_LEVEL:
		if (sbuf_printf(sb, "%d", element->mme_level) == -1)
			return (-1);

		first = 1;
		for (i = 1; i <= MAC_MLS_MAX_COMPARTMENTS; i++) {
			if (MAC_MLS_BIT_TEST(i, element->mme_compartments)) {
				if (first) {
					if (sbuf_putc(sb, ':') == -1)
						return (-1);
					if (sbuf_printf(sb, "%d", i) == -1)
						return (-1);
					first = 0;
				} else {
					if (sbuf_printf(sb, "+%d", i) == -1)
						return (-1);
				}
			}
		}
		return (0);

	default:
		panic("mls_element_to_string: invalid type (%d)",
		    element->mme_type);
	}
}

/*
 * mls_to_string() converts an MLS label to a string, and places the results
 * in the passed sbuf.  It returns 0 on success, or EINVAL if there isn't
 * room in the sbuf.  Note: the sbuf will be modified even in a failure case,
 * so the caller may need to revert the sbuf by restoring the offset if
 * that's undesired.
 */
static int
mls_to_string(struct sbuf *sb, struct mac_mls *mm)
{

	if (mm->mm_flags & MAC_MLS_FLAG_EFFECTIVE) {
		if (mls_element_to_string(sb, &mm->mm_effective) == -1)
			return (EINVAL);
	}

	if (mm->mm_flags & MAC_MLS_FLAG_RANGE) {
		if (sbuf_putc(sb, '(') == -1)
			return (EINVAL);

		if (mls_element_to_string(sb, &mm->mm_rangelow) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, '-') == -1)
			return (EINVAL);

		if (mls_element_to_string(sb, &mm->mm_rangehigh) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, ')') == -1)
			return (EINVAL);
	}

	return (0);
}

static int
mls_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{
	struct mac_mls *mm;

	if (strcmp(MAC_MLS_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	mm = SLOT(label);

	return (mls_to_string(sb, mm));
}

static int
mls_parse_element(struct mac_mls_element *element, char *string)
{
	char *compartment, *end, *level;
	int value;

	if (strcmp(string, "high") == 0 || strcmp(string, "hi") == 0) {
		element->mme_type = MAC_MLS_TYPE_HIGH;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else if (strcmp(string, "low") == 0 || strcmp(string, "lo") == 0) {
		element->mme_type = MAC_MLS_TYPE_LOW;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else if (strcmp(string, "equal") == 0 ||
	    strcmp(string, "eq") == 0) {
		element->mme_type = MAC_MLS_TYPE_EQUAL;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else {
		element->mme_type = MAC_MLS_TYPE_LEVEL;

		/*
		 * Numeric level piece of the element.
		 */
		level = strsep(&string, ":");
		value = strtol(level, &end, 10);
		if (end == level || *end != '\0')
			return (EINVAL);
		if (value < 0 || value > 65535)
			return (EINVAL);
		element->mme_level = value;

		/*
		 * Optional compartment piece of the element.  If none are
		 * included, we assume that the label has no compartments.
		 */
		if (string == NULL)
			return (0);
		if (*string == '\0')
			return (0);

		while ((compartment = strsep(&string, "+")) != NULL) {
			value = strtol(compartment, &end, 10);
			if (compartment == end || *end != '\0')
				return (EINVAL);
			if (value < 1 || value > MAC_MLS_MAX_COMPARTMENTS)
				return (EINVAL);
			MAC_MLS_BIT_SET(value, element->mme_compartments);
		}
	}

	return (0);
}

/*
 * Note: destructively consumes the string, make a local copy before calling
 * if that's a problem.
 */
static int
mls_parse(struct mac_mls *mm, char *string)
{
	char *rangehigh, *rangelow, *effective;
	int error;

	effective = strsep(&string, "(");
	if (*effective == '\0')
		effective = NULL;

	if (string != NULL) {
		rangelow = strsep(&string, "-");
		if (string == NULL)
			return (EINVAL);
		rangehigh = strsep(&string, ")");
		if (string == NULL)
			return (EINVAL);
		if (*string != '\0')
			return (EINVAL);
	} else {
		rangelow = NULL;
		rangehigh = NULL;
	}

	KASSERT((rangelow != NULL && rangehigh != NULL) ||
	    (rangelow == NULL && rangehigh == NULL),
	    ("mls_parse: range mismatch"));

	bzero(mm, sizeof(*mm));
	if (effective != NULL) {
		error = mls_parse_element(&mm->mm_effective, effective);
		if (error)
			return (error);
		mm->mm_flags |= MAC_MLS_FLAG_EFFECTIVE;
	}

	if (rangelow != NULL) {
		error = mls_parse_element(&mm->mm_rangelow, rangelow);
		if (error)
			return (error);
		error = mls_parse_element(&mm->mm_rangehigh, rangehigh);
		if (error)
			return (error);
		mm->mm_flags |= MAC_MLS_FLAG_RANGE;
	}

	error = mls_valid(mm);
	if (error)
		return (error);

	return (0);
}

static int
mls_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{
	struct mac_mls *mm, mm_temp;
	int error;

	if (strcmp(MAC_MLS_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	error = mls_parse(&mm_temp, element_data);
	if (error)
		return (error);

	mm = SLOT(label);
	*mm = mm_temp;

	return (0);
}

static void
mls_copy_label(struct label *src, struct label *dest)
{

	*SLOT(dest) = *SLOT(src);
}

/*
 * Object-specific entry point implementations are sorted alphabetically by
 * object type name and then by operation.
 */
static int
mls_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
     struct ifnet *ifp, struct label *ifplabel)
{
	struct mac_mls *a, *b;

	if (!mls_enabled)
		return (0);

	a = SLOT(dlabel);
	b = SLOT(ifplabel);

	if (mls_equal_effective(a, b))
		return (0);
	return (EACCES);
}

static void
mls_bpfdesc_create(struct ucred *cred, struct bpf_d *d, struct label *dlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(dlabel);

	mls_copy_effective(source, dest);
}

static void
mls_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(dlabel);
	dest = SLOT(mlabel);

	mls_copy_effective(source, dest);
}

static void
mls_cred_associate_nfsd(struct ucred *cred) 
{
	struct mac_mls *label;

	label = SLOT(cred->cr_label);
	mls_set_effective(label, MAC_MLS_TYPE_LOW, 0, NULL);
	mls_set_range(label, MAC_MLS_TYPE_LOW, 0, NULL, MAC_MLS_TYPE_HIGH, 0,
	    NULL);
}

static int
mls_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_mls *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is an MLS label update for the credential, it may be an
	 * update of effective, range, or both.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAGS_BOTH) {
		/*
		 * If the change request modifies both the MLS label
		 * effective and range, check that the new effective will be
		 * in the new range.
		 */
		if ((new->mm_flags & MAC_MLS_FLAGS_BOTH) ==
		    MAC_MLS_FLAGS_BOTH && !mls_effective_in_range(new, new))
			return (EINVAL);

		/*
		 * To change the MLS effective label on a credential, the new
		 * effective label must be in the current range.
		 */
		if (new->mm_flags & MAC_MLS_FLAG_EFFECTIVE &&
		    !mls_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS range label on a credential, the new
		 * range must be in the current range.
		 */
		if (new->mm_flags & MAC_MLS_FLAG_RANGE &&
		    !mls_range_in_range(new, subj))
			return (EPERM);

		/*
		 * To have EQUAL in any component of the new credential MLS
		 * label, the subject must already have EQUAL in their label.
		 */
		if (mls_contains_equal(new)) {
			error = mls_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mls_cred_check_visible(struct ucred *cr1, struct ucred *cr2)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cr1->cr_label);
	obj = SLOT(cr2->cr_label);

	/* XXX: range */
	if (!mls_dominate_effective(subj, obj))
		return (ESRCH);

	return (0);
}

static void
mls_cred_create_init(struct ucred *cred)
{
	struct mac_mls *dest;

	dest = SLOT(cred->cr_label);

	mls_set_effective(dest, MAC_MLS_TYPE_LOW, 0, NULL);
	mls_set_range(dest, MAC_MLS_TYPE_LOW, 0, NULL, MAC_MLS_TYPE_HIGH, 0,
	    NULL);
}

static void
mls_cred_create_swapper(struct ucred *cred)
{
	struct mac_mls *dest;

	dest = SLOT(cred->cr_label);

	mls_set_effective(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
	mls_set_range(dest, MAC_MLS_TYPE_LOW, 0, NULL, MAC_MLS_TYPE_HIGH, 0,
	    NULL);
}

static void
mls_cred_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(cred->cr_label);

	mls_copy(source, dest);
}

static void
mls_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_mls *mm;
	const char *dn;
	int mls_type;

	mm = SLOT(delabel);
	dn = devtoname(dev);
	if (strcmp(dn, "null") == 0 ||
	    strcmp(dn, "zero") == 0 ||
	    strcmp(dn, "random") == 0 ||
	    strncmp(dn, "fd/", strlen("fd/")) == 0)
		mls_type = MAC_MLS_TYPE_EQUAL;
	else if (strcmp(dn, "kmem") == 0 ||
	    strcmp(dn, "mem") == 0)
		mls_type = MAC_MLS_TYPE_HIGH;
	else if (ptys_equal &&
	    (strncmp(dn, "ttyp", strlen("ttyp")) == 0 ||
	    strncmp(dn, "pts/", strlen("pts/")) == 0 ||
	    strncmp(dn, "ptyp", strlen("ptyp")) == 0))
		mls_type = MAC_MLS_TYPE_EQUAL;
	else
		mls_type = MAC_MLS_TYPE_LOW;
	mls_set_effective(mm, mls_type, 0, NULL);
}

static void
mls_devfs_create_directory(struct mount *mp, char *dirname, int dirnamelen,
    struct devfs_dirent *de, struct label *delabel)
{
	struct mac_mls *mm;

	mm = SLOT(delabel);
	mls_set_effective(mm, MAC_MLS_TYPE_LOW, 0, NULL);
}

static void
mls_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(delabel);

	mls_copy_effective(source, dest);
}

static void
mls_devfs_update(struct mount *mp, struct devfs_dirent *de,
    struct label *delabel, struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(vplabel);
	dest = SLOT(delabel);

	mls_copy_effective(source, dest);
}

static void
mls_devfs_vnode_associate(struct mount *mp, struct label *mplabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(delabel);
	dest = SLOT(vplabel);

	mls_copy_effective(source, dest);
}

static int
mls_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_mls *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is an MLS label update for the interface, it may be an
	 * update of effective, range, or both.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * Relabeling network interfaces requires MLS privilege.
	 */
	return (mls_subject_privileged(subj));
}

static int
mls_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *p, *i;

	if (!mls_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(ifplabel);

	return (mls_effective_in_range(p, i) ? 0 : EACCES);
}

static void
mls_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{
	struct mac_mls *dest;
	int type;

	dest = SLOT(ifplabel);

	if (ifp->if_type == IFT_LOOP)
		type = MAC_MLS_TYPE_EQUAL;
	else
		type = MAC_MLS_TYPE_LOW;

	mls_set_effective(dest, type, 0, NULL);
	mls_set_range(dest, type, 0, NULL, type, 0, NULL);
}

static void
mls_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(ifplabel);
	dest = SLOT(mlabel);

	mls_copy_effective(source, dest);
}

static void
mls_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(ifplabel);

	mls_copy(source, dest);
}

static int
mls_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *p, *i;

	if (!mls_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(inplabel);

	return (mls_equal_effective(p, i) ? 0 : EACCES);
}

static int
mls_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
    struct label *inplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(inplabel);

	if (!mls_dominate_effective(subj, obj))
		return (ENOENT);

	return (0);
}

static void
mls_inpcb_create(struct socket *so, struct label *solabel, struct inpcb *inp,
    struct label *inplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	mls_copy_effective(source, dest);
}

static void
mls_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(inplabel);
	dest = SLOT(mlabel);

	mls_copy_effective(source, dest);
}

static void
mls_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_mls *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	mls_copy(source, dest);
}

static void
mls_ip6q_create(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_mls *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(q6label);

	mls_copy_effective(source, dest);
}

static int
mls_ip6q_match(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_mls *a, *b;

	a = SLOT(q6label);
	b = SLOT(mlabel);

	return (mls_equal_effective(a, b));
}

static void
mls_ip6q_reassemble(struct ip6q *q6, struct label *q6label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(q6label);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	mls_copy_effective(source, dest);
}

static void
mls_ip6q_update(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static void
mls_ipq_create(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(qlabel);

	mls_copy_effective(source, dest);
}

static int
mls_ipq_match(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_mls *a, *b;

	a = SLOT(qlabel);
	b = SLOT(mlabel);

	return (mls_equal_effective(a, b));
}

static void
mls_ipq_reassemble(struct ipq *q, struct label *qlabel, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(qlabel);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	mls_copy_effective(source, dest);
}

static void
mls_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static int
mls_mount_check_stat(struct ucred *cred, struct mount *mp,
    struct label *mntlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(mntlabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static void
mls_mount_create(struct ucred *cred, struct mount *mp, struct label *mplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(mplabel);

	mls_copy_effective(source, dest);
}

static void
mls_netinet_arp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *dest;

	dest = SLOT(mlabel);

	mls_set_effective(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
}

static void
mls_netinet_firewall_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	mls_copy_effective(source, dest);
}

static void
mls_netinet_firewall_send(struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *dest;

	dest = SLOT(mlabel);

	/* XXX: where is the label for the firewall really coming from? */
	mls_set_effective(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
}

static void
mls_netinet_fragment(struct mbuf *m, struct label *mlabel, struct mbuf *frag,
    struct label *fraglabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(fraglabel);

	mls_copy_effective(source, dest);
}

static void
mls_netinet_icmp_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	mls_copy_effective(source, dest);
}

static void
mls_netinet_igmp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *dest;

	dest = SLOT(mlabel);

	mls_set_effective(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
}

static void
mls_netinet6_nd6_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *dest;

	dest = SLOT(mlabel);

	mls_set_effective(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
}

static int
mls_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	if (!mls_enabled)
		return (0);

	/* XXX: This will be implemented soon... */

	return (0);
}

static int
mls_pipe_check_poll(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_mls *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	/*
	 * If there is an MLS label update for a pipe, it must be a effective
	 * update.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of a pipe (MLS label or not), MLS must
	 * authorize the relabel.
	 */
	if (!mls_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_EFFECTIVE) {
		/*
		 * To change the MLS label on a pipe, the new pipe label must
		 * be in the subject range.
		 */
		if (!mls_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on a pipe to be EQUAL, the subject
		 * must have appropriate privilege.
		 */
		if (mls_contains_equal(new)) {
			error = mls_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mls_pipe_check_stat(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
mls_pipe_create(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(pplabel);

	mls_copy_effective(source, dest);
}

static void
mls_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(pplabel);

	mls_copy(source, dest);
}

static int
mls_posixsem_check_openunlink(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixsem_check_rdonly(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(kslabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_posixsem_check_setmode(struct ucred *cred, struct ksem *ks,
    struct label *shmlabel, mode_t mode)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixsem_check_setowner(struct ucred *cred, struct ksem *ks,
    struct label *shmlabel, uid_t uid, gid_t gid)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixsem_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(kslabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
mls_posixsem_create(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(kslabel);

	mls_copy_effective(source, dest);
}

static int
mls_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, int prot, int flags)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
	}
	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mls_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, accmode_t accmode)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (accmode & (VREAD | VEXEC | VSTAT_PERMS)) {
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
	}
	if (accmode & VMODIFY_PERMS) {
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mls_posixshm_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shm, struct label *shmlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, mode_t mode)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, uid_t uid, gid_t gid)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixshm_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd, struct label *shmlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_posixshm_check_truncate(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shmfd, struct label *shmlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);
    
	return (0);
}

static int
mls_posixshm_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shm, struct label *shmlabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static void
mls_posixshm_create(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(shmlabel);

	mls_copy_effective(source, dest);
}

static int
mls_proc_check_debug(struct ucred *cred, struct proc *p)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mls_dominate_effective(subj, obj))
		return (ESRCH);
	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_proc_check_sched(struct ucred *cred, struct proc *p)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mls_dominate_effective(subj, obj))
		return (ESRCH);
	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mls_dominate_effective(subj, obj))
		return (ESRCH);
	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *p, *s;
	int error;

	if (!mls_enabled)
		return (0);

	p = SLOT(mlabel);
	s = SLOT(solabel);

	SOCK_LOCK(so);
	error = mls_equal_effective(p, s) ? 0 : EACCES;
	SOCK_UNLOCK(so);

	return (error);
}

static int
mls_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_mls *subj, *obj, *new;
	int error;

	SOCK_LOCK_ASSERT(so);

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	/*
	 * If there is an MLS label update for the socket, it may be an
	 * update of effective.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To relabel a socket, the old socket effective must be in the
	 * subject range.
	 */
	if (!mls_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_EFFECTIVE) {
		/*
		 * To relabel a socket, the new socket effective must be in
		 * the subject range.
		 */
		if (!mls_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on the socket to contain EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (mls_contains_equal(new)) {
			error = mls_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mls_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	SOCK_LOCK(so);
	if (!mls_dominate_effective(subj, obj)) {
		SOCK_UNLOCK(so);
		return (ENOENT);
	}
	SOCK_UNLOCK(so);

	return (0);
}

static void
mls_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(solabel);

	mls_copy_effective(source, dest);
}

static void
mls_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(mlabel);

	SOCK_LOCK(so);
	mls_copy_effective(source, dest);
	SOCK_UNLOCK(so);
}

static void
mls_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{
	struct mac_mls source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);

	dest = SLOT(newsolabel);

	SOCK_LOCK(newso);
	mls_copy_effective(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
mls_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(newlabel);
	dest = SLOT(solabel);

	mls_copy(source, dest);
}

static void
mls_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(sopeerlabel);

	SOCK_LOCK(so);
	mls_copy_effective(source, dest);
	SOCK_UNLOCK(so);
}

static void
mls_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{
	struct mac_mls source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);

	dest = SLOT(newsopeerlabel);

	SOCK_LOCK(newso);
	mls_copy_effective(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
mls_syncache_create(struct label *label, struct inpcb *inp)
{
	struct mac_mls *source, *dest;

	source = SLOT(inp->inp_label);
	dest = SLOT(label);

	mls_copy_effective(source, dest);
}

static void
mls_syncache_create_mbuf(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(sc_label);
	dest = SLOT(mlabel);

	mls_copy_effective(source, dest);
}

static int
mls_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	if (vplabel == NULL)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj) ||
	    !mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj) ||
	    !mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj) ||
	    !mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static void
mls_sysvmsg_cleanup(struct label *msglabel)
{

	bzero(SLOT(msglabel), sizeof(struct mac_mls));
}

static void
mls_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel, struct msg *msgptr, struct label *msglabel)
{
	struct mac_mls *source, *dest;

	/* Ignore the msgq label. */
	source = SLOT(cred->cr_label);
	dest = SLOT(msglabel);

	mls_copy_effective(source, dest);
}

static int
mls_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_sysvmsq_check_msqget(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_sysvmsq_check_msqsnd(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_sysvmsq_check_msqrcv(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_sysvmsq_check_msqctl(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel, int cmd)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
		break;

	case IPC_STAT:
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static void
mls_sysvmsq_cleanup(struct label *msqlabel)
{

	bzero(SLOT(msqlabel), sizeof(struct mac_mls));
}

static void
mls_sysvmsq_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(msqlabel);

	mls_copy_effective(source, dest);
}

static int
mls_sysvsem_check_semctl(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, int cmd)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
	case SETVAL:
	case SETALL:
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
		break;

	case IPC_STAT:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
mls_sysvsem_check_semget(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_sysvsem_check_semop(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, size_t accesstype)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if( accesstype & SEM_R )
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);

	if( accesstype & SEM_A )
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);

	return (0);
}

static void
mls_sysvsem_cleanup(struct label *semalabel)
{

	bzero(SLOT(semalabel), sizeof(struct mac_mls));
}

static void
mls_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semalabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(semalabel);

	mls_copy_effective(source, dest);
}

static int
mls_sysvshm_check_shmat(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);
	if ((shmflg & SHM_RDONLY) == 0) {
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}
	
	return (0);
}

static int
mls_sysvshm_check_shmctl(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int cmd)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
mls_sysvshm_check_shmget(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
mls_sysvshm_cleanup(struct label *shmlabel)
{

	bzero(SLOT(shmlabel), sizeof(struct mac_mls));
}

static void
mls_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(shmlabel);

	mls_copy_effective(source, dest);
}

static int
mls_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls mm_temp, *source, *dest;
	int buflen, error;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	buflen = sizeof(mm_temp);
	bzero(&mm_temp, buflen);

	error = vn_extattr_get(vp, IO_NODELOCKED, MAC_MLS_EXTATTR_NAMESPACE,
	    MAC_MLS_EXTATTR_NAME, &buflen, (char *) &mm_temp, curthread);
	if (error == ENOATTR || error == EOPNOTSUPP) {
		/* Fall back to the mntlabel. */
		mls_copy_effective(source, dest);
		return (0);
	} else if (error)
		return (error);

	if (buflen != sizeof(mm_temp)) {
		printf("mls_vnode_associate_extattr: bad size %d\n", buflen);
		return (EPERM);
	}
	if (mls_valid(&mm_temp) != 0) {
		printf("mls_vnode_associate_extattr: invalid\n");
		return (EPERM);
	}
	if ((mm_temp.mm_flags & MAC_MLS_FLAGS_BOTH) !=
	    MAC_MLS_FLAG_EFFECTIVE) {
		printf("mls_associated_vnode_extattr: not effective\n");
		return (EPERM);
	}

	mls_copy_effective(&mm_temp, dest);
	return (0);
}

static void
mls_vnode_associate_singlelabel(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	mls_copy_effective(source, dest);
}

static int
mls_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{
	struct mac_mls *subj, *obj, *exec;
	int error;

	if (execlabel != NULL) {
		/*
		 * We currently don't permit labels to be changed at
		 * exec-time as part of MLS, so disallow non-NULL MLS label
		 * elements in the execlabel.
		 */
		exec = SLOT(execlabel);
		error = mls_atmostflags(exec, 0);
		if (error)
			return (error);
	}

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	obj = SLOT(vplabel);
	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{

	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{
	struct mac_mls *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
	}
	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mls_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	/* XXX privilege override for admin? */
	if (accmode & (VREAD | VEXEC | VSTAT_PERMS)) {
		if (!mls_dominate_effective(subj, obj))
			return (EACCES);
	}
	if (accmode & VMODIFY_PERMS) {
		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mls_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_mls *old, *new, *subj;
	int error;

	old = SLOT(vplabel);
	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);

	/*
	 * If there is an MLS label update for the vnode, it must be a
	 * effective label.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of the vnode (MLS label or not), MLS must
	 * authorize the relabel.
	 */
	if (!mls_effective_in_range(old, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_EFFECTIVE) {
		/*
		 * To change the MLS label on a vnode, the new vnode label
		 * must be in the subject range.
		 */
		if (!mls_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on the vnode to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (mls_contains_equal(new)) {
			error = mls_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mls_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	if (vp != NULL) {
		obj = SLOT(vplabel);

		if (!mls_dominate_effective(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mls_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	/* XXX: protect the MAC EA in a special way? */

	return (0);
}

static int
mls_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_mls *subj, *obj;

	if (!mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mls_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mls_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mplabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{
	struct mac_mls *source, *dest, mm_temp;
	size_t buflen;
	int error;

	buflen = sizeof(mm_temp);
	bzero(&mm_temp, buflen);

	source = SLOT(cred->cr_label);
	dest = SLOT(vplabel);
	mls_copy_effective(source, &mm_temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_MLS_EXTATTR_NAMESPACE,
	    MAC_MLS_EXTATTR_NAME, buflen, (char *) &mm_temp, curthread);
	if (error == 0)
		mls_copy_effective(source, dest);
	return (error);
}

static void
mls_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *label)
{
	struct mac_mls *source, *dest;

	source = SLOT(label);
	dest = SLOT(vplabel);

	mls_copy(source, dest);
}

static int
mls_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{
	struct mac_mls *source, mm_temp;
	size_t buflen;
	int error;

	buflen = sizeof(mm_temp);
	bzero(&mm_temp, buflen);

	source = SLOT(intlabel);
	if ((source->mm_flags & MAC_MLS_FLAG_EFFECTIVE) == 0)
		return (0);

	mls_copy_effective(source, &mm_temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_MLS_EXTATTR_NAMESPACE,
	    MAC_MLS_EXTATTR_NAME, buflen, (char *) &mm_temp, curthread);
	return (error);
}

static struct mac_policy_ops mls_ops =
{
	.mpo_init = mls_init,

	.mpo_bpfdesc_check_receive = mls_bpfdesc_check_receive,
	.mpo_bpfdesc_create = mls_bpfdesc_create,
	.mpo_bpfdesc_create_mbuf = mls_bpfdesc_create_mbuf,
	.mpo_bpfdesc_destroy_label = mls_destroy_label,
	.mpo_bpfdesc_init_label = mls_init_label,

	.mpo_cred_associate_nfsd = mls_cred_associate_nfsd,
	.mpo_cred_check_relabel = mls_cred_check_relabel,
	.mpo_cred_check_visible = mls_cred_check_visible,
	.mpo_cred_copy_label = mls_copy_label,
	.mpo_cred_create_init = mls_cred_create_init,
	.mpo_cred_create_swapper = mls_cred_create_swapper,
	.mpo_cred_destroy_label = mls_destroy_label,
	.mpo_cred_externalize_label = mls_externalize_label,
	.mpo_cred_init_label = mls_init_label,
	.mpo_cred_internalize_label = mls_internalize_label,
	.mpo_cred_relabel = mls_cred_relabel,

	.mpo_devfs_create_device = mls_devfs_create_device,
	.mpo_devfs_create_directory = mls_devfs_create_directory,
	.mpo_devfs_create_symlink = mls_devfs_create_symlink,
	.mpo_devfs_destroy_label = mls_destroy_label,
	.mpo_devfs_init_label = mls_init_label,
	.mpo_devfs_update = mls_devfs_update,
	.mpo_devfs_vnode_associate = mls_devfs_vnode_associate,

	.mpo_ifnet_check_relabel = mls_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = mls_ifnet_check_transmit,
	.mpo_ifnet_copy_label = mls_copy_label,
	.mpo_ifnet_create = mls_ifnet_create,
	.mpo_ifnet_create_mbuf = mls_ifnet_create_mbuf,
	.mpo_ifnet_destroy_label = mls_destroy_label,
	.mpo_ifnet_externalize_label = mls_externalize_label,
	.mpo_ifnet_init_label = mls_init_label,
	.mpo_ifnet_internalize_label = mls_internalize_label,
	.mpo_ifnet_relabel = mls_ifnet_relabel,

	.mpo_inpcb_check_deliver = mls_inpcb_check_deliver,
	.mpo_inpcb_check_visible = mls_inpcb_check_visible,
	.mpo_inpcb_create = mls_inpcb_create,
	.mpo_inpcb_create_mbuf = mls_inpcb_create_mbuf,
	.mpo_inpcb_destroy_label = mls_destroy_label,
	.mpo_inpcb_init_label = mls_init_label_waitcheck,
	.mpo_inpcb_sosetlabel = mls_inpcb_sosetlabel,

	.mpo_ip6q_create = mls_ip6q_create,
	.mpo_ip6q_destroy_label = mls_destroy_label,
	.mpo_ip6q_init_label = mls_init_label_waitcheck,
	.mpo_ip6q_match = mls_ip6q_match,
	.mpo_ip6q_reassemble = mls_ip6q_reassemble,
	.mpo_ip6q_update = mls_ip6q_update,

	.mpo_ipq_create = mls_ipq_create,
	.mpo_ipq_destroy_label = mls_destroy_label,
	.mpo_ipq_init_label = mls_init_label_waitcheck,
	.mpo_ipq_match = mls_ipq_match,
	.mpo_ipq_reassemble = mls_ipq_reassemble,
	.mpo_ipq_update = mls_ipq_update,

	.mpo_mbuf_copy_label = mls_copy_label,
	.mpo_mbuf_destroy_label = mls_destroy_label,
	.mpo_mbuf_init_label = mls_init_label_waitcheck,

	.mpo_mount_check_stat = mls_mount_check_stat,
	.mpo_mount_create = mls_mount_create,
	.mpo_mount_destroy_label = mls_destroy_label,
	.mpo_mount_init_label = mls_init_label,

	.mpo_netinet_arp_send = mls_netinet_arp_send,
	.mpo_netinet_firewall_reply = mls_netinet_firewall_reply,
	.mpo_netinet_firewall_send = mls_netinet_firewall_send,
	.mpo_netinet_fragment = mls_netinet_fragment,
	.mpo_netinet_icmp_reply = mls_netinet_icmp_reply,
	.mpo_netinet_igmp_send = mls_netinet_igmp_send,

	.mpo_netinet6_nd6_send = mls_netinet6_nd6_send,

	.mpo_pipe_check_ioctl = mls_pipe_check_ioctl,
	.mpo_pipe_check_poll = mls_pipe_check_poll,
	.mpo_pipe_check_read = mls_pipe_check_read,
	.mpo_pipe_check_relabel = mls_pipe_check_relabel,
	.mpo_pipe_check_stat = mls_pipe_check_stat,
	.mpo_pipe_check_write = mls_pipe_check_write,
	.mpo_pipe_copy_label = mls_copy_label,
	.mpo_pipe_create = mls_pipe_create,
	.mpo_pipe_destroy_label = mls_destroy_label,
	.mpo_pipe_externalize_label = mls_externalize_label,
	.mpo_pipe_init_label = mls_init_label,
	.mpo_pipe_internalize_label = mls_internalize_label,
	.mpo_pipe_relabel = mls_pipe_relabel,

	.mpo_posixsem_check_getvalue = mls_posixsem_check_rdonly,
	.mpo_posixsem_check_open = mls_posixsem_check_openunlink,
	.mpo_posixsem_check_post = mls_posixsem_check_write,
	.mpo_posixsem_check_setmode = mls_posixsem_check_setmode,
	.mpo_posixsem_check_setowner = mls_posixsem_check_setowner,
	.mpo_posixsem_check_stat = mls_posixsem_check_rdonly,
	.mpo_posixsem_check_unlink = mls_posixsem_check_openunlink,
	.mpo_posixsem_check_wait = mls_posixsem_check_write,
	.mpo_posixsem_create = mls_posixsem_create,
	.mpo_posixsem_destroy_label = mls_destroy_label,
	.mpo_posixsem_init_label = mls_init_label,

	.mpo_posixshm_check_mmap = mls_posixshm_check_mmap,
	.mpo_posixshm_check_open = mls_posixshm_check_open,
	.mpo_posixshm_check_read = mls_posixshm_check_read,
	.mpo_posixshm_check_setmode = mls_posixshm_check_setmode,
	.mpo_posixshm_check_setowner = mls_posixshm_check_setowner,
	.mpo_posixshm_check_stat = mls_posixshm_check_stat,
	.mpo_posixshm_check_truncate = mls_posixshm_check_truncate,
	.mpo_posixshm_check_unlink = mls_posixshm_check_unlink,
	.mpo_posixshm_check_write = mls_posixshm_check_write,
	.mpo_posixshm_create = mls_posixshm_create,
	.mpo_posixshm_destroy_label = mls_destroy_label,
	.mpo_posixshm_init_label = mls_init_label,

	.mpo_proc_check_debug = mls_proc_check_debug,
	.mpo_proc_check_sched = mls_proc_check_sched,
	.mpo_proc_check_signal = mls_proc_check_signal,

	.mpo_socket_check_deliver = mls_socket_check_deliver,
	.mpo_socket_check_relabel = mls_socket_check_relabel,
	.mpo_socket_check_visible = mls_socket_check_visible,
	.mpo_socket_copy_label = mls_copy_label,
	.mpo_socket_create = mls_socket_create,
	.mpo_socket_create_mbuf = mls_socket_create_mbuf,
	.mpo_socket_destroy_label = mls_destroy_label,
	.mpo_socket_externalize_label = mls_externalize_label,
	.mpo_socket_init_label = mls_init_label_waitcheck,
	.mpo_socket_internalize_label = mls_internalize_label,
	.mpo_socket_newconn = mls_socket_newconn,
	.mpo_socket_relabel = mls_socket_relabel,

	.mpo_socketpeer_destroy_label = mls_destroy_label,
	.mpo_socketpeer_externalize_label = mls_externalize_label,
	.mpo_socketpeer_init_label = mls_init_label_waitcheck,
	.mpo_socketpeer_set_from_mbuf = mls_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = mls_socketpeer_set_from_socket,

	.mpo_syncache_create = mls_syncache_create,
	.mpo_syncache_create_mbuf = mls_syncache_create_mbuf,
	.mpo_syncache_destroy_label = mls_destroy_label,
	.mpo_syncache_init_label = mls_init_label_waitcheck,

	.mpo_sysvmsg_cleanup = mls_sysvmsg_cleanup,
	.mpo_sysvmsg_create = mls_sysvmsg_create,
	.mpo_sysvmsg_destroy_label = mls_destroy_label,
	.mpo_sysvmsg_init_label = mls_init_label,

	.mpo_sysvmsq_check_msgrcv = mls_sysvmsq_check_msgrcv,
	.mpo_sysvmsq_check_msgrmid = mls_sysvmsq_check_msgrmid,
	.mpo_sysvmsq_check_msqget = mls_sysvmsq_check_msqget,
	.mpo_sysvmsq_check_msqsnd = mls_sysvmsq_check_msqsnd,
	.mpo_sysvmsq_check_msqrcv = mls_sysvmsq_check_msqrcv,
	.mpo_sysvmsq_check_msqctl = mls_sysvmsq_check_msqctl,
	.mpo_sysvmsq_cleanup = mls_sysvmsq_cleanup,
	.mpo_sysvmsq_destroy_label = mls_destroy_label,
	.mpo_sysvmsq_init_label = mls_init_label,
	.mpo_sysvmsq_create = mls_sysvmsq_create,

	.mpo_sysvsem_check_semctl = mls_sysvsem_check_semctl,
	.mpo_sysvsem_check_semget = mls_sysvsem_check_semget,
	.mpo_sysvsem_check_semop = mls_sysvsem_check_semop,
	.mpo_sysvsem_cleanup = mls_sysvsem_cleanup,
	.mpo_sysvsem_create = mls_sysvsem_create,
	.mpo_sysvsem_destroy_label = mls_destroy_label,
	.mpo_sysvsem_init_label = mls_init_label,

	.mpo_sysvshm_check_shmat = mls_sysvshm_check_shmat,
	.mpo_sysvshm_check_shmctl = mls_sysvshm_check_shmctl,
	.mpo_sysvshm_check_shmget = mls_sysvshm_check_shmget,
	.mpo_sysvshm_cleanup = mls_sysvshm_cleanup,
	.mpo_sysvshm_create = mls_sysvshm_create,
	.mpo_sysvshm_destroy_label = mls_destroy_label,
	.mpo_sysvshm_init_label = mls_init_label,


	.mpo_system_check_acct = mls_system_check_acct,
	.mpo_system_check_auditctl = mls_system_check_auditctl,
	.mpo_system_check_swapon = mls_system_check_swapon,

	.mpo_vnode_associate_extattr = mls_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = mls_vnode_associate_singlelabel,
	.mpo_vnode_check_access = mls_vnode_check_open,
	.mpo_vnode_check_chdir = mls_vnode_check_chdir,
	.mpo_vnode_check_chroot = mls_vnode_check_chroot,
	.mpo_vnode_check_create = mls_vnode_check_create,
	.mpo_vnode_check_deleteacl = mls_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = mls_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = mls_vnode_check_exec,
	.mpo_vnode_check_getacl = mls_vnode_check_getacl,
	.mpo_vnode_check_getextattr = mls_vnode_check_getextattr,
	.mpo_vnode_check_link = mls_vnode_check_link,
	.mpo_vnode_check_listextattr = mls_vnode_check_listextattr,
	.mpo_vnode_check_lookup = mls_vnode_check_lookup,
	.mpo_vnode_check_mmap = mls_vnode_check_mmap,
	.mpo_vnode_check_open = mls_vnode_check_open,
	.mpo_vnode_check_poll = mls_vnode_check_poll,
	.mpo_vnode_check_read = mls_vnode_check_read,
	.mpo_vnode_check_readdir = mls_vnode_check_readdir,
	.mpo_vnode_check_readlink = mls_vnode_check_readlink,
	.mpo_vnode_check_relabel = mls_vnode_check_relabel,
	.mpo_vnode_check_rename_from = mls_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = mls_vnode_check_rename_to,
	.mpo_vnode_check_revoke = mls_vnode_check_revoke,
	.mpo_vnode_check_setacl = mls_vnode_check_setacl,
	.mpo_vnode_check_setextattr = mls_vnode_check_setextattr,
	.mpo_vnode_check_setflags = mls_vnode_check_setflags,
	.mpo_vnode_check_setmode = mls_vnode_check_setmode,
	.mpo_vnode_check_setowner = mls_vnode_check_setowner,
	.mpo_vnode_check_setutimes = mls_vnode_check_setutimes,
	.mpo_vnode_check_stat = mls_vnode_check_stat,
	.mpo_vnode_check_unlink = mls_vnode_check_unlink,
	.mpo_vnode_check_write = mls_vnode_check_write,
	.mpo_vnode_copy_label = mls_copy_label,
	.mpo_vnode_create_extattr = mls_vnode_create_extattr,
	.mpo_vnode_destroy_label = mls_destroy_label,
	.mpo_vnode_externalize_label = mls_externalize_label,
	.mpo_vnode_init_label = mls_init_label,
	.mpo_vnode_internalize_label = mls_internalize_label,
	.mpo_vnode_relabel = mls_vnode_relabel,
	.mpo_vnode_setlabel_extattr = mls_vnode_setlabel_extattr,
};

MAC_POLICY_SET(&mls_ops, mac_mls, "TrustedBSD MAC/MLS",
    MPC_LOADTIME_FLAG_NOTLATE, &mls_slot);
