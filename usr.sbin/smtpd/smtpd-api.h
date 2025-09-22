/*	$OpenBSD: smtpd-api.h,v 1.37 2024/06/09 10:13:05 gilles Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
 * Copyright (c) 2011 Gilles Chehade <gilles@poolp.org>
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

#ifndef	_SMTPD_API_H_
#define	_SMTPD_API_H_

#include "dict.h"
#include "tree.h"

struct mailaddr {
	char	user[SMTPD_MAXLOCALPARTSIZE];
	char	domain[SMTPD_MAXDOMAINPARTSIZE];
};

#define PROC_QUEUE_API_VERSION	2

enum {
	PROC_QUEUE_OK,
	PROC_QUEUE_FAIL,
	PROC_QUEUE_INIT,
	PROC_QUEUE_CLOSE,
	PROC_QUEUE_MESSAGE_CREATE,
	PROC_QUEUE_MESSAGE_DELETE,
	PROC_QUEUE_MESSAGE_COMMIT,
	PROC_QUEUE_MESSAGE_FD_R,
	PROC_QUEUE_ENVELOPE_CREATE,
	PROC_QUEUE_ENVELOPE_DELETE,
	PROC_QUEUE_ENVELOPE_LOAD,
	PROC_QUEUE_ENVELOPE_UPDATE,
	PROC_QUEUE_ENVELOPE_WALK,
};

#define PROC_SCHEDULER_API_VERSION	2

struct scheduler_info;

enum {
	PROC_SCHEDULER_OK,
	PROC_SCHEDULER_FAIL,
	PROC_SCHEDULER_INIT,
	PROC_SCHEDULER_INSERT,
	PROC_SCHEDULER_COMMIT,
	PROC_SCHEDULER_ROLLBACK,
	PROC_SCHEDULER_UPDATE,
	PROC_SCHEDULER_DELETE,
	PROC_SCHEDULER_HOLD,
	PROC_SCHEDULER_RELEASE,
	PROC_SCHEDULER_BATCH,
	PROC_SCHEDULER_MESSAGES,
	PROC_SCHEDULER_ENVELOPES,
	PROC_SCHEDULER_SCHEDULE,
	PROC_SCHEDULER_REMOVE,
	PROC_SCHEDULER_SUSPEND,
	PROC_SCHEDULER_RESUME,
};

enum envelope_flags {
	EF_AUTHENTICATED	= 0x01,
	EF_BOUNCE		= 0x02,
	EF_INTERNAL		= 0x04, /* Internal expansion forward */

	/* runstate, not saved on disk */

	EF_PENDING		= 0x10,
	EF_INFLIGHT		= 0x20,
	EF_SUSPEND		= 0x40,
	EF_HOLD			= 0x80,
};

struct evpstate {
	uint64_t		evpid;
	uint16_t		flags;
	uint16_t		retry;
	time_t			time;
};

enum delivery_type {
	D_MDA,
	D_MTA,
	D_BOUNCE,
};

struct scheduler_info {
	uint64_t		evpid;
	enum delivery_type	type;
	uint16_t		retry;
	time_t			creation;
	time_t			ttl;
	time_t			lasttry;
	time_t			lastbounce;
	time_t			nexttry;
};

#define SCHED_REMOVE		0x01
#define SCHED_EXPIRE		0x02
#define SCHED_UPDATE		0x04
#define SCHED_BOUNCE		0x08
#define SCHED_MDA		0x10
#define SCHED_MTA		0x20

#define PROC_TABLE_API_VERSION	2

struct table_open_params {
	uint32_t	version;
	char		name[LINE_MAX];
};

enum table_service {
	K_NONE		= 0x000,
	K_ALIAS		= 0x001,	/* returns struct expand	*/
	K_DOMAIN	= 0x002,	/* returns struct destination	*/
	K_CREDENTIALS	= 0x004,	/* returns struct credentials	*/
	K_NETADDR	= 0x008,	/* returns struct netaddr	*/
	K_USERINFO	= 0x010,	/* returns struct userinfo	*/
	K_SOURCE	= 0x020,	/* returns struct source	*/
	K_MAILADDR	= 0x040,	/* returns struct mailaddr	*/
	K_ADDRNAME	= 0x080,	/* returns struct addrname	*/
	K_MAILADDRMAP	= 0x100,	/* returns struct maddrmap	*/
	K_RELAYHOST	= 0x200,	/* returns struct relayhost	*/
	K_STRING	= 0x400,
	K_REGEX		= 0x800,
	K_AUTH		= 0x1000,
};
#define K_ANY		  0xffff

enum {
	PROC_TABLE_OK,
	PROC_TABLE_FAIL,
	PROC_TABLE_OPEN,
	PROC_TABLE_CLOSE,
	PROC_TABLE_UPDATE,
	PROC_TABLE_CHECK,
	PROC_TABLE_LOOKUP,
	PROC_TABLE_FETCH,
};

enum enhanced_status_code {
	/* 0.0 */
	ESC_OTHER_STATUS				= 00,

	/* 1.x */
	ESC_OTHER_ADDRESS_STATUS			= 10,
	ESC_BAD_DESTINATION_MAILBOX_ADDRESS		= 11,
	ESC_BAD_DESTINATION_SYSTEM_ADDRESS		= 12,
	ESC_BAD_DESTINATION_MAILBOX_ADDRESS_SYNTAX     	= 13,
	ESC_DESTINATION_MAILBOX_ADDRESS_AMBIGUOUS	= 14,
	ESC_DESTINATION_ADDRESS_VALID			= 15,
	ESC_DESTINATION_MAILBOX_HAS_MOVED      		= 16,
	ESC_BAD_SENDER_MAILBOX_ADDRESS_SYNTAX		= 17,
	ESC_BAD_SENDER_SYSTEM_ADDRESS			= 18,

	/* 2.x */
	ESC_OTHER_MAILBOX_STATUS			= 20,
	ESC_MAILBOX_DISABLED				= 21,
	ESC_MAILBOX_FULL				= 22,
	ESC_MESSAGE_LENGTH_TOO_LARGE   			= 23,
	ESC_MAILING_LIST_EXPANSION_PROBLEM		= 24,

	/* 3.x */
	ESC_OTHER_MAIL_SYSTEM_STATUS			= 30,
	ESC_MAIL_SYSTEM_FULL				= 31,
	ESC_SYSTEM_NOT_ACCEPTING_MESSAGES		= 32,
	ESC_SYSTEM_NOT_CAPABLE_OF_SELECTED_FEATURES    	= 33,
	ESC_MESSAGE_TOO_BIG_FOR_SYSTEM		    	= 34,
	ESC_SYSTEM_INCORRECTLY_CONFIGURED      	    	= 35,

	/* 4.x */
	ESC_OTHER_NETWORK_ROUTING_STATUS      	    	= 40,
	ESC_NO_ANSWER_FROM_HOST		      	    	= 41,
	ESC_BAD_CONNECTION		      	    	= 42,
	ESC_DIRECTORY_SERVER_FAILURE   	      	    	= 43,
	ESC_UNABLE_TO_ROUTE	   	      	    	= 44,
	ESC_MAIL_SYSTEM_CONGESTION   	      	    	= 45,
	ESC_ROUTING_LOOP_DETECTED   	      	    	= 46,
	ESC_DELIVERY_TIME_EXPIRED   	      	    	= 47,

	/* 5.x */
	ESC_INVALID_RECIPIENT   	      	    	= 50,
	ESC_INVALID_COMMAND	   	      	    	= 51,
	ESC_SYNTAX_ERROR	   	      	    	= 52,
	ESC_TOO_MANY_RECIPIENTS	   	      	    	= 53,
	ESC_INVALID_COMMAND_ARGUMENTS  	      	    	= 54,
	ESC_WRONG_PROTOCOL_VERSION  	      	    	= 55,

	/* 6.x */
	ESC_OTHER_MEDIA_ERROR   	      	    	= 60,
	ESC_MEDIA_NOT_SUPPORTED   	      	    	= 61,
	ESC_CONVERSION_REQUIRED_AND_PROHIBITED		= 62,
	ESC_CONVERSION_REQUIRED_BUT_NOT_SUPPORTED      	= 63,
	ESC_CONVERSION_WITH_LOSS_PERFORMED	     	= 64,
	ESC_CONVERSION_FAILED			     	= 65,

	/* 7.x */
	ESC_OTHER_SECURITY_STATUS      		     	= 70,
	ESC_DELIVERY_NOT_AUTHORIZED_MESSAGE_REFUSED	= 71,
	ESC_MAILING_LIST_EXPANSION_PROHIBITED		= 72,
	ESC_SECURITY_CONVERSION_REQUIRED_NOT_POSSIBLE  	= 73,
	ESC_SECURITY_FEATURES_NOT_SUPPORTED	  	= 74,
	ESC_CRYPTOGRAPHIC_FAILURE			= 75,
	ESC_CRYPTOGRAPHIC_ALGORITHM_NOT_SUPPORTED	= 76,
	ESC_MESSAGE_INTEGRITY_FAILURE			= 77,
};

enum enhanced_status_class {
	ESC_STATUS_OK		= 2,
	ESC_STATUS_TEMPFAIL	= 4,
	ESC_STATUS_PERMFAIL	= 5,
};

static inline uint32_t
evpid_to_msgid(uint64_t evpid)
{
	return (evpid >> 32);
}

static inline uint64_t
msgid_to_evpid(uint32_t msgid)
{
        return ((uint64_t)msgid << 32);
}


/* esc.c */
const char *esc_code(enum enhanced_status_class, enum enhanced_status_code);
const char *esc_description(enum enhanced_status_code);


/* queue */
void queue_api_on_close(int(*)(void));
void queue_api_on_message_create(int(*)(uint32_t *));
void queue_api_on_message_commit(int(*)(uint32_t, const char*));
void queue_api_on_message_delete(int(*)(uint32_t));
void queue_api_on_message_fd_r(int(*)(uint32_t));
void queue_api_on_envelope_create(int(*)(uint32_t, const char *, size_t, uint64_t *));
void queue_api_on_envelope_delete(int(*)(uint64_t));
void queue_api_on_envelope_update(int(*)(uint64_t, const char *, size_t));
void queue_api_on_envelope_load(int(*)(uint64_t, char *, size_t));
void queue_api_on_envelope_walk(int(*)(uint64_t *, char *, size_t));
void queue_api_on_message_walk(int(*)(uint64_t *, char *, size_t,
    uint32_t, int *, void **));
void queue_api_no_chroot(void);
void queue_api_set_chroot(const char *);
void queue_api_set_user(const char *);
int queue_api_dispatch(void);

/* scheduler */
void scheduler_api_on_init(int(*)(void));
void scheduler_api_on_insert(int(*)(struct scheduler_info *));
void scheduler_api_on_commit(size_t(*)(uint32_t));
void scheduler_api_on_rollback(size_t(*)(uint32_t));
void scheduler_api_on_update(int(*)(struct scheduler_info *));
void scheduler_api_on_delete(int(*)(uint64_t));
void scheduler_api_on_hold(int(*)(uint64_t, uint64_t));
void scheduler_api_on_release(int(*)(int, uint64_t, int));
void scheduler_api_on_batch(int(*)(int, int *, size_t *, uint64_t *, int *));
void scheduler_api_on_messages(size_t(*)(uint32_t, uint32_t *, size_t));
void scheduler_api_on_envelopes(size_t(*)(uint64_t, struct evpstate *, size_t));
void scheduler_api_on_schedule(int(*)(uint64_t));
void scheduler_api_on_remove(int(*)(uint64_t));
void scheduler_api_on_suspend(int(*)(uint64_t));
void scheduler_api_on_resume(int(*)(uint64_t));
void scheduler_api_no_chroot(void);
void scheduler_api_set_chroot(const char *);
void scheduler_api_set_user(const char *);
int scheduler_api_dispatch(void);

/* table */
void table_api_on_update(int(*)(void));
void table_api_on_check(int(*)(int, struct dict *, const char *));
void table_api_on_lookup(int(*)(int, struct dict *, const char *, char *, size_t));
void table_api_on_fetch(int(*)(int, struct dict *, char *, size_t));
int table_api_dispatch(void);
const char *table_api_get_name(void);

#endif
