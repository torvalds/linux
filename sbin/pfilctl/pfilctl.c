/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Gleb Smirnoff <glebius@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/pfil.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int dev;

static const char * const typenames[] = {
	[PFIL_TYPE_IP4] = "IPv4",
	[PFIL_TYPE_IP6] = "IPv6",
	[PFIL_TYPE_ETHERNET] = "Ethernet",
};

static void listheads(int argc, char *argv[]);
static void listhooks(int argc, char *argv[]);
static void hook(int argc, char *argv[]);
static void help(void);

static const struct cmd {
	const char	*cmd_name;
	void		(*cmd_func)(int argc, char *argv[]);
} cmds[] = {
	{ "heads",	listheads },
	{ "hooks",	listhooks },
	{ "link",	hook },
	{ "unlink",	hook },
	{ NULL,		NULL },
};

int
main(int argc __unused, char *argv[] __unused)
{
	int cmd = -1;

	if (--argc == 0)
		help();
	argv++;

	for (int i = 0; cmds[i].cmd_name != NULL; i++)
		if (!strncmp(argv[0], cmds[i].cmd_name, strlen(argv[0]))) {
			if (cmd != -1)
				errx(1, "ambiguous command: %s", argv[0]);
			cmd = i;
		}
	if (cmd == -1)
		errx(1, "unknown command: %s", argv[0]);

	dev = open("/dev/" PFILDEV, O_RDWR);
	if (dev == -1)
		err(1, "open(%s)", "/dev/" PFILDEV);

	(*cmds[cmd].cmd_func)(argc, argv);

	return (0);
}

static void
help(void)
{

	fprintf(stderr, "usage: %s (heads|hooks|link|unlink)\n", getprogname());
	exit(0);
}

static void
listheads(int argc __unused, char *argv[] __unused)
{
	struct pfilioc_list plh;
	u_int nheads, nhooks, i;
	int j, h;

	plh.pio_nheads = 0;
	plh.pio_nhooks = 0;
	if (ioctl(dev, PFILIOC_LISTHEADS, &plh) != 0)
		err(1, "ioctl(PFILIOC_LISTHEADS)");

retry:
	plh.pio_heads = calloc(plh.pio_nheads, sizeof(struct pfilioc_head));
	if (plh.pio_heads == NULL)
		err(1, "malloc");
	plh.pio_hooks = calloc(plh.pio_nhooks, sizeof(struct pfilioc_hook));
	if (plh.pio_hooks == NULL)
		err(1, "malloc");

	nheads = plh.pio_nheads;
	nhooks = plh.pio_nhooks;

	if (ioctl(dev, PFILIOC_LISTHEADS, &plh) != 0)
		err(1, "ioctl(PFILIOC_LISTHEADS)");

	if (plh.pio_nheads > nheads || plh.pio_nhooks > nhooks) {
		free(plh.pio_heads);
		free(plh.pio_hooks);
		goto retry;
	}

#define	FMTHD	"%16s %8s\n"
#define	FMTHK	"%29s %16s %16s\n"
	printf(FMTHD, "Intercept point", "Type");
	for (i = 0, h = 0; i < plh.pio_nheads; i++) {
		printf(FMTHD, plh.pio_heads[i].pio_name,
		    typenames[plh.pio_heads[i].pio_type]);
		for (j = 0; j < plh.pio_heads[i].pio_nhooksin; j++, h++)
			printf(FMTHK, "In", plh.pio_hooks[h].pio_module,
			    plh.pio_hooks[h].pio_ruleset);
		for (j = 0; j < plh.pio_heads[i].pio_nhooksout; j++, h++)
			printf(FMTHK, "Out", plh.pio_hooks[h].pio_module,
			    plh.pio_hooks[h].pio_ruleset);
	}
}

static void
listhooks(int argc __unused, char *argv[] __unused)
{
	struct pfilioc_list plh;
	u_int nhooks, i;

	plh.pio_nhooks = 0;
	if (ioctl(dev, PFILIOC_LISTHEADS, &plh) != 0)
		err(1, "ioctl(PFILIOC_LISTHEADS)");
retry:
	plh.pio_hooks = calloc(plh.pio_nhooks, sizeof(struct pfilioc_hook));
	if (plh.pio_hooks == NULL)
		err(1, "malloc");

	nhooks = plh.pio_nhooks;

	if (ioctl(dev, PFILIOC_LISTHOOKS, &plh) != 0)
		err(1, "ioctl(PFILIOC_LISTHOOKS)");

	if (plh.pio_nhooks > nhooks) {
		free(plh.pio_hooks);
		goto retry;
	}

	printf("Available hooks:\n");
	for (i = 0; i < plh.pio_nhooks; i++) {
		printf("\t%s:%s %s\n", plh.pio_hooks[i].pio_module,
		    plh.pio_hooks[i].pio_ruleset,
		    typenames[plh.pio_hooks[i].pio_type]);
	}
}

static void
hook(int argc, char *argv[])
{
	struct pfilioc_link req;
	int c;
	char *ruleset;

	if (argv[0][0] == 'u')
		req.pio_flags = PFIL_UNLINK;
	else
		req.pio_flags = 0;

	while ((c = getopt(argc, argv, "ioa")) != -1)
		switch (c) {
		case 'i':
			req.pio_flags |= PFIL_IN;
			break;
		case 'o':
			req.pio_flags |= PFIL_OUT;
			break;
		case 'a':
			req.pio_flags |= PFIL_APPEND;
			break;
		default:
			help();
		}

	if (!PFIL_DIR(req.pio_flags))
		help();

	argc -= optind;
	argv += optind;

	if (argc != 2)
		help();

	/* link mod:ruleset head */
	if ((ruleset = strchr(argv[0], ':')) == NULL)
		help();
	*ruleset = '\0';
	ruleset++;

	strlcpy(req.pio_name, argv[1], sizeof(req.pio_name));
	strlcpy(req.pio_module, argv[0], sizeof(req.pio_module));
	strlcpy(req.pio_ruleset, ruleset, sizeof(req.pio_ruleset));

	if (ioctl(dev, PFILIOC_LINK, &req) != 0)
		err(1, "ioctl(PFILIOC_LINK)");
}
