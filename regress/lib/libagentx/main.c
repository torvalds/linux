/*	$OpenBSD: main.c,v 1.7 2021/05/01 16:55:14 martijn Exp $	*/

/*
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
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

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <arpa/inet.h>

#include <event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include <agentx.h>

#define LINKDOWN 1, 3, 6, 1, 6, 3, 1, 1, 5, 3
#define IFINDEX 1, 3, 6, 1, 2, 1, 2, 2, 1, 1
#define IFADMINSTATUS 1, 3, 6, 1, 2, 1, 2, 2, 1, 7
#define IFOPERSTATUS 1, 3, 6, 1, 2, 1, 2, 2, 1, 8

void regress_fd(struct agentx *, void *, int);
void regress_tryconnect(int, short, void *);
void regress_read(int, short, void *);
void regress_usr1(int, short, void *);
void regress_usr2(int, short, void *);
void regress_scalarinteger(struct agentx_varbind *);
void regress_scalarstring(struct agentx_varbind *);
void regress_scalarnull(struct agentx_varbind *);
void regress_scalaroid(struct agentx_varbind *);
void regress_scalaripaddress(struct agentx_varbind *);
void regress_scalarcounter32(struct agentx_varbind *);
void regress_scalargauge32(struct agentx_varbind *);
void regress_scalartimeticks(struct agentx_varbind *);
void regress_scalaropaque(struct agentx_varbind *);
void regress_scalarcounter64(struct agentx_varbind *);
void regress_scalarerror(struct agentx_varbind *);

void regress_intindex(struct agentx_varbind *);
void regress_intindex2(struct agentx_varbind *);

void regress_strindex(struct agentx_varbind *);
void regress_implstrindex(struct agentx_varbind *);
void regress_strindex2(struct agentx_varbind *);

void regress_oidimplindex(struct agentx_varbind *);

void regress_ipaddressindex(struct agentx_varbind *);

void regress_intindexstaticvalueint(struct agentx_varbind *);
void regress_intindexstaticvaluestring(struct agentx_varbind *);
void regress_intindexstaticanyint(struct agentx_varbind *);
void regress_intindexstaticanystring(struct agentx_varbind *);
void regress_intindexstaticnewint(struct agentx_varbind *);
void regress_intindexstaticnewstring(struct agentx_varbind *);

struct agentx *sa;
struct agentx_session *sas;
struct agentx_context *sac;
struct agentx_agentcaps *saa;
struct agentx_region *regress;

struct agentx_index *regressidx_int;
struct agentx_index *regressidx_int2;

struct agentx_index *regressidx_str;
struct agentx_index *regressidx_str2;

struct agentx_index *regressidx_oid;

struct agentx_index *regressidx_ipaddress;

struct agentx_index *regressidx_new;
struct agentx_index *regressidx_any;
struct agentx_index *regressidx_value;

struct agentx_object *regressobj_scalarinteger;
struct agentx_object *regressobj_scalarstring;
struct agentx_object *regressobj_scalarnull;
struct agentx_object *regressobj_scalaroid;
struct agentx_object *regressobj_scalaripaddress;
struct agentx_object *regressobj_scalarcounter32;
struct agentx_object *regressobj_scalargauge32;
struct agentx_object *regressobj_scalartimeticks;
struct agentx_object *regressobj_scalaropaque;
struct agentx_object *regressobj_scalarcounter64;

struct agentx_object *regressobj_intindexint;
struct agentx_object *regressobj_intindexint2;

struct agentx_object *regressobj_strindexstr;
struct agentx_object *regressobj_implstrindexstr;
struct agentx_object *regressobj_strindexstr2;

struct agentx_object *regressobj_oidimplindexoid;

struct agentx_object *regressobj_ipaddressindexipaddress;

struct agentx_object *regressobj_intindexstaticvalueint;
struct agentx_object *regressobj_intindexstaticvaluestring;
struct agentx_object *regressobj_intindexstaticanyint;
struct agentx_object *regressobj_intindexstaticanystring;
struct agentx_object *regressobj_intindexstaticnewint;
struct agentx_object *regressobj_intindexstaticnewstring;

struct agentx_object *regressobj_scalarerror;

char *path = AGENTX_MASTER_PATH;
int fd = -1;
struct event rev;
struct event intev, usr1ev, usr2ev, connev;

int
main(int argc, char *argv[])
{
	struct agentx_index *idx[AGENTX_OID_INDEX_MAX_LEN];
	int ch;
	int dflag = 0;

	log_init(2, 1);

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		default:
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc >= 1)
		path = argv[0];

	bzero(&rev, sizeof(rev));

	agentx_log_fatal = fatalx;
	agentx_log_warn = log_warnx;
	agentx_log_info = log_info;
	agentx_log_debug = log_debug;

	if ((sa = agentx(regress_fd, (void *)path)) == NULL)
		fatal("agentx");
	sas = agentx_session(sa, NULL, 0, "OpenAgentX regress", 0);
	if (sas == NULL)
		fatal("agentx_session");
	if ((sac = agentx_context(sas, NULL)) == NULL)
		fatal("agentx_context");
	if ((saa = agentx_agentcaps(sac,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 50, 1),
	    "OpenBSD AgentX regress")) == NULL)
		fatal("agentx_agentcaps");
	if ((regress = agentx_region(sac,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100), 0)) == NULL)
		fatal("agentx_region application");
	if ((regressobj_scalarinteger = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 1), NULL, 0,
	    0, regress_scalarinteger)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalarstring = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 2), NULL, 0,
	    0, regress_scalarstring)) == NULL)
		fatal("agentx_object");
/* netsnmp doesn't return NULL-objects */
/*
	if ((regressobj_scalarnull = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 3), NULL, 0,
	    0, regress_scalarnull)) == NULL)
		fatal("agentx_object");
*/
	if ((regressobj_scalaroid = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 4), NULL, 0,
	    0, regress_scalaroid)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalaripaddress = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 5), NULL, 0,
	    0, regress_scalaripaddress)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalarcounter32 = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 6), NULL, 0,
	    0, regress_scalarcounter32)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalargauge32 = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 7), NULL, 0,
	    0, regress_scalargauge32)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalartimeticks = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 8), NULL, 0,
	    0, regress_scalartimeticks)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalaropaque = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 9), NULL, 0,
	    0, regress_scalaropaque)) == NULL)
		fatal("agentx_object");
	if ((regressobj_scalarcounter64 = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 10), NULL, 0,
	    0, regress_scalarcounter64)) == NULL)
		fatal("agentx_object");

	if ((regressidx_int = agentx_index_integer_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 11, 1, 1))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((regressobj_intindexint = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 11, 1, 1),
	    &regressidx_int, 1, 0, regress_intindex)) == NULL)
		fatal("agentx_object");

	if ((regressidx_int2 = agentx_index_integer_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 12, 1, 1))) == NULL)
		fatal("agentx_index_integer_dynamic");
	idx[0] = regressidx_int;
	idx[1] = regressidx_int2;
	if ((regressobj_intindexint2 = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 12, 1, 1),
	    idx, 2, 0, regress_intindex2)) == NULL)
		fatal("agentx_object");

	if ((regressidx_str = agentx_index_string_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 13, 1, 1))) == NULL)
		fatal("agentx_index_string_dynamic");
	if ((regressobj_strindexstr = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 13, 1, 1),
	    &regressidx_str, 1, 0, regress_strindex)) == NULL)
		fatal("agentx_object");
	if ((regressobj_implstrindexstr = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 14, 1, 1),
	    &regressidx_str, 1, 1, regress_implstrindex)) == NULL)
		fatal("agentx_object");
	if ((regressidx_str2 = agentx_index_string_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 15, 1, 1))) == NULL)
		fatal("agentx_index_string_dynamic");
	idx[0] = regressidx_str;
	idx[1] = regressidx_str2;
	if ((regressobj_strindexstr = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 15, 1, 1),
	    idx, 2, 0, regress_strindex2)) == NULL)
		fatal("agentx_object");

	if ((regressidx_oid = agentx_index_oid_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 16, 1, 1))) == NULL)
		fatal("agentx_index_oid_dynamic");
	if ((regressobj_oidimplindexoid = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 16, 1, 1),
	    &regressidx_oid, 1, 1, regress_oidimplindex)) == NULL)
		fatal("agentx_object");

	if ((regressidx_ipaddress = agentx_index_ipaddress_dynamic(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 17, 1, 1))) == NULL)
		fatal("agentx_index_oid_dynamic");
	if ((regressobj_oidimplindexoid = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 17, 1, 1),
	    &regressidx_ipaddress, 1, 0, regress_ipaddressindex)) == NULL)
		fatal("agentx_object");

	if ((regressidx_value = agentx_index_integer_value(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 18, 1, 1), 5)) == NULL)
		fatal("agentx_index_oid_dynamic");
	if ((regressobj_intindexstaticvalueint = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 18, 1, 1),
	    &regressidx_value, 1, 0, regress_intindexstaticvalueint)) == NULL)
		fatal("agentx_object");
	if ((regressobj_intindexstaticvaluestring = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 18, 1, 2),
	    &regressidx_value, 1, 0, regress_intindexstaticvaluestring)) == NULL)
		fatal("agentx_object");

	if ((regressidx_any = agentx_index_integer_any(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 19, 1, 1))) == NULL)
		fatal("agentx_index_integer_any");
	if ((regressobj_intindexstaticanyint = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 19, 1, 1),
	    &regressidx_any, 1, 0, regress_intindexstaticanyint)) == NULL)
		fatal("agentx_object");
	if ((regressobj_intindexstaticanystring = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 19, 1, 2),
	    &regressidx_any, 1, 0, regress_intindexstaticanystring)) == NULL)
		fatal("agentx_object");

	if ((regressidx_new = agentx_index_integer_new(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 20, 1, 1))) == NULL)
		fatal("agentx_index_integer_new");
	if ((regressobj_intindexstaticnewint = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 20, 1, 1),
	    &regressidx_new, 1, 0, regress_intindexstaticnewint)) == NULL)
		fatal("agentx_object");
	if ((regressobj_intindexstaticnewstring = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, 20, 1, 2),
	    &regressidx_new, 1, 0, regress_intindexstaticnewstring)) == NULL)
		fatal("agentx_object");

	if ((regressobj_scalarerror = agentx_object(regress,
	    AGENTX_OID(AGENTX_ENTERPRISES, 30155, 100, UINT32_MAX), NULL,
	    0, 0, regress_scalarerror)) == NULL)
		fatal("agentx_object");

	if (!dflag) {
		if (daemon(0, 1) == -1)
			fatalx("daemon");
	}

	event_init();

	event_set(&rev, fd, EV_READ|EV_PERSIST, regress_read, NULL);
	if (event_add(&rev, NULL) == -1)
		fatal("event_add");

	event_dispatch();
	return 0;
}

void
regress_fd(struct agentx *sa2, void *cookie, int close)
{
	static int init = 0;
	struct sockaddr_un sun;

	/* For ease of cleanup we take the single run approach */
	if (init) {
		if (!close)
			agentx_free(sa);
		else
			evtimer_del(&rev);
	} else {
		sun.sun_family = AF_UNIX;
		strlcpy(sun.sun_path, path, sizeof(sun.sun_path));

		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ||
		    connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
			fatal("connect");
		}
		agentx_connect(sa2, fd);
		init = 1;
	}
}

void
regress_read(int fd, short event, void *cookie)
{
	agentx_read(sa);
}

#ifdef notyet

void
regress_usr1(int fd, short event, void *cookie)
{
	struct agentx_notify *san;

	if ((san = agentx_notify(sac, AGENTX_OID(LINKDOWN))) == NULL)
		fatal("agentx_notify");

	agentx_notify_integer(san, AGENTX_OID(IFINDEX), 1);
	agentx_notify_integer(san, AGENTX_OID(IFADMINSTATUS), 3);
	agentx_notify_integer(san, AGENTX_OID(IFOPERSTATUS), 6);
	agentx_notify_send(san);
}

#endif

void
regress_usr2(int fd, short event, void *cookie)
{
}

void
regress_scalarstring(struct agentx_varbind *vb)
{
	agentx_varbind_string(vb, "scalar-string");
}

void
regress_scalarinteger(struct agentx_varbind *vb)
{
	agentx_varbind_integer(vb, 1);
}

void
regress_scalarnull(struct agentx_varbind *vb)
{
	agentx_varbind_null(vb);
}

void
regress_scalaroid(struct agentx_varbind *vb)
{
	agentx_varbind_oid(vb, AGENTX_OID(AGENTX_ENTERPRISES, 30155));
}

void
regress_scalaripaddress(struct agentx_varbind *vb)
{
	struct in_addr addr;

	inet_aton("127.0.0.1", &addr);

	agentx_varbind_ipaddress(vb, &addr);
}

void
regress_scalarcounter32(struct agentx_varbind *vb)
{
	agentx_varbind_counter32(vb, 1);
}

void
regress_scalargauge32(struct agentx_varbind *vb)
{
	agentx_varbind_gauge32(vb, 1);
}

void
regress_scalartimeticks(struct agentx_varbind *vb)
{
	agentx_varbind_timeticks(vb, 1);
}

void
regress_scalaropaque(struct agentx_varbind *vb)
{
	agentx_varbind_opaque(vb, "abc", 3);
}

void
regress_scalarcounter64(struct agentx_varbind *vb)
{
	agentx_varbind_counter64(vb, 1);
}

void
regress_intindex(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_int);
	switch (agentx_varbind_request(vb)) {
	case AGENTX_REQUEST_TYPE_GET:
		if (idx == 0) {
			agentx_varbind_notfound(vb);
			return;
		}
		break;
	case AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE:
		if (idx == 0)
			idx++;
		break;
	case AGENTX_REQUEST_TYPE_GETNEXT:
		idx++;
		break;
	}
	if (idx > 0xf)
		agentx_varbind_notfound(vb);
	else {
		agentx_varbind_set_index_integer(vb, regressidx_int, idx);
		agentx_varbind_integer(vb, idx);
	}
}

void
regress_intindex2(struct agentx_varbind *vb)
{
	uint32_t idx1, idx2;
	enum agentx_request_type type;

	idx1 = agentx_varbind_get_index_integer(vb, regressidx_int);
	idx2 = agentx_varbind_get_index_integer(vb, regressidx_int2);
	type = agentx_varbind_request(vb);
	if (type == AGENTX_REQUEST_TYPE_GETNEXT)
		idx2++;
	if (type == AGENTX_REQUEST_TYPE_GETNEXT ||
	    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if (idx2 > 1) {
			idx1++;
			idx2 = 0;
		}
	}
	if (idx2 > 1 || idx1 > 8)
		agentx_varbind_notfound(vb);
	else {
		agentx_varbind_set_index_integer(vb, regressidx_int, idx1);
		agentx_varbind_set_index_integer(vb, regressidx_int2, idx2);
		agentx_varbind_integer(vb, (idx1 << 1) + idx2);
	}
}

void
regress_strindex(struct agentx_varbind *vb)
{
	const unsigned char *idx;
	size_t slen;
	int implied;
	enum agentx_request_type request = agentx_varbind_request(vb);

	idx = agentx_varbind_get_index_string(vb, regressidx_str, &slen,
	    &implied);

	if (implied)
		fatalx("%s: string length should not be implied", __func__);

	if (slen == 0) {
		if (request == AGENTX_REQUEST_TYPE_GET) {
			log_warnx("%s: 0 index should be handled in agentx.c",
			    __func__);
			agentx_varbind_error(vb);
			return;
		}
	}
	/* !implied first needs a length check before content check */
	if (slen > 1) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (request == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx[0] == 'a')
			idx = (unsigned char *)"b";
		else if (idx[0] == 'b')
			idx = (unsigned char *)"c";
		else if (idx[0] >= 'c') {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	if (idx == NULL || idx[0] < 'a') {
		if (request == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx = (unsigned char *)"a";
	}

	agentx_varbind_set_index_string(vb, regressidx_str,
	    (const char *)idx);
	agentx_varbind_string(vb, (const char *)idx);
}

void
regress_implstrindex(struct agentx_varbind *vb)
{
	const unsigned char *idx;
	size_t slen;
	int implied;
	enum agentx_request_type request = agentx_varbind_request(vb);

	idx = agentx_varbind_get_index_string(vb, regressidx_str, &slen,
	    &implied);

	if (!implied)
		fatalx("%s: string length should be implied", __func__);

	if (slen == 0) {
		if (request == AGENTX_REQUEST_TYPE_GET)
			fatalx("%s: 0 index should be handled in agentx.c",
			    __func__);
	}
	if (request == AGENTX_REQUEST_TYPE_GET && (slen != 1 ||
	    (idx[0] != 'a' && idx[0] != 'b' && idx[0] != 'c'))) {
		agentx_varbind_notfound(vb);
		return;
	}
	/* implied doesn't needs a length check before content check */
	if (request == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx[0] == 'a')
			idx = (const unsigned char *)"b";
		else if (idx[0] == 'b')
			idx = (const unsigned char *)"c";
		else if (idx[0] >= 'c') {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	if (idx == NULL || idx[0] < 'a')
		idx = (const unsigned char *)"a";

	agentx_varbind_set_index_string(vb, regressidx_str,
	    (const char *)idx);
	agentx_varbind_string(vb, (const char *)idx);
}

void
regress_strindex2(struct agentx_varbind *vb)
{
	/* Opt is !implied sorted */
	const char *opt1[] = {"a", "b", "c"};
	const char *opt2[] = {"b", "aa", "bb"};
	size_t opt1len, opt2len;
	size_t opt1i, opt2i;
	const unsigned char *idx1, *idx2;
	size_t slen1, slen2;
	int implied1, implied2;
	enum agentx_request_type request = agentx_varbind_request(vb);
	int match;

	idx1 = agentx_varbind_get_index_string(vb, regressidx_str, &slen1,
	    &implied1);
	idx2 = agentx_varbind_get_index_string(vb, regressidx_str2, &slen2,
	    &implied2);

	/* agentx.c debugging checks */
	if (implied1 || implied2)
		fatalx("%s: string length should not be implied", __func__);
	if (slen1 == 0 || slen2 == 0) {
		if (request == AGENTX_REQUEST_TYPE_GET)
			fatalx("%s: 0 index should be handled in agentx.c",
			    __func__);
	}

	opt1len = sizeof(opt1) / sizeof(*opt1);
	match = 0;
	for (opt1i = 0; opt1i < opt1len; opt1i++) {
		if (strlen(opt1[opt1i]) < slen1 ||
		    (strlen(opt1[opt1i]) == slen1 &&
		    memcmp(opt1[opt1i], idx1, slen1) < 0)) {
			continue;
		}
		if (strlen(opt1[opt1i]) == slen1 &&
		    memcmp(opt1[opt1i], idx1, slen1) == 0)
			match = 1;
		break;
	}
	if (opt1i == opt1len) {
		agentx_varbind_notfound(vb);
		return;
	}
	opt2len = sizeof(opt2) / sizeof(*opt2);
	for (opt2i = 0; opt2i < opt2len; opt2i++) {
		if (!match)
			break;
		if (strlen(opt2[opt2i]) < slen2 ||
		    (strlen(opt2[opt2i]) == slen2 &&
		    memcmp(opt2[opt2i], idx2, slen2) < 0)) {
			continue;
		}
		if (strlen(opt2[opt2i]) != slen2 ||
		    memcmp(opt2[opt2i], idx2, slen2) > 0)
			match = 0;
		break;
	}
	if (opt2i == opt2len)
		match = 0;
	if (request == AGENTX_REQUEST_TYPE_GET) {
		if (!match) {
			agentx_varbind_notfound(vb);
			return;
		}
	} else {
		if (AGENTX_REQUEST_TYPE_GETNEXT && match)
			opt2i++;
		if (opt2i >= opt2len) {
			if (++opt1i == opt1len) {
				agentx_varbind_notfound(vb);
				return;
			}
			opt2i = 0;
		}
	}

	agentx_varbind_set_index_string(vb, regressidx_str, opt1[opt1i]);
	agentx_varbind_set_index_string(vb, regressidx_str2, opt2[opt2i]);
	agentx_varbind_printf(vb, "%s - %s", opt1[opt1i], opt2[opt2i]);
}

void
regress_oidimplindex(struct agentx_varbind *vb)
{
	struct agentx_object *obj;
	const uint32_t *idx;
	size_t oidlen;
	int implied;
	enum agentx_request_type request = agentx_varbind_request(vb);

	idx = agentx_varbind_get_index_oid(vb, regressidx_oid, &oidlen,
	    &implied);

	if (!implied)
		fatalx("%s: string length should be implied", __func__);

	if (request == AGENTX_REQUEST_TYPE_GET)
		obj = agentx_context_object_find(sac, idx, oidlen, 1, 1);
	else
		obj = agentx_context_object_nfind(sac, idx, oidlen, 1,
		    request == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE);

	if (obj == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}

	agentx_varbind_set_index_object(vb, regressidx_oid, obj);
	agentx_varbind_object(vb, obj);
}

void
regress_ipaddressindex(struct agentx_varbind *vb)
{
	const struct in_addr *addr;
	struct in_addr addrlist[4];
	enum agentx_request_type request = agentx_varbind_request(vb);
	size_t i;
	int cmp;

	inet_pton(AF_INET, "10.0.0.0", &(addrlist[0]));
	inet_pton(AF_INET, "127.0.0.1", &(addrlist[1]));
	inet_pton(AF_INET, "172.16.0.0", &(addrlist[2]));
	inet_pton(AF_INET, "192.168.0.0", &(addrlist[3]));

	addr = agentx_varbind_get_index_ipaddress(vb, regressidx_ipaddress);

	for (i = 0; i < 4; i++) {
		if ((cmp = memcmp(&(addrlist[i]), addr, sizeof(*addr))) == 0) {
			if (request == AGENTX_REQUEST_TYPE_GET ||
			    request == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				break;
		} else if (cmp > 0) {
			if (request == AGENTX_REQUEST_TYPE_GET) {
				agentx_varbind_notfound(vb);
				return;
			}
			break;
		}
	}
	if (i == 4) {
		agentx_varbind_notfound(vb);
		return;
	}

	agentx_varbind_set_index_ipaddress(vb, regressidx_ipaddress,
	    &(addrlist[i]));
	agentx_varbind_ipaddress(vb, &(addrlist[i]));
}

void
regress_intindexstaticvalueint(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_value);
	agentx_varbind_integer(vb, idx);
}

void
regress_intindexstaticvaluestring(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_value);
	agentx_varbind_printf(vb, "%u", idx);
}

void
regress_intindexstaticanyint(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_any);
	agentx_varbind_integer(vb, idx);
}

void
regress_intindexstaticanystring(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_any);
	agentx_varbind_printf(vb, "%u", idx);
}

void
regress_intindexstaticnewint(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_new);
	agentx_varbind_integer(vb, idx);
}

void
regress_intindexstaticnewstring(struct agentx_varbind *vb)
{
	uint32_t idx;

	idx = agentx_varbind_get_index_integer(vb, regressidx_new);
	agentx_varbind_printf(vb, "%u", idx);
}

void
regress_scalarerror(struct agentx_varbind *vb)
{
	agentx_varbind_error(vb);
}
