/*	$OpenBSD: cmd.h,v 1.19 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#define CMD_BUFF_SIZE		133
#define BOOTDEVLEN		1024

struct cmd_table {
	char *cmd_name;
	char cmd_type;
#define CMDT_CMD 0
#define CMDT_VAR 1
#define CMDT_SET 2
#define CMDT_MDC 3
	int (*cmd_exec)(void);
};

struct cmd_state {
	char bootdev[BOOTDEVLEN]; /* device */
	char image[MAXPATHLEN - 16]; /* image */
	int  boothowto; /* howto */
	char *conf; /* /etc/boot.conf normally */
	int timeout;

	char path[MAXPATHLEN]; /* buffer for pathname compose */
	const struct cmd_table *cmd;
	int argc;
	char *argv[8];	/* XXX i hope this is enough */
};
extern struct cmd_state cmd;

int getcmd(void);
int read_conf(void);
int bootparse(int);
void boot(dev_t);

int upgrade(void);
int docmd(void);		/* No longer static: needed by regress test */
