/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/msg/unimsglib.h,v 1.6 2004/07/08 08:22:07 brandt Exp $
 */
#ifndef _NETNATM_MSG_UNIMSGLIB_H_
#define _NETNATM_MSG_UNIMSGLIB_H_

#include <netnatm/msg/uni_config.h>

struct uni_msg;

enum uni_ierr_type {
	UNI_IERR_UNK,	/* unknown IE */
	UNI_IERR_LEN,	/* length error */
	UNI_IERR_BAD,	/* content error */
	UNI_IERR_ACC,	/* access element content error */
	UNI_IERR_MIS,	/* mandatory IE missing (not used here) */
};

struct uni_ierr {
	enum uni_ierr_type	err;	/* what error */
	enum uni_ieact		act;	/* the action indicator */
	u_int			ie:8;	/* the ie type */
	u_int			man:1;	/* mandatory flag */
	u_int			epref:1;/* Q.2971 9.5.3.2.1 low-pri epref */
};

/*
 * Context buffer. Needed to reduce number of arguments to routines.
 */
struct unicx {
	/*
	 * globals for error handling
	 */
	u_int		errcnt;		/* number of bad IEs */
	struct uni_ierr	err[UNI_MAX_ERRIE]; /* the errors */

	int		q2932;		/* Enable GFP */
	int		pnni;		/* Enable PNNI */

	int		git_hard;	/* do hard check on GIT IE */
	int		bearer_hard;	/* do hard check on BEARER IE */
	int		cause_hard;	/* do hard check on cause */

	int		multiline;	/* printing mode */
	u_int		tabsiz;		/* tabulation size */

	/*
	 * Internal context of library -- don't touch
	 */
	struct uni_ie_repeat	repeat;	/* repeat IE during decoding */
	enum uni_ietype		ielast;	/* last IE seen for repeat handling */

	const char 		*prefix[20];
	u_int			nprefix;
	int			doindent;
	char			*buf;
	size_t			bufsiz;
	u_int			indent;		/* indentation */
	int			dont_init;
};

/*
 * Functions for all messages
 */
void uni_print_cref(char *, size_t, const struct uni_cref *, struct unicx *);
void uni_print_msghdr(char *, size_t, const struct uni_msghdr *, struct unicx *);
void uni_print(char *, size_t, const struct uni_all *, struct unicx *);
void uni_print_msg(char *, size_t, u_int _mtype, const union uni_msgall *,
    struct unicx *);
int uni_encode(struct uni_msg *, struct uni_all *, struct unicx *);
int uni_decode(struct uni_msg *, struct uni_all *, struct unicx *);

int uni_decode_head(struct uni_msg *, struct uni_all *, struct unicx *);
int uni_decode_body(struct uni_msg *, struct uni_all *, struct unicx *);

int uni_encode_msg_hdr(struct uni_msg *, struct uni_msghdr *, enum uni_msgtype,
    struct unicx *, int *);


/*
 * Functions for all information elements
 */
void uni_print_ie(char *, size_t, enum uni_ietype, const union uni_ieall *,
    struct unicx *);
int uni_check_ie(enum uni_ietype, union uni_ieall *, struct unicx *);
int uni_encode_ie(enum uni_ietype, struct uni_msg *, union uni_ieall *,
    struct unicx *);
int uni_decode_ie_hdr(enum uni_ietype *, struct uni_iehdr *, struct uni_msg *,
    struct unicx *, u_int *);
int uni_encode_ie_hdr(struct uni_msg *, enum uni_ietype, struct uni_iehdr *,
    u_int, struct unicx *);
int uni_decode_ie_body(enum uni_ietype, union uni_ieall *, struct uni_msg *,
    u_int, struct unicx *);


/*
 * Context handling
 */
void uni_initcx(struct unicx *);
void uni_print_cx(char *, size_t, struct unicx *);

#define	UNI_SAVE_IERR(CX, IETYPE, ACT, ERRCODE)				\
	(((CX)->errcnt < UNI_MAX_ERRIE) ?				\
	 ((CX)->err[(CX)->errcnt].ie = (IETYPE),			\
	  (CX)->err[(CX)->errcnt].act = (ACT),				\
	  (CX)->err[(CX)->errcnt].err = (ERRCODE),			\
	  (CX)->err[(CX)->errcnt].man = 0,				\
	  (CX)->errcnt++,						\
	  1) : 0)

/*
 * Traffic classification
 */
enum uni_traffic_class {
	UNI_TRAFFIC_CBR1,
	UNI_TRAFFIC_CBR2,
	UNI_TRAFFIC_CBR3,
	UNI_TRAFFIC_rtVBR1,
	UNI_TRAFFIC_rtVBR2,
	UNI_TRAFFIC_rtVBR3,
	UNI_TRAFFIC_rtVBR4,
	UNI_TRAFFIC_rtVBR5,
	UNI_TRAFFIC_rtVBR6,
	UNI_TRAFFIC_nrtVBR1,
	UNI_TRAFFIC_nrtVBR2,
	UNI_TRAFFIC_nrtVBR3,
	UNI_TRAFFIC_nrtVBR4,
	UNI_TRAFFIC_nrtVBR5,
	UNI_TRAFFIC_nrtVBR6,
	UNI_TRAFFIC_ABR,
	UNI_TRAFFIC_UBR1,
	UNI_TRAFFIC_UBR2,
};

/* classify traffic */
int uni_classify_traffic(const struct uni_ie_bearer *,
    const struct uni_ie_traffic *,
    enum uni_traffic_class *, enum uni_traffic_class *,
    char *, size_t);

#endif
