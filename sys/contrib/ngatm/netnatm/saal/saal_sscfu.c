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
 * $Begemot: libunimsg/netnatm/saal/saal_sscfu.c,v 1.4 2004/07/08 08:22:10 brandt Exp $
 *
 * SSCF on the UNI
 */

#include <netnatm/saal/sscfu.h>
#include <netnatm/saal/sscfupriv.h>

#define MKSTR(S)	#S

static const char *const sscf_sigs[] = {
	MKSTR(SAAL_ESTABLISH_request),
	MKSTR(SAAL_ESTABLISH_indication),
	MKSTR(SAAL_ESTABLISH_confirm),
	MKSTR(SAAL_RELEASE_request),
	MKSTR(SAAL_RELEASE_confirm),
	MKSTR(SAAL_RELEASE_indication),
	MKSTR(SAAL_DATA_request),
	MKSTR(SAAL_DATA_indication),
	MKSTR(SAAL_UDATA_request),
	MKSTR(SAAL_UDATA_indication),
};

static const char *const sscf_states[] = {
	MKSTR(SSCF_RELEASED),
	MKSTR(SSCF_AWAITING_ESTABLISH),
	MKSTR(SSCF_AWAITING_RELEASE),
	MKSTR(SSCF_ESTABLISHED),
	MKSTR(SSCF_RESYNC),
};

#define AA_SIG(S,G,M) \
	((S)->funcs->send_upper((S), (S)->aarg, (G), (M)))

#define SSCOP_AASIG(S,G,M,P) \
	((S)->funcs->send_lower((S), (S)->aarg, (G), (M), (P)))

MEMINIT();

static void sscfu_unqueue(struct sscfu *sscf);

/************************************************************/
/*
 * INSTANCE AND CLASS MANAGEMENT
 */

/*
 * Initialize SSCF.
 */
struct sscfu *
sscfu_create(void *a, const struct sscfu_funcs *funcs)
{
	struct sscfu *sscf;

	MEMZALLOC(sscf, struct sscfu *, sizeof(struct sscfu));
	if (sscf == NULL)
		return (NULL);

	sscf->funcs = funcs;
	sscf->aarg = a;
	sscf->state = SSCFU_RELEASED;
	sscf->inhand = 0;
	SIGQ_INIT(&sscf->sigs);
	sscf->debug = 0;

	return (sscf);
}

/*
 * Reset the instance. Call only if you know, what you're doing.
 */
void
sscfu_reset(struct sscfu *sscf)
{
	sscf->state = SSCFU_RELEASED;
	sscf->inhand = 0;
	SIGQ_CLEAR(&sscf->sigs);
}

/*
 * Destroy SSCF 
 */
void
sscfu_destroy(struct sscfu *sscf)
{
	SIGQ_CLEAR(&sscf->sigs);
	MEMFREE(sscf);
}

enum sscfu_state
sscfu_getstate(const struct sscfu *sscf)
{
	return (sscf->state);
}

u_int
sscfu_getdefparam(struct sscop_param *p)
{
	memset(p, 0, sizeof(*p));

	p->timer_cc = 1000;
	p->timer_poll = 750;
	p->timer_keep_alive = 2000;
	p->timer_no_response = 7000;
	p->timer_idle = 15000;
	p->maxk = 4096;
	p->maxj = 4096;
	p->maxcc = 4;
	p->maxpd = 25;

	return (SSCOP_SET_TCC | SSCOP_SET_TPOLL | SSCOP_SET_TKA |
	    SSCOP_SET_TNR | SSCOP_SET_TIDLE | SSCOP_SET_MAXK |
	    SSCOP_SET_MAXJ | SSCOP_SET_MAXCC | SSCOP_SET_MAXPD);
}

const char *
sscfu_signame(enum saal_sig sig)
{
	static char str[40];

	if (sig >= sizeof(sscf_sigs)/sizeof(sscf_sigs[0])) {
		sprintf(str, "BAD SAAL_SIGNAL %u", sig);
		return (str);
	} else {
		return (sscf_sigs[sig]);
	}
}

const char *
sscfu_statename(enum sscfu_state s)
{
	static char str[40];

	if (s >= sizeof(sscf_states)/sizeof(sscf_states[0])) {
		sprintf(str, "BAD SSCFU state %u", s);
		return (str);
	} else {
		return (sscf_states[s]);
	}
}

/************************************************************/
/*
 * EXTERNAL INPUT SIGNAL MAPPING
 */
static __inline void
set_state(struct sscfu *sscf, enum sscfu_state state)
{
	VERBOSE(sscf, SSCFU_DBG_STATE, (sscf, sscf->aarg,
	    "change state from %s to %s",
	    sscf_states[sscf->state], sscf_states[state]));
	sscf->state = state;
}

/*
 * signal from SSCOP to SSCF
 * Message must be freed by the user specified handler, if
 * it is passed.
 */
void
sscfu_input(struct sscfu *sscf, enum sscop_aasig sig,
    struct SSCFU_MBUF_T *m, u_int arg __unused)
{
	sscf->inhand = 1;

	VERBOSE(sscf, SSCFU_DBG_LSIG, (sscf, sscf->aarg,
	    "SSCF got signal %d. in state %s", sig, sscf_states[sscf->state]));

	switch (sig) {

	  case SSCOP_RELEASE_indication:
		/* arg is: UU, SRC */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_AWAITING_ESTABLISH:
			set_state(sscf, SSCFU_RELEASED);
			AA_SIG(sscf, SAAL_RELEASE_indication, m);
			break;

		  case SSCFU_AWAITING_RELEASE:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_ESTABLISHED:
			set_state(sscf, SSCFU_RELEASED);
			AA_SIG(sscf, SAAL_RELEASE_indication, m);
			break;

		  case SSCFU_RESYNC:
			set_state(sscf, SSCFU_RELEASED);
			AA_SIG(sscf, SAAL_RELEASE_indication, m);
			break;
		}
		break;

	  case SSCOP_ESTABLISH_indication:
		/* arg is: UU */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
			set_state(sscf, SSCFU_ESTABLISHED);
			SSCOP_AASIG(sscf, SSCOP_ESTABLISH_response, NULL, 1);
			AA_SIG(sscf, SAAL_ESTABLISH_indication, m);
			break;

		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
		  case SSCFU_ESTABLISHED:
		  case SSCFU_RESYNC:
			if (m)
				MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SSCOP_ESTABLISH_confirm:
		/* arg is: UU */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_AWAITING_ESTABLISH:
			set_state(sscf, SSCFU_ESTABLISHED);
			AA_SIG(sscf, SAAL_ESTABLISH_confirm, m);
			break;

		  case SSCFU_AWAITING_RELEASE:
		  case SSCFU_ESTABLISHED:
		  case SSCFU_RESYNC:
			if (m)
				MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SSCOP_RELEASE_confirm:
		/* arg is: */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
			goto badsig;

		  case SSCFU_AWAITING_RELEASE:
			set_state(sscf, SSCFU_RELEASED);
			AA_SIG(sscf, SAAL_RELEASE_confirm, NULL);
			break;

		  case SSCFU_ESTABLISHED:
		  case SSCFU_RESYNC:
			goto badsig;
		}
		break;

	  case SSCOP_DATA_indication:
		/* arg is: MU */
		sscf->funcs->window(sscf, sscf->aarg, 1);
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
			MBUF_FREE(m);
			goto badsig;

		  case SSCFU_ESTABLISHED:
			AA_SIG(sscf, SAAL_DATA_indication, m);
			break;

		  case SSCFU_RESYNC:
			MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SSCOP_RECOVER_indication:
		/* arg is: */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
			goto badsig;

		  case SSCFU_ESTABLISHED:
			SSCOP_AASIG(sscf, SSCOP_RECOVER_response, NULL, 0);
			AA_SIG(sscf, SAAL_ESTABLISH_indication, NULL);
			break;

		  case SSCFU_RESYNC:
			goto badsig;
		}
		break;

	  case SSCOP_RESYNC_indication:
		/* arg is: UU */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_ESTABLISHED:
			SSCOP_AASIG(sscf, SSCOP_RESYNC_response, NULL, 0);
			AA_SIG(sscf, SAAL_ESTABLISH_indication, m);
			break;

		  case SSCFU_RESYNC:
			if (m)
				MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SSCOP_RESYNC_confirm:
		/* arg is: */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
		  case SSCFU_ESTABLISHED:

		  case SSCFU_RESYNC:
			set_state(sscf, SSCFU_ESTABLISHED);
			AA_SIG(sscf, SAAL_ESTABLISH_confirm, NULL);
			break;
		}
		break;

	  case SSCOP_UDATA_indication:
		/* arg is: MD */
		AA_SIG(sscf, SAAL_UDATA_indication, m);
		break;


	  case SSCOP_RETRIEVE_indication:
		if (m)
			MBUF_FREE(m);
		goto badsig;

	  case SSCOP_RETRIEVE_COMPL_indication:
		goto badsig;

	  case SSCOP_ESTABLISH_request:
	  case SSCOP_RELEASE_request:
	  case SSCOP_ESTABLISH_response:
	  case SSCOP_DATA_request:
	  case SSCOP_RECOVER_response:
	  case SSCOP_RESYNC_request:
	  case SSCOP_RESYNC_response:
	  case SSCOP_UDATA_request:
	  case SSCOP_RETRIEVE_request:
		ASSERT(0);
		break;
	}

	sscfu_unqueue(sscf);
	return;

  badsig:
	VERBOSE(sscf, SSCFU_DBG_ERR, (sscf, sscf->aarg,
	    "bad signal %d. in state %s", sig, sscf_states[sscf->state]));
	sscfu_unqueue(sscf);
}


/*
 * Handle signals from the user
 */
static void
sscfu_dosig(struct sscfu *sscf, enum saal_sig sig, struct SSCFU_MBUF_T *m)
{
	VERBOSE(sscf, SSCFU_DBG_EXEC, (sscf, sscf->aarg,
	    "executing signal %s(%s)",
	    sscf_sigs[sig], sscf_states[sscf->state]));

	switch (sig) {

	  case SAAL_ESTABLISH_request:
		/* arg is opt UU */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
			set_state(sscf, SSCFU_AWAITING_ESTABLISH);
			SSCOP_AASIG(sscf, SSCOP_ESTABLISH_request, m, 1);
			break;

		  case SSCFU_AWAITING_ESTABLISH:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_AWAITING_RELEASE:
			set_state(sscf, SSCFU_AWAITING_ESTABLISH);
			SSCOP_AASIG(sscf, SSCOP_ESTABLISH_request, m, 1);
			break;

		  case SSCFU_ESTABLISHED:
			set_state(sscf, SSCFU_RESYNC);
			SSCOP_AASIG(sscf, SSCOP_RESYNC_request, m, 0);
			break;

		  case SSCFU_RESYNC:
			if (m)
				MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SAAL_RELEASE_request:
		/* arg is opt UU */
		switch(sscf->state) {

		  case SSCFU_RELEASED:
			if (m)
				MBUF_FREE(m);
			AA_SIG(sscf, SAAL_RELEASE_confirm, NULL);
			break;

		  case SSCFU_AWAITING_ESTABLISH:
			set_state(sscf, SSCFU_AWAITING_RELEASE);
			SSCOP_AASIG(sscf, SSCOP_RELEASE_request, m, 0);
			break;

		  case SSCFU_AWAITING_RELEASE:
			if (m)
				MBUF_FREE(m);
			goto badsig;

		  case SSCFU_ESTABLISHED:
			set_state(sscf, SSCFU_AWAITING_RELEASE);
			SSCOP_AASIG(sscf, SSCOP_RELEASE_request, m, 0);
			break;

		  case SSCFU_RESYNC:
			set_state(sscf, SSCFU_AWAITING_RELEASE);
			SSCOP_AASIG(sscf, SSCOP_RELEASE_request, m, 0);
			break;
		}
		break;

	  case SAAL_DATA_request:
		/* arg is DATA */
		switch (sscf->state) {

		  case SSCFU_RELEASED:
		  case SSCFU_AWAITING_ESTABLISH:
		  case SSCFU_AWAITING_RELEASE:
			MBUF_FREE(m);
			goto badsig;

		  case SSCFU_ESTABLISHED:
			SSCOP_AASIG(sscf, SSCOP_DATA_request, m, 0);
			break;

		  case SSCFU_RESYNC:
			MBUF_FREE(m);
			goto badsig;
		}
		break;

	  case SAAL_UDATA_request:
		/* arg is UDATA */
		SSCOP_AASIG(sscf, SSCOP_UDATA_request, m, 0);
		break;

	  case SAAL_ESTABLISH_indication:
	  case SAAL_ESTABLISH_confirm:
	  case SAAL_RELEASE_confirm:
	  case SAAL_RELEASE_indication:
	  case SAAL_DATA_indication:
	  case SAAL_UDATA_indication:
		ASSERT(0);
		break;
	}
	return;

  badsig:
	VERBOSE(sscf, SSCFU_DBG_ERR, (sscf, sscf->aarg,
	    "bad signal %s in state %s", sscf_sigs[sig],
	    sscf_states[sscf->state]));
}

/*
 * Handle user signal.
 */
int
sscfu_saalsig(struct sscfu *sscf, enum saal_sig sig, struct SSCFU_MBUF_T *m)
{
	struct sscfu_sig *s;

	if (sscf->inhand) {
		VERBOSE(sscf, SSCFU_DBG_EXEC, (sscf, sscf->aarg,
		    "queuing user signal %s(%s)",
		    sscf_sigs[sig], sscf_states[sscf->state]));
		SIG_ALLOC(s);
		if (s == NULL)
			return (ENOMEM);
		s->sig = sig;
		s->m = m;
		SIGQ_APPEND(&sscf->sigs, s);
		return (0);
	}

	sscf->inhand = 1;
	sscfu_dosig(sscf, sig, m);
	sscfu_unqueue(sscf);
	return (0);
}

/*
 * Unqueue all qeueued signals. Must be called with inhand==1.
 */
static void
sscfu_unqueue(struct sscfu *sscf)
{
	struct sscfu_sig *s;

	while ((s = SIGQ_GET(&sscf->sigs)) != NULL) {
		sscfu_dosig(sscf, s->sig, s->m);
		SIG_FREE(s);
	}
	sscf->inhand = 0;
}

void
sscfu_setdebug(struct sscfu *sscf, u_int n)
{
	sscf->debug = n;
}

u_int
sscfu_getdebug(const struct sscfu *sscf)
{
	return (sscf->debug);
}
