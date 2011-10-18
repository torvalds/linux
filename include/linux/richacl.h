/*
 * Copyright (C) 2006, 2010  Novell, Inc.
 * Written by Andreas Gruenbacher <agruen@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __RICHACL_H
#define __RICHACL_H
#include <linux/slab.h>

#define ACE_OWNER_ID		130
#define ACE_GROUP_ID		131
#define ACE_EVERYONE_ID		110

struct richace {
	unsigned short	e_type;
	unsigned short	e_flags;
	unsigned int	e_mask;
	unsigned int	e_id;
};

struct richacl {
	atomic_t	a_refcount;
	unsigned int	a_owner_mask;
	unsigned int	a_group_mask;
	unsigned int	a_other_mask;
	unsigned short	a_count;
	unsigned short	a_flags;
	struct richace	a_entries[0];
};

#define richacl_for_each_entry(_ace, _acl) \
	for (_ace = _acl->a_entries; \
	     _ace != _acl->a_entries + _acl->a_count; \
	     _ace++)

#define richacl_for_each_entry_reverse(_ace, _acl) \
	for (_ace = _acl->a_entries + _acl->a_count - 1; \
	     _ace != _acl->a_entries - 1; \
	     _ace--)

/* Flag values defined by rich-acl */
#define ACL4_MASKED			0x80

#define ACL4_VALID_FLAGS (			\
		ACL4_MASKED)

/* e_type values */
#define ACE4_ACCESS_ALLOWED_ACE_TYPE	0x0000
#define ACE4_ACCESS_DENIED_ACE_TYPE	0x0001
/*#define ACE4_SYSTEM_AUDIT_ACE_TYPE	0x0002*/
/*#define ACE4_SYSTEM_ALARM_ACE_TYPE	0x0003*/

/* e_flags bitflags */
#define ACE4_FILE_INHERIT_ACE		0x0001
#define ACE4_DIRECTORY_INHERIT_ACE	0x0002
#define ACE4_NO_PROPAGATE_INHERIT_ACE	0x0004
#define ACE4_INHERIT_ONLY_ACE		0x0008
/*#define ACE4_SUCCESSFUL_ACCESS_ACE_FLAG	0x0010*/
/*#define ACE4_FAILED_ACCESS_ACE_FLAG	0x0020*/
#define ACE4_IDENTIFIER_GROUP		0x0040
/* richacl specific flag values */
#define ACE4_SPECIAL_WHO		0x4000

#define ACE4_VALID_FLAGS (			\
	ACE4_FILE_INHERIT_ACE |			\
	ACE4_DIRECTORY_INHERIT_ACE |		\
	ACE4_NO_PROPAGATE_INHERIT_ACE |		\
	ACE4_INHERIT_ONLY_ACE |			\
	ACE4_IDENTIFIER_GROUP |			\
	ACE4_SPECIAL_WHO)

/* e_mask bitflags */
#define ACE4_READ_DATA			0x00000001
#define ACE4_LIST_DIRECTORY		0x00000001
#define ACE4_WRITE_DATA			0x00000002
#define ACE4_ADD_FILE			0x00000002
#define ACE4_APPEND_DATA		0x00000004
#define ACE4_ADD_SUBDIRECTORY		0x00000004
#define ACE4_READ_NAMED_ATTRS		0x00000008
#define ACE4_WRITE_NAMED_ATTRS		0x00000010
#define ACE4_EXECUTE			0x00000020
#define ACE4_DELETE_CHILD		0x00000040
#define ACE4_READ_ATTRIBUTES		0x00000080
#define ACE4_WRITE_ATTRIBUTES		0x00000100
#define ACE4_WRITE_RETENTION		0x00000200
#define ACE4_WRITE_RETENTION_HOLD	0x00000400
#define ACE4_DELETE			0x00010000
#define ACE4_READ_ACL			0x00020000
#define ACE4_WRITE_ACL			0x00040000
#define ACE4_WRITE_OWNER		0x00080000
#define ACE4_SYNCHRONIZE		0x00100000

/* Valid ACE4_* flags for directories and non-directories */
#define ACE4_VALID_MASK (				\
	ACE4_READ_DATA | ACE4_LIST_DIRECTORY |		\
	ACE4_WRITE_DATA | ACE4_ADD_FILE |		\
	ACE4_APPEND_DATA | ACE4_ADD_SUBDIRECTORY |	\
	ACE4_READ_NAMED_ATTRS |				\
	ACE4_WRITE_NAMED_ATTRS |			\
	ACE4_EXECUTE |					\
	ACE4_DELETE_CHILD |				\
	ACE4_READ_ATTRIBUTES |				\
	ACE4_WRITE_ATTRIBUTES |				\
	ACE4_WRITE_RETENTION |				\
	ACE4_WRITE_RETENTION_HOLD |			\
	ACE4_DELETE |					\
	ACE4_READ_ACL |					\
	ACE4_WRITE_ACL |				\
	ACE4_WRITE_OWNER |				\
	ACE4_SYNCHRONIZE)

/**
 * richacl_get  -  grab another reference to a richacl handle
 */
static inline struct richacl *
richacl_get(struct richacl *acl)
{
	if (acl)
		atomic_inc(&acl->a_refcount);
	return acl;
}

/**
 * richacl_put  -  free a richacl handle
 */
static inline void
richacl_put(struct richacl *acl)
{
	if (acl && atomic_dec_and_test(&acl->a_refcount))
		kfree(acl);
}

/**
 * richace_is_owner  -  check if @ace is an OWNER@ entry
 */
static inline int
richace_is_owner(const struct richace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->e_id == ACE_OWNER_ID;
}

/**
 * richace_is_group  -  check if @ace is a GROUP@ entry
 */
static inline int
richace_is_group(const struct richace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->e_id == ACE_GROUP_ID;
}

/**
 * richace_is_everyone  -  check if @ace is an EVERYONE@ entry
 */
static inline int
richace_is_everyone(const struct richace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->e_id == ACE_EVERYONE_ID;
}

/**
 * richace_is_unix_id  -  check if @ace applies to a specific uid or gid
 */
static inline int
richace_is_unix_id(const struct richace *ace)
{
	return !(ace->e_flags & ACE4_SPECIAL_WHO);
}

/**
 * richace_is_inherit_only  -  check if @ace is for inheritance only
 *
 * ACEs with the %ACE4_INHERIT_ONLY_ACE flag set have no effect during
 * permission checking.
 */
static inline int
richace_is_inherit_only(const struct richace *ace)
{
	return ace->e_flags & ACE4_INHERIT_ONLY_ACE;
}

/**
 * richace_is_inheritable  -  check if @ace is inheritable
 */
static inline int
richace_is_inheritable(const struct richace *ace)
{
	return ace->e_flags & (ACE4_FILE_INHERIT_ACE |
			       ACE4_DIRECTORY_INHERIT_ACE);
}

/**
 * richace_clear_inheritance_flags  - clear all inheritance flags in @ace
 */
static inline void
richace_clear_inheritance_flags(struct richace *ace)
{
	ace->e_flags &= ~(ACE4_FILE_INHERIT_ACE |
			  ACE4_DIRECTORY_INHERIT_ACE |
			  ACE4_NO_PROPAGATE_INHERIT_ACE |
			  ACE4_INHERIT_ONLY_ACE);
}

/**
 * richace_is_allow  -  check if @ace is an %ALLOW type entry
 */
static inline int
richace_is_allow(const struct richace *ace)
{
	return ace->e_type == ACE4_ACCESS_ALLOWED_ACE_TYPE;
}

/**
 * richace_is_deny  -  check if @ace is a %DENY type entry
 */
static inline int
richace_is_deny(const struct richace *ace)
{
	return ace->e_type == ACE4_ACCESS_DENIED_ACE_TYPE;
}

extern struct richacl *richacl_alloc(int);
extern int richace_is_same_identifier(const struct richace *,
				      const struct richace *);
#endif /* __RICHACL_H */
