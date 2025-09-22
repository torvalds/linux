/*	$OpenBSD: ca.c,v 1.49 2024/11/21 13:22:21 claudio Exp $	*/

/*
 * Copyright (c) 2014 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

#include <openssl/err.h>
#include <openssl/pem.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"
#include "ssl.h"

static int	 rsae_send_imsg(int, const unsigned char *, unsigned char *,
		    RSA *, int, unsigned int);
static int	 rsae_priv_enc(int, const unsigned char *, unsigned char *,
		    RSA *, int);
static int	 rsae_priv_dec(int, const unsigned char *, unsigned char *,
		    RSA *, int);
static ECDSA_SIG *ecdsae_do_sign(const unsigned char *, int, const BIGNUM *,
    const BIGNUM *, EC_KEY *);

static struct dict pkeys;
static uint64_t	 reqid = 0;

static void
ca_shutdown(void)
{
	log_debug("debug: ca agent exiting");
	_exit(0);
}

int
ca(void)
{
	struct passwd	*pw;

	purge_config(PURGE_LISTENERS|PURGE_TABLES|PURGE_RULES|PURGE_DISPATCHERS);

	if ((pw = getpwnam(SMTPD_USER)) == NULL)
		fatalx("unknown user " SMTPD_USER);

	if (chroot(PATH_CHROOT) == -1)
		fatal("ca: chroot");
	if (chdir("/") == -1)
		fatal("ca: chdir(\"/\")");

	config_process(PROC_CA);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("ca: cannot drop privileges");

	imsg_callback = ca_imsg;
	event_init();

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	config_peer(PROC_CONTROL);
	config_peer(PROC_PARENT);
	config_peer(PROC_DISPATCHER);

	/* Ignore them until we get our config */
	mproc_disable(p_dispatcher);

	if (pledge("stdio", NULL) == -1)
		fatal("pledge");

	event_dispatch();
	fatalx("exited event loop");

	return (0);
}

void
ca_init(void)
{
	BIO		*in = NULL;
	EVP_PKEY	*pkey = NULL;
	struct pki	*pki;
	const char	*k;
	void		*iter_dict;
	char		*hash;

	log_debug("debug: init private ssl-tree");
	dict_init(&pkeys);
	iter_dict = NULL;
	while (dict_iter(env->sc_pki_dict, &iter_dict, &k, (void **)&pki)) {
		if (pki->pki_key == NULL)
			continue;

		in = BIO_new_mem_buf(pki->pki_key, pki->pki_key_len);
		if (in == NULL)
			fatalx("ca_init: key");
		pkey = PEM_read_bio_PrivateKey(in, NULL, NULL, NULL);
		if (pkey == NULL)
			fatalx("ca_init: PEM");
		BIO_free(in);

		hash = ssl_pubkey_hash(pki->pki_cert, pki->pki_cert_len);
		if (dict_check(&pkeys, hash))
			EVP_PKEY_free(pkey);
		else
			dict_xset(&pkeys, hash, pkey);
		free(hash);
	}
}

int
ca_X509_verify(void *certificate, void *chain, const char *CAfile,
    const char *CRLfile, const char **errstr)
{
	X509_STORE     *store = NULL;
	X509_STORE_CTX *xsc = NULL;
	int		ret = 0;
	long		error = 0;

	if ((store = X509_STORE_new()) == NULL)
		goto end;

	if (!X509_STORE_load_locations(store, CAfile, NULL)) {
		log_warn("warn: unable to load CA file %s", CAfile);
		goto end;
	}
	X509_STORE_set_default_paths(store);

	if ((xsc = X509_STORE_CTX_new()) == NULL)
		goto end;

	if (X509_STORE_CTX_init(xsc, store, certificate, chain) != 1)
		goto end;

	ret = X509_verify_cert(xsc);

end:
	*errstr = NULL;
	if (ret != 1) {
		if (xsc) {
			error = X509_STORE_CTX_get_error(xsc);
			*errstr = X509_verify_cert_error_string(error);
		}
		else if (ERR_peek_last_error())
			*errstr = ERR_error_string(ERR_peek_last_error(), NULL);
	}

	X509_STORE_CTX_free(xsc);
	X509_STORE_free(store);

	return ret > 0 ? 1 : 0;
}

void
ca_imsg(struct mproc *p, struct imsg *imsg)
{
	EVP_PKEY		*pkey;
	RSA			*rsa = NULL;
	EC_KEY			*ecdsa = NULL;
	const void		*from = NULL;
	unsigned char		*to = NULL;
	struct msg		 m;
	const char		*hash;
	size_t			 flen, tlen, padding;
	int			 buf_len;
	int			 ret = 0;
	uint64_t		 id;
	int			 v;

	if (imsg == NULL)
		ca_shutdown();

	switch (imsg->hdr.type) {
	case IMSG_CONF_START:
		return;
	case IMSG_CONF_END:
		ca_init();

		/* Start fulfilling requests */
		mproc_enable(p_dispatcher);
		return;

	case IMSG_CTL_VERBOSE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		log_trace_verbose(v);
		return;

	case IMSG_CTL_PROFILE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		profiling = v;
		return;

	case IMSG_CA_RSA_PRIVENC:
	case IMSG_CA_RSA_PRIVDEC:
		m_msg(&m, imsg);
		m_get_id(&m, &id);
		m_get_string(&m, &hash);
		m_get_data(&m, &from, &flen);
		m_get_size(&m, &tlen);
		m_get_size(&m, &padding);
		m_end(&m);

		pkey = dict_get(&pkeys, hash);
		if (pkey == NULL || (rsa = EVP_PKEY_get1_RSA(pkey)) == NULL)
			fatalx("ca_imsg: invalid pkey hash");

		if ((to = calloc(1, tlen)) == NULL)
			fatalx("ca_imsg: calloc");

		switch (imsg->hdr.type) {
		case IMSG_CA_RSA_PRIVENC:
			ret = RSA_private_encrypt(flen, from, to, rsa,
			    padding);
			break;
		case IMSG_CA_RSA_PRIVDEC:
			ret = RSA_private_decrypt(flen, from, to, rsa,
			    padding);
			break;
		}

		m_create(p, imsg->hdr.type, 0, 0, -1);
		m_add_id(p, id);
		m_add_int(p, ret);
		if (ret > 0)
			m_add_data(p, to, (size_t)ret);
		m_close(p);

		free(to);
		RSA_free(rsa);
		return;

	case IMSG_CA_ECDSA_SIGN:
		m_msg(&m, imsg);
		m_get_id(&m, &id);
		m_get_string(&m, &hash);
		m_get_data(&m, &from, &flen);
		m_end(&m);

		pkey = dict_get(&pkeys, hash);
		if (pkey == NULL ||
		    (ecdsa = EVP_PKEY_get1_EC_KEY(pkey)) == NULL)
			fatalx("ca_imsg: invalid pkey hash");

		buf_len = ECDSA_size(ecdsa);
		if ((to = calloc(1, buf_len)) == NULL)
			fatalx("ca_imsg: calloc");
		ret = ECDSA_sign(0, from, flen, to, &buf_len, ecdsa);
		m_create(p, imsg->hdr.type, 0, 0, -1);
		m_add_id(p, id);
		m_add_int(p, ret);
		if (ret > 0)
			m_add_data(p, to, (size_t)buf_len);
		m_close(p);
		free(to);
		EC_KEY_free(ecdsa);
		return;
	}

	fatalx("ca_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

/*
 * RSA privsep engine (called from unprivileged processes)
 */

const RSA_METHOD *rsa_default = NULL;

static RSA_METHOD *rsae_method = NULL;

static int
rsae_send_imsg(int flen, const unsigned char *from, unsigned char *to,
    RSA *rsa, int padding, unsigned int cmd)
{
	int		 ret = 0;
	struct imsgbuf	*ibuf;
	struct imsg	 imsg;
	int		 n, done = 0;
	const void	*toptr;
	char		*hash;
	size_t		 tlen;
	struct msg	 m;
	uint64_t	 id;

	if ((hash = RSA_get_ex_data(rsa, 0)) == NULL)
		return (0);

	/*
	 * Send a synchronous imsg because we cannot defer the RSA
	 * operation in OpenSSL's engine layer.
	 */
	m_create(p_ca, cmd, 0, 0, -1);
	reqid++;
	m_add_id(p_ca, reqid);
	m_add_string(p_ca, hash);
	m_add_data(p_ca, (const void *)from, (size_t)flen);
	m_add_size(p_ca, (size_t)RSA_size(rsa));
	m_add_size(p_ca, (size_t)padding);
	m_flush(p_ca);

	ibuf = &p_ca->imsgbuf;

	while (!done) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatalx("imsgbuf_read");
		if (n == 0)
			fatalx("pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				fatalx("imsg_get error");
			if (n == 0)
				break;

			log_imsg(PROC_DISPATCHER, PROC_CA, &imsg);

			switch (imsg.hdr.type) {
			case IMSG_CA_RSA_PRIVENC:
			case IMSG_CA_RSA_PRIVDEC:
				break;
			default:
				/* Another imsg is queued up in the buffer */
				dispatcher_imsg(p_ca, &imsg);
				imsg_free(&imsg);
				continue;
			}

			m_msg(&m, &imsg);
			m_get_id(&m, &id);
			if (id != reqid)
				fatalx("invalid response id");
			m_get_int(&m, &ret);
			if (ret > 0)
				m_get_data(&m, &toptr, &tlen);
			m_end(&m);

			if (ret > 0)
				memcpy(to, toptr, tlen);
			done = 1;

			imsg_free(&imsg);
		}
	}
	mproc_event_add(p_ca);

	return (ret);
}

static int
rsae_priv_enc(int flen, const unsigned char *from, unsigned char *to, RSA *rsa,
    int padding)
{
	log_debug("debug: %s: %s", proc_name(smtpd_process), __func__);
	if (RSA_get_ex_data(rsa, 0) != NULL)
		return (rsae_send_imsg(flen, from, to, rsa, padding,
		    IMSG_CA_RSA_PRIVENC));
	return (RSA_meth_get_priv_enc(rsa_default)(flen, from, to, rsa, padding));
}

static int
rsae_priv_dec(int flen, const unsigned char *from, unsigned char *to, RSA *rsa,
    int padding)
{
	log_debug("debug: %s: %s", proc_name(smtpd_process), __func__);
	if (RSA_get_ex_data(rsa, 0) != NULL)
		return (rsae_send_imsg(flen, from, to, rsa, padding,
		    IMSG_CA_RSA_PRIVDEC));

	return (RSA_meth_get_priv_dec(rsa_default)(flen, from, to, rsa, padding));
}

/*
 * ECDSA privsep engine (called from unprivileged processes)
 */

const EC_KEY_METHOD *ecdsa_default = NULL;

static EC_KEY_METHOD *ecdsae_method = NULL;

static ECDSA_SIG *
ecdsae_send_enc_imsg(const unsigned char *dgst, int dgst_len,
    const BIGNUM *inv, const BIGNUM *rp, EC_KEY *eckey)
{
	int		 ret = 0;
	struct imsgbuf	*ibuf;
	struct imsg	 imsg;
	int		 n, done = 0;
	const void	*toptr;
	char		*hash;
	size_t		 tlen;
	struct msg	 m;
	uint64_t	 id;
	ECDSA_SIG	*sig = NULL;

	if ((hash = EC_KEY_get_ex_data(eckey, 0)) == NULL)
		return (0);

	/*
	 * Send a synchronous imsg because we cannot defer the ECDSA
	 * operation in OpenSSL's engine layer.
	 */
	m_create(p_ca, IMSG_CA_ECDSA_SIGN, 0, 0, -1);
	reqid++;
	m_add_id(p_ca, reqid);
	m_add_string(p_ca, hash);
	m_add_data(p_ca, (const void *)dgst, (size_t)dgst_len);
	m_flush(p_ca);

	ibuf = &p_ca->imsgbuf;

	while (!done) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatalx("imsgbuf_read");
		if (n == 0)
			fatalx("pipe closed");
		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				fatalx("imsg_get error");
			if (n == 0)
				break;

			log_imsg(PROC_DISPATCHER, PROC_CA, &imsg);

			switch (imsg.hdr.type) {
			case IMSG_CA_ECDSA_SIGN:
				break;
			default:
				/* Another imsg is queued up in the buffer */
				dispatcher_imsg(p_ca, &imsg);
				imsg_free(&imsg);
				continue;
			}

			m_msg(&m, &imsg);
			m_get_id(&m, &id);
			if (id != reqid)
				fatalx("invalid response id");
			m_get_int(&m, &ret);
			if (ret > 0)
				m_get_data(&m, &toptr, &tlen);
			m_end(&m);
			done = 1;

			if (ret > 0)
				d2i_ECDSA_SIG(&sig, (const unsigned char **)&toptr, tlen);
			imsg_free(&imsg);
		}
	}
	mproc_event_add(p_ca);

	return (sig);
}

static ECDSA_SIG *
ecdsae_do_sign(const unsigned char *dgst, int dgst_len, const BIGNUM *inv,
    const BIGNUM *rp, EC_KEY *eckey)
{
	ECDSA_SIG *(*psign_sig)(const unsigned char *, int, const BIGNUM *,
	    const BIGNUM *, EC_KEY *);

	log_debug("debug: %s: %s", proc_name(smtpd_process), __func__);
	if (EC_KEY_get_ex_data(eckey, 0) != NULL)
		return (ecdsae_send_enc_imsg(dgst, dgst_len, inv, rp, eckey));
	EC_KEY_METHOD_get_sign(ecdsa_default, NULL, NULL, &psign_sig);
	return (psign_sig(dgst, dgst_len, inv, rp, eckey));
}

static void
rsa_engine_init(void)
{
	const char	*errstr;

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
	ssl_error(errstr);
	fatalx("%s", errstr);
}

static void
ecdsa_engine_init(void)
{
	int (*sign)(int, const unsigned char *, int, unsigned char *,
	    unsigned int *, const BIGNUM *, const BIGNUM *, EC_KEY *);
	int (*sign_setup)(EC_KEY *, BN_CTX *, BIGNUM **, BIGNUM **);
	const char *errstr;

	if ((ecdsa_default = EC_KEY_get_default_method()) == NULL) {
		errstr = "EC_KEY_get_default_method";
		goto fail;
	}

	if ((ecdsae_method = EC_KEY_METHOD_new(ecdsa_default)) == NULL) {
		errstr = "EC_KEY_METHOD_new";
		goto fail;
	}

	EC_KEY_METHOD_get_sign(ecdsa_default, &sign, &sign_setup, NULL);
	EC_KEY_METHOD_set_sign(ecdsae_method, sign, sign_setup,
	    ecdsae_do_sign);

	EC_KEY_set_default_method(ecdsae_method);

	return;

 fail:
	ssl_error(errstr);
	fatalx("%s", errstr);
}

void
ca_engine_init(void)
{
	rsa_engine_init();
	ecdsa_engine_init();
}
