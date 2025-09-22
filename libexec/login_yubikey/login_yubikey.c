/* $OpenBSD: login_yubikey.c,v 1.17 2025/08/14 19:09:09 tb Exp $ */

/*
 * Copyright (c) 2010 Daniel Hartmeier <daniel@benzedrine.cx>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/unistd.h>
#include <ctype.h>
#include <login_cap.h>
#include <pwd.h>
#include <readpassphrase.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "yubikey.h"

#define	MODE_LOGIN	0
#define	MODE_CHALLENGE	1
#define	MODE_RESPONSE	2

#define	AUTH_OK		0
#define	AUTH_FAILED	-1

static const char *path = "/var/db/yubikey";

static int clean_string(const char *);
static int yubikey_login(const char *, const char *);

int
main(int argc, char *argv[])
{
	int ch, ret, mode = MODE_LOGIN, count;
	FILE *f = NULL;
	char *username, *password = NULL;
	char pbuf[1024];
	char response[1024];

	setpriority(PRIO_PROCESS, 0, 0);

	if (pledge("stdio tty wpath rpath cpath", NULL) == -1) {
		syslog(LOG_AUTH|LOG_ERR, "pledge: %m");
		exit(EXIT_FAILURE);
	}

	openlog(NULL, LOG_ODELAY, LOG_AUTH);

	while ((ch = getopt(argc, argv, "dv:s:")) != -1) {
		switch (ch) {
		case 'd':
			f = stdout;
			break;
		case 'v':
			break;
		case 's':
			if (!strcmp(optarg, "login"))
				mode = MODE_LOGIN;
			else if (!strcmp(optarg, "response"))
				mode = MODE_RESPONSE;
			else if (!strcmp(optarg, "challenge"))
				mode = MODE_CHALLENGE;
			else {
				syslog(LOG_ERR, "%s: invalid service", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		default:
			syslog(LOG_ERR, "usage error1");
			exit(EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2 && argc != 1) {
		syslog(LOG_ERR, "usage error2");
		exit(EXIT_FAILURE);
	}
	username = argv[0];
	/* passed by sshd(8) for non-existing users */
	if (!strcmp(username, "NOUSER"))
		exit(EXIT_FAILURE);
	if (!clean_string(username)) {
		syslog(LOG_ERR, "clean_string username");
		exit(EXIT_FAILURE);
	}

	if (f == NULL && (f = fdopen(3, "r+")) == NULL) {
		syslog(LOG_ERR, "user %s: fdopen: %m", username);
		exit(EXIT_FAILURE);
	}

	switch (mode) {
	case MODE_LOGIN:
		if ((password = readpassphrase("Password:", pbuf, sizeof(pbuf),
			    RPP_ECHO_OFF)) == NULL) {
			syslog(LOG_ERR, "user %s: readpassphrase: %m",
			    username);
			exit(EXIT_FAILURE);
		}
		break;
	case MODE_CHALLENGE:
		/* see login.conf(5) section CHALLENGES */
		fprintf(f, "%s\n", BI_SILENT);
		exit(EXIT_SUCCESS);
		break;
	case MODE_RESPONSE:
		/* see login.conf(5) section RESPONSES */
		/* this happens e.g. when called from sshd(8) */
		mode = 0;
		count = -1;
		while (++count < sizeof(response) &&
		    read(3, &response[count], 1) == 1) {
			if (response[count] == '\0' && ++mode == 2)
				break;
			if (response[count] == '\0' && mode == 1)
				password = response + count + 1;
		}
		if (mode < 2) {
			syslog(LOG_ERR, "user %s: protocol error "
			    "on back channel", username);
			exit(EXIT_FAILURE);
		}
		break;
	}

	ret = yubikey_login(username, password);
	explicit_bzero(password, strlen(password));
	if (ret == AUTH_OK) {
		syslog(LOG_INFO, "user %s: authorize", username);
		fprintf(f, "%s\n", BI_AUTH);
	} else {
		syslog(LOG_INFO, "user %s: reject", username);
		fprintf(f, "%s\n", BI_REJECT);
	}
	closelog();
	return (EXIT_SUCCESS);
}

static int
clean_string(const char *s)
{
	while (*s) {
		if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
			return (0);
		++s;
	}
	return (1);
}

static int
yubikey_login(const char *username, const char *password)
{
	char fn[PATH_MAX];
	char hexkey[33], key[YUBIKEY_KEY_SIZE];
	char hexuid[13], uid[YUBIKEY_UID_SIZE];
	FILE *f;
	yubikey_token_st tok;
	u_int32_t last_ctr = 0, ctr;
	int r, i = 0, mapok = 0, crcok = 0;

	snprintf(fn, sizeof(fn), "%s/%s.uid", path, username);
	if ((f = fopen(fn, "r")) == NULL) {
		syslog(LOG_ERR, "user %s: fopen: %s: %m", username, fn);
		return (AUTH_FAILED);
	}
	if (fscanf(f, "%12s", hexuid) != 1) {
		syslog(LOG_ERR, "user %s: fscanf: %s: %m", username, fn);
		fclose(f);
		return (AUTH_FAILED);
	}
	fclose(f);

	snprintf(fn, sizeof(fn), "%s/%s.key", path, username);
	if ((f = fopen(fn, "r")) == NULL) {
		syslog(LOG_ERR, "user %s: fopen: %s: %m", username, fn);
		return (AUTH_FAILED);
	}
	if (fscanf(f, "%32s", hexkey) != 1) {
		syslog(LOG_ERR, "user %s: fscanf: %s: %m", username, fn);
		fclose(f);
		return (AUTH_FAILED);
	}
	fclose(f);
	if (strlen(hexkey) != 32) {
		syslog(LOG_ERR, "user %s: key len != 32", username);
		return (AUTH_FAILED);
	}

	snprintf(fn, sizeof(fn), "%s/%s.ctr", path, username);
	if ((f = fopen(fn, "r")) != NULL) {
		if (fscanf(f, "%u", &last_ctr) != 1)
			last_ctr = 0;
		fclose(f);
	}

	yubikey_hex_decode(uid, hexuid, YUBIKEY_UID_SIZE);
	yubikey_hex_decode(key, hexkey, YUBIKEY_KEY_SIZE);

	explicit_bzero(hexkey, sizeof(hexkey));

	/*
	 * Cycle through the key mapping table.
         * XXX brute force, unoptimized; a lookup table for valid mappings may
	 * be appropriate.
	 */
	while (1) {
		r = yubikey_parse((uint8_t *)password, (uint8_t *)key, &tok, i++);
		switch (r) {
		case EMSGSIZE:
			syslog(LOG_INFO, "user %s failed: password too short.",
			    username);
			explicit_bzero(key, sizeof(key));
			return (AUTH_FAILED);
		case EINVAL:	/* keyboard mapping invalid */
			continue;
		case 0:		/* found a valid keyboard mapping */
			mapok++;
			if (!yubikey_crc_ok_p((uint8_t *)&tok))
				continue;	/* try another one */
			crcok++;
			syslog(LOG_DEBUG, "user %s: crc ok", username);

			if (memcmp(tok.uid, uid, YUBIKEY_UID_SIZE)) {
				syslog(LOG_DEBUG, "user %s: uid doesn't match",
				    username);
				continue;	/* try another one */
			}
			break; /* uid matches */
		case -1:
			syslog(LOG_INFO, "user %s: could not decode password "
			    "with any keymap (%d crc ok)",
			    username, crcok);
			explicit_bzero(key, sizeof(key));
			return (AUTH_FAILED);
		default:
			syslog(LOG_DEBUG, "user %s failed: %s",
			    username, strerror(r));
			explicit_bzero(key, sizeof(key));
			return (AUTH_FAILED);
		}
		break; /* only reached through the bottom of case 0 */
	}

	explicit_bzero(key, sizeof(key));

	syslog(LOG_INFO, "user %s uid: %d matching keymaps (%d checked), "
	    "%d crc ok", username, mapok, i, crcok);

	ctr = ((u_int32_t)yubikey_counter(tok.ctr) << 8) | tok.use;
	if (ctr <= last_ctr) {
		syslog(LOG_INFO, "user %s: counter <= last (REPLAY ATTACK!)",
		    username);
		return (AUTH_FAILED);
	}
	syslog(LOG_INFO, "user %s: counter > last [OK]", username);
	umask(S_IRWXO);
	if ((f = fopen(fn, "w")) == NULL) {
		syslog(LOG_ERR, "user %s: fopen: %s: %m", username, fn);
		return (AUTH_FAILED);
	}
	fprintf(f, "%u", ctr);
	fclose(f);

	return (AUTH_OK);
}
