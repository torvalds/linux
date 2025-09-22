/*	$OpenBSD: ca.c,v 1.45 2024/11/21 13:21:34 claudio Exp $	*/

/*
 * Copyright (c) 2014 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/uio.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <imsg.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>

#include "relayd.h"

void	 ca_init(struct privsep *, struct privsep_proc *p, void *);
void	 ca_launch(void);

int	 ca_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 ca_dispatch_relay(int, struct privsep_proc *, struct imsg *);

int	 rsae_priv_enc(int, const u_char *, u_char *, RSA *, int);
int	 rsae_priv_dec(int, const u_char *, u_char *, RSA *, int);

static struct relayd *env = NULL;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	ca_dispatch_parent },
	{ "relay",	PROC_RELAY,	ca_dispatch_relay },
};

void
ca(struct privsep *ps, struct privsep_proc *p)
{
	env = ps->ps_env;

	proc_run(ps, p, procs, nitems(procs), ca_init, NULL);
}

void
ca_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	env->sc_id = getpid() & 0xffff;
}

void
hash_x509(X509 *cert, char *hash, size_t hashlen)
{
	static const char	hex[] = "0123456789abcdef";
	size_t			off;
	char			digest[EVP_MAX_MD_SIZE];
	int			dlen, i;

	if (X509_pubkey_digest(cert, EVP_sha256(), digest, &dlen) != 1)
		fatalx("%s: X509_pubkey_digest failed", __func__);

	if (hashlen < 2 * dlen + sizeof("SHA256:"))
		fatalx("%s: hash buffer too small", __func__);

	off = strlcpy(hash, "SHA256:", hashlen);

	for (i = 0; i < dlen; i++) {
		hash[off++] = hex[(digest[i] >> 4) & 0x0f];
		hash[off++] = hex[digest[i] & 0x0f];
	}
	hash[off] = 0;
}

void
ca_launch(void)
{
	char			 hash[TLS_CERT_HASH_SIZE];
	char			*buf;
	BIO			*in = NULL;
	EVP_PKEY		*pkey = NULL;
	struct relay		*rlay;
	struct relay_cert	*cert;
	X509			*x509 = NULL;
	off_t			 len;

	TAILQ_FOREACH(cert, env->sc_certs, cert_entry) {
		if (cert->cert_fd == -1 || cert->cert_key_fd == -1)
			continue;

		if ((buf = relay_load_fd(cert->cert_fd, &len)) == NULL)
			fatal("ca_launch: cert relay_load_fd");

		if ((in = BIO_new_mem_buf(buf, len)) == NULL)
			fatalx("ca_launch: cert BIO_new_mem_buf");

		if ((x509 = PEM_read_bio_X509(in, NULL,
		    NULL, NULL)) == NULL)
			fatalx("ca_launch: cert PEM_read_bio_X509");

		hash_x509(x509, hash, sizeof(hash));

		BIO_free(in);
		X509_free(x509);
		purge_key(&buf, len);

		if ((buf = relay_load_fd(cert->cert_key_fd, &len)) == NULL)
			fatal("ca_launch: key relay_load_fd");

		if ((in = BIO_new_mem_buf(buf, len)) == NULL)
			fatalx("%s: key", __func__);

		if ((pkey = PEM_read_bio_PrivateKey(in,
		    NULL, NULL, NULL)) == NULL)
			fatalx("%s: PEM", __func__);

		cert->cert_pkey = pkey;

		if (pkey_add(env, pkey, hash) == NULL)
			fatalx("tls pkey");

		BIO_free(in);
		purge_key(&buf, len);
	}

	TAILQ_FOREACH(rlay, env->sc_relays, rl_entry) {
		if ((rlay->rl_conf.flags & (F_TLS|F_TLSCLIENT)) == 0)
			continue;

		if (rlay->rl_tls_cacert_fd != -1 &&
		    rlay->rl_conf.tls_cakey_len) {
			if ((buf = relay_load_fd(rlay->rl_tls_cacert_fd,
			    &len)) == NULL)
				fatal("ca_launch: cacert relay_load_fd");

			if ((in = BIO_new_mem_buf(buf, len)) == NULL)
				fatalx("ca_launch: cacert BIO_new_mem_buf");

			if ((x509 = PEM_read_bio_X509(in, NULL,
			    NULL, NULL)) == NULL)
				fatalx("ca_launch: cacert PEM_read_bio_X509");

			hash_x509(x509, hash, sizeof(hash));

			BIO_free(in);
			X509_free(x509);
			purge_key(&buf, len);

			if ((in = BIO_new_mem_buf(rlay->rl_tls_cakey,
			    rlay->rl_conf.tls_cakey_len)) == NULL)
				fatalx("%s: key", __func__);

			if ((pkey = PEM_read_bio_PrivateKey(in,
			    NULL, NULL, NULL)) == NULL)
				fatalx("%s: PEM", __func__);
			BIO_free(in);

			rlay->rl_tls_capkey = pkey;

			if (pkey_add(env, pkey, hash) == NULL)
				fatalx("ca pkey");

			purge_key(&rlay->rl_tls_cakey,
			    rlay->rl_conf.tls_cakey_len);
		}
		close(rlay->rl_tls_ca_fd);
	}
}

int
ca_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CFG_RELAY:
		config_getrelay(env, imsg);
		break;
	case IMSG_CFG_RELAY_FD:
		config_getrelayfd(env, imsg);
		break;
	case IMSG_CFG_DONE:
		config_getcfg(env, imsg);
		break;
	case IMSG_CTL_START:
		ca_launch();
		break;
	case IMSG_CTL_RESET:
		config_getreset(env, imsg);
		break;
	default:
		return -1;
	}

	return 0;
}

int
ca_dispatch_relay(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct ctl_keyop	 cko;
	EVP_PKEY		*pkey;
	RSA			*rsa;
	u_char			*from = NULL, *to = NULL;
	struct iovec		 iov[2];
	int			 c = 0;

	switch (imsg->hdr.type) {
	case IMSG_CA_PRIVENC:
	case IMSG_CA_PRIVDEC:
		IMSG_SIZE_CHECK(imsg, (&cko));
		bcopy(imsg->data, &cko, sizeof(cko));
		if (cko.cko_proc > env->sc_conf.prefork_relay)
			fatalx("%s: invalid relay proc", __func__);
		if (IMSG_DATA_SIZE(imsg) != (sizeof(cko) + cko.cko_flen))
			fatalx("%s: invalid key operation", __func__);
		if ((pkey = pkey_find(env, cko.cko_hash)) == NULL)
			fatalx("%s: invalid relay hash '%s'",
			    __func__, cko.cko_hash);
		if ((rsa = EVP_PKEY_get1_RSA(pkey)) == NULL)
			fatalx("%s: invalid relay key", __func__);

		DPRINTF("%s:%d: key hash %s proc %d",
		    __func__, __LINE__, cko.cko_hash, cko.cko_proc);

		from = (u_char *)imsg->data + sizeof(cko);
		if ((to = calloc(1, cko.cko_tlen)) == NULL)
			fatalx("%s: calloc", __func__);

		switch (imsg->hdr.type) {
		case IMSG_CA_PRIVENC:
			cko.cko_tlen = RSA_private_encrypt(cko.cko_flen,
			    from, to, rsa, cko.cko_padding);
			break;
		case IMSG_CA_PRIVDEC:
			cko.cko_tlen = RSA_private_decrypt(cko.cko_flen,
			    from, to, rsa, cko.cko_padding);
			break;
		}

		if (cko.cko_tlen == -1) {
			char buf[256];
			log_warnx("%s: %s", __func__,
			    ERR_error_string(ERR_get_error(), buf));
		}

		iov[c].iov_base = &cko;
		iov[c++].iov_len = sizeof(cko);
		if (cko.cko_tlen > 0) {
			iov[c].iov_base = to;
			iov[c++].iov_len = cko.cko_tlen;
		}

		if (proc_composev_imsg(env->sc_ps, PROC_RELAY, cko.cko_proc,
		    imsg->hdr.type, -1, -1, iov, c) == -1)
			log_warn("%s: proc_composev_imsg", __func__);

		free(to);
		RSA_free(rsa);
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * RSA privsep engine (called from unprivileged processes)
 */

static const RSA_METHOD *rsa_default;
static RSA_METHOD *rsae_method;

static int
rsae_send_imsg(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding, u_int cmd)
{
	struct privsep	*ps = env->sc_ps;
	struct pollfd	 pfd[1];
	struct ctl_keyop cko;
	int		 ret = 0;
	char		*hash;
	struct iovec	 iov[2];
	struct imsgbuf	*ibuf;
	struct imsgev	*iev;
	struct imsg	 imsg;
	int		 n, done = 0, cnt = 0;
	u_char		*toptr;
	static u_int	 seq = 0;

	if ((hash = RSA_get_ex_data(rsa, 0)) == NULL)
		return 0;

	iev = proc_iev(ps, PROC_CA, ps->ps_instance);
	ibuf = &iev->ibuf;

	/*
	 * XXX this could be nicer...
	 */

	(void)strlcpy(cko.cko_hash, hash, sizeof(cko.cko_hash));
	cko.cko_proc = ps->ps_instance;
	cko.cko_flen = flen;
	cko.cko_tlen = RSA_size(rsa);
	cko.cko_padding = padding;
	cko.cko_cookie = seq++;

	iov[cnt].iov_base = &cko;
	iov[cnt++].iov_len = sizeof(cko);
	iov[cnt].iov_base = (void *)(uintptr_t)from;
	iov[cnt++].iov_len = flen;

	/*
	 * Send a synchronous imsg because we cannot defer the RSA
	 * operation in OpenSSL's engine layer.
	 */
	if (imsg_composev(ibuf, cmd, 0, 0, -1, iov, cnt) == -1)
		log_warn("%s: imsg_composev", __func__);
	if (imsgbuf_flush(ibuf) == -1)
		log_warn("%s: imsgbuf_flush", __func__);

	pfd[0].fd = ibuf->fd;
	pfd[0].events = POLLIN;
	while (!done) {
		switch (poll(pfd, 1, RELAY_TLS_PRIV_TIMEOUT)) {
		case -1:
			if (errno != EINTR)
				fatal("%s: poll", __func__);
			continue;
		case 0:
			log_warnx("%s: priv%s poll timeout, keyop #%x",
			    __func__,
			    cmd == IMSG_CA_PRIVENC ? "enc" : "dec",
			    cko.cko_cookie);
			return -1;
		default:
			break;
		}
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatalx("imsgbuf_read");
		if (n == 0)
			fatalx("pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				fatalx("imsg_get error");
			if (n == 0)
				break;

			IMSG_SIZE_CHECK(&imsg, (&cko));
			memcpy(&cko, imsg.data, sizeof(cko));

			/*
			 * Due to earlier timed out requests, there may be
			 * responses that need to be skipped.
			 */
			if (cko.cko_cookie != seq - 1) {
				log_warnx(
				    "%s: priv%s obsolete keyop #%x", __func__,
				    cmd == IMSG_CA_PRIVENC ? "enc" : "dec",
				    cko.cko_cookie);
				continue;
			}

			if (imsg.hdr.type != cmd)
				fatalx("invalid response");

			ret = cko.cko_tlen;
			if (ret > 0) {
				if (IMSG_DATA_SIZE(&imsg) !=
				    (sizeof(cko) + ret))
					fatalx("data size");
				toptr = (u_char *)imsg.data + sizeof(cko);
				memcpy(to, toptr, ret);
			}
			done = 1;

			imsg_free(&imsg);
		}
	}
	imsg_event_add(iev);

	return ret;
}

int
rsae_priv_enc(int flen, const u_char *from, u_char *to, RSA *rsa, int padding)
{
	DPRINTF("%s:%d", __func__, __LINE__);
	return rsae_send_imsg(flen, from, to, rsa, padding, IMSG_CA_PRIVENC);
}

int
rsae_priv_dec(int flen, const u_char *from, u_char *to, RSA *rsa, int padding)
{
	DPRINTF("%s:%d", __func__, __LINE__);
	return rsae_send_imsg(flen, from, to, rsa, padding, IMSG_CA_PRIVDEC);
}

void
ca_engine_init(struct relayd *x_env)
{
	const char	*errstr;

	if (env == NULL)
		env = x_env;

	if (rsa_default != NULL)
		return;

	if ((rsa_default = RSA_get_default_method()) == NULL) {
		errstr = "RSA_get_default_method";
		goto fail;
	}

	if ((rsae_method = RSA_meth_dup(rsa_default)) == NULL) {
		errstr = "RSA_meth_dup";
		goto fail;
	}

	RSA_meth_set_priv_enc(rsae_method, rsae_priv_enc);
	RSA_meth_set_priv_dec(rsae_method, rsae_priv_dec);

	RSA_meth_set_flags(rsae_method,
	    RSA_meth_get_flags(rsa_default) | RSA_METHOD_FLAG_NO_CHECK);
	RSA_meth_set0_app_data(rsae_method,
	    RSA_meth_get0_app_data(rsa_default));

	RSA_set_default_method(rsae_method);

	return;

 fail:
	RSA_meth_free(rsae_method);
	fatalx("%s: %s", __func__, errstr);
}
