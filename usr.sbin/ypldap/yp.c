/*	$OpenBSD: yp.c,v 1.22 2023/07/18 13:06:33 claudio Exp $ */
/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/tree.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_rmt.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#include "ypldap.h"
#include "log.h"

#define BINDINGDIR		"/var/yp/binding"

void	yp_dispatch(struct svc_req *, SVCXPRT *);
void	yp_disable_events(void);
void	yp_fd_event(int, short, void *);
int	yp_check(struct svc_req *);
int	yp_valid_domain(char *, struct ypresp_val *);
void	yp_make_val(struct ypresp_val *, char *, int);
void	yp_make_keyval(struct ypresp_key_val *, char *, char *);
int	yp_write_binding(int, int);

static struct env	*env;

struct yp_event {
	TAILQ_ENTRY(yp_event)	 ye_entry;
	struct event		 ye_event;
};

struct yp_data {
	SVCXPRT			*yp_trans_udp;
	SVCXPRT			*yp_trans_tcp;
	TAILQ_HEAD(, yp_event)	 yd_events;
};

void
yp_disable_events(void)
{
	struct yp_event	*ye;

	while ((ye = TAILQ_FIRST(&env->sc_yp->yd_events)) != NULL) {
		TAILQ_REMOVE(&env->sc_yp->yd_events, ye, ye_entry);
		event_del(&ye->ye_event);
		free(ye);
	}
}

void
yp_enable_events(void)
{
	int		 i;
	extern fd_set	*__svc_fdset;
	extern int	 __svc_fdsetsize;
	struct yp_event	*ye;

	for (i = 0; i < __svc_fdsetsize; i++) {
		if (FD_ISSET(i, __svc_fdset)) {
			if ((ye = calloc(1, sizeof(*ye))) == NULL)
				fatal(NULL);
			event_set(&ye->ye_event, i, EV_READ, yp_fd_event, NULL);
			event_add(&ye->ye_event, NULL);
			TAILQ_INSERT_TAIL(&env->sc_yp->yd_events, ye, ye_entry);
		}
	}
}

void
yp_fd_event(int fd, short event, void *p)
{
	svc_getreq_common(fd);
	yp_disable_events();
	yp_enable_events();
}

void
yp_init(struct env *x_env)
{
	struct sockaddr_in	 addr;
	struct yp_data		*yp;
	int			 s, udpport, tcpport;

	if ((yp = calloc(1, sizeof(*yp))) == NULL)
		fatal(NULL);
	TAILQ_INIT(&yp->yd_events);

	env = x_env;
	env->sc_yp = yp;
	
	switch (env->sc_bind_mode) {
	case BIND_MODE_LOCAL:
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_len = sizeof(struct sockaddr_in);
		addr.sin_family = AF_INET;

		s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == -1)
			fatal("cannot create udp socket");
		addr.sin_port = 0;
		if (bindresvport(s, &addr))
			fatal("cannot bind udp socket");
		if ((yp->yp_trans_udp = svcudp_create(s)) == NULL)
			fatal("cannot create udp service");
		udpport = ntohs(addr.sin_port);

		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == -1)
			fatal("cannot create tcp socket");
		addr.sin_port = 0;
		if (bindresvport(s, &addr))
			fatal("cannot bind tcp socket");
		if ((yp->yp_trans_tcp = svctcp_create(s, 0, 0)) == NULL)
			fatal("cannot create tcp service");
		tcpport = ntohs(addr.sin_port);

		/* protocol 0 means don't register with portmap */
		if (!svc_register(yp->yp_trans_udp, YPPROG, YPVERS,
		    yp_dispatch, 0)) {
			fatal("unable to register (YPPROG, YPVERS, udp)");
		}
		if (!svc_register(yp->yp_trans_tcp, YPPROG, YPVERS,
		    yp_dispatch, 0)) {
			fatal("unable to register (YPPROG, YPVERS, tcp)");
		}

		if (yp_write_binding(udpport, tcpport))
			fatal("cannot write yp binding file");

		break;

	case BIND_MODE_PORTMAP:
		(void)pmap_unset(YPPROG, YPVERS);

		if ((yp->yp_trans_udp = svcudp_create(RPC_ANYSOCK)) == NULL)
			fatal("cannot create udp service");
		if ((yp->yp_trans_tcp = svctcp_create(RPC_ANYSOCK, 0, 0)) ==
		    NULL)
			fatal("cannot create tcp service");

		if (!svc_register(yp->yp_trans_udp, YPPROG, YPVERS,
		    yp_dispatch, IPPROTO_UDP)) {
			fatal("unable to register (YPPROG, YPVERS, udp)");
		}
		if (!svc_register(yp->yp_trans_tcp, YPPROG, YPVERS,
		    yp_dispatch, IPPROTO_TCP)) {
			fatal("unable to register (YPPROG, YPVERS, tcp)");
		}
		break;
	}
}

int
yp_write_binding(int udpport, int tcpport)
{
	char path[PATH_MAX];
	struct ypbind_resp ybr;
	struct iovec iov[3];
	struct in_addr bindaddr;
	u_short ypbind, ypserv_tcp, ypserv_udp;
	ssize_t total;
	int fd;

	snprintf(path, sizeof path, "%s/%s.%ld", BINDINGDIR, env->sc_domainname,
	    YPVERS);
	fd = open(path, O_CREAT|O_SHLOCK|O_RDWR|O_TRUNC, 0644);
	if (fd == -1) {
		(void)mkdir(BINDINGDIR, 0755);
		fd = open(path, O_CREAT|O_SHLOCK|O_RDWR|O_TRUNC,
		    0644);
		if (fd == -1)
			return -1;
	}

	if (fchmod(fd, 0644) == -1)
		return -1;

	iov[0].iov_base = (caddr_t)&ypbind;
	iov[0].iov_len = sizeof ypbind;
	iov[1].iov_base = (caddr_t)&ybr;
	iov[1].iov_len = sizeof ybr;
	iov[2].iov_base = (caddr_t)&ypserv_tcp;
	iov[2].iov_len = sizeof ypserv_tcp;

	bindaddr.s_addr = htonl(INADDR_LOOPBACK);
	ypserv_tcp = htons(tcpport);
	ypserv_udp = htons(udpport);
	ypbind = 0;
	memset(&ybr, 0, sizeof ybr);
	ybr.ypbind_status = YPBIND_SUCC_VAL;
	memmove(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr,
	    &bindaddr,
	    sizeof(ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr));
	memmove(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port,
	    &ypserv_udp,
	    sizeof(ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port));
	
	total = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
	if (writev(fd, iov, sizeof(iov)/sizeof(iov[0])) !=
	    total) {
		close(fd);
		unlink(path);
		return -1;
	}

	return 0;
}

/*
 * lots of inspiration from ypserv by Mats O Jansson
 */
void
yp_dispatch(struct svc_req *req, SVCXPRT *trans)
{
	xdrproc_t		 xdr_argument;
	xdrproc_t		 xdr_result;
	char			*result;
	char			*(*cb)(char *, struct svc_req *);
        union {
		domainname	 ypproc_domain_2_arg;
		domainname	 ypproc_domain_nonack_2_arg;
		ypreq_key	 ypproc_match_2_arg;
		ypreq_nokey	 ypproc_first_2_arg;
		ypreq_key	 ypproc_next_2_arg;
		ypreq_xfr	 ypproc_xfr_2_arg;
		ypreq_nokey	 ypproc_all_2_arg;
		ypreq_nokey	 ypproc_master_2_arg;
		ypreq_nokey	 ypproc_order_2_arg;
		domainname	 ypproc_maplist_2_arg;
	} argument;

	xdr_argument = (xdrproc_t) xdr_void;
	xdr_result = (xdrproc_t) xdr_void;
	cb = NULL;
	switch (req->rq_proc) {
	case YPPROC_NULL:
		xdr_argument = (xdrproc_t) xdr_void;
		xdr_result = (xdrproc_t) xdr_void;
		if (yp_check(req) == -1)
			return;
		result = NULL;
		if (!svc_sendreply(trans, (xdrproc_t) xdr_void,
		    (void *)&result))
			svcerr_systemerr(trans);
		return;
	case YPPROC_DOMAIN:
		xdr_argument = (xdrproc_t) xdr_domainname;
		xdr_result = (xdrproc_t) xdr_bool;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_domain_2_svc;
		break;
	case YPPROC_DOMAIN_NONACK:
		xdr_argument = (xdrproc_t) xdr_domainname;
		xdr_result = (xdrproc_t) xdr_bool;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_domain_nonack_2_svc;
		break;
	case YPPROC_MATCH:
		xdr_argument = (xdrproc_t) xdr_ypreq_key;
		xdr_result = (xdrproc_t) xdr_ypresp_val;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_match_2_svc;
		break;
	case YPPROC_FIRST:
		xdr_argument = (xdrproc_t) xdr_ypreq_nokey;
		xdr_result = (xdrproc_t) xdr_ypresp_key_val;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_first_2_svc;
		break;
	case YPPROC_NEXT:
		xdr_argument = (xdrproc_t) xdr_ypreq_key;
		xdr_result = (xdrproc_t) xdr_ypresp_key_val;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_next_2_svc;
		break;
	case YPPROC_XFR:
		if (yp_check(req) == -1)
			return;
		svcerr_noproc(trans);
		return;
	case YPPROC_CLEAR:
		log_debug("ypproc_clear");
		if (yp_check(req) == -1)
			return;
		svcerr_noproc(trans);
		return;
	case YPPROC_ALL:
		log_debug("ypproc_all");
		xdr_argument = (xdrproc_t) xdr_ypreq_nokey;
		xdr_result = (xdrproc_t) xdr_ypresp_all;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_all_2_svc;
		break;
	case YPPROC_MASTER:
		xdr_argument = (xdrproc_t) xdr_ypreq_nokey;
		xdr_result = (xdrproc_t) xdr_ypresp_master;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_master_2_svc;
		break;
	case YPPROC_ORDER:
		log_debug("ypproc_order");
		if (yp_check(req) == -1)
			return;
		svcerr_noproc(trans);
		return;
	case YPPROC_MAPLIST:
		xdr_argument = (xdrproc_t) xdr_domainname;
		xdr_result = (xdrproc_t) xdr_ypresp_maplist;
		if (yp_check(req) == -1)
			return;
		cb = (void *)ypproc_maplist_2_svc;
		break;
	default:
		svcerr_noproc(trans);
		return;
	}
	(void)memset(&argument, 0, sizeof(argument));

	if (!svc_getargs(trans, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(trans);
		return;
	}
	result = (*cb)((char *)&argument, req);
	if (result != NULL && !svc_sendreply(trans, xdr_result, result))
		svcerr_systemerr(trans);
	if (!svc_freeargs(trans, xdr_argument, (caddr_t)&argument)) {
		/*
		 * ypserv does it too.
		 */
		fatal("unable to free arguments");
	}
}

int
yp_check(struct svc_req *req)
{
#ifdef notyet
	struct sockaddr_in	*caller;

	caller = svc_getcaller(req->rq_xprt);
	/*
	 * We might want to know who we allow here.
	 */
#endif
	return (0);
}

int
yp_valid_domain(char *domain, struct ypresp_val *res)
{
	if (domain == NULL) {
		log_debug("NULL domain !");
		return (-1);
	}
	if (strcmp(domain, env->sc_domainname) != 0) {
		res->stat = YP_NODOM;
		return (-1);
	}
	return (0);
}

bool_t *
ypproc_domain_2_svc(domainname *arg, struct svc_req *req)
{
	static bool_t	res;
	
	res = (bool_t)1;
	if (strcmp(*arg, env->sc_domainname) != 0)
		res = (bool_t)0;
	return (&res);
}

bool_t *
ypproc_domain_nonack_2_svc(domainname *arg, struct svc_req *req)
{
	static bool_t	res;
	
	if (strcmp(*arg, env->sc_domainname) != 0)
		return NULL;
	res = (bool_t)1;
	return (&res);
}

ypresp_val *
ypproc_match_2_svc(ypreq_key *arg, struct svc_req *req)
{
	struct userent		 ukey;
	struct userent		*ue;
	struct groupent		 gkey;
	struct groupent		*ge;
	static struct ypresp_val res;
	const char		*estr;
	char			*bp, *cp;
	char			 key[YPMAXRECORD+1];

	log_debug("matching '%.*s' in map %s", arg->key.keydat_len,
	   arg->key.keydat_val, arg->map);

	if (yp_valid_domain(arg->domain, (struct ypresp_val *)&res) == -1)
		return (&res);

	if (env->sc_user_names == NULL) {
		/*
		 * tree not ready.
		 */
		return (NULL);
	}

	if (arg->key.keydat_len > YPMAXRECORD) {
		log_debug("argument too long");
		return (NULL);
	}
	memset(key, 0, sizeof(key));
	(void)strncpy(key, arg->key.keydat_val, arg->key.keydat_len);

	if (strcmp(arg->map, "passwd.byname") == 0 ||
	    strcmp(arg->map, "master.passwd.byname") == 0) {
		ukey.ue_line = key;
		if ((ue = RB_FIND(user_name_tree, env->sc_user_names,
		    &ukey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		yp_make_val(&res, ue->ue_line, 1);
		return (&res);
	} else if (strcmp(arg->map, "passwd.byuid") == 0 ||
		   strcmp(arg->map, "master.passwd.byuid") == 0) {
		ukey.ue_uid = strtonum(key, 0, UID_MAX, &estr); 
		if (estr) {
			res.stat = YP_BADARGS;
			return (&res);
		}

		if ((ue = RB_FIND(user_uid_tree, &env->sc_user_uids,
		    &ukey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		yp_make_val(&res, ue->ue_line, 1);
		return (&res);
	} else if (strcmp(arg->map, "group.bygid") == 0) {
		gkey.ge_gid = strtonum(key, 0, GID_MAX, &estr); 
		if (estr) {
			res.stat = YP_BADARGS;
			return (&res);
		}
		if ((ge = RB_FIND(group_gid_tree, &env->sc_group_gids,
		    &gkey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		yp_make_val(&res, ge->ge_line, 1);
		return (&res);
	} else if (strcmp(arg->map, "group.byname") == 0) {
		gkey.ge_line = key;
		if ((ge = RB_FIND(group_name_tree, env->sc_group_names,
		    &gkey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		yp_make_val(&res, ge->ge_line, 1);
		return (&res);
	} else if (strcmp(arg->map, "netid.byname") == 0) {
		bp = cp = key;

		if (strncmp(bp, "unix.", strlen("unix.")) != 0) {
			res.stat = YP_BADARGS;
			return (&res);
		}

		bp += strlen("unix.");

		if (*bp == '\0') {
			res.stat = YP_BADARGS;
			return (&res);
		}

		if (!(cp = strsep(&bp, "@"))) {
			res.stat = YP_BADARGS;
			return (&res);
		}

		if (strcmp(bp, arg->domain) != 0) {
			res.stat = YP_BADARGS;
			return (&res);
		}

		ukey.ue_uid = strtonum(cp, 0, UID_MAX, &estr); 
		if (estr) {
			res.stat = YP_BADARGS;
			return (&res);
		}

		if ((ue = RB_FIND(user_uid_tree, &env->sc_user_uids,
		    &ukey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		yp_make_val(&res, ue->ue_netid_line, 0);
		return (&res);
	
	} else {
		log_debug("unknown map %s", arg->map);
		res.stat = YP_NOMAP;
		return (&res);
	}
}

ypresp_key_val *
ypproc_first_2_svc(ypreq_nokey *arg, struct svc_req *req)
{
	static struct ypresp_key_val	res;

	if (yp_valid_domain(arg->domain, (struct ypresp_val *)&res) == -1)
		return (&res);

	if (strcmp(arg->map, "passwd.byname") == 0 ||
	    strcmp(arg->map, "master.passwd.byname") == 0) {
		if (env->sc_user_lines == NULL)
			return (NULL);

		yp_make_keyval(&res, env->sc_user_lines, env->sc_user_lines);
	} else if (strcmp(arg->map, "group.byname") == 0) {
		if (env->sc_group_lines == NULL)
			return (NULL);

		yp_make_keyval(&res, env->sc_group_lines, env->sc_group_lines);
	} else {
		log_debug("unknown map %s", arg->map);
		res.stat = YP_NOMAP;
	}

	return (&res);
}

ypresp_key_val *
ypproc_next_2_svc(ypreq_key *arg, struct svc_req *req)
{
	struct userent			 ukey;
	struct userent			*ue;
	struct groupent			 gkey;
	struct groupent			*ge;
	char				*line;
	static struct ypresp_key_val	 res;
	char				 key[YPMAXRECORD+1];

	if (yp_valid_domain(arg->domain, (struct ypresp_val *)&res) == -1)
		return (&res);

	if (strcmp(arg->map, "passwd.byname") == 0 ||
	    strcmp(arg->map, "master.passwd.byname") == 0) {
		memset(key, 0, sizeof(key));
		(void)strncpy(key, arg->key.keydat_val,
		    arg->key.keydat_len);
		ukey.ue_line = key;
		if ((ue = RB_NFIND(user_name_tree, env->sc_user_names,
		    &ukey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}
		line = ue->ue_line + (strlen(ue->ue_line) + 1);
		line = line + (strlen(line) + 1);
		yp_make_keyval(&res, line, line);
		return (&res);


	} else if (strcmp(arg->map, "group.byname") == 0) {
		memset(key, 0, sizeof(key));
		(void)strncpy(key, arg->key.keydat_val,
		    arg->key.keydat_len);
		
		gkey.ge_line = key;
		if ((ge = RB_NFIND(group_name_tree, env->sc_group_names,
		    &gkey)) == NULL) {
			res.stat = YP_NOKEY;
			return (&res);
		}

		line = ge->ge_line + (strlen(ge->ge_line) + 1);
		line = line + (strlen(line) + 1);
		yp_make_keyval(&res, line, line);
		return (&res);
	} else {
		log_debug("unknown map %s", arg->map);
		res.stat = YP_NOMAP;
		return (&res);
	}
}

ypresp_all *
ypproc_all_2_svc(ypreq_nokey *arg, struct svc_req *req)
{
	static struct ypresp_all	res;

	if (yp_valid_domain(arg->domain, (struct ypresp_val *)&res) == -1)
		return (&res);

	svcerr_auth(req->rq_xprt, AUTH_FAILED);
	return (NULL);
}

ypresp_master *
ypproc_master_2_svc(ypreq_nokey *arg, struct svc_req *req)
{
	static struct ypresp_master	 res;
	static char master[YPMAXPEER + 1];

	memset(&res, 0, sizeof(res));
	if (yp_valid_domain(arg->domain, (struct ypresp_val *)&res) == -1)
		return (&res);
	if (gethostname(master, sizeof(master)) == 0) {
		res.peer = (peername)master;
		res.stat = YP_TRUE;
	} else
		res.stat = YP_NOKEY;
	return (&res);
}

ypresp_maplist *
ypproc_maplist_2_svc(domainname *arg, struct svc_req *req)
{
	size_t			 i;
	static struct {
		char		*name;
		int		 cond;
	}			 mapnames[] = {
		{ "passwd.byname",		YPMAP_PASSWD_BYNAME },
		{ "passwd.byuid",		YPMAP_PASSWD_BYUID },
		{ "master.passwd.byname",	YPMAP_MASTER_PASSWD_BYNAME },
		{ "master.passwd.byuid",	YPMAP_MASTER_PASSWD_BYUID },
		{ "group.byname",		YPMAP_GROUP_BYNAME },
		{ "group.bygid",		YPMAP_GROUP_BYGID },
		{ "netid.byname",		YPMAP_NETID_BYNAME },
	};
	static ypresp_maplist	 res;
	static struct ypmaplist	 maps[sizeof(mapnames) / sizeof(mapnames[0])];
	
	if (yp_valid_domain(*arg, (struct ypresp_val *)&res) == -1)
		return (&res);

	res.stat = YP_TRUE;
	res.maps = NULL;
	for (i = 0; i < sizeof(mapnames) / sizeof(mapnames[0]); i++) {
		if (!(env->sc_flags & mapnames[i].cond))
			continue;
		maps[i].map = mapnames[i].name;
		maps[i].next = res.maps;
		res.maps = &maps[i];
	}

	return (&res);
}

void
yp_make_val(struct ypresp_val *res, char *line, int replacecolon)
{
	static char		 buf[LINE_WIDTH];

	memset(buf, 0, sizeof(buf));

	if (replacecolon)
		line[strlen(line)] = ':';
	(void)strlcpy(buf, line, sizeof(buf));
	if (replacecolon)
		line[strcspn(line, ":")] = '\0';
	log_debug("sending out %s", buf);

	res->stat = YP_TRUE;
	res->val.valdat_len = strlen(buf);
	res->val.valdat_val = buf;
}

void
yp_make_keyval(struct ypresp_key_val *res, char *key, char *line)
{
	static char	keybuf[YPMAXRECORD+1];
	static char	buf[LINE_WIDTH];

	memset(keybuf, 0, sizeof(keybuf));
	memset(buf, 0, sizeof(buf));
	
	(void)strlcpy(keybuf, key, sizeof(keybuf));
	res->key.keydat_len = strlen(keybuf);
	res->key.keydat_val = keybuf;

	if (*line == '\0') {
		res->stat = YP_NOMORE;
		return;
	}
	res->stat = YP_TRUE;
	line[strlen(line)] = ':';
	(void)strlcpy(buf, line, sizeof(buf));
	line[strcspn(line, ":")] = '\0';
	log_debug("sending out %s => %s", keybuf, buf);

	res->val.valdat_len = strlen(buf);
	res->val.valdat_val = buf;
}
