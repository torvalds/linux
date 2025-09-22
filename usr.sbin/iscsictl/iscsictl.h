/*	$OpenBSD: iscsictl.h,v 1.4 2014/04/21 17:44:47 claudio Exp $ */

/*
 * Copyright (c) 2009 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define ISCSID_OPT_NOACTION	0x01

struct iscsi_config {
	SIMPLEQ_HEAD(, session_ctlcfg)	sessions;
	struct initiator_config		initiator;
};

struct session_ctlcfg {
	struct session_config		session;
	SIMPLEQ_ENTRY(session_ctlcfg)	entry;
};

enum actions {
	NONE,
	LOG_VERBOSE,
	LOG_BRIEF,
	SHOW_SUM,
	SHOW_SESS,
	SHOW_VSCSI_STATS,
	RELOAD,
	DISCOVERY
};

struct parse_result {
	struct sockaddr_storage	addr;
	char			name[32];
	int			flags;
	enum actions		action;
	u_int8_t		prefixlen;
};

/* parse.y */
struct iscsi_config *	parse_config(char *);
int			cmdline_symset(char *);

/* parser.c */
struct parse_result	*parse(int, char *[]);
const struct token	*match_token(const char *, const struct token *);
void			 show_valid_args(const struct token *);
int			 parse_addr(const char *, struct sockaddr_storage *);
