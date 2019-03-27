%{
/*-
 * parser.y
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: parser.y,v 1.5 2003/06/07 21:22:30 max Exp $
 * $FreeBSD$
 */

#include <sys/fcntl.h>
#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "hcsecd.h"

	int	yyparse  (void);
	int	yylex    (void);

static	void	free_key (link_key_p key);
static	int	hexa2int4(char *a);
static	int	hexa2int8(char *a);

extern	int			 yylineno;
static	LIST_HEAD(, link_key)	 link_keys;
	char			*config_file = "/etc/bluetooth/hcsecd.conf";

static	link_key_p		 key = NULL;
%}

%union {
	char	*string;
}

%token <string> T_BDADDRSTRING T_HEXSTRING T_STRING
%token T_DEVICE T_BDADDR T_NAME T_KEY T_PIN T_NOKEY T_NOPIN T_JUNK

%%

config:		line
		| config line
		;

line:		T_DEVICE
			{
			key = (link_key_p) malloc(sizeof(*key));
			if (key == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"config entry");
				exit(1);
			}

			memset(key, 0, sizeof(*key));
			}
		'{' options '}'
			{
			if (get_key(&key->bdaddr, 1) != NULL) {
				syslog(LOG_ERR, "Ignoring duplicated entry " \
						"for bdaddr %s",
						bt_ntoa(&key->bdaddr, NULL));
				free_key(key);
			} else 
				LIST_INSERT_HEAD(&link_keys, key, next);

			key = NULL;
			}
		;

options:	option ';'
		| options option ';'
		;

option:		bdaddr
		| name
		| key
		| pin
		;

bdaddr:		T_BDADDR T_BDADDRSTRING
			{
			if (!bt_aton($2, &key->bdaddr)) {
				syslog(LOG_ERR, "Cound not parse BD_ADDR " \
						"'%s'", $2);
				exit(1);
			}
			}
		;

name:		T_NAME T_STRING
			{
			if (key->name != NULL)
				free(key->name);

			key->name = strdup($2);
			if (key->name == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"device name");
				exit(1);
			}
			}
		;

key:		T_KEY T_HEXSTRING
			{
			int	i, len;

			if (key->key != NULL)
				free(key->key);

			key->key = (uint8_t *) malloc(NG_HCI_KEY_SIZE);
			if (key->key == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"link key");
				exit(1);
			}

			memset(key->key, 0, NG_HCI_KEY_SIZE);

			len = strlen($2) / 2;
			if (len > NG_HCI_KEY_SIZE)
				len = NG_HCI_KEY_SIZE;

			for (i = 0; i < len; i ++)
				key->key[i] = hexa2int8((char *)($2) + 2*i);
			}
		| T_KEY T_NOKEY
			{
			if (key->key != NULL)
				free(key->key);

			key->key = NULL;
			}
		;

pin:		T_PIN T_STRING
			{
			if (key->pin != NULL)
				free(key->pin);

			key->pin = strdup($2);
			if (key->pin == NULL) {
				syslog(LOG_ERR, "Could not allocate new " \
						"PIN code");
				exit(1);
			}
			}
		| T_PIN T_NOPIN
			{
			if (key->pin != NULL)
				free(key->pin);

			key->pin = NULL;
			}
		;

%%

/* Display parser error message */
void
yyerror(char const *message)
{
	syslog(LOG_ERR, "%s in line %d", message, yylineno);
}

/* Re-read config file */
void
read_config_file(void)
{
	extern FILE	*yyin;

	if (config_file == NULL) {
		syslog(LOG_ERR, "Unknown config file name!");
		exit(1);
	}

	if ((yyin = fopen(config_file, "r")) == NULL) {
		syslog(LOG_ERR, "Could not open config file '%s'. %s (%d)",
				config_file, strerror(errno), errno);
		exit(1);
	}

	clean_config();
	if (yyparse() < 0) {
		syslog(LOG_ERR, "Could not parse config file '%s'",config_file);
		exit(1);
	}

	fclose(yyin);
	yyin = NULL;

#if __config_debug__
	dump_config();
#endif
}

/* Clean config */
void
clean_config(void)
{
	link_key_p	key = NULL;

	while ((key = LIST_FIRST(&link_keys)) != NULL) {
		LIST_REMOVE(key, next);
		free_key(key);
	}
}

/* Find link key entry in the list. Return exact or default match */
link_key_p
get_key(bdaddr_p bdaddr, int exact_match)
{
	link_key_p	key = NULL, defkey = NULL;

	LIST_FOREACH(key, &link_keys, next) {
		if (memcmp(bdaddr, &key->bdaddr, sizeof(key->bdaddr)) == 0)
			break;

		if (!exact_match)
			if (memcmp(NG_HCI_BDADDR_ANY, &key->bdaddr,
					sizeof(key->bdaddr)) == 0)
				defkey = key;
	}

	return ((key != NULL)? key : defkey);
}

#if __config_debug__
/* Dump config */
void
dump_config(void)
{
	link_key_p	key = NULL;
	char		buffer[64];

	LIST_FOREACH(key, &link_keys, next) {
		if (key->key != NULL)
			snprintf(buffer, sizeof(buffer),
"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
				key->key[0], key->key[1], key->key[2],
				key->key[3], key->key[4], key->key[5],
				key->key[6], key->key[7], key->key[8],
				key->key[9], key->key[10], key->key[11],
				key->key[12], key->key[13], key->key[14],
				key->key[15]);

		syslog(LOG_DEBUG, 
"device %s " \
"bdaddr %s " \
"pin %s " \
"key %s",
			(key->name != NULL)? key->name : "noname",
			bt_ntoa(&key->bdaddr, NULL),
			(key->pin != NULL)? key->pin : "nopin",
			(key->key != NULL)? buffer : "nokey");
	}
}
#endif

/* Read keys file */
int
read_keys_file(void)
{
	FILE		*f = NULL;
	link_key_t	*key = NULL;
	char		 buf[HCSECD_BUFFER_SIZE], *p = NULL, *cp = NULL;
	bdaddr_t	 bdaddr;
	int		 i, len;

	if ((f = fopen(HCSECD_KEYSFILE, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);

		syslog(LOG_ERR, "Could not open keys file %s. %s (%d)\n",
				HCSECD_KEYSFILE, strerror(errno), errno);

		return (-1);
	}

	while ((p = fgets(buf, sizeof(buf), f)) != NULL) {
		if (*p == '#')
			continue;
		if ((cp = strpbrk(p, " ")) == NULL)
			continue;

		*cp++ = '\0';

		if (!bt_aton(p, &bdaddr))
			continue;

		if ((key = get_key(&bdaddr, 1)) == NULL)
			continue;

		if (key->key == NULL) {
			key->key = (uint8_t *) malloc(NG_HCI_KEY_SIZE);
			if (key->key == NULL) {
				syslog(LOG_ERR, "Could not allocate link key");
				exit(1);
			}
		}

		memset(key->key, 0, NG_HCI_KEY_SIZE);

		len = strlen(cp) / 2;
		if (len > NG_HCI_KEY_SIZE)
			len = NG_HCI_KEY_SIZE;

		for (i = 0; i < len; i ++)
			key->key[i] = hexa2int8(cp + 2*i);

		syslog(LOG_DEBUG, "Restored link key for the entry, " \
				"remote bdaddr %s, name '%s'",
				bt_ntoa(&key->bdaddr, NULL),
				(key->name != NULL)? key->name : "No name");
	}

	fclose(f);

	return (0);
}

/* Dump keys file */
int
dump_keys_file(void)
{
	link_key_p	key = NULL;
	char		tmp[PATH_MAX], buf[HCSECD_BUFFER_SIZE];
	int		f;

	snprintf(tmp, sizeof(tmp), "%s.tmp", HCSECD_KEYSFILE);
	if ((f = open(tmp, O_RDWR|O_CREAT|O_TRUNC|O_EXCL, 0600)) < 0) {
		syslog(LOG_ERR, "Could not create temp keys file %s. %s (%d)\n",
				tmp, strerror(errno), errno);
		return (-1);
	}

	LIST_FOREACH(key, &link_keys, next) {
		if (key->key == NULL)
			continue;

		snprintf(buf, sizeof(buf),
"%s %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			bt_ntoa(&key->bdaddr, NULL),
			key->key[0],  key->key[1],  key->key[2],  key->key[3],
			key->key[4],  key->key[5],  key->key[6],  key->key[7],
			key->key[8],  key->key[9],  key->key[10], key->key[11],
			key->key[12], key->key[13], key->key[14], key->key[15]);

		if (write(f, buf, strlen(buf)) < 0) {
			syslog(LOG_ERR, "Could not write temp keys file. " \
					"%s (%d)\n", strerror(errno), errno);
			break;
		}
	}

	close(f);

	if (rename(tmp, HCSECD_KEYSFILE) < 0) {
		syslog(LOG_ERR, "Could not rename(%s, %s). %s (%d)\n",
				tmp, HCSECD_KEYSFILE, strerror(errno), errno);
		unlink(tmp);
		return (-1);
	}

	return (0);
}

/* Free key entry */
static void
free_key(link_key_p key)
{
	if (key->name != NULL)
		free(key->name);
	if (key->key != NULL)
		free(key->key);
	if (key->pin != NULL)
		free(key->pin);

	memset(key, 0, sizeof(*key));
	free(key);
}

/* Convert hex ASCII to int4 */
static int
hexa2int4(char *a)
{
	if ('0' <= *a && *a <= '9')
		return (*a - '0');

	if ('A' <= *a && *a <= 'F')
		return (*a - 'A' + 0xa);

	if ('a' <= *a && *a <= 'f')
		return (*a - 'a' + 0xa);

	syslog(LOG_ERR, "Invalid hex character: '%c' (%#x)", *a, *a);
	exit(1);
}

/* Convert hex ASCII to int8 */
static int
hexa2int8(char *a)
{
	return ((hexa2int4(a) << 4) | hexa2int4(a + 1));
}

