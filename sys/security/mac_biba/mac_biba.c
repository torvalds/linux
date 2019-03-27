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
 * Biba fixed label mandatory integrity policy.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/extattr.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
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
#include <security/mac_biba/mac_biba.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, biba, CTLFLAG_RW, 0,
    "TrustedBSD mac_biba policy controls");

static int	biba_label_size = sizeof(struct mac_biba);
SYSCTL_INT(_security_mac_biba, OID_AUTO, label_size, CTLFLAG_RD,
    &biba_label_size, 0, "Size of struct mac_biba");

static int	biba_enabled = 1;
SYSCTL_INT(_security_mac_biba, OID_AUTO, enabled, CTLFLAG_RWTUN, &biba_enabled,
    0, "Enforce MAC/Biba policy");

static int	destroyed_not_inited;
SYSCTL_INT(_security_mac_biba, OID_AUTO, destroyed_not_inited, CTLFLAG_RD,
    &destroyed_not_inited, 0, "Count of labels destroyed but not inited");

static int	trust_all_interfaces = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, trust_all_interfaces, CTLFLAG_RDTUN,
    &trust_all_interfaces, 0, "Consider all interfaces 'trusted' by MAC/Biba");

static char	trusted_interfaces[128];
SYSCTL_STRING(_security_mac_biba, OID_AUTO, trusted_interfaces, CTLFLAG_RDTUN,
    trusted_interfaces, 0, "Interfaces considered 'trusted' by MAC/Biba");

static int	max_compartments = MAC_BIBA_MAX_COMPARTMENTS;
SYSCTL_INT(_security_mac_biba, OID_AUTO, max_compartments, CTLFLAG_RD,
    &max_compartments, 0, "Maximum supported compartments");

static int	ptys_equal = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, ptys_equal, CTLFLAG_RWTUN, &ptys_equal,
    0, "Label pty devices as biba/equal on create");

static int	interfaces_equal = 1;
SYSCTL_INT(_security_mac_biba, OID_AUTO, interfaces_equal, CTLFLAG_RWTUN,
    &interfaces_equal, 0, "Label network interfaces as biba/equal on create");

static int	revocation_enabled = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, revocation_enabled, CTLFLAG_RWTUN,
    &revocation_enabled, 0, "Revoke access to objects on relabel");

static int	biba_slot;
#define	SLOT(l)	((struct mac_biba *)mac_label_get((l), biba_slot))
#define	SLOT_SET(l, val) mac_label_set((l), biba_slot, (uintptr_t)(val))

static uma_zone_t	zone_biba;

static __inline int
biba_bit_set_empty(u_char *set) {
	int i;

	for (i = 0; i < MAC_BIBA_MAX_COMPARTMENTS >> 3; i++)
		if (set[i] != 0)
			return (0);
	return (1);
}

static struct mac_biba *
biba_alloc(int flag)
{

	return (uma_zalloc(zone_biba, flag | M_ZERO));
}

static void
biba_free(struct mac_biba *mb)
{

	if (mb != NULL)
		uma_zfree(zone_biba, mb);
	else
		atomic_add_int(&destroyed_not_inited, 1);
}

static int
biba_atmostflags(struct mac_biba *mb, int flags)
{

	if ((mb->mb_flags & flags) != mb->mb_flags)
		return (EINVAL);
	return (0);
}

static int
biba_dominate_element(struct mac_biba_element *a, struct mac_biba_element *b)
{
	int bit;

	switch (a->mbe_type) {
	case MAC_BIBA_TYPE_EQUAL:
	case MAC_BIBA_TYPE_HIGH:
		return (1);

	case MAC_BIBA_TYPE_LOW:
		switch (b->mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
		case MAC_BIBA_TYPE_HIGH:
			return (0);

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_LOW:
			return (1);

		default:
			panic("biba_dominate_element: b->mbe_type invalid");
		}

	case MAC_BIBA_TYPE_GRADE:
		switch (b->mbe_type) {
		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_LOW:
			return (1);

		case MAC_BIBA_TYPE_HIGH:
			return (0);

		case MAC_BIBA_TYPE_GRADE:
			for (bit = 1; bit <= MAC_BIBA_MAX_COMPARTMENTS; bit++)
				if (!MAC_BIBA_BIT_TEST(bit,
				    a->mbe_compartments) &&
				    MAC_BIBA_BIT_TEST(bit, b->mbe_compartments))
					return (0);
			return (a->mbe_grade >= b->mbe_grade);

		default:
			panic("biba_dominate_element: b->mbe_type invalid");
		}

	default:
		panic("biba_dominate_element: a->mbe_type invalid");
	}

	return (0);
}

static int
biba_subject_dominate_high(struct mac_biba *mb)
{
	struct mac_biba_element *element;

	KASSERT((mb->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_effective_in_range: mb not effective"));
	element = &mb->mb_effective;

	return (element->mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    element->mbe_type == MAC_BIBA_TYPE_HIGH);
}

static int
biba_range_in_range(struct mac_biba *rangea, struct mac_biba *rangeb)
{

	return (biba_dominate_element(&rangeb->mb_rangehigh,
	    &rangea->mb_rangehigh) &&
	    biba_dominate_element(&rangea->mb_rangelow,
	    &rangeb->mb_rangelow));
}

static int
biba_effective_in_range(struct mac_biba *effective, struct mac_biba *range)
{

	KASSERT((effective->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_effective_in_range: a not effective"));
	KASSERT((range->mb_flags & MAC_BIBA_FLAG_RANGE) != 0,
	    ("biba_effective_in_range: b not range"));

	return (biba_dominate_element(&range->mb_rangehigh,
	    &effective->mb_effective) &&
	    biba_dominate_element(&effective->mb_effective,
	    &range->mb_rangelow));

	return (1);
}

static int
biba_dominate_effective(struct mac_biba *a, struct mac_biba *b)
{
	KASSERT((a->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_dominate_effective: a not effective"));
	KASSERT((b->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_dominate_effective: b not effective"));

	return (biba_dominate_element(&a->mb_effective, &b->mb_effective));
}

static int
biba_equal_element(struct mac_biba_element *a, struct mac_biba_element *b)
{

	if (a->mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    b->mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (1);

	return (a->mbe_type == b->mbe_type && a->mbe_grade == b->mbe_grade);
}

static int
biba_equal_effective(struct mac_biba *a, struct mac_biba *b)
{

	KASSERT((a->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_equal_effective: a not effective"));
	KASSERT((b->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_equal_effective: b not effective"));

	return (biba_equal_element(&a->mb_effective, &b->mb_effective));
}

static int
biba_contains_equal(struct mac_biba *mb)
{

	if (mb->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		if (mb->mb_effective.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);
	}

	if (mb->mb_flags & MAC_BIBA_FLAG_RANGE) {
		if (mb->mb_rangelow.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);
		if (mb->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);
	}

	return (0);
}

static int
biba_subject_privileged(struct mac_biba *mb)
{

	KASSERT((mb->mb_flags & MAC_BIBA_FLAGS_BOTH) == MAC_BIBA_FLAGS_BOTH,
	    ("biba_subject_privileged: subject doesn't have both labels"));

	/* If the effective is EQUAL, it's ok. */
	if (mb->mb_effective.mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (0);

	/* If either range endpoint is EQUAL, it's ok. */
	if (mb->mb_rangelow.mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    mb->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (0);

	/* If the range is low-high, it's ok. */
	if (mb->mb_rangelow.mbe_type == MAC_BIBA_TYPE_LOW &&
	    mb->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_HIGH)
		return (0);

	/* It's not ok. */
	return (EPERM);
}

static int
biba_high_effective(struct mac_biba *mb)
{

	KASSERT((mb->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_equal_effective: mb not effective"));

	return (mb->mb_effective.mbe_type == MAC_BIBA_TYPE_HIGH);
}

static int
biba_valid(struct mac_biba *mb)
{

	if (mb->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		switch (mb->mb_effective.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mb->mb_effective.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mb->mb_effective.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (mb->mb_effective.mbe_type != MAC_BIBA_TYPE_UNDEF)
			return (EINVAL);
	}

	if (mb->mb_flags & MAC_BIBA_FLAG_RANGE) {
		switch (mb->mb_rangelow.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mb->mb_rangelow.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mb->mb_rangelow.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}

		switch (mb->mb_rangehigh.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mb->mb_rangehigh.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mb->mb_rangehigh.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
		if (!biba_dominate_element(&mb->mb_rangehigh,
		    &mb->mb_rangelow))
			return (EINVAL);
	} else {
		if (mb->mb_rangelow.mbe_type != MAC_BIBA_TYPE_UNDEF ||
		    mb->mb_rangehigh.mbe_type != MAC_BIBA_TYPE_UNDEF)
			return (EINVAL);
	}

	return (0);
}

static void
biba_set_range(struct mac_biba *mb, u_short typelow, u_short gradelow,
    u_char *compartmentslow, u_short typehigh, u_short gradehigh,
    u_char *compartmentshigh)
{

	mb->mb_rangelow.mbe_type = typelow;
	mb->mb_rangelow.mbe_grade = gradelow;
	if (compartmentslow != NULL)
		memcpy(mb->mb_rangelow.mbe_compartments, compartmentslow,
		    sizeof(mb->mb_rangelow.mbe_compartments));
	mb->mb_rangehigh.mbe_type = typehigh;
	mb->mb_rangehigh.mbe_grade = gradehigh;
	if (compartmentshigh != NULL)
		memcpy(mb->mb_rangehigh.mbe_compartments, compartmentshigh,
		    sizeof(mb->mb_rangehigh.mbe_compartments));
	mb->mb_flags |= MAC_BIBA_FLAG_RANGE;
}

static void
biba_set_effective(struct mac_biba *mb, u_short type, u_short grade,
    u_char *compartments)
{

	mb->mb_effective.mbe_type = type;
	mb->mb_effective.mbe_grade = grade;
	if (compartments != NULL)
		memcpy(mb->mb_effective.mbe_compartments, compartments,
		    sizeof(mb->mb_effective.mbe_compartments));
	mb->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
}

static void
biba_copy_range(struct mac_biba *labelfrom, struct mac_biba *labelto)
{

	KASSERT((labelfrom->mb_flags & MAC_BIBA_FLAG_RANGE) != 0,
	    ("biba_copy_range: labelfrom not range"));

	labelto->mb_rangelow = labelfrom->mb_rangelow;
	labelto->mb_rangehigh = labelfrom->mb_rangehigh;
	labelto->mb_flags |= MAC_BIBA_FLAG_RANGE;
}

static void
biba_copy_effective(struct mac_biba *labelfrom, struct mac_biba *labelto)
{

	KASSERT((labelfrom->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("biba_copy_effective: labelfrom not effective"));

	labelto->mb_effective = labelfrom->mb_effective;
	labelto->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
}

static void
biba_copy(struct mac_biba *source, struct mac_biba *dest)
{

	if (source->mb_flags & MAC_BIBA_FLAG_EFFECTIVE)
		biba_copy_effective(source, dest);
	if (source->mb_flags & MAC_BIBA_FLAG_RANGE)
		biba_copy_range(source, dest);
}

/*
 * Policy module operations.
 */
static void
biba_init(struct mac_policy_conf *conf)
{

	zone_biba = uma_zcreate("mac_biba", sizeof(struct mac_biba), NULL,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
}

/*
 * Label operations.
 */
static void
biba_init_label(struct label *label)
{

	SLOT_SET(label, biba_alloc(M_WAITOK));
}

static int
biba_init_label_waitcheck(struct label *label, int flag)
{

	SLOT_SET(label, biba_alloc(flag));
	if (SLOT(label) == NULL)
		return (ENOMEM);

	return (0);
}

static void
biba_destroy_label(struct label *label)
{

	biba_free(SLOT(label));
	SLOT_SET(label, NULL);
}

/*
 * biba_element_to_string() accepts an sbuf and Biba element.  It converts
 * the Biba element to a string and stores the result in the sbuf; if there
 * isn't space in the sbuf, -1 is returned.
 */
static int
biba_element_to_string(struct sbuf *sb, struct mac_biba_element *element)
{
	int i, first;

	switch (element->mbe_type) {
	case MAC_BIBA_TYPE_HIGH:
		return (sbuf_printf(sb, "high"));

	case MAC_BIBA_TYPE_LOW:
		return (sbuf_printf(sb, "low"));

	case MAC_BIBA_TYPE_EQUAL:
		return (sbuf_printf(sb, "equal"));

	case MAC_BIBA_TYPE_GRADE:
		if (sbuf_printf(sb, "%d", element->mbe_grade) == -1)
			return (-1);

		first = 1;
		for (i = 1; i <= MAC_BIBA_MAX_COMPARTMENTS; i++) {
			if (MAC_BIBA_BIT_TEST(i, element->mbe_compartments)) {
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
		panic("biba_element_to_string: invalid type (%d)",
		    element->mbe_type);
	}
}

/*
 * biba_to_string() converts a Biba label to a string, and places the results
 * in the passed sbuf.  It returns 0 on success, or EINVAL if there isn't
 * room in the sbuf.  Note: the sbuf will be modified even in a failure case,
 * so the caller may need to revert the sbuf by restoring the offset if
 * that's undesired.
 */
static int
biba_to_string(struct sbuf *sb, struct mac_biba *mb)
{

	if (mb->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		if (biba_element_to_string(sb, &mb->mb_effective) == -1)
			return (EINVAL);
	}

	if (mb->mb_flags & MAC_BIBA_FLAG_RANGE) {
		if (sbuf_putc(sb, '(') == -1)
			return (EINVAL);

		if (biba_element_to_string(sb, &mb->mb_rangelow) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, '-') == -1)
			return (EINVAL);

		if (biba_element_to_string(sb, &mb->mb_rangehigh) == -1)
			return (EINVAL);

		if (sbuf_putc(sb, ')') == -1)
			return (EINVAL);
	}

	return (0);
}

static int
biba_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{
	struct mac_biba *mb;

	if (strcmp(MAC_BIBA_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	mb = SLOT(label);
	return (biba_to_string(sb, mb));
}

static int
biba_parse_element(struct mac_biba_element *element, char *string)
{
	char *compartment, *end, *grade;
	int value;

	if (strcmp(string, "high") == 0 || strcmp(string, "hi") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_HIGH;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else if (strcmp(string, "low") == 0 || strcmp(string, "lo") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_LOW;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else if (strcmp(string, "equal") == 0 ||
	    strcmp(string, "eq") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_EQUAL;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else {
		element->mbe_type = MAC_BIBA_TYPE_GRADE;

		/*
		 * Numeric grade piece of the element.
		 */
		grade = strsep(&string, ":");
		value = strtol(grade, &end, 10);
		if (end == grade || *end != '\0')
			return (EINVAL);
		if (value < 0 || value > 65535)
			return (EINVAL);
		element->mbe_grade = value;

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
			if (value < 1 || value > MAC_BIBA_MAX_COMPARTMENTS)
				return (EINVAL);
			MAC_BIBA_BIT_SET(value, element->mbe_compartments);
		}
	}

	return (0);
}

/*
 * Note: destructively consumes the string, make a local copy before calling
 * if that's a problem.
 */
static int
biba_parse(struct mac_biba *mb, char *string)
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
	    ("biba_parse: range mismatch"));

	bzero(mb, sizeof(*mb));
	if (effective != NULL) {
		error = biba_parse_element(&mb->mb_effective, effective);
		if (error)
			return (error);
		mb->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
	}

	if (rangelow != NULL) {
		error = biba_parse_element(&mb->mb_rangelow, rangelow);
		if (error)
			return (error);
		error = biba_parse_element(&mb->mb_rangehigh, rangehigh);
		if (error)
			return (error);
		mb->mb_flags |= MAC_BIBA_FLAG_RANGE;
	}

	error = biba_valid(mb);
	if (error)
		return (error);

	return (0);
}

static int
biba_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{
	struct mac_biba *mb, mb_temp;
	int error;

	if (strcmp(MAC_BIBA_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	error = biba_parse(&mb_temp, element_data);
	if (error)
		return (error);

	mb = SLOT(label);
	*mb = mb_temp;

	return (0);
}

static void
biba_copy_label(struct label *src, struct label *dest)
{

	*SLOT(dest) = *SLOT(src);
}

/*
 * Object-specific entry point implementations are sorted alphabetically by
 * object type name and then by operation.
 */
static int
biba_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{
	struct mac_biba *a, *b;

	if (!biba_enabled)
		return (0);

	a = SLOT(dlabel);
	b = SLOT(ifplabel);

	if (biba_equal_effective(a, b))
		return (0);
	return (EACCES);
}

static void
biba_bpfdesc_create(struct ucred *cred, struct bpf_d *d,
    struct label *dlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(dlabel);

	biba_copy_effective(source, dest);
}

static void
biba_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(dlabel);
	dest = SLOT(mlabel);

	biba_copy_effective(source, dest);
}

static void
biba_cred_associate_nfsd(struct ucred *cred)
{
	struct mac_biba *label;

	label = SLOT(cred->cr_label);
	biba_set_effective(label, MAC_BIBA_TYPE_LOW, 0, NULL);
	biba_set_range(label, MAC_BIBA_TYPE_LOW, 0, NULL, MAC_BIBA_TYPE_HIGH,
	    0, NULL);
}

static int
biba_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_biba *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a Biba label update for the credential, it may
	 * be an update of the effective, range, or both.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAGS_BOTH) {
		/*
		 * If the change request modifies both the Biba label
		 * effective and range, check that the new effective will be
		 * in the new range.
		 */
		if ((new->mb_flags & MAC_BIBA_FLAGS_BOTH) ==
		    MAC_BIBA_FLAGS_BOTH &&
		    !biba_effective_in_range(new, new))
			return (EINVAL);

		/*
		 * To change the Biba effective label on a credential, the
		 * new effective label must be in the current range.
		 */
		if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE &&
		    !biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba range on a credential, the new range
		 * label must be in the current range.
		 */
		if (new->mb_flags & MAC_BIBA_FLAG_RANGE &&
		    !biba_range_in_range(new, subj))
			return (EPERM);

		/*
		 * To have EQUAL in any component of the new credential Biba
		 * label, the subject must already have EQUAL in their label.
		 */
		if (biba_contains_equal(new)) {
			error = biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
biba_cred_check_visible(struct ucred *u1, struct ucred *u2)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(u1->cr_label);
	obj = SLOT(u2->cr_label);

	/* XXX: range */
	if (!biba_dominate_effective(obj, subj))
		return (ESRCH);

	return (0);
}

static void
biba_cred_create_init(struct ucred *cred)
{
	struct mac_biba *dest;

	dest = SLOT(cred->cr_label);

	biba_set_effective(dest, MAC_BIBA_TYPE_HIGH, 0, NULL);
	biba_set_range(dest, MAC_BIBA_TYPE_LOW, 0, NULL, MAC_BIBA_TYPE_HIGH,
	    0, NULL);
}

static void
biba_cred_create_swapper(struct ucred *cred)
{
	struct mac_biba *dest;

	dest = SLOT(cred->cr_label);

	biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
	biba_set_range(dest, MAC_BIBA_TYPE_LOW, 0, NULL, MAC_BIBA_TYPE_HIGH,
	    0, NULL);
}

static void
biba_cred_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(cred->cr_label);

	biba_copy(source, dest);
}

static void
biba_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_biba *mb;
	const char *dn;
	int biba_type;

	mb = SLOT(delabel);
	dn = devtoname(dev);
	if (strcmp(dn, "null") == 0 ||
	    strcmp(dn, "zero") == 0 ||
	    strcmp(dn, "random") == 0 ||
	    strncmp(dn, "fd/", strlen("fd/")) == 0)
		biba_type = MAC_BIBA_TYPE_EQUAL;
	else if (ptys_equal &&
	    (strncmp(dn, "ttyp", strlen("ttyp")) == 0 ||
	    strncmp(dn, "pts/", strlen("pts/")) == 0 ||
	    strncmp(dn, "ptyp", strlen("ptyp")) == 0))
		biba_type = MAC_BIBA_TYPE_EQUAL;
	else
		biba_type = MAC_BIBA_TYPE_HIGH;
	biba_set_effective(mb, biba_type, 0, NULL);
}

static void
biba_devfs_create_directory(struct mount *mp, char *dirname, int dirnamelen,
    struct devfs_dirent *de, struct label *delabel)
{
	struct mac_biba *mb;

	mb = SLOT(delabel);

	biba_set_effective(mb, MAC_BIBA_TYPE_HIGH, 0, NULL);
}

static void
biba_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(delabel);

	biba_copy_effective(source, dest);
}

static void
biba_devfs_update(struct mount *mp, struct devfs_dirent *de,
    struct label *delabel, struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(vplabel);
	dest = SLOT(delabel);

	biba_copy(source, dest);
}

static void
biba_devfs_vnode_associate(struct mount *mp, struct label *mntlabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(delabel);
	dest = SLOT(vplabel);

	biba_copy_effective(source, dest);
}

static int
biba_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_biba *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a Biba label update for the interface, it may be an
	 * update of the effective, range, or both.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * Relabling network interfaces requires Biba privilege.
	 */
	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
biba_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *i;

	if (!biba_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(ifplabel);

	return (biba_effective_in_range(p, i) ? 0 : EACCES);
}

static void
biba_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{
	char tifname[IFNAMSIZ], *p, *q;
	char tiflist[sizeof(trusted_interfaces)];
	struct mac_biba *dest;
	int len, type;

	dest = SLOT(ifplabel);

	if (ifp->if_type == IFT_LOOP || interfaces_equal != 0) {
		type = MAC_BIBA_TYPE_EQUAL;
		goto set;
	}

	if (trust_all_interfaces) {
		type = MAC_BIBA_TYPE_HIGH;
		goto set;
	}

	type = MAC_BIBA_TYPE_LOW;

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
					type = MAC_BIBA_TYPE_HIGH;
					break;
				}
			} else {
				*p = '\0';
				printf("mac_biba warning: interface name "
				    "\"%s\" is too long (must be < %d)\n",
				    q, IFNAMSIZ);
			}
			if (*p == '\0')
				break;
			q = p + 1;
		}
	}
set:
	biba_set_effective(dest, type, 0, NULL);
	biba_set_range(dest, type, 0, NULL, type, 0, NULL);
}

static void
biba_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(ifplabel);
	dest = SLOT(mlabel);

	biba_copy_effective(source, dest);
}

static void
biba_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(ifplabel);

	biba_copy(source, dest);
}

static int
biba_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *i;

	if (!biba_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(inplabel);

	return (biba_equal_effective(p, i) ? 0 : EACCES);
}

static int
biba_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
    struct label *inplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(inplabel);

	if (!biba_dominate_effective(obj, subj))
		return (ENOENT);

	return (0);
}

static void
biba_inpcb_create(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	SOCK_LOCK(so);
	biba_copy_effective(source, dest);
	SOCK_UNLOCK(so);
}

static void
biba_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(inplabel);
	dest = SLOT(mlabel);

	biba_copy_effective(source, dest);
}

static void
biba_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_biba *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	biba_copy(source, dest);
}

static void
biba_ip6q_create(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(q6label);

	biba_copy_effective(source, dest);
}

static int
biba_ip6q_match(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{
	struct mac_biba *a, *b;

	a = SLOT(q6label);
	b = SLOT(mlabel);

	return (biba_equal_effective(a, b));
}

static void
biba_ip6q_reassemble(struct ip6q *q6, struct label *q6label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(q6label);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	biba_copy_effective(source, dest);
}

static void
biba_ip6q_update(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static void
biba_ipq_create(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(qlabel);

	biba_copy_effective(source, dest);
}

static int
biba_ipq_match(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{
	struct mac_biba *a, *b;

	a = SLOT(qlabel);
	b = SLOT(mlabel);

	return (biba_equal_effective(a, b));
}

static void
biba_ipq_reassemble(struct ipq *q, struct label *qlabel, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(qlabel);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	biba_copy_effective(source, dest);
}

static void
biba_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static int
biba_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	obj = SLOT(vplabel);
	if (!biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
biba_mount_check_stat(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(mplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
biba_mount_create(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(mplabel);

	biba_copy_effective(source, dest);
}

static void
biba_netinet_arp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *dest;

	dest = SLOT(mlabel);

	biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

static void
biba_netinet_firewall_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	biba_copy_effective(source, dest);
}

static void
biba_netinet_firewall_send(struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *dest;

	dest = SLOT(mlabel);

	/* XXX: where is the label for the firewall really coming from? */
	biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

static void
biba_netinet_fragment(struct mbuf *m, struct label *mlabel,
    struct mbuf *frag, struct label *fraglabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(fraglabel);

	biba_copy_effective(source, dest);
}

static void
biba_netinet_icmp_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mrecvlabel);
	dest = SLOT(msendlabel);

	biba_copy_effective(source, dest);
}

static void
biba_netinet_igmp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *dest;

	dest = SLOT(mlabel);

	biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

static void
biba_netinet6_nd6_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *dest;

	dest = SLOT(mlabel);

	biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

static int
biba_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	if(!biba_enabled)
		return (0);

	/* XXX: This will be implemented soon... */

	return (0);
}

static int
biba_pipe_check_poll(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_biba *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	/*
	 * If there is a Biba label update for a pipe, it must be a effective
	 * update.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of a pipe (Biba label or not), Biba must
	 * authorize the relabel.
	 */
	if (!biba_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To change the Biba label on a pipe, the new pipe label
		 * must be in the subject range.
		 */
		if (!biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on a pipe to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (biba_contains_equal(new)) {
			error = biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
biba_pipe_check_stat(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static void
biba_pipe_create(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(pplabel);

	biba_copy_effective(source, dest);
}

static void
biba_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(pplabel);

	biba_copy(source, dest);
}

static int
biba_posixsem_check_openunlink(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixsem_check_setmode(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, mode_t mode)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixsem_check_setowner(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, uid_t uid, gid_t gid)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixsem_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(kslabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixsem_check_rdonly(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(kslabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
biba_posixsem_create(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(kslabel);

	biba_copy_effective(source, dest);
}

static int
biba_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, int prot, int flags)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
biba_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, accmode_t accmode)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (accmode & (VREAD | VEXEC | VSTAT_PERMS)) {
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (accmode & VMODIFY_PERMS) {
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
biba_posixshm_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *vp, struct label *shmlabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, mode_t mode)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, uid_t uid, gid_t gid)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixshm_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd, struct label *shmlabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_posixshm_check_truncate(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shmfd, struct label *shmlabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);
    
	return (0);
}

static int
biba_posixshm_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *vp, struct label *shmlabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(shmlabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
biba_posixshm_create(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(shmlabel);

	biba_copy_effective(source, dest);
}

/*
 * Some system privileges are allowed regardless of integrity grade; others
 * are allowed only when running with privilege with respect to the Biba
 * policy as they might otherwise allow bypassing of the integrity policy.
 */
static int
biba_priv_check(struct ucred *cred, int priv)
{
	struct mac_biba *subj;
	int error;

	if (!biba_enabled)
		return (0);

	/*
	 * Exempt only specific privileges from the Biba integrity policy.
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
	 * holds privilege with respect to the Biba policy.
	 */
	default:
		subj = SLOT(cred->cr_label);
		error = biba_subject_privileged(subj);
		if (error)
			return (error);
	}
	return (0);
}

static int
biba_proc_check_debug(struct ucred *cred, struct proc *p)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_proc_check_sched(struct ucred *cred, struct proc *p)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *s;
	int error;

	if (!biba_enabled)
		return (0);

	p = SLOT(mlabel);
	s = SLOT(solabel);

	SOCK_LOCK(so);
	error = biba_equal_effective(p, s) ? 0 : EACCES;
	SOCK_UNLOCK(so);
	return (error);
}

static int
biba_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_biba *subj, *obj, *new;
	int error;

	SOCK_LOCK_ASSERT(so);

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	/*
	 * If there is a Biba label update for the socket, it may be an
	 * update of effective.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To relabel a socket, the old socket effective must be in the
	 * subject range.
	 */
	if (!biba_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To relabel a socket, the new socket effective must be in
		 * the subject range.
		 */
		if (!biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on the socket to contain EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (biba_contains_equal(new)) {
			error = biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
biba_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	SOCK_LOCK(so);
	if (!biba_dominate_effective(obj, subj)) {
		SOCK_UNLOCK(so);
		return (ENOENT);
	}
	SOCK_UNLOCK(so);

	return (0);
}

static void
biba_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(solabel);

	biba_copy_effective(source, dest);
}

static void
biba_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(mlabel);

	SOCK_LOCK(so);
	biba_copy_effective(source, dest);
	SOCK_UNLOCK(so);
}

static void
biba_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{
	struct mac_biba source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);

	dest = SLOT(newsolabel);

	SOCK_LOCK(newso);
	biba_copy_effective(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
biba_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	SOCK_LOCK_ASSERT(so);

	source = SLOT(newlabel);
	dest = SLOT(solabel);

	biba_copy(source, dest);
}

static void
biba_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(sopeerlabel);

	SOCK_LOCK(so);
	biba_copy_effective(source, dest);
	SOCK_UNLOCK(so);
}

static void
biba_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{
	struct mac_biba source, *dest;

	SOCK_LOCK(oldso);
	source = *SLOT(oldsolabel);
	SOCK_UNLOCK(oldso);
	dest = SLOT(newsopeerlabel);

	SOCK_LOCK(newso);
	biba_copy_effective(&source, dest);
	SOCK_UNLOCK(newso);
}

static void
biba_syncache_create(struct label *label, struct inpcb *inp)
{
	struct mac_biba *source, *dest;

	source = SLOT(inp->inp_label);
	dest = SLOT(label);
	biba_copy_effective(source, dest);
}

static void
biba_syncache_create_mbuf(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(sc_label);
	dest = SLOT(mlabel);
	biba_copy_effective(source, dest);
}

static int
biba_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	if (vplabel == NULL)
		return (0);

	obj = SLOT(vplabel);
	if (!biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
biba_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	if (vplabel == NULL)
		return (0);

	obj = SLOT(vplabel);
	if (!biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
biba_system_check_auditon(struct ucred *cred, int cmd)
{
	struct mac_biba *subj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
biba_system_check_swapoff(struct ucred *cred, struct vnode *vp,
    struct label *label)
{
	struct mac_biba *subj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
biba_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	error = biba_subject_privileged(subj);
	if (error)
		return (error);

	if (!biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
biba_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
    void *arg1, int arg2, struct sysctl_req *req)
{
	struct mac_biba *subj;
	int error;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	/*
	 * Treat sysctl variables without CTLFLAG_ANYBODY flag as biba/high,
	 * but also require privilege to change them.
	 */
	if (req->newptr != NULL && (oidp->oid_kind & CTLFLAG_ANYBODY) == 0) {
		if (!biba_subject_dominate_high(subj))
			return (EACCES);

		error = biba_subject_privileged(subj);
		if (error)
			return (error);
	}

	return (0);
}

static void
biba_sysvmsg_cleanup(struct label *msglabel)
{

	bzero(SLOT(msglabel), sizeof(struct mac_biba));
}

static void
biba_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel, struct msg *msgptr, struct label *msglabel)
{
	struct mac_biba *source, *dest;

	/* Ignore the msgq label */
	source = SLOT(cred->cr_label);
	dest = SLOT(msglabel);

	biba_copy_effective(source, dest);
}

static int
biba_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_sysvmsq_check_msqget(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_sysvmsq_check_msqsnd(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_sysvmsq_check_msqrcv(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_sysvmsq_check_msqctl(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static void
biba_sysvmsq_cleanup(struct label *msqlabel)
{

	bzero(SLOT(msqlabel), sizeof(struct mac_biba));
}

static void
biba_sysvmsq_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(msqlabel);

	biba_copy_effective(source, dest);
}

static int
biba_sysvsem_check_semctl(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
	case SETVAL:
	case SETALL:
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
biba_sysvsem_check_semget(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_sysvsem_check_semop(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, size_t accesstype)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if (accesstype & SEM_R)
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);

	if (accesstype & SEM_A)
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);

	return (0);
}

static void
biba_sysvsem_cleanup(struct label *semalabel)
{

	bzero(SLOT(semalabel), sizeof(struct mac_biba));
}

static void
biba_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semalabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(semalabel);

	biba_copy_effective(source, dest);
}

static int
biba_sysvshm_check_shmat(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);
	if ((shmflg & SHM_RDONLY) == 0) {
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}
	
	return (0);
}

static int
biba_sysvshm_check_shmctl(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
biba_sysvshm_check_shmget(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static void
biba_sysvshm_cleanup(struct label *shmlabel)
{

	bzero(SLOT(shmlabel), sizeof(struct mac_biba));
}

static void
biba_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(shmlabel);

	biba_copy_effective(source, dest);
}

static int
biba_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba mb_temp, *source, *dest;
	int buflen, error;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	buflen = sizeof(mb_temp);
	bzero(&mb_temp, buflen);

	error = vn_extattr_get(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, &buflen, (char *) &mb_temp, curthread);
	if (error == ENOATTR || error == EOPNOTSUPP) {
		/* Fall back to the mntlabel. */
		biba_copy_effective(source, dest);
		return (0);
	} else if (error)
		return (error);

	if (buflen != sizeof(mb_temp)) {
		printf("biba_vnode_associate_extattr: bad size %d\n",
		    buflen);
		return (EPERM);
	}
	if (biba_valid(&mb_temp) != 0) {
		printf("biba_vnode_associate_extattr: invalid\n");
		return (EPERM);
	}
	if ((mb_temp.mb_flags & MAC_BIBA_FLAGS_BOTH) !=
	    MAC_BIBA_FLAG_EFFECTIVE) {
		printf("biba_vnode_associate_extattr: not effective\n");
		return (EPERM);
	}

	biba_copy_effective(&mb_temp, dest);
	return (0);
}

static void
biba_vnode_associate_singlelabel(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	biba_copy_effective(source, dest);
}

static int
biba_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{
	struct mac_biba *subj, *obj, *exec;
	int error;

	if (execlabel != NULL) {
		/*
		 * We currently don't permit labels to be changed at
		 * exec-time as part of Biba, so disallow non-NULL Biba label
		 * elements in the execlabel.
		 */
		exec = SLOT(execlabel);
		error = biba_atmostflags(exec, 0);
		if (error)
			return (error);
	}

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{
	struct mac_biba *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
biba_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	/* XXX privilege override for admin? */
	if (accmode & (VREAD | VEXEC | VSTAT_PERMS)) {
		if (!biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (accmode & VMODIFY_PERMS) {
		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
biba_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_biba *old, *new, *subj;
	int error;

	old = SLOT(vplabel);
	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);

	/*
	 * If there is a Biba label update for the vnode, it must be a
	 * effective label.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of the vnode (Biba label or not), Biba must
	 * authorize the relabel.
	 */
	if (!biba_effective_in_range(old, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To change the Biba label on a vnode, the new vnode label
		 * must be in the subject range.
		 */
		if (!biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on the vnode to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (biba_contains_equal(new)) {
			error = biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
biba_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	if (vp != NULL) {
		obj = SLOT(vplabel);

		if (!biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
biba_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	/* XXX: protect the MAC EA in a special way? */

	return (0);
}

static int
biba_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_check_write(struct ucred *active_cred,
    struct ucred *file_cred, struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
biba_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mplabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{
	struct mac_biba *source, *dest, mb_temp;
	size_t buflen;
	int error;

	buflen = sizeof(mb_temp);
	bzero(&mb_temp, buflen);

	source = SLOT(cred->cr_label);
	dest = SLOT(vplabel);
	biba_copy_effective(source, &mb_temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, buflen, (char *) &mb_temp, curthread);
	if (error == 0)
		biba_copy_effective(source, dest);
	return (error);
}

static void
biba_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(vplabel);

	biba_copy(source, dest);
}

static int
biba_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{
	struct mac_biba *source, mb_temp;
	size_t buflen;
	int error;

	buflen = sizeof(mb_temp);
	bzero(&mb_temp, buflen);

	source = SLOT(intlabel);
	if ((source->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) == 0)
		return (0);

	biba_copy_effective(source, &mb_temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, buflen, (char *) &mb_temp, curthread);
	return (error);
}

static struct mac_policy_ops mac_biba_ops =
{
	.mpo_init = biba_init,

	.mpo_bpfdesc_check_receive = biba_bpfdesc_check_receive,
	.mpo_bpfdesc_create = biba_bpfdesc_create,
	.mpo_bpfdesc_create_mbuf = biba_bpfdesc_create_mbuf,
	.mpo_bpfdesc_destroy_label = biba_destroy_label,
	.mpo_bpfdesc_init_label = biba_init_label,

	.mpo_cred_associate_nfsd = biba_cred_associate_nfsd,
	.mpo_cred_check_relabel = biba_cred_check_relabel,
	.mpo_cred_check_visible = biba_cred_check_visible,
	.mpo_cred_copy_label = biba_copy_label,
	.mpo_cred_create_init = biba_cred_create_init,
	.mpo_cred_create_swapper = biba_cred_create_swapper,
	.mpo_cred_destroy_label = biba_destroy_label,
	.mpo_cred_externalize_label = biba_externalize_label,
	.mpo_cred_init_label = biba_init_label,
	.mpo_cred_internalize_label = biba_internalize_label,
	.mpo_cred_relabel = biba_cred_relabel,

	.mpo_devfs_create_device = biba_devfs_create_device,
	.mpo_devfs_create_directory = biba_devfs_create_directory,
	.mpo_devfs_create_symlink = biba_devfs_create_symlink,
	.mpo_devfs_destroy_label = biba_destroy_label,
	.mpo_devfs_init_label = biba_init_label,
	.mpo_devfs_update = biba_devfs_update,
	.mpo_devfs_vnode_associate = biba_devfs_vnode_associate,

	.mpo_ifnet_check_relabel = biba_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = biba_ifnet_check_transmit,
	.mpo_ifnet_copy_label = biba_copy_label,
	.mpo_ifnet_create = biba_ifnet_create,
	.mpo_ifnet_create_mbuf = biba_ifnet_create_mbuf,
	.mpo_ifnet_destroy_label = biba_destroy_label,
	.mpo_ifnet_externalize_label = biba_externalize_label,
	.mpo_ifnet_init_label = biba_init_label,
	.mpo_ifnet_internalize_label = biba_internalize_label,
	.mpo_ifnet_relabel = biba_ifnet_relabel,

	.mpo_inpcb_check_deliver = biba_inpcb_check_deliver,
	.mpo_inpcb_check_visible = biba_inpcb_check_visible,
	.mpo_inpcb_create = biba_inpcb_create,
	.mpo_inpcb_create_mbuf = biba_inpcb_create_mbuf,
	.mpo_inpcb_destroy_label = biba_destroy_label,
	.mpo_inpcb_init_label = biba_init_label_waitcheck,
	.mpo_inpcb_sosetlabel = biba_inpcb_sosetlabel,

	.mpo_ip6q_create = biba_ip6q_create,
	.mpo_ip6q_destroy_label = biba_destroy_label,
	.mpo_ip6q_init_label = biba_init_label_waitcheck,
	.mpo_ip6q_match = biba_ip6q_match,
	.mpo_ip6q_reassemble = biba_ip6q_reassemble,
	.mpo_ip6q_update = biba_ip6q_update,

	.mpo_ipq_create = biba_ipq_create,
	.mpo_ipq_destroy_label = biba_destroy_label,
	.mpo_ipq_init_label = biba_init_label_waitcheck,
	.mpo_ipq_match = biba_ipq_match,
	.mpo_ipq_reassemble = biba_ipq_reassemble,
	.mpo_ipq_update = biba_ipq_update,

	.mpo_kld_check_load = biba_kld_check_load,

	.mpo_mbuf_copy_label = biba_copy_label,
	.mpo_mbuf_destroy_label = biba_destroy_label,
	.mpo_mbuf_init_label = biba_init_label_waitcheck,

	.mpo_mount_check_stat = biba_mount_check_stat,
	.mpo_mount_create = biba_mount_create,
	.mpo_mount_destroy_label = biba_destroy_label,
	.mpo_mount_init_label = biba_init_label,

	.mpo_netinet_arp_send = biba_netinet_arp_send,
	.mpo_netinet_firewall_reply = biba_netinet_firewall_reply,
	.mpo_netinet_firewall_send = biba_netinet_firewall_send,
	.mpo_netinet_fragment = biba_netinet_fragment,
	.mpo_netinet_icmp_reply = biba_netinet_icmp_reply,
	.mpo_netinet_igmp_send = biba_netinet_igmp_send,

	.mpo_netinet6_nd6_send = biba_netinet6_nd6_send,

	.mpo_pipe_check_ioctl = biba_pipe_check_ioctl,
	.mpo_pipe_check_poll = biba_pipe_check_poll,
	.mpo_pipe_check_read = biba_pipe_check_read,
	.mpo_pipe_check_relabel = biba_pipe_check_relabel,
	.mpo_pipe_check_stat = biba_pipe_check_stat,
	.mpo_pipe_check_write = biba_pipe_check_write,
	.mpo_pipe_copy_label = biba_copy_label,
	.mpo_pipe_create = biba_pipe_create,
	.mpo_pipe_destroy_label = biba_destroy_label,
	.mpo_pipe_externalize_label = biba_externalize_label,
	.mpo_pipe_init_label = biba_init_label,
	.mpo_pipe_internalize_label = biba_internalize_label,
	.mpo_pipe_relabel = biba_pipe_relabel,

	.mpo_posixsem_check_getvalue = biba_posixsem_check_rdonly,
	.mpo_posixsem_check_open = biba_posixsem_check_openunlink,
	.mpo_posixsem_check_post = biba_posixsem_check_write,
	.mpo_posixsem_check_setmode = biba_posixsem_check_setmode,
	.mpo_posixsem_check_setowner = biba_posixsem_check_setowner,
	.mpo_posixsem_check_stat = biba_posixsem_check_rdonly,
	.mpo_posixsem_check_unlink = biba_posixsem_check_openunlink,
	.mpo_posixsem_check_wait = biba_posixsem_check_write,
	.mpo_posixsem_create = biba_posixsem_create,
	.mpo_posixsem_destroy_label = biba_destroy_label,
	.mpo_posixsem_init_label = biba_init_label,

	.mpo_posixshm_check_mmap = biba_posixshm_check_mmap,
	.mpo_posixshm_check_open = biba_posixshm_check_open,
	.mpo_posixshm_check_read = biba_posixshm_check_read,
	.mpo_posixshm_check_setmode = biba_posixshm_check_setmode,
	.mpo_posixshm_check_setowner = biba_posixshm_check_setowner,
	.mpo_posixshm_check_stat = biba_posixshm_check_stat,
	.mpo_posixshm_check_truncate = biba_posixshm_check_truncate,
	.mpo_posixshm_check_unlink = biba_posixshm_check_unlink,
	.mpo_posixshm_check_write = biba_posixshm_check_write,
	.mpo_posixshm_create = biba_posixshm_create,
	.mpo_posixshm_destroy_label = biba_destroy_label,
	.mpo_posixshm_init_label = biba_init_label,

	.mpo_priv_check = biba_priv_check,

	.mpo_proc_check_debug = biba_proc_check_debug,
	.mpo_proc_check_sched = biba_proc_check_sched,
	.mpo_proc_check_signal = biba_proc_check_signal,

	.mpo_socket_check_deliver = biba_socket_check_deliver,
	.mpo_socket_check_relabel = biba_socket_check_relabel,
	.mpo_socket_check_visible = biba_socket_check_visible,
	.mpo_socket_copy_label = biba_copy_label,
	.mpo_socket_create = biba_socket_create,
	.mpo_socket_create_mbuf = biba_socket_create_mbuf,
	.mpo_socket_destroy_label = biba_destroy_label,
	.mpo_socket_externalize_label = biba_externalize_label,
	.mpo_socket_init_label = biba_init_label_waitcheck,
	.mpo_socket_internalize_label = biba_internalize_label,
	.mpo_socket_newconn = biba_socket_newconn,
	.mpo_socket_relabel = biba_socket_relabel,

	.mpo_socketpeer_destroy_label = biba_destroy_label,
	.mpo_socketpeer_externalize_label = biba_externalize_label,
	.mpo_socketpeer_init_label = biba_init_label_waitcheck,
	.mpo_socketpeer_set_from_mbuf = biba_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = biba_socketpeer_set_from_socket,

	.mpo_syncache_create = biba_syncache_create,
	.mpo_syncache_create_mbuf = biba_syncache_create_mbuf,
	.mpo_syncache_destroy_label = biba_destroy_label,
	.mpo_syncache_init_label = biba_init_label_waitcheck,

	.mpo_system_check_acct = biba_system_check_acct,
	.mpo_system_check_auditctl = biba_system_check_auditctl,
	.mpo_system_check_auditon = biba_system_check_auditon,
	.mpo_system_check_swapoff = biba_system_check_swapoff,
	.mpo_system_check_swapon = biba_system_check_swapon,
	.mpo_system_check_sysctl = biba_system_check_sysctl,

	.mpo_sysvmsg_cleanup = biba_sysvmsg_cleanup,
	.mpo_sysvmsg_create = biba_sysvmsg_create,
	.mpo_sysvmsg_destroy_label = biba_destroy_label,
	.mpo_sysvmsg_init_label = biba_init_label,

	.mpo_sysvmsq_check_msgrcv = biba_sysvmsq_check_msgrcv,
	.mpo_sysvmsq_check_msgrmid = biba_sysvmsq_check_msgrmid,
	.mpo_sysvmsq_check_msqget = biba_sysvmsq_check_msqget,
	.mpo_sysvmsq_check_msqsnd = biba_sysvmsq_check_msqsnd,
	.mpo_sysvmsq_check_msqrcv = biba_sysvmsq_check_msqrcv,
	.mpo_sysvmsq_check_msqctl = biba_sysvmsq_check_msqctl,
	.mpo_sysvmsq_cleanup = biba_sysvmsq_cleanup,
	.mpo_sysvmsq_create = biba_sysvmsq_create,
	.mpo_sysvmsq_destroy_label = biba_destroy_label,
	.mpo_sysvmsq_init_label = biba_init_label,

	.mpo_sysvsem_check_semctl = biba_sysvsem_check_semctl,
	.mpo_sysvsem_check_semget = biba_sysvsem_check_semget,
	.mpo_sysvsem_check_semop = biba_sysvsem_check_semop,
	.mpo_sysvsem_cleanup = biba_sysvsem_cleanup,
	.mpo_sysvsem_create = biba_sysvsem_create,
	.mpo_sysvsem_destroy_label = biba_destroy_label,
	.mpo_sysvsem_init_label = biba_init_label,

	.mpo_sysvshm_check_shmat = biba_sysvshm_check_shmat,
	.mpo_sysvshm_check_shmctl = biba_sysvshm_check_shmctl,
	.mpo_sysvshm_check_shmget = biba_sysvshm_check_shmget,
	.mpo_sysvshm_cleanup = biba_sysvshm_cleanup,
	.mpo_sysvshm_create = biba_sysvshm_create,
	.mpo_sysvshm_destroy_label = biba_destroy_label,
	.mpo_sysvshm_init_label = biba_init_label,

	.mpo_vnode_associate_extattr = biba_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = biba_vnode_associate_singlelabel,
	.mpo_vnode_check_access = biba_vnode_check_open,
	.mpo_vnode_check_chdir = biba_vnode_check_chdir,
	.mpo_vnode_check_chroot = biba_vnode_check_chroot,
	.mpo_vnode_check_create = biba_vnode_check_create,
	.mpo_vnode_check_deleteacl = biba_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = biba_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = biba_vnode_check_exec,
	.mpo_vnode_check_getacl = biba_vnode_check_getacl,
	.mpo_vnode_check_getextattr = biba_vnode_check_getextattr,
	.mpo_vnode_check_link = biba_vnode_check_link,
	.mpo_vnode_check_listextattr = biba_vnode_check_listextattr,
	.mpo_vnode_check_lookup = biba_vnode_check_lookup,
	.mpo_vnode_check_mmap = biba_vnode_check_mmap,
	.mpo_vnode_check_open = biba_vnode_check_open,
	.mpo_vnode_check_poll = biba_vnode_check_poll,
	.mpo_vnode_check_read = biba_vnode_check_read,
	.mpo_vnode_check_readdir = biba_vnode_check_readdir,
	.mpo_vnode_check_readlink = biba_vnode_check_readlink,
	.mpo_vnode_check_relabel = biba_vnode_check_relabel,
	.mpo_vnode_check_rename_from = biba_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = biba_vnode_check_rename_to,
	.mpo_vnode_check_revoke = biba_vnode_check_revoke,
	.mpo_vnode_check_setacl = biba_vnode_check_setacl,
	.mpo_vnode_check_setextattr = biba_vnode_check_setextattr,
	.mpo_vnode_check_setflags = biba_vnode_check_setflags,
	.mpo_vnode_check_setmode = biba_vnode_check_setmode,
	.mpo_vnode_check_setowner = biba_vnode_check_setowner,
	.mpo_vnode_check_setutimes = biba_vnode_check_setutimes,
	.mpo_vnode_check_stat = biba_vnode_check_stat,
	.mpo_vnode_check_unlink = biba_vnode_check_unlink,
	.mpo_vnode_check_write = biba_vnode_check_write,
	.mpo_vnode_create_extattr = biba_vnode_create_extattr,
	.mpo_vnode_copy_label = biba_copy_label,
	.mpo_vnode_destroy_label = biba_destroy_label,
	.mpo_vnode_externalize_label = biba_externalize_label,
	.mpo_vnode_init_label = biba_init_label,
	.mpo_vnode_internalize_label = biba_internalize_label,
	.mpo_vnode_relabel = biba_vnode_relabel,
	.mpo_vnode_setlabel_extattr = biba_vnode_setlabel_extattr,
};

MAC_POLICY_SET(&mac_biba_ops, mac_biba, "TrustedBSD MAC/Biba",
    MPC_LOADTIME_FLAG_NOTLATE, &biba_slot);
