/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000, 2001 Robert N. M. Watson
 * All rights reserved.
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
 * Support functionality for the POSIX.1e ACL interface
 * These calls are intended only to be called within the library.
 */
#ifndef _ACL_SUPPORT_H
#define _ACL_SUPPORT_H

#define _POSIX1E_ACL_STRING_PERM_MAXSIZE 3       /* read, write, exec */
#define _ACL_T_ALIGNMENT_BITS		13

int	_acl_type_unold(acl_type_t type);
int	_acl_differs(const acl_t a, const acl_t b);
int	_acl_type_not_valid_for_acl(const acl_t acl, acl_type_t type);
void	_acl_brand_from_type(acl_t acl, acl_type_t type);
int	_acl_brand(const acl_t acl);
int	_entry_brand(const acl_entry_t entry);
int	_acl_brand_may_be(const acl_t acl, int brand);
int	_entry_brand_may_be(const acl_entry_t entry, int brand);
void	_acl_brand_as(acl_t acl, int brand);
void	_entry_brand_as(const acl_entry_t entry, int brand);
int	_nfs4_acl_entry_from_text(acl_t, char *);
char	*_nfs4_acl_to_text_np(const acl_t, ssize_t *, int);
int	_nfs4_format_flags(char *str, size_t size, acl_flag_t var, int verbose);
int	_nfs4_format_access_mask(char *str, size_t size, acl_perm_t var, int verbose);
int	_nfs4_parse_flags(const char *str, acl_flag_t *var);
int	_nfs4_parse_access_mask(const char *str, acl_perm_t *var);
int	_posix1e_acl_check(acl_t acl);
void	_posix1e_acl_sort(acl_t acl);
int	_posix1e_acl(acl_t acl, acl_type_t type);
int	_posix1e_acl_id_to_name(acl_tag_t tag, uid_t id, ssize_t buf_len,
	    char *buf, int flags);
int	_posix1e_acl_perm_to_string(acl_perm_t perm, ssize_t buf_len,
	    char *buf);
int	_posix1e_acl_string_to_perm(char *string, acl_perm_t *perm);
int	_posix1e_acl_add_entry(acl_t acl, acl_tag_t tag, uid_t id,
	    acl_perm_t perm);
char	*string_skip_whitespace(char *string);
void	string_trim_trailing_whitespace(char *string);
int	_acl_name_to_id(acl_tag_t tag, char *name, uid_t *id);

#endif
