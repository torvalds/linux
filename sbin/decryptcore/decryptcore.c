/*-
 * Copyright (c) 2016 Konrad Witaszczyk <def@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/endian.h>
#include <sys/kerneldump.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <ctype.h>
#include <capsicum_helpers.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>

#include "pjdlog.h"

#define	DECRYPTCORE_CRASHDIR	"/var/crash"

static void
usage(void)
{

	pjdlog_exitx(1,
	    "usage: decryptcore [-fLv] -p privatekeyfile -k keyfile -e encryptedcore -c core\n"
	    "       decryptcore [-fLv] [-d crashdir] -p privatekeyfile -n dumpnr");
}

static int
wait_for_process(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, WUNTRACED | WEXITED) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to wait for a child process");
		return (1);
	}

	if (WIFEXITED(status))
		return (WEXITSTATUS(status));

	return (1);
}

static struct kerneldumpkey *
read_key(int kfd)
{
	struct kerneldumpkey *kdk;
	ssize_t size;
	size_t kdksize;

	PJDLOG_ASSERT(kfd >= 0);

	kdksize = sizeof(*kdk);
	kdk = calloc(1, kdksize);
	if (kdk == NULL) {
		pjdlog_errno(LOG_ERR, "Unable to allocate kernel dump key");
		goto failed;
	}

	size = read(kfd, kdk, kdksize);
	if (size == (ssize_t)kdksize) {
		kdk->kdk_encryptedkeysize = dtoh32(kdk->kdk_encryptedkeysize);
		kdksize += (size_t)kdk->kdk_encryptedkeysize;
		kdk = realloc(kdk, kdksize);
		if (kdk == NULL) {
			pjdlog_errno(LOG_ERR, "Unable to reallocate kernel dump key");
			goto failed;
		}
		size += read(kfd, &kdk->kdk_encryptedkey,
		    kdk->kdk_encryptedkeysize);
	}
	if (size != (ssize_t)kdksize) {
		pjdlog_errno(LOG_ERR, "Unable to read key");
		goto failed;
	}

	return (kdk);
failed:
	free(kdk);
	return (NULL);
}

static bool
decrypt(int ofd, const char *privkeyfile, const char *keyfile,
    const char *input)
{
	uint8_t buf[KERNELDUMP_BUFFER_SIZE], key[KERNELDUMP_KEY_MAX_SIZE];
	EVP_CIPHER_CTX *ctx;
	const EVP_CIPHER *cipher;
	FILE *fp;
	struct kerneldumpkey *kdk;
	RSA *privkey;
	int ifd, kfd, olen, privkeysize;
	ssize_t bytes;
	pid_t pid;

	PJDLOG_ASSERT(ofd >= 0);
	PJDLOG_ASSERT(privkeyfile != NULL);
	PJDLOG_ASSERT(keyfile != NULL);
	PJDLOG_ASSERT(input != NULL);

	ctx = NULL;
	privkey = NULL;

	/*
	 * Decrypt a core dump in a child process so we can unlink a partially
	 * decrypted core if the child process fails.
	 */
	pid = fork();
	if (pid == -1) {
		pjdlog_errno(LOG_ERR, "Unable to create child process");
		close(ofd);
		return (false);
	}

	if (pid > 0) {
		close(ofd);
		return (wait_for_process(pid) == 0);
	}

	kfd = open(keyfile, O_RDONLY);
	if (kfd == -1) {
		pjdlog_errno(LOG_ERR, "Unable to open %s", keyfile);
		goto failed;
	}
	ifd = open(input, O_RDONLY);
	if (ifd == -1) {
		pjdlog_errno(LOG_ERR, "Unable to open %s", input);
		goto failed;
	}
	fp = fopen(privkeyfile, "r");
	if (fp == NULL) {
		pjdlog_errno(LOG_ERR, "Unable to open %s", privkeyfile);
		goto failed;
	}

	if (caph_enter() < 0) {
		pjdlog_errno(LOG_ERR, "Unable to enter capability mode");
		goto failed;
	}

	privkey = RSA_new();
	if (privkey == NULL) {
		pjdlog_error("Unable to allocate an RSA structure: %s",
		    ERR_error_string(ERR_get_error(), NULL));
		goto failed;
	}
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		goto failed;

	kdk = read_key(kfd);
	close(kfd);
	if (kdk == NULL)
		goto failed;

	privkey = PEM_read_RSAPrivateKey(fp, &privkey, NULL, NULL);
	fclose(fp);
	if (privkey == NULL) {
		pjdlog_error("Unable to read data from %s.", privkeyfile);
		goto failed;
	}

	privkeysize = RSA_size(privkey);
	if (privkeysize != (int)kdk->kdk_encryptedkeysize) {
		pjdlog_error("RSA modulus size mismatch: equals %db and should be %ub.",
		    8 * privkeysize, 8 * kdk->kdk_encryptedkeysize);
		goto failed;
	}

	switch (kdk->kdk_encryption) {
	case KERNELDUMP_ENC_AES_256_CBC:
		cipher = EVP_aes_256_cbc();
		break;
	default:
		pjdlog_error("Invalid encryption algorithm.");
		goto failed;
	}

	if (RSA_private_decrypt(kdk->kdk_encryptedkeysize,
	    kdk->kdk_encryptedkey, key, privkey,
	    RSA_PKCS1_PADDING) != sizeof(key)) {
		pjdlog_error("Unable to decrypt key: %s",
		    ERR_error_string(ERR_get_error(), NULL));
		goto failed;
	}
	RSA_free(privkey);
	privkey = NULL;

	EVP_DecryptInit_ex(ctx, cipher, NULL, key, kdk->kdk_iv);
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	explicit_bzero(key, sizeof(key));

	do {
		bytes = read(ifd, buf, sizeof(buf));
		if (bytes < 0) {
			pjdlog_errno(LOG_ERR, "Unable to read data from %s",
			    input);
			goto failed;
		}

		if (bytes > 0) {
			if (EVP_DecryptUpdate(ctx, buf, &olen, buf,
			    bytes) == 0) {
				pjdlog_error("Unable to decrypt core.");
				goto failed;
			}
		} else {
			if (EVP_DecryptFinal_ex(ctx, buf, &olen) == 0) {
				pjdlog_error("Unable to decrypt core.");
				goto failed;
			}
		}

		if (olen > 0 && write(ofd, buf, olen) != olen) {
			pjdlog_errno(LOG_ERR, "Unable to write core");
			goto failed;
		}
	} while (bytes > 0);

	explicit_bzero(buf, sizeof(buf));
	EVP_CIPHER_CTX_free(ctx);
	exit(0);
failed:
	explicit_bzero(key, sizeof(key));
	explicit_bzero(buf, sizeof(buf));
	RSA_free(privkey);
	if (ctx != NULL)
		EVP_CIPHER_CTX_free(ctx);
	exit(1);
}

int
main(int argc, char **argv)
{
	char core[PATH_MAX], encryptedcore[PATH_MAX], keyfile[PATH_MAX];
	const char *crashdir, *dumpnr, *privatekey;
	int ch, debug, error, ofd;
	size_t ii;
	bool force, usesyslog;

	error = 1;

	pjdlog_init(PJDLOG_MODE_STD);
	pjdlog_prefix_set("(decryptcore) ");

	debug = 0;
	*core = '\0';
	crashdir = NULL;
	dumpnr = NULL;
	*encryptedcore = '\0';
	force = false;
	*keyfile = '\0';
	privatekey = NULL;
	usesyslog = false;
	while ((ch = getopt(argc, argv, "Lc:d:e:fk:n:p:v")) != -1) {
		switch (ch) {
		case 'L':
			usesyslog = true;
			break;
		case 'c':
			if (strlcpy(core, optarg, sizeof(core)) >= sizeof(core))
				pjdlog_exitx(1, "Core file path is too long.");
			break;
		case 'd':
			crashdir = optarg;
			break;
		case 'e':
			if (strlcpy(encryptedcore, optarg,
			    sizeof(encryptedcore)) >= sizeof(encryptedcore)) {
				pjdlog_exitx(1, "Encrypted core file path is too long.");
			}
			break;
		case 'f':
			force = true;
			break;
		case 'k':
			if (strlcpy(keyfile, optarg, sizeof(keyfile)) >=
			    sizeof(keyfile)) {
				pjdlog_exitx(1, "Key file path is too long.");
			}
			break;
		case 'n':
			dumpnr = optarg;
			break;
		case 'p':
			privatekey = optarg;
			break;
		case 'v':
			debug++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	/* Verify mutually exclusive options. */
	if ((crashdir != NULL || dumpnr != NULL) &&
	    (*keyfile != '\0' || *encryptedcore != '\0' || *core != '\0')) {
		usage();
	}

	/*
	 * Set key, encryptedcore and core file names using crashdir and dumpnr.
	 */
	if (dumpnr != NULL) {
		for (ii = 0; ii < strnlen(dumpnr, PATH_MAX); ii++) {
			if (isdigit((int)dumpnr[ii]) == 0)
				usage();
		}

		if (crashdir == NULL)
			crashdir = DECRYPTCORE_CRASHDIR;
		PJDLOG_VERIFY(snprintf(keyfile, sizeof(keyfile),
		    "%s/key.%s", crashdir, dumpnr) > 0);
		PJDLOG_VERIFY(snprintf(core, sizeof(core),
		    "%s/vmcore.%s", crashdir, dumpnr) > 0);
		PJDLOG_VERIFY(snprintf(encryptedcore, sizeof(encryptedcore),
		    "%s/vmcore_encrypted.%s", crashdir, dumpnr) > 0);
	}

	if (privatekey == NULL || *keyfile == '\0' || *encryptedcore == '\0' ||
	    *core == '\0') {
		usage();
	}

	if (usesyslog)
		pjdlog_mode_set(PJDLOG_MODE_SYSLOG);
	pjdlog_debug_set(debug);

	if (force && unlink(core) == -1 && errno != ENOENT) {
		pjdlog_errno(LOG_ERR, "Unable to remove old core");
		goto out;
	}
	ofd = open(core, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (ofd == -1) {
		pjdlog_errno(LOG_ERR, "Unable to open %s", core);
		goto out;
	}

	if (!decrypt(ofd, privatekey, keyfile, encryptedcore)) {
		if (unlink(core) == -1 && errno != ENOENT)
			pjdlog_errno(LOG_ERR, "Unable to remove core");
		goto out;
	}

	error = 0;
out:
	pjdlog_fini();
	exit(error);
}
