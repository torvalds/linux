/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY FRAUNHOFER FOKUS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * FRAUNHOFER FOKUS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Netgraph interface for SNMPd.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <netgraph.h>
#include <bsnmp/snmpmod.h>
#include "snmp_netgraph.h"
#include "netgraph_tree.h"
#include "netgraph_oid.h"

/* maximum message size */
#define RESBUFSIZ	20000

/* default node name */
#define NODENAME	"NgSnmpd"

/* my node Id */
ng_ID_t snmp_node;
u_char *snmp_nodename;

/* the Object Resource registration index */
static u_int reg_index;
static const struct asn_oid oid_begemotNg = OIDX_begemotNg;

/* configuration */
/* this must be smaller than int32_t because the functions in libnetgraph
 * falsely return an int */
static size_t resbufsiz = RESBUFSIZ;
static u_int timeout = 1000;
static u_int debug_level;

/* number of microseconds per clock tick */
static struct clockinfo clockinfo;

/* Csock buffers. Communication on the csock is asynchronuous. This means
 * if we wait for a specific response, we may get other messages. Put these
 * into a queue and execute them when we are idle. */
struct csock_buf {
	STAILQ_ENTRY(csock_buf) link;
	struct ng_mesg *mesg;
	char path[NG_PATHSIZ];
};
static STAILQ_HEAD(, csock_buf) csock_bufs =
	STAILQ_HEAD_INITIALIZER(csock_bufs);

/*
 * We dispatch unsolicieted messages by node cookies and ids.
 * So we must keep a list of hook names and dispatch functions.
 */
struct msgreg {
	u_int32_t 	cookie;
	ng_ID_t		id;
	ng_cookie_f	*func;
	void		*arg;
	const struct lmodule *mod;
	SLIST_ENTRY(msgreg) link;
};
static SLIST_HEAD(, msgreg) msgreg_list =
	SLIST_HEAD_INITIALIZER(msgreg_list);

/*
 * Data messages are dispatched by hook names.
 */
struct datareg {
	char		hook[NG_HOOKSIZ];
	ng_hook_f	*func;
	void		*arg;
	const struct lmodule *mod;
	SLIST_ENTRY(datareg) link;
};
static SLIST_HEAD(, datareg) datareg_list =
	SLIST_HEAD_INITIALIZER(datareg_list);

/* the netgraph sockets */
static int csock, dsock;
static void *csock_fd, *dsock_fd;

/* our module handle */
static struct lmodule *module;

/* statistics */
static u_int32_t stats[LEAF_begemotNgTooLargeDatas+1];

/* netgraph type list */
struct ngtype {
	char		name[NG_TYPESIZ];
	struct asn_oid	index;
	TAILQ_ENTRY(ngtype) link;
};
TAILQ_HEAD(ngtype_list, ngtype);

static struct ngtype_list ngtype_list;
static uint64_t ngtype_tick;


/*
 * Register a function to receive unsolicited messages
 */
void *
ng_register_cookie(const struct lmodule *mod, u_int32_t cookie, ng_ID_t id,
    ng_cookie_f *func, void *arg)
{
	struct msgreg *d;

	if ((d = malloc(sizeof(*d))) == NULL)
		return (NULL);

	d->cookie = cookie;
	d->id = id;
	d->func = func;
	d->arg = arg;
	d->mod = mod;

	SLIST_INSERT_HEAD(&msgreg_list, d, link);

	return (d);
}

/*
 * Remove a registration.
 */
void
ng_unregister_cookie(void *dd)
{
	struct msgreg *d = dd;

	SLIST_REMOVE(&msgreg_list, d, msgreg, link);
	free(d);
}

/*
 * Register a function for hook data.
 */
void *
ng_register_hook(const struct lmodule *mod, const char *hook,
    ng_hook_f *func, void *arg)
{
	struct datareg *d;

	if ((d = malloc(sizeof(*d))) == NULL)
		return (NULL);

	strcpy(d->hook, hook);
	d->func = func;
	d->arg = arg;
	d->mod = mod;

	SLIST_INSERT_HEAD(&datareg_list, d, link);

	return (d);
}

/*
 * Unregister a hook function
 */
void
ng_unregister_hook(void *dd)
{
	struct datareg *d = dd;

	SLIST_REMOVE(&datareg_list, d, datareg, link);
	free(d);
}

/*
 * Unregister all hooks and cookies for that module. Note: doesn't disconnect
 * any hooks!
 */
void
ng_unregister_module(const struct lmodule *mod)
{
	struct msgreg *m, *m1;
	struct datareg *d, *d1;

	m = SLIST_FIRST(&msgreg_list);
	while (m != NULL) {
		m1 = SLIST_NEXT(m, link);
		if (m->mod == mod) {
			SLIST_REMOVE(&msgreg_list, m, msgreg, link);
			free(m);
		}
		m = m1;
	}

	d = SLIST_FIRST(&datareg_list);
	while (d != NULL) {
		d1 = SLIST_NEXT(d, link);
		if (d->mod == mod) {
			SLIST_REMOVE(&datareg_list, d, datareg, link);
			free(d);
		}
		d = d1;
	}
}

/*
 * Dispatch a message to the correct module and delete it. More than one
 * module can get a message.
 */
static void
csock_handle(struct ng_mesg *mesg, const char *path)
{
	struct msgreg *d, *d1;
	u_int id;
	int len;

	if (sscanf(path, "[%x]:%n", &id, &len) != 1 ||
	    (u_int)len != strlen(path)) {
		syslog(LOG_ERR, "cannot parse message path '%s'", path);
		id = 0;
	}

	d = SLIST_FIRST(&msgreg_list);
	while (d != NULL) {
		d1 = SLIST_NEXT(d, link);
		if (d->cookie == mesg->header.typecookie &&
		    (d->id == 0 || d->id == id || id == 0))
			(*d->func)(mesg, path, id, d->arg);
		d = d1;
	}
	free(mesg);
}

/*
 * Input from the control socket.
 */
static struct ng_mesg *
csock_read(char *path)
{
	struct ng_mesg *mesg;
	int ret, err;

	if ((mesg = malloc(resbufsiz + 1)) == NULL) {
		stats[LEAF_begemotNgNoMems]++;
		syslog(LOG_CRIT, "out of memory");
		errno = ENOMEM;
		return (NULL);
	}
	if ((ret = NgRecvMsg(csock, mesg, resbufsiz + 1, path)) < 0) {
		err = errno;
		free(mesg);
		if (errno == EWOULDBLOCK) {
			errno = err;
			return (NULL);
		}
		stats[LEAF_begemotNgMsgReadErrs]++;
		syslog(LOG_WARNING, "read from csock: %m");
		errno = err;
		return (NULL);
	}
	if (ret == 0) {
		syslog(LOG_DEBUG, "node closed -- exiting");
		exit(0);
	}
	if ((size_t)ret > resbufsiz) {
		stats[LEAF_begemotNgTooLargeMsgs]++;
		syslog(LOG_WARNING, "ng message too large");
		free(mesg);
		errno = EFBIG;
		return (NULL);
	}
	return (mesg);
}

static void
csock_input(int fd __unused, void *udata __unused)
{
	struct ng_mesg *mesg;
	char path[NG_PATHSIZ];

	if ((mesg = csock_read(path)) == NULL)
		return;

	csock_handle(mesg, path);
}

/*
 * Write a message to a node.
 */
int
ng_output(const char *path, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	return (NgSendMsg(csock, path, (int)cookie, (int)opcode, arg, arglen));
}
int
ng_output_node(const char *node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	char path[NG_PATHSIZ];

	sprintf(path, "%s:", node);
	return (ng_output(path, cookie, opcode, arg, arglen));
}
int
ng_output_id(ng_ID_t node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	char path[NG_PATHSIZ];

	sprintf(path, "[%x]:", node);
	return (ng_output(path, cookie, opcode, arg, arglen));
}



/*
 * Execute a synchronuous dialog with the csock. All message we receive, that
 * do not match our request, are queue until the next call to the IDLE function.
 */
struct ng_mesg *
ng_dialog(const char *path, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	int token, err;
	struct ng_mesg *mesg;
	char rpath[NG_PATHSIZ];
	struct csock_buf *b;
	struct timeval end, tv;

	if ((token = ng_output(path, cookie, opcode, arg, arglen)) < 0)
		return (NULL);

	if (csock_fd)
		fd_suspend(csock_fd);

	gettimeofday(&end, NULL);
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	timeradd(&end, &tv, &end);
	for (;;) {
		mesg = NULL;
		gettimeofday(&tv, NULL);
		if (timercmp(&tv, &end, >=)) {
  block:
			syslog(LOG_WARNING, "no response for request %u/%u",
			    cookie, opcode);
			errno = EWOULDBLOCK;
			break;
		}
		timersub(&end, &tv, &tv);
		if (tv.tv_sec == 0 && tv.tv_usec < clockinfo.tick)
			goto block;

		if (setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
			syslog(LOG_WARNING, "setsockopt(SO_RCVTIMEO): %m");
		if ((mesg = csock_read(rpath)) == NULL) {
			if (errno == EWOULDBLOCK)
				continue;
			break;
		}
		if (mesg->header.token == (u_int)token)
			break;
		if ((b = malloc(sizeof(*b))) == NULL) {
			stats[LEAF_begemotNgNoMems]++;
			syslog(LOG_ERR, "out of memory");
			free(mesg);
			continue;
		}
		b->mesg = mesg;
		strcpy(b->path, rpath);
		STAILQ_INSERT_TAIL(&csock_bufs, b, link);
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if (setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
		syslog(LOG_WARNING, "setsockopt(SO_RCVTIMEO,0): %m");

	if (csock_fd) {
		err = errno;
		fd_resume(csock_fd);
		errno = err;
	}

	return (mesg);
}
struct ng_mesg *
ng_dialog_node(const char *node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	char path[NG_PATHSIZ];

	sprintf(path, "%s:", node);
	return (ng_dialog(path, cookie, opcode, arg, arglen));
}
struct ng_mesg *
ng_dialog_id(ng_ID_t id, u_int cookie, u_int opcode,
    const void *arg, size_t arglen)
{
	char path[NG_PATHSIZ];

	sprintf(path, "[%x]:", id);
	return (ng_dialog(path, cookie, opcode, arg, arglen));
}


/*
 * Send a data message to a given hook.
 */
int
ng_send_data(const char *hook, const void *sndbuf, size_t sndlen)
{
	return (NgSendData(dsock, hook, sndbuf, sndlen));
}

/*
 * Input from a data socket. Dispatch to the function for that hook.
 */
static void
dsock_input(int fd __unused, void *udata __unused)
{
	u_char *resbuf, embuf[100];
	ssize_t len;
	char hook[NG_HOOKSIZ];
	struct datareg *d, *d1;

	if ((resbuf = malloc(resbufsiz + 1)) == NULL) {
		stats[LEAF_begemotNgNoMems]++;
		syslog(LOG_CRIT, "out of memory");
		(void)NgRecvData(fd, embuf, sizeof(embuf), hook);
		errno = ENOMEM;
		return;
	}
	if ((len = NgRecvData(fd, resbuf, resbufsiz + 1, hook)) == -1) {
		stats[LEAF_begemotNgDataReadErrs]++;
		syslog(LOG_ERR, "reading message: %m");
		free(resbuf);
		return;
	}
	if (len == 0) {
		free(resbuf);
		return;
	}
	if ((size_t)len == resbufsiz + 1) {
		stats[LEAF_begemotNgTooLargeDatas]++;
		syslog(LOG_WARNING, "message too long");
		free(resbuf);
		return;
	}

	/*
	 * Dispatch message. Maybe dispatched to more than one function.
	 */
	d = SLIST_FIRST(&datareg_list);
	while (d != NULL) {
		d1 = SLIST_NEXT(d, link);
		if (strcmp(hook, d->hook) == 0)
			(*d->func)(hook, resbuf, len, d->arg);
		d = d1;
	}

	free(resbuf);
}

/*
 * The SNMP daemon is about to wait for an event. Look whether we have
 * netgraph messages waiting. If yes, drain the queue.
 */
static void
ng_idle(void)
{
	struct csock_buf *b;

	/* execute waiting csock_bufs */
	while ((b = STAILQ_FIRST(&csock_bufs)) != NULL) {
		STAILQ_REMOVE_HEAD(&csock_bufs, link);
		csock_handle(b->mesg, b->path);
		free(b);
	}
}

/*
 * Called when the module is loaded. Returning a non-zero value means,
 * rejecting the initialisation.
 *
 * We make the netgraph socket.
 */
static int
ng_init(struct lmodule *mod, int argc, char *argv[])
{
	int name[2];
	size_t len;

	module = mod;

	if (argc == 0) {
		if ((snmp_nodename = malloc(strlen(NODENAME) + 1)) == NULL)
			return (ENOMEM);
		strcpy(snmp_nodename, NODENAME);
	} else {
		if ((snmp_nodename = malloc(NG_NODESIZ)) == NULL)
			return (ENOMEM);
		strlcpy(snmp_nodename, argv[0], NG_NODESIZ);
	}

	/* fetch clockinfo (for the number of microseconds per tick) */
	name[0] = CTL_KERN;
	name[1] = KERN_CLOCKRATE;
	len = sizeof(clockinfo);
	if (sysctl(name, 2, &clockinfo, &len, NULL, 0) == -1)
		return (errno);

	TAILQ_INIT(&ngtype_list);

	return (0);
}

/*
 * Get the node Id/name/type of a node.
 */
ng_ID_t
ng_node_id(const char *path)
{
	struct ng_mesg *resp;
	ng_ID_t id;

	if ((resp = ng_dialog(path, NGM_GENERIC_COOKIE, NGM_NODEINFO,
	    NULL, 0)) == NULL)
		return (0);
	id = ((struct nodeinfo *)(void *)resp->data)->id;
	free(resp);
	return (id);
}
ng_ID_t
ng_node_id_node(const char *node)
{
	struct ng_mesg *resp;
	ng_ID_t id;

	if ((resp = ng_dialog_node(node, NGM_GENERIC_COOKIE, NGM_NODEINFO,
	    NULL, 0)) == NULL)
		return (0);
	id = ((struct nodeinfo *)(void *)resp->data)->id;
	free(resp);
	return (id);
}
ng_ID_t
ng_node_name(ng_ID_t id, char *name)
{
	struct ng_mesg *resp;

	if ((resp = ng_dialog_id(id, NGM_GENERIC_COOKIE, NGM_NODEINFO,
	    NULL, 0)) == NULL)
		return (0);
	strcpy(name, ((struct nodeinfo *)(void *)resp->data)->name);
	free(resp);
	return (id);

}
ng_ID_t
ng_node_type(ng_ID_t id, char *type)
{
	struct ng_mesg *resp;

	if ((resp = ng_dialog_id(id, NGM_GENERIC_COOKIE, NGM_NODEINFO,
	    NULL, 0)) == NULL)
		return (0);
	strcpy(type, ((struct nodeinfo *)(void *)resp->data)->type);
	free(resp);
	return (id);
}

/*
 * Connect our node to some other node
 */
int
ng_connect_node(const char *node, const char *ourhook, const char *peerhook)
{
	struct ngm_connect conn;

	snprintf(conn.path, NG_PATHSIZ, "%s:", node);
	strlcpy(conn.ourhook, ourhook, NG_HOOKSIZ);
	strlcpy(conn.peerhook, peerhook, NG_HOOKSIZ);
	return (NgSendMsg(csock, ".:",
	    NGM_GENERIC_COOKIE, NGM_CONNECT, &conn, sizeof(conn)));
}
int
ng_connect_id(ng_ID_t id, const char *ourhook, const char *peerhook)
{
	struct ngm_connect conn;

	snprintf(conn.path, NG_PATHSIZ, "[%x]:", id);
	strlcpy(conn.ourhook, ourhook, NG_HOOKSIZ);
	strlcpy(conn.peerhook, peerhook, NG_HOOKSIZ);
	return (NgSendMsg(csock, ".:",
	    NGM_GENERIC_COOKIE, NGM_CONNECT, &conn, sizeof(conn)));
}

int
ng_connect2_id(ng_ID_t id, ng_ID_t peer, const char *ourhook,
    const char *peerhook)
{
	struct ngm_connect conn;
	char path[NG_PATHSIZ];

	snprintf(path, NG_PATHSIZ, "[%x]:", id);

	snprintf(conn.path, NG_PATHSIZ, "[%x]:", peer);
	strlcpy(conn.ourhook, ourhook, NG_HOOKSIZ);
	strlcpy(conn.peerhook, peerhook, NG_HOOKSIZ);
	return (NgSendMsg(csock, path,
	    NGM_GENERIC_COOKIE, NGM_CONNECT, &conn, sizeof(conn)));
}

int
ng_connect2_tee_id(ng_ID_t id, ng_ID_t peer, const char *ourhook,
    const char *peerhook)
{
	struct ngm_connect conn;
	char path[NG_PATHSIZ];
	ng_ID_t tee;

	if ((tee = ng_mkpeer_id(id, NULL, "tee", ourhook, "left")) == 0)
		return (-1);

	snprintf(path, NG_PATHSIZ, "[%x]:", tee);

	snprintf(conn.path, NG_PATHSIZ, "[%x]:", peer);
	strlcpy(conn.ourhook, "right", NG_HOOKSIZ);
	strlcpy(conn.peerhook, peerhook, NG_HOOKSIZ);
	return (NgSendMsg(csock, path,
	    NGM_GENERIC_COOKIE, NGM_CONNECT, &conn, sizeof(conn)));
}

/*
 * Ensure that a node of type 'type' is connected to 'hook' of 'node'
 * and return its node id. tee nodes between node and the target node
 * are skipped. If the type is wrong, or the hook is a dead-end return 0.
 * If type is NULL, it is not checked.
 */
static ng_ID_t
ng_next_node_id_internal(ng_ID_t node, const char *type, const char *hook,
    int skip_tee)
{
	struct ng_mesg *resp;
	struct hooklist *hooklist;
	u_int i;

	if ((resp = ng_dialog_id(node, NGM_GENERIC_COOKIE, NGM_LISTHOOKS,
	    NULL, 0)) == NULL) {
		syslog(LOG_ERR, "get hook list: %m");
		exit(1);
	}
	hooklist = (struct hooklist *)(void *)resp->data;

	for (i = 0; i < hooklist->nodeinfo.hooks; i++)
		if (strcmp(hooklist->link[i].ourhook, hook) == 0)
			break;

	if (i == hooklist->nodeinfo.hooks) {
		free(resp);
		return (0);
	}

	node = hooklist->link[i].nodeinfo.id;

	if (skip_tee && strcmp(hooklist->link[i].nodeinfo.type, "tee") == 0) {
		if (strcmp(hooklist->link[i].peerhook, "left") == 0)
			node = ng_next_node_id(node, type, "right");
		else if (strcmp(hooklist->link[i].peerhook, "right") == 0)
			node = ng_next_node_id(node, type, "left");
		else if (type != NULL &&
		    strcmp(hooklist->link[i].nodeinfo.type, type) != 0)
			node = 0;

	} else if (type != NULL &&
	    strcmp(hooklist->link[i].nodeinfo.type, type) != 0)
		node = 0;

	free(resp);

	return (node);
}

/*
 * Ensure that a node of type 'type' is connected to 'hook' of 'node'
 * and return its node id. tee nodes between node and the target node
 * are skipped. If the type is wrong, or the hook is a dead-end return 0.
 * If type is NULL, it is not checked.
 */
ng_ID_t
ng_next_node_id(ng_ID_t node, const char *type, const char *hook)
{
	return (ng_next_node_id_internal(node, type, hook, 1));
}

ng_ID_t
ng_mkpeer_id(ng_ID_t id, const char *nodename, const char *type,
    const char *hook, const char *peerhook)
{
	char path[NG_PATHSIZ];
	struct ngm_mkpeer mkpeer;
	struct ngm_name name;

	strlcpy(mkpeer.type, type, NG_TYPESIZ);
	strlcpy(mkpeer.ourhook, hook, NG_HOOKSIZ);
	strlcpy(mkpeer.peerhook, peerhook, NG_HOOKSIZ);

	sprintf(path, "[%x]:", id);
	if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_MKPEER,
	    &mkpeer, sizeof(mkpeer)) == -1)
		return (0);

	if ((id = ng_next_node_id_internal(id, NULL, hook, 0)) == 0)
		return (0);

	if (nodename != NULL) {
		strcpy(name.name, nodename);
		sprintf(path, "[%x]:", id);
		if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_NAME,
		    &name, sizeof(name)) == -1)
			return (0);
	}
	return (id);
}

/*
 * SHutdown node
 */
int
ng_shutdown_id(ng_ID_t id)
{
	char path[NG_PATHSIZ];

	snprintf(path, NG_PATHSIZ, "[%x]:", id);
	return (NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	    NGM_SHUTDOWN, NULL, 0));
}

/*
 * Disconnect one of our hooks
 */
int
ng_rmhook(const char *ourhook)
{
	struct ngm_rmhook rmhook;

	strlcpy(rmhook.ourhook, ourhook, NG_HOOKSIZ);
	return (NgSendMsg(csock, ".:",
	    NGM_GENERIC_COOKIE, NGM_RMHOOK, &rmhook, sizeof(rmhook)));
}

/*
 * Disconnect a hook of a node
 */
int
ng_rmhook_id(ng_ID_t id, const char *hook)
{
	struct ngm_rmhook rmhook;
	char path[NG_PATHSIZ];

	strlcpy(rmhook.ourhook, hook, NG_HOOKSIZ);
	snprintf(path, NG_PATHSIZ, "[%x]:", id);
	return (NgSendMsg(csock, path,
	    NGM_GENERIC_COOKIE, NGM_RMHOOK, &rmhook, sizeof(rmhook)));
}

/*
 * Disconnect a hook and shutdown all tee nodes that were connected to that
 * hook.
 */
int
ng_rmhook_tee_id(ng_ID_t node, const char *hook)
{
	struct ng_mesg *resp;
	struct hooklist *hooklist;
	u_int i;
	int first = 1;
	ng_ID_t next_node;
	const char *next_hook;

  again:
	/* if we have just shutdown a tee node, which had no other hooks
	 * connected, the node id may already be wrong here. */
	if ((resp = ng_dialog_id(node, NGM_GENERIC_COOKIE, NGM_LISTHOOKS,
	    NULL, 0)) == NULL)
		return (0);

	hooklist = (struct hooklist *)(void *)resp->data;

	for (i = 0; i < hooklist->nodeinfo.hooks; i++)
		if (strcmp(hooklist->link[i].ourhook, hook) == 0)
			break;

	if (i == hooklist->nodeinfo.hooks) {
		free(resp);
		return (0);
	}

	next_node = 0;
	next_hook = NULL;
	if (strcmp(hooklist->link[i].nodeinfo.type, "tee") == 0) {
		if (strcmp(hooklist->link[i].peerhook, "left") == 0) {
			next_node = hooklist->link[i].nodeinfo.id;
			next_hook = "right";
		} else if (strcmp(hooklist->link[i].peerhook, "right") == 0) {
			next_node = hooklist->link[i].nodeinfo.id;
			next_hook = "left";
		}
	}
	free(resp);

	if (first) {
		ng_rmhook_id(node, hook);
		first = 0;
	} else {
		ng_shutdown_id(node);
	}
	if ((node = next_node) == 0)
		return (0);
	hook = next_hook;

	goto again;
}

/*
 * Get the peer hook of a hook on a given node. Skip any tee nodes in between
 */
int
ng_peer_hook_id(ng_ID_t node, const char *hook, char *peerhook)
{
	struct ng_mesg *resp;
	struct hooklist *hooklist;
	u_int i;
	int ret;

	if ((resp = ng_dialog_id(node, NGM_GENERIC_COOKIE, NGM_LISTHOOKS,
	    NULL, 0)) == NULL) {
		syslog(LOG_ERR, "get hook list: %m");
		exit(1);
	}
	hooklist = (struct hooklist *)(void *)resp->data;

	for (i = 0; i < hooklist->nodeinfo.hooks; i++)
		if (strcmp(hooklist->link[i].ourhook, hook) == 0)
			break;

	if (i == hooklist->nodeinfo.hooks) {
		free(resp);
		return (-1);
	}

	node = hooklist->link[i].nodeinfo.id;

	ret = 0;
	if (strcmp(hooklist->link[i].nodeinfo.type, "tee") == 0) {
		if (strcmp(hooklist->link[i].peerhook, "left") == 0)
			ret = ng_peer_hook_id(node, "right", peerhook);
		else if (strcmp(hooklist->link[i].peerhook, "right") == 0)
			ret = ng_peer_hook_id(node, "left", peerhook);
		else
			strcpy(peerhook, hooklist->link[i].peerhook);

	} else
		strcpy(peerhook, hooklist->link[i].peerhook);

	free(resp);

	return (ret);
}


/*
 * Now the module is started. Select on the sockets, so that we can get
 * unsolicited input.
 */
static void
ng_start(void)
{
	if (snmp_node == 0) {
		if (NgMkSockNode(snmp_nodename, &csock, &dsock) < 0) {
			syslog(LOG_ERR, "NgMkSockNode: %m");
			exit(1);
		}
		snmp_node = ng_node_id(".:");
	}

	if ((csock_fd = fd_select(csock, csock_input, NULL, module)) == NULL) {
		syslog(LOG_ERR, "fd_select failed on csock: %m");
		return;
	}
	if ((dsock_fd = fd_select(dsock, dsock_input, NULL, module)) == NULL) {
		syslog(LOG_ERR, "fd_select failed on dsock: %m");
		return;
	}

	reg_index = or_register(&oid_begemotNg,
	    "The MIB for the NetGraph access module for SNMP.", module);
}

/*
 * Called, when the module is to be unloaded after it was successfully loaded
 */
static int
ng_fini(void)
{
	struct ngtype *t;

	while ((t = TAILQ_FIRST(&ngtype_list)) != NULL) {
		TAILQ_REMOVE(&ngtype_list, t, link);
		free(t);
	}

	if (csock_fd != NULL)
		fd_deselect(csock_fd);
	(void)close(csock);

	if (dsock_fd != NULL)
		fd_deselect(dsock_fd);
	(void)close(dsock);

	free(snmp_nodename);

	or_unregister(reg_index);

	return (0);
}

const struct snmp_module config = {
	"This module implements access to the netgraph sub-system",
	ng_init,
	ng_fini,
	ng_idle,
	NULL,
	NULL,
	ng_start,
	NULL,
	netgraph_ctree,
	netgraph_CTREE_SIZE,
	NULL
};

int
op_ng_config(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	int ret;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		/*
		 * Come here for GET, GETNEXT and COMMIT
		 */
		switch (which) {

		  case LEAF_begemotNgControlNodeName:
			return (string_get(value, snmp_nodename, -1));

		  case LEAF_begemotNgResBufSiz:
			value->v.integer = resbufsiz;
			break;

		  case LEAF_begemotNgTimeout:
			value->v.integer = timeout;
			break;

		  case LEAF_begemotNgDebugLevel:
			value->v.uint32 = debug_level;
			break;

		  default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		switch (which) {

		  case LEAF_begemotNgControlNodeName:
			/* only at initialisation */
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);

			if (snmp_node != 0)
				return (SNMP_ERR_NOT_WRITEABLE);

			if ((ret = string_save(value, ctx, -1, &snmp_nodename))
			    != SNMP_ERR_NOERROR)
				return (ret);

			if (NgMkSockNode(snmp_nodename, &csock, &dsock) < 0) {
				syslog(LOG_ERR, "NgMkSockNode: %m");
				string_rollback(ctx, &snmp_nodename);
				return (SNMP_ERR_GENERR);
			}
			snmp_node = ng_node_id(".:");

			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgResBufSiz:
			ctx->scratch->int1 = resbufsiz;
			if (value->v.integer < 1024 ||
			    value->v.integer > 0x10000)
				return (SNMP_ERR_WRONG_VALUE);
			resbufsiz = value->v.integer;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgTimeout:
			ctx->scratch->int1 = timeout;
			if (value->v.integer < 10 ||
			    value->v.integer > 10000)
				return (SNMP_ERR_WRONG_VALUE);
			timeout = value->v.integer;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgDebugLevel:
			ctx->scratch->int1 = debug_level;
			debug_level = value->v.uint32;
			NgSetDebug(debug_level);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (which) {

		  case LEAF_begemotNgControlNodeName:
			string_rollback(ctx, &snmp_nodename);
			close(csock);
			close(dsock);
			snmp_node = 0;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgResBufSiz:
			resbufsiz = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgTimeout:
			timeout = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgDebugLevel:
			debug_level = ctx->scratch->int1;
			NgSetDebug(debug_level);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (which) {

		  case LEAF_begemotNgControlNodeName:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNgResBufSiz:
		  case LEAF_begemotNgTimeout:
		  case LEAF_begemotNgDebugLevel:
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();
}

int
op_ng_stats(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		value->v.uint32 = stats[value->var.subs[sub - 1] - 1];
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		abort();
	}
	abort();
}

/*
 * Netgraph type table
 */
static int
fetch_types(void)
{
	struct ngtype *t;
	struct typelist *typelist;
	struct ng_mesg *resp;
	u_int u, i;

	if (this_tick <= ngtype_tick)
		return (0);

	while ((t = TAILQ_FIRST(&ngtype_list)) != NULL) {
		TAILQ_REMOVE(&ngtype_list, t, link);
		free(t);
	}

	if ((resp = ng_dialog_id(snmp_node, NGM_GENERIC_COOKIE,
	    NGM_LISTTYPES, NULL, 0)) == NULL)
		return (SNMP_ERR_GENERR);
	typelist = (struct typelist *)(void *)resp->data;

	for (u = 0; u < typelist->numtypes; u++) {
		if ((t = malloc(sizeof(*t))) == NULL) {
			free(resp);
			return (SNMP_ERR_GENERR);
		}
		strcpy(t->name, typelist->typeinfo[u].type_name);
		t->index.subs[0] = strlen(t->name);
		t->index.len = t->index.subs[0] + 1;
		for (i = 0; i < t->index.subs[0]; i++)
			t->index.subs[i + 1] = t->name[i];

		INSERT_OBJECT_OID(t, &ngtype_list);
	}

	ngtype_tick = this_tick;

	free(resp);
	return (0);
}

/*
 * Try to load the netgraph type with the given name. We assume, that
 * type 'type' is implemented in the kernel module 'ng_type'.
 */
static int
ngtype_load(const u_char *name, size_t namelen)
{
	char *mod;
	int ret;

	if ((mod = malloc(namelen + 4)) == NULL)
		return (-1);
	strcpy(mod, "ng_");
	strncpy(mod + 3, name, namelen);
	mod[namelen + 3] = '\0';

	ret = kldload(mod);
	free(mod);
	return (ret);
}

/*
 * Unload a netgraph type.
 */
static int
ngtype_unload(const u_char *name, size_t namelen)
{
	char *mod;
	int id;

	if ((mod = malloc(namelen + 4)) == NULL)
		return (-1);
	strcpy(mod, "ng_");
	strncpy(mod + 3, name, namelen);
	mod[namelen + 3] = '\0';

	if ((id = kldfind(mod)) == -1) {
		free(mod);
		return (-1);
	}
	free(mod);
	return (kldunload(id));
}

int
op_ng_type(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	struct ngtype *t;
	u_char *name;
	size_t namelen;
	int status = 1;
	int ret;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((ret = fetch_types()) != 0)
			return (ret);
		if ((t = NEXT_OBJECT_OID(&ngtype_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &t->index);
		break;

	  case SNMP_OP_GET:
		if ((ret = fetch_types()) != 0)
			return (ret);
		if ((t = FIND_OBJECT_OID(&ngtype_list, &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, &name, &namelen))
			return (SNMP_ERR_NO_CREATION);
		if (namelen == 0 || namelen >= NG_TYPESIZ) {
			free(name);
			return (SNMP_ERR_NO_CREATION);
		}
		if ((ret = fetch_types()) != 0) {
			free(name);
			return (ret);
		}
		t = FIND_OBJECT_OID(&ngtype_list, &value->var, sub);

		if (which != LEAF_begemotNgTypeStatus) {
			free(name);
			if (t != NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			return (SNMP_ERR_NO_CREATION);
		}
		if (!TRUTH_OK(value->v.integer)) {
			free(name);
			return (SNMP_ERR_WRONG_VALUE);
		}
		ctx->scratch->int1 = TRUTH_GET(value->v.integer);
		ctx->scratch->int1 |= (t != NULL) << 1;
		ctx->scratch->ptr2 = name;
		ctx->scratch->int2 = namelen;

		if (t == NULL) {
			/* type not loaded */
			if (ctx->scratch->int1 & 1) {
				/* request to load */
				if (ngtype_load(name, namelen) == -1) {
					free(name);
					if (errno == ENOENT)
						return (SNMP_ERR_INCONS_NAME);
					else
						return (SNMP_ERR_GENERR);
				}
			}
		} else {
			/* is type loaded */
			if (!(ctx->scratch->int1 & 1)) {
				/* request to unload */
				if (ngtype_unload(name, namelen) == -1) {
					free(name);
					return (SNMP_ERR_GENERR);
				}
			}
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
		ret = SNMP_ERR_NOERROR;
		if (!(ctx->scratch->int1 & 2)) {
			/* did not exist */
			if (ctx->scratch->int1 & 1) {
				/* request to load - unload */
				if (ngtype_unload(ctx->scratch->ptr2,
				    ctx->scratch->int2) == -1)
					ret = SNMP_ERR_UNDO_FAILED;
			}
		} else {
			/* did exist */
			if (!(ctx->scratch->int1 & 1)) {
				/* request to unload - reload */
				if (ngtype_load(ctx->scratch->ptr2,
				    ctx->scratch->int2) == -1)
					ret = SNMP_ERR_UNDO_FAILED;
			}
		}
		free(ctx->scratch->ptr2);
		return (ret);

	  case SNMP_OP_COMMIT:
		free(ctx->scratch->ptr2);
		return (SNMP_ERR_NOERROR);

	  default:
		abort();
	}

	/*
	 * Come here for GET and COMMIT
	 */
	switch (which) {

	  case LEAF_begemotNgTypeStatus:
		value->v.integer = status;
		break;

	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * Implement the node table
 */
static int
find_node(const struct asn_oid *oid, u_int sub, struct nodeinfo *info)
{
	ng_ID_t id = oid->subs[sub];
	struct ng_mesg *resp;

	if ((resp = ng_dialog_id(id, NGM_GENERIC_COOKIE, NGM_NODEINFO,
	    NULL, 0)) == NULL)
		return (-1);

	*info = *(struct nodeinfo *)(void *)resp->data;
	free(resp);
	return (0);
}

static int
ncmp(const void *p1, const void *p2)
{
	const struct nodeinfo *i1 = p1;
	const struct nodeinfo *i2 = p2;

	if (i1->id < i2->id)
		return (-1);
	if (i1->id > i2->id)
		return (+1);
	return (0);
}

static int
find_node_next(const struct asn_oid *oid, u_int sub, struct nodeinfo *info)
{
	u_int idxlen = oid->len - sub;
	struct ng_mesg *resp;
	struct namelist *list;
	ng_ID_t id;
	u_int i;

	if ((resp = ng_dialog_id(snmp_node, NGM_GENERIC_COOKIE, NGM_LISTNODES,
	    NULL, 0)) == NULL)
		return (-1);
	list = (struct namelist *)(void *)resp->data;

	qsort(list->nodeinfo, list->numnames, sizeof(list->nodeinfo[0]), ncmp);

	if (idxlen == 0) {
		if (list->numnames == 0) {
			free(resp);
			return (-1);
		}
		*info = list->nodeinfo[0];
		free(resp);
		return (0);
	}
	id = oid->subs[sub];

	for (i = 0; i < list->numnames; i++)
		if (list->nodeinfo[i].id > id) {
			*info = list->nodeinfo[i];
			free(resp);
			return (0);
		}

	free(resp);
	return (-1);
}

int
op_ng_node(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	u_int idxlen = value->var.len - sub;
	struct nodeinfo nodeinfo;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if (find_node_next(&value->var, sub, &nodeinfo) == -1)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = nodeinfo.id;
		break;

	  case SNMP_OP_GET:
		if (idxlen != 1)
			return (SNMP_ERR_NOSUCHNAME);
		if (find_node(&value->var, sub, &nodeinfo) == -1)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (idxlen != 1)
			return (SNMP_ERR_NO_CREATION);
		if (find_node(&value->var, sub, &nodeinfo) == -1)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
	  default:
		abort();
	}

	/*
	 * Come here for GET and COMMIT
	 */
	switch (which) {

	  case LEAF_begemotNgNodeStatus:
		value->v.integer = 1;
		break;
	  case LEAF_begemotNgNodeName:
		return (string_get(value, nodeinfo.name, -1));
	  case LEAF_begemotNgNodeType:
		return (string_get(value, nodeinfo.type, -1));
	  case LEAF_begemotNgNodeHooks:
		value->v.uint32 = nodeinfo.hooks;
		break;

	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * Implement the hook table
 */
static int
find_hook(int32_t id, const u_char *hook, size_t hooklen, struct linkinfo *info)
{
	struct ng_mesg *resp;
	struct hooklist *list;
	u_int i;

	if ((resp = ng_dialog_id(id, NGM_GENERIC_COOKIE,
	    NGM_LISTHOOKS, NULL, 0)) == NULL)
		return (-1);

	list = (struct hooklist *)(void *)resp->data;

	for (i = 0; i < list->nodeinfo.hooks; i++) {
		if (strlen(list->link[i].ourhook) == hooklen &&
		    strncmp(list->link[i].ourhook, hook, hooklen) == 0) {
			*info = list->link[i];
			free(resp);
			return (0);
		}
	}
	free(resp);
	return (-1);
}

static int
hook_cmp(const void *p1, const void *p2)
{
	const struct linkinfo *i1 = p1;
	const struct linkinfo *i2 = p2;

	if (strlen(i1->ourhook) < strlen(i2->ourhook))
		return (-1);
	if (strlen(i1->ourhook) > strlen(i2->ourhook))
		return (+1);
	return (strcmp(i1->ourhook, i2->ourhook));
}

static int
find_hook_next(const struct asn_oid *oid, u_int sub, struct nodeinfo *nodeinfo,
    struct linkinfo *linkinfo)
{
	u_int idxlen = oid->len - sub;
	struct namelist *list;
	struct ng_mesg *resp;
	struct hooklist *hooks;
	struct ng_mesg *resp1;
	u_int node_index;
	struct asn_oid idx;
	u_int i, j;

	/*
	 * Get and sort Node list
	 */
	if ((resp = ng_dialog_id(snmp_node, NGM_GENERIC_COOKIE, NGM_LISTNODES,
	    NULL, 0)) == NULL)
		return (-1);
	list = (struct namelist *)(void *)resp->data;

	qsort(list->nodeinfo, list->numnames, sizeof(list->nodeinfo[0]), ncmp);

	/*
	 * If we have no index, take the first node and return the
	 * first hook.
	 */
	if (idxlen == 0) {
		node_index = 0;
		goto return_first_hook;
	}

	/*
	 * Locate node
	 */
	for (node_index = 0; node_index < list->numnames; node_index++)
		if (list->nodeinfo[node_index].id >= oid->subs[sub])
			break;

	/*
	 * If we have only the node part of the index take, or
	 * there is no node with that Id, take the first hook of that node.
	 */
	if (idxlen == 1 || node_index >= list->numnames ||
	    list->nodeinfo[node_index].id > oid->subs[sub])
		goto return_first_hook;

	/*
	 * We had an exact match on the node id and have (at last part)
	 * of the hook name index. Loop through the hooks of the node
	 * and find the next one.
	 */
	if ((resp1 = ng_dialog_id(list->nodeinfo[node_index].id,
	    NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0)) == NULL) {
		free(resp);
		return (-1);
	}
	hooks = (struct hooklist *)(void *)resp1->data;
	if (hooks->nodeinfo.hooks > 0) {
		qsort(hooks->link, hooks->nodeinfo.hooks,
		    sizeof(hooks->link[0]), hook_cmp);
		for (i = 0; i < hooks->nodeinfo.hooks; i++) {
			idx.len = strlen(hooks->link[i].ourhook) + 1;
			idx.subs[0] = idx.len - 1;
			for (j = 0; j < idx.len; j++)
				idx.subs[j + 1] = hooks->link[i].ourhook[j];
			if (index_compare(oid, sub + 1, &idx) < 0)
				break;
		}
		if (i < hooks->nodeinfo.hooks) {
			*nodeinfo = hooks->nodeinfo;
			*linkinfo = hooks->link[i];

			free(resp);
			free(resp1);
			return (0);
		}
	}

	/* no hook found larger than the index on the index node - take
	 * first hook of next node */
	free(resp1);
	node_index++;

  return_first_hook:
	while (node_index < list->numnames) {
		if ((resp1 = ng_dialog_id(list->nodeinfo[node_index].id,
		    NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL, 0)) == NULL)
			break;
		hooks = (struct hooklist *)(void *)resp1->data;
		if (hooks->nodeinfo.hooks > 0) {
			qsort(hooks->link, hooks->nodeinfo.hooks,
			    sizeof(hooks->link[0]), hook_cmp);

			*nodeinfo = hooks->nodeinfo;
			*linkinfo = hooks->link[0];

			free(resp);
			free(resp1);
			return (0);
		}

		/* if we don't have hooks, try next node */
		free(resp1);
		node_index++;
	}

	free(resp);
	return (-1);
}

int
op_ng_hook(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	struct linkinfo linkinfo;
	struct nodeinfo nodeinfo;
	u_int32_t lid;
	u_char *hook;
	size_t hooklen;
	u_int i;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if (find_hook_next(&value->var, sub, &nodeinfo, &linkinfo) == -1)
			return (SNMP_ERR_NOSUCHNAME);

		value->var.len = sub + 1 + 1 + strlen(linkinfo.ourhook);
		value->var.subs[sub] = nodeinfo.id;
		value->var.subs[sub + 1] = strlen(linkinfo.ourhook);
		for (i = 0; i < strlen(linkinfo.ourhook); i++)
			value->var.subs[sub + i + 2] =
			    linkinfo.ourhook[i];
		break;

	  case SNMP_OP_GET:
		if (index_decode(&value->var, sub, iidx, &lid,
		    &hook, &hooklen))
			return (SNMP_ERR_NOSUCHNAME);
		if (find_hook(lid, hook, hooklen, &linkinfo) == -1) {
			free(hook);
			return (SNMP_ERR_NOSUCHNAME);
		}
		free(hook);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, &lid,
		    &hook, &hooklen))
			return (SNMP_ERR_NO_CREATION);
		if (find_hook(lid, hook, hooklen, &linkinfo) == -1) {
			free(hook);
			return (SNMP_ERR_NO_CREATION);
		}
		free(hook);
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
	  default:
		abort();

	}

	switch (which) {

	  case LEAF_begemotNgHookStatus:
		value->v.integer = 1;
		break;
	  case LEAF_begemotNgHookPeerNodeId:
		value->v.uint32 = linkinfo.nodeinfo.id;
		break;
	  case LEAF_begemotNgHookPeerHook:
		return (string_get(value, linkinfo.peerhook, -1));
	  case LEAF_begemotNgHookPeerType:
		return (string_get(value, linkinfo.nodeinfo.type, -1));
	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}
