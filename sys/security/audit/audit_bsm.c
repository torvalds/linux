/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2009 Apple Inc.
 * Copyright (c) 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/ipc.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/extattr.h>
#include <sys/fcntl.h>
#include <sys/user.h>
#include <sys/systm.h>

#include <bsm/audit.h>
#include <bsm/audit_internal.h>
#include <bsm/audit_record.h>
#include <bsm/audit_kevents.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

MALLOC_DEFINE(M_AUDITBSM, "audit_bsm", "Audit BSM data");

static void	audit_sys_auditon(struct audit_record *ar,
		    struct au_record *rec);

/*
 * Initialize the BSM auditing subsystem.
 */
void
kau_init(void)
{

	au_evclassmap_init();
	au_evnamemap_init();
}

/*
 * This call reserves memory for the audit record.  Memory must be guaranteed
 * before any auditable event can be generated.  The au_record structure
 * maintains a reference to the memory allocated above and also the list of
 * tokens associated with this record.
 */
static struct au_record *
kau_open(void)
{
	struct au_record *rec;

	rec = malloc(sizeof(*rec), M_AUDITBSM, M_WAITOK);
	rec->data = NULL;
	TAILQ_INIT(&rec->token_q);
	rec->len = 0;
	rec->used = 1;

	return (rec);
}

/*
 * Store the token with the record descriptor.
 */
static void
kau_write(struct au_record *rec, struct au_token *tok)
{

	KASSERT(tok != NULL, ("kau_write: tok == NULL"));

	TAILQ_INSERT_TAIL(&rec->token_q, tok, tokens);
	rec->len += tok->len;
}

/*
 * Close out the audit record by adding the header token, identifying any
 * missing tokens.  Write out the tokens to the record memory.
 */
static void
kau_close(struct au_record *rec, struct timespec *ctime, short event)
{
	u_char *dptr;
	size_t tot_rec_size;
	token_t *cur, *hdr, *trail;
	struct timeval tm;
	size_t hdrsize;
	struct auditinfo_addr ak;
	struct in6_addr *ap;

	audit_get_kinfo(&ak);
	hdrsize = 0;
	switch (ak.ai_termid.at_type) {
	case AU_IPv4:
		hdrsize = (ak.ai_termid.at_addr[0] == INADDR_ANY) ?
		    AUDIT_HEADER_SIZE : AUDIT_HEADER_EX_SIZE(&ak);
		break;
	case AU_IPv6:
		ap = (struct in6_addr *)&ak.ai_termid.at_addr[0];
		hdrsize = (IN6_IS_ADDR_UNSPECIFIED(ap)) ? AUDIT_HEADER_SIZE :
		    AUDIT_HEADER_EX_SIZE(&ak);
		break;
	default:
		panic("kau_close: invalid address family");
	}
	tot_rec_size = rec->len + hdrsize + AUDIT_TRAILER_SIZE;
	rec->data = malloc(tot_rec_size, M_AUDITBSM, M_WAITOK | M_ZERO);

	tm.tv_usec = ctime->tv_nsec / 1000;
	tm.tv_sec = ctime->tv_sec;
	if (hdrsize != AUDIT_HEADER_SIZE)
		hdr = au_to_header32_ex_tm(tot_rec_size, event, 0, tm, &ak);
	else
		hdr = au_to_header32_tm(tot_rec_size, event, 0, tm);
	TAILQ_INSERT_HEAD(&rec->token_q, hdr, tokens);

	trail = au_to_trailer(tot_rec_size);
	TAILQ_INSERT_TAIL(&rec->token_q, trail, tokens);

	rec->len = tot_rec_size;
	dptr = rec->data;
	TAILQ_FOREACH(cur, &rec->token_q, tokens) {
		memcpy(dptr, cur->t_data, cur->len);
		dptr += cur->len;
	}
}

/*
 * Free a BSM audit record by releasing all the tokens and clearing the audit
 * record information.
 */
void
kau_free(struct au_record *rec)
{
	struct au_token *tok;

	/* Free the token list. */
	while ((tok = TAILQ_FIRST(&rec->token_q))) {
		TAILQ_REMOVE(&rec->token_q, tok, tokens);
		free(tok->t_data, M_AUDITBSM);
		free(tok, M_AUDITBSM);
	}

	rec->used = 0;
	rec->len = 0;
	free(rec->data, M_AUDITBSM);
	free(rec, M_AUDITBSM);
}

/*
 * XXX: May want turn some (or all) of these macros into functions in order
 * to reduce the generated code size.
 *
 * XXXAUDIT: These macros assume that 'kar', 'ar', 'rec', and 'tok' in the
 * caller are OK with this.
 */
#define	ATFD1_TOKENS(argnum) do {					\
	if (ARG_IS_VALID(kar, ARG_ATFD1)) {				\
		tok = au_to_arg32(argnum, "at fd 1", ar->ar_arg_atfd1);	\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	ATFD2_TOKENS(argnum) do {					\
	if (ARG_IS_VALID(kar, ARG_ATFD2)) {				\
		tok = au_to_arg32(argnum, "at fd 2", ar->ar_arg_atfd2);	\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	UPATH1_TOKENS do {						\
	if (ARG_IS_VALID(kar, ARG_UPATH1)) {				\
		tok = au_to_path(ar->ar_arg_upath1);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	UPATH2_TOKENS do {						\
	if (ARG_IS_VALID(kar, ARG_UPATH2)) {				\
		tok = au_to_path(ar->ar_arg_upath2);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	VNODE1_TOKENS do {						\
	if (ARG_IS_VALID(kar, ARG_ATFD)) {				\
		tok = au_to_arg32(1, "at fd", ar->ar_arg_atfd);		\
		kau_write(rec, tok);					\
	}								\
	if (ARG_IS_VALID(kar, ARG_VNODE1)) {				\
		tok = au_to_attr32(&ar->ar_arg_vnode1);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	UPATH1_VNODE1_TOKENS do {					\
	UPATH1_TOKENS;							\
	if (ARG_IS_VALID(kar, ARG_VNODE1)) {				\
		tok = au_to_attr32(&ar->ar_arg_vnode1);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	VNODE2_TOKENS do {						\
	if (ARG_IS_VALID(kar, ARG_VNODE2)) {				\
		tok = au_to_attr32(&ar->ar_arg_vnode2);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	FD_VNODE1_TOKENS do {						\
	if (ARG_IS_VALID(kar, ARG_VNODE1)) {				\
		if (ARG_IS_VALID(kar, ARG_FD)) {			\
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);	\
			kau_write(rec, tok);				\
		}							\
		tok = au_to_attr32(&ar->ar_arg_vnode1);			\
		kau_write(rec, tok);					\
	} else {							\
		if (ARG_IS_VALID(kar, ARG_FD)) {			\
			tok = au_to_arg32(1, "non-file: fd",		\
			    ar->ar_arg_fd);				\
			kau_write(rec, tok);				\
		}							\
	}								\
} while (0)

#define	PROCESS_PID_TOKENS(argn) do {					\
	if ((ar->ar_arg_pid > 0) /* Reference a single process */	\
	    && (ARG_IS_VALID(kar, ARG_PROCESS))) {			\
		tok = au_to_process32_ex(ar->ar_arg_auid,		\
		    ar->ar_arg_euid, ar->ar_arg_egid,			\
		    ar->ar_arg_ruid, ar->ar_arg_rgid,			\
		    ar->ar_arg_pid, ar->ar_arg_asid,			\
		    &ar->ar_arg_termid_addr);				\
		kau_write(rec, tok);					\
	} else if (ARG_IS_VALID(kar, ARG_PID)) {			\
		tok = au_to_arg32(argn, "process", ar->ar_arg_pid);	\
		kau_write(rec, tok);					\
	}								\
} while (0)

#define	EXTATTR_TOKENS(namespace_argnum) do {				\
	if (ARG_IS_VALID(kar, ARG_VALUE)) {				\
		switch (ar->ar_arg_value) {				\
		case EXTATTR_NAMESPACE_USER:				\
			tok = au_to_text(EXTATTR_NAMESPACE_USER_STRING);\
			break;						\
		case EXTATTR_NAMESPACE_SYSTEM:				\
			tok = au_to_text(EXTATTR_NAMESPACE_SYSTEM_STRING);\
			break;						\
		default:						\
			tok = au_to_arg32((namespace_argnum),		\
			    "attrnamespace", ar->ar_arg_value);		\
			break;						\
		}							\
		kau_write(rec, tok);					\
	}								\
	/* attrname is in the text field */				\
	if (ARG_IS_VALID(kar, ARG_TEXT)) {				\
		tok = au_to_text(ar->ar_arg_text);			\
		kau_write(rec, tok);					\
	}								\
} while (0)

/*
 * Not all pointer arguments to system calls are of interest, but in some
 * cases they reflect delegation of rights, such as mmap(2) followed by
 * minherit(2) before execve(2), so do the best we can.
 */
#define	ADDR_TOKEN(argnum, argname) do {				\
	if (ARG_IS_VALID(kar, ARG_ADDR)) {				\
		if (sizeof(void *) == sizeof(uint32_t))			\
			tok = au_to_arg32((argnum), (argname),		\
			    (uint32_t)(uintptr_t)ar->ar_arg_addr);	\
		else							\
			tok = au_to_arg64((argnum), (argname),		\
			    (uint64_t)(uintptr_t)ar->ar_arg_addr);	\
		kau_write(rec, tok);					\
	}								\
} while (0)


/*
 * Implement auditing for the auditon() system call. The audit tokens that
 * are generated depend on the command that was sent into the auditon()
 * system call.
 */
static void
audit_sys_auditon(struct audit_record *ar, struct au_record *rec)
{
	struct au_token *tok;

	tok = au_to_arg32(3, "length", ar->ar_arg_len);
	kau_write(rec, tok);
	switch (ar->ar_arg_cmd) {
	case A_OLDSETPOLICY:
		if ((size_t)ar->ar_arg_len == sizeof(int64_t)) {
			tok = au_to_arg64(2, "policy",
			    ar->ar_arg_auditon.au_policy64);
			kau_write(rec, tok);
			break;
		}
		/* FALLTHROUGH */

	case A_SETPOLICY:
		tok = au_to_arg32(2, "policy", ar->ar_arg_auditon.au_policy);
		kau_write(rec, tok);
		break;

	case A_SETKMASK:
		tok = au_to_arg32(2, "setkmask:as_success",
		    ar->ar_arg_auditon.au_mask.am_success);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setkmask:as_failure",
		    ar->ar_arg_auditon.au_mask.am_failure);
		kau_write(rec, tok);
		break;

	case A_OLDSETQCTRL:
		if ((size_t)ar->ar_arg_len == sizeof(au_qctrl64_t)) {
			tok = au_to_arg64(2, "setqctrl:aq_hiwater",
			    ar->ar_arg_auditon.au_qctrl64.aq64_hiwater);
			kau_write(rec, tok);
			tok = au_to_arg64(2, "setqctrl:aq_lowater",
			    ar->ar_arg_auditon.au_qctrl64.aq64_lowater);
			kau_write(rec, tok);
			tok = au_to_arg64(2, "setqctrl:aq_bufsz",
			    ar->ar_arg_auditon.au_qctrl64.aq64_bufsz);
			kau_write(rec, tok);
			tok = au_to_arg64(2, "setqctrl:aq_delay",
			    ar->ar_arg_auditon.au_qctrl64.aq64_delay);
			kau_write(rec, tok);
			tok = au_to_arg64(2, "setqctrl:aq_minfree",
			    ar->ar_arg_auditon.au_qctrl64.aq64_minfree);
			kau_write(rec, tok);
			break;
		}
		/* FALLTHROUGH */

	case A_SETQCTRL:
		tok = au_to_arg32(2, "setqctrl:aq_hiwater",
		    ar->ar_arg_auditon.au_qctrl.aq_hiwater);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setqctrl:aq_lowater",
		    ar->ar_arg_auditon.au_qctrl.aq_lowater);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setqctrl:aq_bufsz",
		    ar->ar_arg_auditon.au_qctrl.aq_bufsz);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setqctrl:aq_delay",
		    ar->ar_arg_auditon.au_qctrl.aq_delay);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setqctrl:aq_minfree",
		    ar->ar_arg_auditon.au_qctrl.aq_minfree);
		kau_write(rec, tok);
		break;

	case A_SETUMASK:
		tok = au_to_arg32(2, "setumask:as_success",
		    ar->ar_arg_auditon.au_auinfo.ai_mask.am_success);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setumask:as_failure",
		    ar->ar_arg_auditon.au_auinfo.ai_mask.am_failure);
		kau_write(rec, tok);
		break;

	case A_SETSMASK:
		tok = au_to_arg32(2, "setsmask:as_success",
		    ar->ar_arg_auditon.au_auinfo.ai_mask.am_success);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setsmask:as_failure",
		    ar->ar_arg_auditon.au_auinfo.ai_mask.am_failure);
		kau_write(rec, tok);
		break;

	case A_OLDSETCOND:
		if ((size_t)ar->ar_arg_len == sizeof(int64_t)) {
			tok = au_to_arg64(2, "setcond",
			    ar->ar_arg_auditon.au_cond64);
			kau_write(rec, tok);
			break;
		}
		/* FALLTHROUGH */

	case A_SETCOND:
		tok = au_to_arg32(2, "setcond", ar->ar_arg_auditon.au_cond);
		kau_write(rec, tok);
		break;

	case A_SETCLASS:
		tok = au_to_arg32(2, "setclass:ec_event",
		    ar->ar_arg_auditon.au_evclass.ec_number);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setclass:ec_class",
		    ar->ar_arg_auditon.au_evclass.ec_class);
		kau_write(rec, tok);
		break;

	case A_SETPMASK:
		tok = au_to_arg32(2, "setpmask:as_success",
		    ar->ar_arg_auditon.au_aupinfo.ap_mask.am_success);
		kau_write(rec, tok);
		tok = au_to_arg32(2, "setpmask:as_failure",
		    ar->ar_arg_auditon.au_aupinfo.ap_mask.am_failure);
		kau_write(rec, tok);
		break;

	case A_SETFSIZE:
		tok = au_to_arg32(2, "setfsize:filesize",
		    ar->ar_arg_auditon.au_fstat.af_filesz);
		kau_write(rec, tok);
		break;

	default:
		break;
	}
}

/*
 * Convert an internal kernel audit record to a BSM record and return a
 * success/failure indicator. The BSM record is passed as an out parameter to
 * this function.
 *
 * Return conditions:
 *   BSM_SUCCESS: The BSM record is valid
 *   BSM_FAILURE: Failure; the BSM record is NULL.
 *   BSM_NOAUDIT: The event is not auditable for BSM; the BSM record is NULL.
 */
int
kaudit_to_bsm(struct kaudit_record *kar, struct au_record **pau)
{
	struct au_token *tok, *subj_tok, *jail_tok;
	struct au_record *rec;
	au_tid_t tid;
	struct audit_record *ar;
	int ctr;

	KASSERT(kar != NULL, ("kaudit_to_bsm: kar == NULL"));

	*pau = NULL;
	ar = &kar->k_ar;
	rec = kau_open();

	/*
	 * Create the subject token.  If this credential was jailed be sure to
	 * generate a zonename token.
	 */
	if (ar->ar_jailname[0] != '\0')
		jail_tok = au_to_zonename(ar->ar_jailname);
	else
		jail_tok = NULL;
	switch (ar->ar_subj_term_addr.at_type) {
	case AU_IPv4:
		tid.port = ar->ar_subj_term_addr.at_port;
		tid.machine = ar->ar_subj_term_addr.at_addr[0];
		subj_tok = au_to_subject32(ar->ar_subj_auid,  /* audit ID */
		    ar->ar_subj_cred.cr_uid, /* eff uid */
		    ar->ar_subj_egid,	/* eff group id */
		    ar->ar_subj_ruid,	/* real uid */
		    ar->ar_subj_rgid,	/* real group id */
		    ar->ar_subj_pid,	/* process id */
		    ar->ar_subj_asid,	/* session ID */
		    &tid);
		break;
	case AU_IPv6:
		subj_tok = au_to_subject32_ex(ar->ar_subj_auid,
		    ar->ar_subj_cred.cr_uid,
		    ar->ar_subj_egid,
		    ar->ar_subj_ruid,
		    ar->ar_subj_rgid,
		    ar->ar_subj_pid,
		    ar->ar_subj_asid,
		    &ar->ar_subj_term_addr);
		break;
	default:
		bzero(&tid, sizeof(tid));
		subj_tok = au_to_subject32(ar->ar_subj_auid,
		    ar->ar_subj_cred.cr_uid,
		    ar->ar_subj_egid,
		    ar->ar_subj_ruid,
		    ar->ar_subj_rgid,
		    ar->ar_subj_pid,
		    ar->ar_subj_asid,
		    &tid);
	}

	/*
	 * The logic inside each case fills in the tokens required for the
	 * event, except for the header, trailer, and return tokens.  The
	 * header and trailer tokens are added by the kau_close() function.
	 * The return token is added outside of the switch statement.
	 */
	switch(ar->ar_event) {
	case AUE_ACCEPT:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRINET)) {
			tok = au_to_sock_inet((struct sockaddr_in *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRUNIX)) {
			tok = au_to_sock_unix((struct sockaddr_un *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
			UPATH1_TOKENS;
		}
		break;

	case AUE_BIND:
	case AUE_LISTEN:
	case AUE_CONNECT:
	case AUE_RECV:
	case AUE_RECVFROM:
	case AUE_RECVMSG:
	case AUE_SEND:
	case AUE_SENDMSG:
	case AUE_SENDTO:
		/*
		 * Socket-related events.
		 */
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRINET)) {
			tok = au_to_sock_inet((struct sockaddr_in *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRUNIX)) {
			tok = au_to_sock_unix((struct sockaddr_un *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
			UPATH1_TOKENS;
		}
		/* XXX Need to handle ARG_SADDRINET6 */
		break;

	case AUE_BINDAT:
	case AUE_CONNECTAT:
		ATFD1_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(2, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRUNIX)) {
			tok = au_to_sock_unix((struct sockaddr_un *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
			UPATH1_TOKENS;
		}
		break;

	case AUE_SENDFILE:
		FD_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_SADDRINET)) {
			tok = au_to_sock_inet((struct sockaddr_in *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SADDRUNIX)) {
			tok = au_to_sock_unix((struct sockaddr_un *)
			    &ar->ar_arg_sockaddr);
			kau_write(rec, tok);
			UPATH1_TOKENS;
		}
		/* XXX Need to handle ARG_SADDRINET6 */
		break;

	case AUE_SOCKET:
	case AUE_SOCKETPAIR:
		if (ARG_IS_VALID(kar, ARG_SOCKINFO)) {
			tok = au_to_arg32(1, "domain",
			    ar->ar_arg_sockinfo.so_domain);
			kau_write(rec, tok);
			tok = au_to_arg32(2, "type",
			    ar->ar_arg_sockinfo.so_type);
			kau_write(rec, tok);
			tok = au_to_arg32(3, "protocol",
			    ar->ar_arg_sockinfo.so_protocol);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETSOCKOPT:
	case AUE_SHUTDOWN:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		break;

	case AUE_ACCT:
		if (ARG_IS_VALID(kar, ARG_UPATH1)) {
			UPATH1_VNODE1_TOKENS;
		} else {
			tok = au_to_arg32(1, "accounting off", 0);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETAUID:
		if (ARG_IS_VALID(kar, ARG_AUID)) {
			tok = au_to_arg32(2, "setauid", ar->ar_arg_auid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETAUDIT:
		if (ARG_IS_VALID(kar, ARG_AUID) &&
		    ARG_IS_VALID(kar, ARG_ASID) &&
		    ARG_IS_VALID(kar, ARG_AMASK) &&
		    ARG_IS_VALID(kar, ARG_TERMID)) {
			tok = au_to_arg32(1, "setaudit:auid",
			    ar->ar_arg_auid);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit:port",
			    ar->ar_arg_termid.port);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit:machine",
			    ar->ar_arg_termid.machine);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit:as_success",
			    ar->ar_arg_amask.am_success);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit:as_failure",
			    ar->ar_arg_amask.am_failure);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit:asid",
			    ar->ar_arg_asid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETAUDIT_ADDR:
		if (ARG_IS_VALID(kar, ARG_AUID) &&
		    ARG_IS_VALID(kar, ARG_ASID) &&
		    ARG_IS_VALID(kar, ARG_AMASK) &&
		    ARG_IS_VALID(kar, ARG_TERMID_ADDR)) {
			tok = au_to_arg32(1, "setaudit_addr:auid",
			    ar->ar_arg_auid);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit_addr:as_success",
			    ar->ar_arg_amask.am_success);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit_addr:as_failure",
			    ar->ar_arg_amask.am_failure);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit_addr:asid",
			    ar->ar_arg_asid);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit_addr:type",
			    ar->ar_arg_termid_addr.at_type);
			kau_write(rec, tok);
			tok = au_to_arg32(1, "setaudit_addr:port",
			    ar->ar_arg_termid_addr.at_port);
			kau_write(rec, tok);
			if (ar->ar_arg_termid_addr.at_type == AU_IPv6)
				tok = au_to_in_addr_ex((struct in6_addr *)
				    &ar->ar_arg_termid_addr.at_addr[0]);
			if (ar->ar_arg_termid_addr.at_type == AU_IPv4)
				tok = au_to_in_addr((struct in_addr *)
				    &ar->ar_arg_termid_addr.at_addr[0]);
			kau_write(rec, tok);
		}
		break;

	case AUE_AUDITON:
		/*
		 * For AUDITON commands without own event, audit the cmd.
		 */
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(1, "cmd", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_AUDITON_GETCAR:
	case AUE_AUDITON_GETCLASS:
	case AUE_AUDITON_GETCOND:
	case AUE_AUDITON_GETCWD:
	case AUE_AUDITON_GETKMASK:
	case AUE_AUDITON_GETSTAT:
	case AUE_AUDITON_GPOLICY:
	case AUE_AUDITON_GQCTRL:
	case AUE_AUDITON_SETCLASS:
	case AUE_AUDITON_SETCOND:
	case AUE_AUDITON_SETKMASK:
	case AUE_AUDITON_SETSMASK:
	case AUE_AUDITON_SETSTAT:
	case AUE_AUDITON_SETUMASK:
	case AUE_AUDITON_SPOLICY:
	case AUE_AUDITON_SQCTRL:
		if (ARG_IS_VALID(kar, ARG_AUDITON))
			audit_sys_auditon(ar, rec);
		break;

	case AUE_AUDITCTL:
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_EXIT:
		if (ARG_IS_VALID(kar, ARG_EXIT)) {
			tok = au_to_exit(ar->ar_arg_exitretval,
			    ar->ar_arg_exitstatus);
			kau_write(rec, tok);
		}
		break;

	case AUE_ADJTIME:
	case AUE_CLOCK_SETTIME:
	case AUE_AUDIT:
	case AUE_DUP2:
	case AUE_GETAUDIT:
	case AUE_GETAUDIT_ADDR:
	case AUE_GETAUID:
	case AUE_GETCWD:
	case AUE_GETFSSTAT:
	case AUE_GETRESUID:
	case AUE_GETRESGID:
	case AUE_KQUEUE:
	case AUE_MODLOAD:
	case AUE_MODUNLOAD:
	case AUE_MSGSYS:
	case AUE_NTP_ADJTIME:
	case AUE_PIPE:
	case AUE_POSIX_OPENPT:
	case AUE_PROFILE:
	case AUE_RTPRIO:
	case AUE_SEMSYS:
	case AUE_SETFIB:
	case AUE_SHMSYS:
	case AUE_SETPGRP:
	case AUE_SETRLIMIT:
	case AUE_SETSID:
	case AUE_SETTIMEOFDAY:
	case AUE_SYSARCH:

		/*
		 * Header, subject, and return tokens added at end.
		 */
		break;

	case AUE_ACL_DELETE_FD:
	case AUE_ACL_DELETE_FILE:
	case AUE_ACL_CHECK_FD:
	case AUE_ACL_CHECK_FILE:
	case AUE_ACL_CHECK_LINK:
	case AUE_ACL_DELETE_LINK:
	case AUE_ACL_GET_FD:
	case AUE_ACL_GET_FILE:
	case AUE_ACL_GET_LINK:
	case AUE_ACL_SET_FD:
	case AUE_ACL_SET_FILE:
	case AUE_ACL_SET_LINK:
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(1, "type", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_CHDIR:
	case AUE_CHROOT:
	case AUE_FSTATAT:
	case AUE_FUTIMESAT:
	case AUE_GETATTRLIST:
	case AUE_JAIL:
	case AUE_LUTIMES:
	case AUE_NFS_GETFH:
	case AUE_LGETFH:
	case AUE_LSTAT:
	case AUE_LPATHCONF:
	case AUE_PATHCONF:
	case AUE_READLINK:
	case AUE_READLINKAT:
	case AUE_REVOKE:
	case AUE_RMDIR:
	case AUE_SEARCHFS:
	case AUE_SETATTRLIST:
	case AUE_STAT:
	case AUE_STATFS:
	case AUE_SWAPON:
	case AUE_SWAPOFF:
	case AUE_TRUNCATE:
	case AUE_UNDELETE:
	case AUE_UNLINK:
	case AUE_UNLINKAT:
	case AUE_UTIMES:
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_ACCESS:
	case AUE_EACCESS:
	case AUE_FACCESSAT:
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(2, "mode", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		break;

	case AUE_FHSTATFS:
	case AUE_FHOPEN:
	case AUE_FHSTAT:
		/* XXXRW: Need to audit vnode argument. */
		break;

	case AUE_CHFLAGS:
	case AUE_LCHFLAGS:
	case AUE_CHFLAGSAT:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_CHMOD:
	case AUE_LCHMOD:
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(2, "new file mode",
			    ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_FCHMODAT:
		ATFD1_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(3, "new file mode",
			    ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_CHOWN:
	case AUE_LCHOWN:
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(2, "new file uid", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_GID)) {
			tok = au_to_arg32(3, "new file gid", ar->ar_arg_gid);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_FCHOWNAT:
		ATFD1_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(3, "new file uid", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_GID)) {
			tok = au_to_arg32(4, "new file gid", ar->ar_arg_gid);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_EXCHANGEDATA:
		UPATH1_VNODE1_TOKENS;
		UPATH2_TOKENS;
		break;

	case AUE_CLOSE:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_CLOSEFROM:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		break;

	case AUE_CORE:
		if (ARG_IS_VALID(kar, ARG_SIGNUM)) {
			tok = au_to_arg32(1, "signal", ar->ar_arg_signum);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_EXTATTRCTL:
		UPATH1_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "cmd", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		/* extattrctl(2) filename parameter is in upath2/vnode2 */
		UPATH2_TOKENS;
		VNODE2_TOKENS;
		EXTATTR_TOKENS(4);
		break;

	case AUE_EXTATTR_GET_FILE:
	case AUE_EXTATTR_SET_FILE:
	case AUE_EXTATTR_LIST_FILE:
	case AUE_EXTATTR_DELETE_FILE:
	case AUE_EXTATTR_GET_LINK:
	case AUE_EXTATTR_SET_LINK:
	case AUE_EXTATTR_LIST_LINK:
	case AUE_EXTATTR_DELETE_LINK:
		UPATH1_VNODE1_TOKENS;
		EXTATTR_TOKENS(2);
		break;

	case AUE_EXTATTR_GET_FD:
	case AUE_EXTATTR_SET_FD:
	case AUE_EXTATTR_LIST_FD:
	case AUE_EXTATTR_DELETE_FD:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(2, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		EXTATTR_TOKENS(2);
		break;

	case AUE_FEXECVE:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_EXECVE:
	case AUE_MAC_EXECVE:
		if (ARG_IS_VALID(kar, ARG_ARGV)) {
			tok = au_to_exec_args(ar->ar_arg_argv,
			    ar->ar_arg_argc);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_ENVV)) {
			tok = au_to_exec_env(ar->ar_arg_envv,
			    ar->ar_arg_envc);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_FCHMOD:
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(2, "new file mode",
			    ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		FD_VNODE1_TOKENS;
		break;

	/*
	 * XXXRW: Some of these need to handle non-vnode cases as well.
	 */
	case AUE_FCHDIR:
	case AUE_FPATHCONF:
	case AUE_FSTAT:
	case AUE_FSTATFS:
	case AUE_FSYNC:
	case AUE_FTRUNCATE:
	case AUE_FUTIMES:
	case AUE_GETDIRENTRIES:
	case AUE_GETDIRENTRIESATTR:
	case AUE_LSEEK:
	case AUE_POLL:
	case AUE_POSIX_FALLOCATE:
	case AUE_PREAD:
	case AUE_PWRITE:
	case AUE_READ:
	case AUE_READV:
	case AUE_WRITE:
	case AUE_WRITEV:
		FD_VNODE1_TOKENS;
		break;

	case AUE_FCHOWN:
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(2, "new file uid", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_GID)) {
			tok = au_to_arg32(3, "new file gid", ar->ar_arg_gid);
			kau_write(rec, tok);
		}
		FD_VNODE1_TOKENS;
		break;

	case AUE_FCNTL:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "cmd",
			    au_fcntl_cmd_to_bsm(ar->ar_arg_cmd));
			kau_write(rec, tok);
		}
		FD_VNODE1_TOKENS;
		break;

	case AUE_FCHFLAGS:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		FD_VNODE1_TOKENS;
		break;

	case AUE_FLOCK:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "operation", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		FD_VNODE1_TOKENS;
		break;

	case AUE_RFORK:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(1, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_FORK:
	case AUE_VFORK:
		if (ARG_IS_VALID(kar, ARG_PID)) {
			tok = au_to_arg32(0, "child PID", ar->ar_arg_pid);
			kau_write(rec, tok);
		}
		break;

	case AUE_IOCTL:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "cmd", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_VNODE1))
			FD_VNODE1_TOKENS;
		else {
			if (ARG_IS_VALID(kar, ARG_SOCKINFO)) {
				tok = kau_to_socket(&ar->ar_arg_sockinfo);
				kau_write(rec, tok);
			} else {
				if (ARG_IS_VALID(kar, ARG_FD)) {
					tok = au_to_arg32(1, "fd",
					    ar->ar_arg_fd);
					kau_write(rec, tok);
				}
			}
		}
		break;

	case AUE_KILL:
	case AUE_KILLPG:
		if (ARG_IS_VALID(kar, ARG_SIGNUM)) {
			tok = au_to_arg32(2, "signal", ar->ar_arg_signum);
			kau_write(rec, tok);
		}
		PROCESS_PID_TOKENS(1);
		break;

	case AUE_KTRACE:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "ops", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(3, "trpoints", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		PROCESS_PID_TOKENS(4);
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_LINK:
	case AUE_LINKAT:
	case AUE_RENAME:
	case AUE_RENAMEAT:
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		ATFD2_TOKENS(3);
		UPATH2_TOKENS;
		break;

	case AUE_LOADSHFILE:
		ADDR_TOKEN(4, "base addr");
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_MKDIR:
	case AUE_MKDIRAT:
	case AUE_MKFIFO:
	case AUE_MKFIFOAT:
		ATFD1_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(2, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_MKNOD:
	case AUE_MKNODAT:
		ATFD1_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(2, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_DEV)) {
			tok = au_to_arg32(3, "dev", ar->ar_arg_dev);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_MMAP:
	case AUE_MUNMAP:
	case AUE_MPROTECT:
	case AUE_MLOCK:
	case AUE_MUNLOCK:
	case AUE_MINHERIT:
		ADDR_TOKEN(1, "addr");
		if (ARG_IS_VALID(kar, ARG_LEN)) {
			tok = au_to_arg32(2, "len", ar->ar_arg_len);
			kau_write(rec, tok);
		}
		if (ar->ar_event == AUE_MMAP)
			FD_VNODE1_TOKENS;
		if (ar->ar_event == AUE_MPROTECT) {
			if (ARG_IS_VALID(kar, ARG_VALUE)) {
				tok = au_to_arg32(3, "protection",
				    ar->ar_arg_value);
				kau_write(rec, tok);
			}
		}
		if (ar->ar_event == AUE_MINHERIT) {
			if (ARG_IS_VALID(kar, ARG_VALUE)) {
				tok = au_to_arg32(3, "inherit",
				    ar->ar_arg_value);
				kau_write(rec, tok);
			}
		}
		break;

	case AUE_MOUNT:
	case AUE_NMOUNT:
		/* XXX Need to handle NFS mounts */
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(3, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_TEXT)) {
			tok = au_to_text(ar->ar_arg_text);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_NFS_SVC:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(1, "flags", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		break;

	case AUE_UMOUNT:
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_TEXT)) {
			tok = au_to_text(ar->ar_arg_text);
			kau_write(rec, tok);
		}
		break;

	case AUE_MSGCTL:
		ar->ar_event = audit_msgctl_to_event(ar->ar_arg_svipc_cmd);
		/* Fall through */

	case AUE_MSGRCV:
	case AUE_MSGSND:
		tok = au_to_arg32(1, "msg ID", ar->ar_arg_svipc_id);
		kau_write(rec, tok);
		if (ar->ar_errno != EINVAL) {
			tok = au_to_ipc(AT_IPC_MSG, ar->ar_arg_svipc_id);
			kau_write(rec, tok);
		}
		break;

	case AUE_MSGGET:
		if (ar->ar_errno == 0) {
			if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
				tok = au_to_ipc(AT_IPC_MSG,
				    ar->ar_arg_svipc_id);
				kau_write(rec, tok);
			}
		}
		break;

	case AUE_RESETSHFILE:
		ADDR_TOKEN(1, "base addr");
		break;

	case AUE_OPEN_RC:
	case AUE_OPEN_RTC:
	case AUE_OPEN_RWC:
	case AUE_OPEN_RWTC:
	case AUE_OPEN_WC:
	case AUE_OPEN_WTC:
	case AUE_CREAT:
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(3, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_OPEN_R:
	case AUE_OPEN_RT:
	case AUE_OPEN_RW:
	case AUE_OPEN_RWT:
	case AUE_OPEN_W:
	case AUE_OPEN_WT:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_OPENAT_RC:
	case AUE_OPENAT_RTC:
	case AUE_OPENAT_RWC:
	case AUE_OPENAT_RWTC:
	case AUE_OPENAT_WC:
	case AUE_OPENAT_WTC:
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(3, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_OPENAT_R:
	case AUE_OPENAT_RT:
	case AUE_OPENAT_RW:
	case AUE_OPENAT_RWT:
	case AUE_OPENAT_W:
	case AUE_OPENAT_WT:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_PROCCTL:
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(1, "idtype", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "com", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		PROCESS_PID_TOKENS(3);
		break;

	case AUE_PTRACE:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(1, "request", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(4, "data", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		PROCESS_PID_TOKENS(2);
		break;

	case AUE_QUOTACTL:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(2, "command", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(3, "uid", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_GID)) {
			tok = au_to_arg32(3, "gid", ar->ar_arg_gid);
			kau_write(rec, tok);
		}
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_REBOOT:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(1, "howto", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		break;

	case AUE_SEMCTL:
		ar->ar_event = audit_semctl_to_event(ar->ar_arg_svipc_cmd);
		/* Fall through */

	case AUE_SEMOP:
		if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
			tok = au_to_arg32(1, "sem ID", ar->ar_arg_svipc_id);
			kau_write(rec, tok);
			if (ar->ar_errno != EINVAL) {
				tok = au_to_ipc(AT_IPC_SEM,
				    ar->ar_arg_svipc_id);
				kau_write(rec, tok);
			}
		}
		break;

	case AUE_SEMGET:
		if (ar->ar_errno == 0) {
			if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
				tok = au_to_ipc(AT_IPC_SEM,
				    ar->ar_arg_svipc_id);
				kau_write(rec, tok);
			}
		}
		break;

	case AUE_SETEGID:
		if (ARG_IS_VALID(kar, ARG_EGID)) {
			tok = au_to_arg32(1, "egid", ar->ar_arg_egid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETEUID:
		if (ARG_IS_VALID(kar, ARG_EUID)) {
			tok = au_to_arg32(1, "euid", ar->ar_arg_euid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETREGID:
		if (ARG_IS_VALID(kar, ARG_RGID)) {
			tok = au_to_arg32(1, "rgid", ar->ar_arg_rgid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_EGID)) {
			tok = au_to_arg32(2, "egid", ar->ar_arg_egid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETREUID:
		if (ARG_IS_VALID(kar, ARG_RUID)) {
			tok = au_to_arg32(1, "ruid", ar->ar_arg_ruid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_EUID)) {
			tok = au_to_arg32(2, "euid", ar->ar_arg_euid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETRESGID:
		if (ARG_IS_VALID(kar, ARG_RGID)) {
			tok = au_to_arg32(1, "rgid", ar->ar_arg_rgid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_EGID)) {
			tok = au_to_arg32(2, "egid", ar->ar_arg_egid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SGID)) {
			tok = au_to_arg32(3, "sgid", ar->ar_arg_sgid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETRESUID:
		if (ARG_IS_VALID(kar, ARG_RUID)) {
			tok = au_to_arg32(1, "ruid", ar->ar_arg_ruid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_EUID)) {
			tok = au_to_arg32(2, "euid", ar->ar_arg_euid);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SUID)) {
			tok = au_to_arg32(3, "suid", ar->ar_arg_suid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETGID:
		if (ARG_IS_VALID(kar, ARG_GID)) {
			tok = au_to_arg32(1, "gid", ar->ar_arg_gid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETUID:
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(1, "uid", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETGROUPS:
		if (ARG_IS_VALID(kar, ARG_GROUPSET)) {
			for(ctr = 0; ctr < ar->ar_arg_groups.gidset_size; ctr++)
			{
				tok = au_to_arg32(1, "setgroups",
				    ar->ar_arg_groups.gidset[ctr]);
				kau_write(rec, tok);
			}
		}
		break;

	case AUE_SETLOGIN:
		if (ARG_IS_VALID(kar, ARG_LOGIN)) {
			tok = au_to_text(ar->ar_arg_login);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETPRIORITY:
		if (ARG_IS_VALID(kar, ARG_CMD)) {
			tok = au_to_arg32(1, "which", ar->ar_arg_cmd);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_UID)) {
			tok = au_to_arg32(2, "who", ar->ar_arg_uid);
			kau_write(rec, tok);
		}
		PROCESS_PID_TOKENS(2);
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(3, "priority", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		break;

	case AUE_SETPRIVEXEC:
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(1, "flag", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		break;

	/* AUE_SHMAT, AUE_SHMCTL, AUE_SHMDT and AUE_SHMGET are SysV IPC */
	case AUE_SHMAT:
		if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
			tok = au_to_arg32(1, "shmid", ar->ar_arg_svipc_id);
			kau_write(rec, tok);
			/* XXXAUDIT: Does having the ipc token make sense? */
			tok = au_to_ipc(AT_IPC_SHM, ar->ar_arg_svipc_id);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SVIPC_ADDR)) {
			tok = au_to_arg32(2, "shmaddr",
			    (int)(uintptr_t)ar->ar_arg_svipc_addr);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SVIPC_PERM)) {
			tok = au_to_ipc_perm(&ar->ar_arg_svipc_perm);
			kau_write(rec, tok);
		}
		break;

	case AUE_SHMCTL:
		if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
			tok = au_to_arg32(1, "shmid", ar->ar_arg_svipc_id);
			kau_write(rec, tok);
			/* XXXAUDIT: Does having the ipc token make sense? */
			tok = au_to_ipc(AT_IPC_SHM, ar->ar_arg_svipc_id);
			kau_write(rec, tok);
		}
		switch (ar->ar_arg_svipc_cmd) {
		case IPC_STAT:
			ar->ar_event = AUE_SHMCTL_STAT;
			break;
		case IPC_RMID:
			ar->ar_event = AUE_SHMCTL_RMID;
			break;
		case IPC_SET:
			ar->ar_event = AUE_SHMCTL_SET;
			if (ARG_IS_VALID(kar, ARG_SVIPC_PERM)) {
				tok = au_to_ipc_perm(&ar->ar_arg_svipc_perm);
				kau_write(rec, tok);
			}
			break;
		default:
			break;	/* We will audit a bad command */
		}
		break;

	case AUE_SHMDT:
		if (ARG_IS_VALID(kar, ARG_SVIPC_ADDR)) {
			tok = au_to_arg32(1, "shmaddr",
			    (int)(uintptr_t)ar->ar_arg_svipc_addr);
			kau_write(rec, tok);
		}
		break;

	case AUE_SHMGET:
		/* This is unusual; the return value is in an argument token */
		if (ARG_IS_VALID(kar, ARG_SVIPC_ID)) {
			tok = au_to_arg32(0, "shmid", ar->ar_arg_svipc_id);
			kau_write(rec, tok);
			tok = au_to_ipc(AT_IPC_SHM, ar->ar_arg_svipc_id);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_SVIPC_PERM)) {
			tok = au_to_ipc_perm(&ar->ar_arg_svipc_perm);
			kau_write(rec, tok);
		}
		break;

	/* AUE_SHMOPEN, AUE_SHMUNLINK, AUE_SEMOPEN, AUE_SEMCLOSE
	 * and AUE_SEMUNLINK are Posix IPC */
	case AUE_SHMOPEN:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(3, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_SHMUNLINK:
		UPATH1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_POSIX_IPC_PERM)) {
			struct ipc_perm perm;

			perm.uid = ar->ar_arg_pipc_perm.pipc_uid;
			perm.gid = ar->ar_arg_pipc_perm.pipc_gid;
			perm.cuid = ar->ar_arg_pipc_perm.pipc_uid;
			perm.cgid = ar->ar_arg_pipc_perm.pipc_gid;
			perm.mode = ar->ar_arg_pipc_perm.pipc_mode;
			perm.seq = 0;
			perm.key = 0;
			tok = au_to_ipc_perm(&perm);
			kau_write(rec, tok);
		}
		break;

	case AUE_SEMOPEN:
		if (ARG_IS_VALID(kar, ARG_FFLAGS)) {
			tok = au_to_arg32(2, "flags", ar->ar_arg_fflags);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_MODE)) {
			tok = au_to_arg32(3, "mode", ar->ar_arg_mode);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(4, "value", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		/* FALLTHROUGH */

	case AUE_SEMUNLINK:
		if (ARG_IS_VALID(kar, ARG_TEXT)) {
			tok = au_to_text(ar->ar_arg_text);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_POSIX_IPC_PERM)) {
			struct ipc_perm perm;

			perm.uid = ar->ar_arg_pipc_perm.pipc_uid;
			perm.gid = ar->ar_arg_pipc_perm.pipc_gid;
			perm.cuid = ar->ar_arg_pipc_perm.pipc_uid;
			perm.cgid = ar->ar_arg_pipc_perm.pipc_gid;
			perm.mode = ar->ar_arg_pipc_perm.pipc_mode;
			perm.seq = 0;
			perm.key = 0;
			tok = au_to_ipc_perm(&perm);
			kau_write(rec, tok);
		}
		break;

	case AUE_SEMCLOSE:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "sem", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		break;

	case AUE_SYMLINK:
	case AUE_SYMLINKAT:
		if (ARG_IS_VALID(kar, ARG_TEXT)) {
			tok = au_to_text(ar->ar_arg_text);
			kau_write(rec, tok);
		}
		ATFD1_TOKENS(1);
		UPATH1_VNODE1_TOKENS;
		break;

	case AUE_SYSCTL:
	case AUE_SYSCTL_NONADMIN:
		if (ARG_IS_VALID(kar, ARG_CTLNAME | ARG_LEN)) {
			for (ctr = 0; ctr < ar->ar_arg_len; ctr++) {
				tok = au_to_arg32(1, "name",
				    ar->ar_arg_ctlname[ctr]);
				kau_write(rec, tok);
			}
		}
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(5, "newval", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		if (ARG_IS_VALID(kar, ARG_TEXT)) {
			tok = au_to_text(ar->ar_arg_text);
			kau_write(rec, tok);
		}
		break;

	case AUE_UMASK:
		if (ARG_IS_VALID(kar, ARG_MASK)) {
			tok = au_to_arg32(1, "new mask", ar->ar_arg_mask);
			kau_write(rec, tok);
		}
		tok = au_to_arg32(0, "prev mask", ar->ar_retval);
		kau_write(rec, tok);
		break;

	case AUE_WAIT4:
	case AUE_WAIT6:
		PROCESS_PID_TOKENS(1);
		if (ARG_IS_VALID(kar, ARG_VALUE)) {
			tok = au_to_arg32(3, "options", ar->ar_arg_value);
			kau_write(rec, tok);
		}
		break;

	case AUE_CAP_RIGHTS_LIMIT:
		/*
		 * XXXRW/XXXJA: Would be nice to audit socket/etc information.
		 */
		FD_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_RIGHTS)) {
			tok = au_to_rights(&ar->ar_arg_rights);
			kau_write(rec, tok);
		}
		break;

	case AUE_CAP_FCNTLS_GET:
	case AUE_CAP_IOCTLS_GET:
	case AUE_CAP_IOCTLS_LIMIT:
	case AUE_CAP_RIGHTS_GET:
		if (ARG_IS_VALID(kar, ARG_FD)) {
			tok = au_to_arg32(1, "fd", ar->ar_arg_fd);
			kau_write(rec, tok);
		}
		break;

	case AUE_CAP_FCNTLS_LIMIT:
		FD_VNODE1_TOKENS;
		if (ARG_IS_VALID(kar, ARG_FCNTL_RIGHTS)) {
			tok = au_to_arg32(2, "fcntlrights",
			    ar->ar_arg_fcntl_rights);
			kau_write(rec, tok);
		}
		break;

	case AUE_CAP_ENTER:
	case AUE_CAP_GETMODE:
		break;

	case AUE_NULL:
	default:
		printf("BSM conversion requested for unknown event %d\n",
		    ar->ar_event);

		/*
		 * Write the subject token so it is properly freed here.
		 */
		if (jail_tok != NULL)
			kau_write(rec, jail_tok);
		kau_write(rec, subj_tok);
		kau_free(rec);
		return (BSM_NOAUDIT);
	}

	if (jail_tok != NULL)
		kau_write(rec, jail_tok);
	kau_write(rec, subj_tok);
	tok = au_to_return32(au_errno_to_bsm(ar->ar_errno), ar->ar_retval);
	kau_write(rec, tok);  /* Every record gets a return token */

	kau_close(rec, &ar->ar_endtime, ar->ar_event);

	*pau = rec;
	return (BSM_SUCCESS);
}

/*
 * Verify that a record is a valid BSM record. This verification is simple
 * now, but may be expanded on sometime in the future.  Return 1 if the
 * record is good, 0 otherwise.
 */
int
bsm_rec_verify(void *rec)
{
	char c = *(char *)rec;

	/*
	 * Check the token ID of the first token; it has to be a header
	 * token.
	 *
	 * XXXAUDIT There needs to be a token structure to map a token.
	 * XXXAUDIT 'Shouldn't be simply looking at the first char.
	 */
	if ((c != AUT_HEADER32) && (c != AUT_HEADER32_EX) &&
	    (c != AUT_HEADER64) && (c != AUT_HEADER64_EX))
		return (0);
	return (1);
}
