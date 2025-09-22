/*	$OpenBSD: radiusd_file.c,v 1.8 2024/11/21 13:43:10 claudio Exp $	*/

/*
 * Copyright (c) 2024 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <limits.h>
#include <md5.h>
#include <radius.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chap_ms.h"
#include "imsg_subr.h"
#include "log.h"
#include "radiusd.h"
#include "radiusd_module.h"

struct module_file_params {
	int			 debug;
	char			 path[PATH_MAX];
};

struct module_file {
	struct module_base	*base;
	struct imsgbuf		 ibuf;
	struct module_file_params
				 params;
};

struct module_file_userinfo {
	struct in_addr		frame_ip_address;
	char			password[0];
};

/* IPC between priv and main */
enum {
	IMSG_RADIUSD_FILE_OK = 1000,
	IMSG_RADIUSD_FILE_NG,
	IMSG_RADIUSD_FILE_PARAMS,
	IMSG_RADIUSD_FILE_USERINFO
};

static void	 parent_dispatch_main(struct module_file_params *,
		    struct imsgbuf *, struct imsg *);
static void	 module_file_main(void) __dead;
static pid_t	 start_child(char *, int);
static void	 module_file_config_set(void *, const char *, int,
		    char * const *);
static void	 module_file_start(void *);
static void	 module_file_access_request(void *, u_int, const u_char *,
		    size_t);
static void	 auth_pap(struct module_file *, u_int, RADIUS_PACKET *, char *,
		    struct module_file_userinfo *);
static void	 auth_md5chap(struct module_file *, u_int, RADIUS_PACKET *,
		    char *, struct module_file_userinfo *);
static void	 auth_mschapv2(struct module_file *, u_int, RADIUS_PACKET *,
		    char *, struct module_file_userinfo *);
static void	 auth_reject(struct module_file *, u_int, RADIUS_PACKET *,
		    char *, struct module_file_userinfo *);

static struct module_handlers module_file_handlers = {
	.access_request		= module_file_access_request,
	.config_set		= module_file_config_set,
	.start			= module_file_start
};

int
main(int argc, char *argv[])
{
	int				 ch, pairsock[2], status;
	pid_t				 pid;
	char				*saved_argv0;
	struct imsgbuf			 ibuf;
	struct imsg			 imsg;
	ssize_t				 n;
	size_t				 datalen;
	struct module_file_params	*paramsp, params;
	char				 pathdb[PATH_MAX];

	while ((ch = getopt(argc, argv, "M")) != -1)
		switch (ch) {
		case 'M':
			module_file_main();
			/* not reached */
			break;
		}
	saved_argv0 = argv[0];

	argc -= optind;
	argv += optind;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, PF_UNSPEC,
	    pairsock) == -1)
		err(EXIT_FAILURE, "socketpair");

	log_init(0);

	pid = start_child(saved_argv0, pairsock[1]);

	/* Privileged process */
	if (pledge("stdio rpath unveil", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
	setproctitle("[priv]");
	if (imsgbuf_init(&ibuf, pairsock[0]) == -1)
		err(EXIT_FAILURE, "imsgbuf_init");

	/* Receive parameters from the main process. */
	if (imsg_sync_read(&ibuf, 2000) <= 0 ||
	    (n = imsg_get(&ibuf, &imsg)) <= 0)
		exit(EXIT_FAILURE);
	if (imsg.hdr.type != IMSG_RADIUSD_FILE_PARAMS)
		err(EXIT_FAILURE, "Receieved unknown message type %d",
		    imsg.hdr.type);
	datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (datalen < sizeof(params))
		err(EXIT_FAILURE, "Receieved IMSG_RADIUSD_FILE_PARAMS "
		    "message is wrong size");
	paramsp = imsg.data;
	if (paramsp->path[0] != '\0') {
		strlcpy(pathdb, paramsp->path, sizeof(pathdb));
		strlcat(pathdb, ".db", sizeof(pathdb));
		if (unveil(paramsp->path, "r") == -1 ||
		    unveil(pathdb, "r") == -1)
			err(EXIT_FAILURE, "unveil");
	}
	if (paramsp->debug)
		log_init(1);

	if (unveil(NULL, NULL) == -1)
		err(EXIT_FAILURE, "unveil");

	memcpy(&params, paramsp, sizeof(params));

	for (;;) {
		if (imsgbuf_read(&ibuf) != 1)
			break;
		for (;;) {
			if ((n = imsg_get(&ibuf, &imsg)) == -1)
				break;
			if (n == 0)
				break;
			parent_dispatch_main(&params, &ibuf, &imsg);
			imsg_free(&imsg);
			imsgbuf_flush(&ibuf);
		}
		imsgbuf_flush(&ibuf);
	}
	imsgbuf_clear(&ibuf);

	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR)
			break;
	}
	exit(WEXITSTATUS(status));
}

void
parent_dispatch_main(struct module_file_params *params, struct imsgbuf *ibuf,
    struct imsg *imsg)
{
	size_t				 datalen, entsz, passz;
	const char			*username;
	char				*buf, *db[2], *str;
	int				 ret;
	struct module_file_userinfo	*ent;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	switch (imsg->hdr.type) {
	case IMSG_RADIUSD_FILE_USERINFO:
		if (datalen == 0 ||
		    *((char *)imsg->data + datalen - 1) != '\0') {
			log_warn("%s: received IMSG_RADIUSD_FILE_USERINFO "
			    "is wrong", __func__);
			goto on_error;
		}
		username = imsg->data;
		db[0] = params->path;
		db[1] = NULL;
		if ((ret = cgetent(&buf, db, username)) < 0) {
			log_info("user `%s' is not configured", username);
			goto on_error;
		}
		if ((ret = cgetstr(buf, "password", &str)) < 0) {
			log_info("password for `%s' is not configured",
			    username);
			goto on_error;
		}
		passz = strlen(str) + 1;
		entsz = offsetof(struct module_file_userinfo, password[passz]);
		if ((ent = calloc(1, entsz)) == NULL) {
			log_warn("%s; calloc", __func__);
			goto on_error;
		}
		strlcpy(ent->password, str, passz);
		imsg_compose(ibuf, IMSG_RADIUSD_FILE_USERINFO, 0, -1, -1,
		    ent, entsz);
		freezero(ent, entsz);
		break;
	}
	return;
 on_error:
	imsg_compose(ibuf, IMSG_RADIUSD_FILE_NG, 0, -1, -1, NULL, 0);
}

/* main process */
void
module_file_main(void)
{
	struct module_file	 module_file;

	setproctitle("[main]");

	memset(&module_file, 0, sizeof(module_file));
	if ((module_file.base = module_create(STDIN_FILENO, &module_file,
	    &module_file_handlers)) == NULL)
		err(1, "Could not create a module instance");

	module_drop_privilege(module_file.base, 0);

	module_load(module_file.base);
	if (imsgbuf_init(&module_file.ibuf, 3) == -1)
		err(EXIT_FAILURE, "imsgbuf_init");

	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
	while (module_run(module_file.base) == 0)
		;

	module_destroy(module_file.base);

	exit(0);
}

pid_t
start_child(char *argv0, int fd)
{
	char *argv[5];
	int argc = 0;
	pid_t pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:
		close(fd);
		return (pid);
	}

	if (fd != 3) {
		if (dup2(fd, 3) == -1)
			fatal("cannot setup imsg fd");
	} else if (fcntl(fd, F_SETFD, 0) == -1)
		fatal("cannot setup imsg fd");

	argv[argc++] = argv0;
	argv[argc++] = "-M";	/* main proc */
	argv[argc++] = NULL;
	execvp(argv0, argv);
	fatal("execvp");
}

void
module_file_config_set(void *ctx, const char *name, int valc,
    char * const * valv)
{
	struct module_file	*module = ctx;
	char			*errmsg;

	if (strcmp(name, "path") == 0) {
		SYNTAX_ASSERT(valc == 1, "`path' must have a argument");
		if (strlcpy(module->params.path, valv[0], sizeof(
		    module->params.path)) >= sizeof(module->params.path)) {
			module_send_message(module->base, IMSG_NG,
			    "`path' is too long");
			return;
		}
		module_send_message(module->base, IMSG_OK, NULL);
	} else if (strcmp(name, "_debug") == 0) {
		log_init(1);
		module->params.debug = 1;
		module_send_message(module->base, IMSG_OK, NULL);
	} else if (strncmp(name, "_", 1) == 0)
		/* ignore all internal messages */
		module_send_message(module->base, IMSG_OK, NULL);
	else
		module_send_message(module->base, IMSG_NG,
		    "Unknown config parameter `%s'", name);
	return;
 syntax_error:
	module_send_message(module->base, IMSG_NG, "%s", errmsg);
	return;
}

void
module_file_start(void *ctx)
{
	struct module_file	*module = ctx;

	/* Send parameters to parent */
	if (module->params.path[0] == '\0') {
		module_send_message(module->base, IMSG_NG,
		    "`path' is not configured");
		return;
	}
	imsg_compose(&module->ibuf, IMSG_RADIUSD_FILE_PARAMS, 0, -1, -1,
	    &module->params, sizeof(module->params));
	imsgbuf_flush(&module->ibuf);

	module_send_message(module->base, IMSG_OK, NULL);
}

void
module_file_access_request(void *ctx, u_int query_id, const u_char *pkt,
    size_t pktlen)
{
	size_t				 datalen;
	struct module_file		*self = ctx;
	RADIUS_PACKET			*radpkt = NULL;
	char				 username[256];
	ssize_t				 n;
	struct imsg			 imsg;
	struct module_file_userinfo	*ent;

	memset(&imsg, 0, sizeof(imsg));

	if ((radpkt = radius_convert_packet(pkt, pktlen)) == NULL) {
		log_warn("%s: radius_convert_packet()", __func__);
		goto out;
	}
	radius_get_string_attr(radpkt, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username));

	imsg_compose(&self->ibuf, IMSG_RADIUSD_FILE_USERINFO, 0, -1, -1,
	    username, strlen(username) + 1);
	imsgbuf_flush(&self->ibuf);
	if (imsgbuf_read(&self->ibuf) != 1) {
		log_warn("%s: imsgbuf_read()", __func__);
		goto out;
	}
	if ((n = imsg_get(&self->ibuf, &imsg)) <= 0) {
		log_warn("%s: imsg_get()", __func__);
		goto out;
	}

	datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (imsg.hdr.type == IMSG_RADIUSD_FILE_USERINFO) {
		if (datalen <= offsetof(struct module_file_userinfo,
		    password[0])) {
			log_warn("%s: received IMSG_RADIUSD_FILE_USERINFO is "
			    "invalid", __func__);
			goto out;
		}
		ent = imsg.data;
		if (radius_has_attr(radpkt, RADIUS_TYPE_USER_PASSWORD))
			auth_pap(self, query_id, radpkt, username, ent);
		else if (radius_has_attr(radpkt, RADIUS_TYPE_CHAP_PASSWORD))
			auth_md5chap(self, query_id, radpkt, username, ent);
		else if (radius_has_vs_attr(radpkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_RESPONSE))
			auth_mschapv2(self, query_id, radpkt, username, ent);
		else
			auth_reject(self, query_id, radpkt, username, ent);
	} else
		auth_reject(self, query_id, radpkt, username, NULL);
 out:
	if (radpkt != NULL)
		radius_delete_packet(radpkt);
	imsg_free(&imsg);
	return;
}

void
auth_pap(struct module_file *self, u_int q_id, RADIUS_PACKET *radpkt,
    char *username, struct module_file_userinfo *ent)
{
	RADIUS_PACKET	*respkt = NULL;
	char		 pass[256];
	int		 ret;

	if (radius_get_string_attr(radpkt, RADIUS_TYPE_USER_PASSWORD, pass,
	    sizeof(pass)) != 0) {
		log_warnx("%s: radius_get_string_attr", __func__);
		return;
	}
	ret = strcmp(ent->password, pass);
	explicit_bzero(ent->password, strlen(ent->password));
	log_info("q=%u User `%s' authentication %s (PAP)", q_id, username,
	    (ret == 0)? "succeeded" : "failed");
	if ((respkt = radius_new_response_packet((ret == 0)?
	    RADIUS_CODE_ACCESS_ACCEPT : RADIUS_CODE_ACCESS_REJECT, radpkt))
	    == NULL) {
		log_warn("%s: radius_new_response_packet()", __func__);
		return;
	}
	module_accsreq_answer(self->base, q_id,
	    radius_get_data(respkt), radius_get_length(respkt));
	radius_delete_packet(respkt);
}

void
auth_md5chap(struct module_file *self, u_int q_id, RADIUS_PACKET *radpkt,
    char *username, struct module_file_userinfo *ent)
{
	RADIUS_PACKET	*respkt = NULL;
	size_t		 attrlen, challlen;
	u_char		 chall[256], idpass[17], digest[16];
	int		 ret;
	MD5_CTX		 md5;

	attrlen = sizeof(idpass);
	if (radius_get_raw_attr(radpkt, RADIUS_TYPE_CHAP_PASSWORD, idpass,
	    &attrlen) != 0) {
		log_warnx("%s: radius_get_string_attr", __func__);
		return;
	}
	challlen = sizeof(chall);
	if (radius_get_raw_attr(radpkt, RADIUS_TYPE_CHAP_CHALLENGE, chall,
	    &challlen) != 0) {
		log_warnx("%s: radius_get_string_attr", __func__);
		return;
	}
	MD5Init(&md5);
	MD5Update(&md5, idpass, 1);
	MD5Update(&md5, ent->password, strlen(ent->password));
	MD5Update(&md5, chall, challlen);
	MD5Final(digest, &md5);

	ret = timingsafe_bcmp(idpass + 1, digest, sizeof(digest));
	log_info("q=%u User `%s' authentication %s (CHAP)", q_id, username,
	    (ret == 0)? "succeeded" : "failed");
	if ((respkt = radius_new_response_packet((ret == 0)?
	    RADIUS_CODE_ACCESS_ACCEPT : RADIUS_CODE_ACCESS_REJECT, radpkt))
	    == NULL) {
		log_warn("%s: radius_new_response_packet()", __func__);
		return;
	}
	module_accsreq_answer(self->base, q_id,
	    radius_get_data(respkt), radius_get_length(respkt));
	radius_delete_packet(respkt);
}

void
auth_mschapv2(struct module_file *self, u_int q_id, RADIUS_PACKET *radpkt,
    char *username, struct module_file_userinfo *ent)
{
	RADIUS_PACKET		*respkt = NULL;
	size_t			 attrlen;
	int			 i, lpass;
	char			*pass = NULL;
	uint8_t			 chall[MSCHAPV2_CHALLENGE_SZ];
	uint8_t			 ntresponse[24], authenticator[16];
	uint8_t			 pwhash[16], pwhash2[16], master[64];
	struct {
		uint8_t		 salt[2];
		uint8_t		 len;
		uint8_t		 key[16];
		uint8_t		 pad[15];
	} __packed		 rcvkey, sndkey;
	struct {
		uint8_t		 ident;
		uint8_t		 flags;
		uint8_t		 peerchall[16];
		uint8_t		 reserved[8];
		uint8_t		 ntresponse[24];
	} __packed		 resp;
	struct authresp {
		uint8_t		 ident;
		uint8_t		 authresp[42];
	} __packed		 authresp;


	attrlen = sizeof(chall);
	if (radius_get_vs_raw_attr(radpkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP_CHALLENGE, chall, &attrlen) != 0) {
		log_info("q=%u failed to retribute MS-CHAP-Challenge", q_id);
		goto on_error;
	}
	attrlen = sizeof(resp);
	if (radius_get_vs_raw_attr(radpkt, RADIUS_VENDOR_MICROSOFT,
	    RADIUS_VTYPE_MS_CHAP2_RESPONSE, &resp, &attrlen) != 0) {
		log_info("q=%u failed to retribute MS-CHAP2-Response", q_id);
		goto on_error;
	}

	/* convert the password to UTF16-LE */
	lpass = strlen(ent->password);
	if ((pass = calloc(1, lpass * 2)) == NULL) {
		log_warn("%s: calloc()", __func__);
		goto on_error;
	}
	for (i = 0; i < lpass; i++) {
		pass[i * 2] = ent->password[i];
		pass[i * 2 + 1] = '\0';
	}

	/* calculate NT-Response by the password */
	mschap_nt_response(chall, resp.peerchall,
	    username, strlen(username), pass, lpass * 2, ntresponse);

	if (timingsafe_bcmp(ntresponse, resp.ntresponse, 24) != 0) {
		log_info("q=%u User `%s' authentication failed (MSCHAPv2)",
		    q_id, username);
		if ((respkt = radius_new_response_packet(
		    RADIUS_CODE_ACCESS_REJECT, radpkt)) == NULL) {
			log_warn("%s: radius_new_response_packet()", __func__);
			goto on_error;
		}
		authresp.ident = resp.ident;
		strlcpy(authresp.authresp, "E=691 R=0 V=3",
		    sizeof(authresp.authresp));
		radius_put_vs_raw_attr(respkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_ERROR, &authresp,
		    offsetof(struct authresp, authresp[13]));
	} else {
		log_info("q=%u User `%s' authentication succeeded (MSCHAPv2)",
		    q_id, username);
		if ((respkt = radius_new_response_packet(
		    RADIUS_CODE_ACCESS_ACCEPT, radpkt)) == NULL) {
			log_warn("%s: radius_new_response_packet()", __func__);
			goto on_error;
		}
		mschap_auth_response(pass, lpass * 2, ntresponse, chall,
		    resp.peerchall, username, strlen(username),
		    authresp.authresp);
		authresp.ident = resp.ident;

		radius_put_vs_raw_attr(respkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_SUCCESS, &authresp,
		    offsetof(struct authresp, authresp[42]));

		mschap_ntpassword_hash(pass, lpass * 2, pwhash);
		mschap_ntpassword_hash(pwhash, sizeof(pwhash), pwhash2);
		mschap_masterkey(pwhash2, ntresponse, master);
		radius_get_authenticator(radpkt, authenticator);

		/* MS-MPPE-Recv-Key  */
		memset(&rcvkey, 0, sizeof(rcvkey));
		arc4random_buf(rcvkey.salt, sizeof(rcvkey.salt));
		rcvkey.salt[0] |= 0x80;
		rcvkey.len = 16;
		mschap_asymetric_startkey(master, rcvkey.key, 16, 0, 1);
		radius_put_vs_raw_attr(respkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MPPE_RECV_KEY, &rcvkey, sizeof(rcvkey));

		/* MS-MPPE-Send-Key  */
		memset(&sndkey, 0, sizeof(sndkey));
		arc4random_buf(sndkey.salt, sizeof(sndkey.salt));
		sndkey.salt[0] |= 0x80;
		sndkey.len = 16;
		mschap_asymetric_startkey(master, sndkey.key, 16, 1, 1);
		radius_put_vs_raw_attr(respkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MPPE_SEND_KEY, &sndkey, sizeof(sndkey));
	}

	module_accsreq_answer(self->base, q_id,
	    radius_get_data(respkt), radius_get_length(respkt));
 on_error:
	/* bzero password */
	explicit_bzero(ent->password, strlen(ent->password));
	if (pass != NULL)
		explicit_bzero(pass, lpass * 2);
	free(pass);
	if (respkt != NULL)
		radius_delete_packet(respkt);
}

void
auth_reject(struct module_file *self, u_int q_id, RADIUS_PACKET *radpkt,
    char *username, struct module_file_userinfo *ent)
{
	RADIUS_PACKET	*respkt = NULL;

	if (ent != NULL)
		explicit_bzero(ent->password, strlen(ent->password));

	log_info("q=%u User `%s' authentication failed", q_id,
	    username);
	if ((respkt = radius_new_response_packet(RADIUS_CODE_ACCESS_REJECT,
	    radpkt)) == NULL) {
		log_warn("%s: radius_new_response_packet()", __func__);
		return;
	}
	module_accsreq_answer(self->base, q_id,
	    radius_get_data(respkt), radius_get_length(respkt));
	radius_delete_packet(respkt);
}
