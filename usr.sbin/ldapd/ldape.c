/*	$OpenBSD: ldape.c,v 1.40 2025/05/11 15:38:48 tb Exp $ */

/*
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldapd.h"
#include "log.h"

void			 ldape_sig_handler(int fd, short why, void *data);
static void		 ldape_auth_result(struct imsg *imsg);
static void		 ldape_open_result(struct imsg *imsg);
static void		 ldape_imsgev(struct imsgev *iev, int code,
			    struct imsg *imsg);
static void		 ldape_needfd(struct imsgev *iev);

int			 ldap_starttls(struct request *req);
void			 send_ldap_extended_response(struct conn *conn,
				int msgid, unsigned int type,
				long long result_code,
				const char *extended_oid);

struct imsgev		*iev_ldapd;
struct control_sock	 csock;

void
ldape_sig_handler(int sig, short why, void *data)
{
	log_debug("ldape: got signal %d", sig);
	if (sig == SIGCHLD) {
		for (;;) {
			pid_t	 pid;
			int	 status;

			pid = waitpid(WAIT_ANY, &status, WNOHANG);
			if (pid <= 0)
				break;
		}
		return;
	}

	event_loopexit(NULL);
}

void
send_ldap_extended_response(struct conn *conn, int msgid, unsigned int type,
    long long result_code, const char *extended_oid)
{
	ssize_t			 rc;
	struct ber_element	*root, *elm;
	void			*buf;

	log_debug("sending response %u with result %lld", type, result_code);

	if ((root = ober_add_sequence(NULL)) == NULL)
		goto fail;

	elm = ober_printf_elements(root, "d{tEss",
	    msgid, BER_CLASS_APP, type, result_code, "", "");
	if (elm == NULL)
		goto fail;

	if (extended_oid)
		if (ober_add_string(elm, extended_oid) == NULL)
			goto fail;

	ldap_debug_elements(root, type, "sending response on fd %d", conn->fd);

	rc = ober_write_elements(&conn->ber, root);
	ober_free_elements(root);

	if (rc < 0)
		log_warn("failed to create ldap result");
	else {
		ober_get_writebuf(&conn->ber, &buf);
		if (bufferevent_write(conn->bev, buf, rc) != 0)
			log_warn("failed to send ldap result");
	}

	return;
fail:
	ober_free_elements(root);
}

int
ldap_refer(struct request *req, const char *basedn, struct search *search,
    struct referrals *refs)
{
	struct ber_element	*root, *elm, *ref_root = NULL;
	struct referral		*ref;
	long long		 result_code = LDAP_REFERRAL;
	unsigned int		 type;
	ssize_t			 rc;
	void			*buf;
	char			*url, *scope_str = NULL;

	if (req->type == LDAP_REQ_SEARCH)
		type = LDAP_RES_SEARCH_RESULT;
	else
		type = req->type + 1;

	if (search != NULL) {
		if (search->scope != LDAP_SCOPE_SUBTREE)
			scope_str = "base";
		else
			scope_str = "sub";
	}

	log_debug("sending referral in response %u on msgid %lld",
	    type, req->msgid);

	if ((root = ober_add_sequence(NULL)) == NULL)
		goto fail;

	if ((elm = ref_root = ober_add_sequence(NULL)) == NULL)
		goto fail;
	ober_set_header(ref_root, BER_CLASS_CONTEXT, LDAP_REQ_SEARCH);
	SLIST_FOREACH(ref, refs, next) {
		if (search != NULL)
			rc = asprintf(&url, "%s/%s??%s", ref->url, basedn,
			    scope_str);
		else
			rc = asprintf(&url, "%s/%s", ref->url, basedn);
		if (rc == -1) {
			log_warn("asprintf");
			goto fail;
		}
		log_debug("adding referral '%s'", url);
		elm = ober_add_string(elm, url);
		free(url);
		if (elm == NULL)
			goto fail;
	}

	elm = ober_printf_elements(root, "d{tEsse",
	    req->msgid, BER_CLASS_APP, type, result_code, "", "", ref_root);
	if (elm == NULL)
		goto fail;
	ref_root = NULL;

	rc = ober_write_elements(&req->conn->ber, root);
	ober_free_elements(root);

	if (rc < 0)
		log_warn("failed to create ldap result");
	else {
		ober_get_writebuf(&req->conn->ber, &buf);
		if (bufferevent_write(req->conn->bev, buf, rc) != 0)
			log_warn("failed to send ldap result");
	}

	request_free(req);
	return LDAP_REFERRAL;

fail:
	ober_free_elements(root);
	ober_free_elements(ref_root);
	request_free(req);
	return LDAP_REFERRAL;
}

void
send_ldap_result(struct conn *conn, int msgid, unsigned int type,
    long long result_code)
{
	send_ldap_extended_response(conn, msgid, type, result_code, NULL);
}

int
ldap_respond(struct request *req, int code)
{
	if (code >= 0)
		send_ldap_result(req->conn, req->msgid, req->type + 1, code);
	request_free(req);
	return code;
}

int
ldap_abandon(struct request *req)
{
	long long	 msgid;
	struct search	*search;

	if (ober_scanf_elements(req->op, "i", &msgid) != 0) {
		request_free(req);
		return -1;	/* protocol error, but don't respond */
	}

	TAILQ_FOREACH(search, &req->conn->searches, next) {
		if (search->req->msgid == msgid) {
			/* unlinks the search from conn->searches */
			search_close(search);
			break;
		}
	}
	request_free(req);
	return -1;
}

int
ldap_unbind(struct request *req)
{
	log_debug("current bind dn = %s",
	    req->conn->binddn == NULL ? "" : req->conn->binddn);
	conn_disconnect(req->conn);
	request_free(req);
	return -1;		/* don't send any response */
}

int
ldap_compare(struct request *req)
{
	struct ber_element	*entry, *elm, *attr;
	struct namespace	*ns;
	struct referrals	*refs;
	struct attr_type	*at;
	char			*dn, *aname, *value, *s;

	if (ober_scanf_elements(req->op, "{s{ss", &dn, &aname, &value) != 0) {
		log_debug("%s: protocol error", __func__);
		request_free(req);
		return -1;
	}

	if ((at = lookup_attribute(conf->schema, aname)) == NULL)
		return ldap_respond(req, LDAP_UNDEFINED_TYPE);

	if ((ns = namespace_for_base(dn)) == NULL) {
		refs = namespace_referrals(dn);
		if (refs == NULL)
			return ldap_respond(req, LDAP_NO_SUCH_OBJECT);
		else
			return ldap_refer(req, dn, NULL, refs);
	}

	if ((entry = namespace_get(ns, dn)) == NULL)
		return ldap_respond(req, LDAP_NO_SUCH_OBJECT);

	if ((attr = ldap_find_attribute(entry, at)) == NULL) {
		ober_free_elements(entry);
		return ldap_respond(req, LDAP_NO_SUCH_ATTRIBUTE);
	}

	if ((attr = attr->be_next) == NULL) {	/* skip attribute name */
		ober_free_elements(entry);
		return ldap_respond(req, LDAP_OTHER);
	}

	for (elm = attr->be_sub; elm != NULL; elm = elm->be_next) {
		if (ober_get_string(elm, &s) != 0) {
			ober_free_elements(entry);
			return ldap_respond(req, LDAP_OTHER);
		}
		if (strcasecmp(value, s) == 0) {
			ober_free_elements(entry);
			return ldap_respond(req, LDAP_COMPARE_TRUE);
		}
	}

	ober_free_elements(entry);
	return ldap_respond(req, LDAP_COMPARE_FALSE);
}

int
ldap_starttls(struct request *req)
{
	if ((req->conn->listener->flags & F_STARTTLS) == 0) {
		log_debug("StartTLS not configured for this connection");
		return LDAP_OPERATIONS_ERROR;
	}

	req->conn->s_flags |= F_STARTTLS;
	return LDAP_SUCCESS;
}

int
ldap_extended(struct request *req)
{
	int			 i, rc = LDAP_PROTOCOL_ERROR;
	char			*oid = NULL;
	struct {
		const char	*oid;
		int (*fn)(struct request *);
	} extended_ops[] = {
		{ "1.3.6.1.4.1.1466.20037", ldap_starttls },
		{ NULL }
	};

	if (ober_scanf_elements(req->op, "{s", &oid) != 0)
		goto done;

	log_debug("got extended operation %s", oid);
	req->op = req->op->be_sub->be_next;

	for (i = 0; extended_ops[i].oid != NULL; i++) {
		if (strcmp(oid, extended_ops[i].oid) == 0) {
			rc = extended_ops[i].fn(req);
			break;
		}
	}

	if (extended_ops[i].fn == NULL)
		log_warnx("unimplemented extended operation %s", oid);

done:
	send_ldap_extended_response(req->conn, req->msgid, LDAP_RES_EXTENDED,
	    rc, oid);

	request_free(req);
	return 0;
}

void
ldape(int debug, int verbose, char *csockpath)
{
	int			 on = 1;
	struct namespace	*ns;
	struct listener		*l;
	struct sockaddr_un	*sun = NULL;
	struct event		 ev_sigint;
	struct event		 ev_sigterm;
	struct event		 ev_sigchld;
	struct event		 ev_sighup;
	struct ssl		 key;
	struct passwd		*pw;
	char			 host[128];
	mode_t			old_umask = 0;
	
	TAILQ_INIT(&conn_list);

	ldap_loginit("ldap server", debug, verbose);
	setproctitle("ldap server");
	event_init();

	signal_set(&ev_sigint, SIGINT, ldape_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, ldape_sig_handler, NULL);
	signal_set(&ev_sigchld, SIGCHLD, ldape_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, ldape_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sigchld, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* Initialize parent imsg events. */
	if ((iev_ldapd = calloc(1, sizeof(struct imsgev))) == NULL)
		fatal("calloc");
	imsgev_init(iev_ldapd, PROC_PARENT_SOCK_FILENO, NULL, ldape_imsgev,
	    ldape_needfd);

	/* Initialize control socket. */
	memset(&csock, 0, sizeof(csock));
	csock.cs_name = csockpath;
	control_init(&csock);
	control_listen(&csock);

	/* Initialize LDAP listeners.
	 */
	TAILQ_FOREACH(l, &conf->listeners, entry) {
		l->fd = socket(l->ss.ss_family, SOCK_STREAM | SOCK_NONBLOCK,
		    0);
		if (l->fd == -1)
			fatal("ldape: socket");

		setsockopt(l->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

		if (l->ss.ss_family == AF_UNIX) {
			sun = (struct sockaddr_un *)&l->ss;
			log_info("listening on %s", sun->sun_path);
			if (unlink(sun->sun_path) == -1 && errno != ENOENT)
				fatal("ldape: unlink");
		} else {
			print_host(&l->ss, host, sizeof(host));
			log_info("listening on %s:%d", host, ntohs(l->port));
		}

		if (l->ss.ss_family == AF_UNIX) {
			old_umask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
		}

		if (bind(l->fd, (struct sockaddr *)&l->ss, l->ss.ss_len) != 0)
			fatal("ldape: bind");

		if (l->ss.ss_family == AF_UNIX) {
			mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;

			(void)umask(old_umask);
			if (chmod(sun->sun_path, mode) == -1) {
				unlink(sun->sun_path);
				fatal("ldape: chmod");
			}
		}

		if (listen(l->fd, 20) != 0)
			fatal("ldape: listen");

		event_set(&l->ev, l->fd, EV_READ, conn_accept, l);
		event_add(&l->ev, NULL);
		evtimer_set(&l->evt, conn_accept, l);

		if (l->flags & F_SSL) {
			if (strlcpy(key.ssl_name, l->ssl_cert_name,
			    sizeof(key.ssl_name)) >= sizeof(key.ssl_name))
				fatal("ldape: certificate name truncated");

			l->ssl = SPLAY_FIND(ssltree, conf->sc_ssl, &key);
			if (l->ssl == NULL)
				fatal("ldape: certificate tree corrupted");

			l->tls = tls_server();
			if (l->tls == NULL)
				fatal("ldape: couldn't allocate tls context");

			if (tls_configure(l->tls, l->ssl->config)) {
				log_warnx("ldape: %s", tls_error(l->tls));
				fatalx("ldape: couldn't configure tls");
			}
		}
	}

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		if (!namespace_has_referrals(ns) && namespace_open(ns) != 0)
			fatal("%s", ns->suffix);
	}

	if ((pw = getpwnam(LDAPD_USER)) == NULL)
		fatal("getpwnam");

	if (pw != NULL) {
		if (chroot(pw->pw_dir) == -1)
			fatal("chroot");
		if (chdir("/") == -1)
			fatal("chdir(\"/\")");

		if (setgroups(1, &pw->pw_gid) ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
			fatal("cannot drop privileges");
	}

	if (pledge("stdio flock inet unix recvfd", NULL) == -1)
		fatal("pledge");

	log_debug("ldape: entering event loop");
	event_dispatch();

	while ((ns = TAILQ_FIRST(&conf->namespaces)) != NULL)
		namespace_remove(ns);

	control_cleanup(&csock);

	log_info("ldape: exiting");
	exit(0);
}

static void
ldape_imsgev(struct imsgev *iev, int code, struct imsg *imsg)
{
	switch (code) {
	case IMSGEV_IMSG:
		log_debug("%s: got imsg %d on fd %d",
		    __func__, imsg->hdr.type, iev->ibuf.fd);
		switch (imsg->hdr.type) {
		case IMSG_LDAPD_AUTH_RESULT:
			ldape_auth_result(imsg);
			break;
		case IMSG_LDAPD_OPEN_RESULT:
			ldape_open_result(imsg);
			break;
		default:
			log_debug("%s: unexpected imsg %d",
			    __func__, imsg->hdr.type);
			break;
		}
		break;
	case IMSGEV_EREAD:
	case IMSGEV_EWRITE:
	case IMSGEV_EIMSG:
		fatal("imsgev read/write error");
		break;
	case IMSGEV_DONE:
		event_loopexit(NULL);
		break;
	}
}

static void
ldape_needfd(struct imsgev *iev)
{
	/* Try to close a control connection first */
	if (control_close_any(&csock) == 0) {
		log_warn("closed a control connection");
		return;
	}

	if (conn_close_any() == 0) {
		log_warn("closed a client connection");
		return;
	}

	fatal("unable to free an fd");
}

static void
ldape_auth_result(struct imsg *imsg)
{
	struct conn		*conn;
	struct auth_res		*ares = imsg->data;

	log_debug("authentication on conn %d/%lld = %d", ares->fd, ares->msgid,
	    ares->ok);
	conn = conn_by_fd(ares->fd);
	if (conn->bind_req != NULL && conn->bind_req->msgid == ares->msgid)
		ldap_bind_continue(conn, ares->ok);
	else
		log_warnx("spurious auth result");
}

static void
ldape_open_result(struct imsg *imsg)
{
	struct namespace	*ns;
	struct open_req		*oreq = imsg->data;
	int			 fd;

	if (imsg->hdr.len != sizeof(*oreq) + IMSG_HEADER_SIZE)
		fatal("invalid size of open result");

	if (oreq->path[PATH_MAX-1] != '\0')
		fatal("bogus path");

	fd = imsg_get_fd(imsg);
	log_debug("open(%s) returned fd %d", oreq->path, fd);

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		if (namespace_has_referrals(ns))
			continue;
		if (strcmp(oreq->path, ns->data_path) == 0) {
			namespace_set_data_fd(ns, fd);
			break;
		}
		if (strcmp(oreq->path, ns->indx_path) == 0) {
			namespace_set_indx_fd(ns, fd);
			break;
		}
	}

	if (ns == NULL) {
		log_warnx("spurious open result");
		close(fd);
	} else
		namespace_queue_schedule(ns, 0);
}

