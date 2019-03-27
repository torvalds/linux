/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2004 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Network Associates
 * Laboratories, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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

#ifndef _UGIDFW_H
#define	_UGIDFW_H

__BEGIN_DECLS
int	bsde_rule_to_string(struct mac_bsdextended_rule *rule, char *buf,
	    size_t buflen);
int	bsde_parse_mode(int argc, char *argv[], mode_t *mode, size_t buflen,
	    char *errstr);
int	bsde_parse_rule(int argc, char *argv[],
	    struct mac_bsdextended_rule *rule, size_t buflen, char *errstr);
int	bsde_parse_rule_string(const char *string,
	    struct mac_bsdextended_rule *rule, size_t buflen, char *errstr);
int	bsde_get_mib(const char *string, int *name, size_t *namelen);
int	bsde_get_rule_count(size_t buflen, char *errstr);
int	bsde_get_rule_slots(size_t buflen, char *errstr);
int	bsde_get_rule(int rulenum, struct mac_bsdextended_rule *rule,
	    size_t errlen, char *errstr);
int	bsde_delete_rule(int rulenum, size_t buflen, char *errstr);
int	bsde_set_rule(int rulenum, struct mac_bsdextended_rule *rule,
	    size_t buflen, char *errstr);
int	bsde_add_rule(int *rulename, struct mac_bsdextended_rule *rule,
	    size_t buflen, char *errstr);
__END_DECLS

#endif
