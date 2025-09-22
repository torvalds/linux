/*	$OpenBSD: optree.c,v 1.11 2021/10/24 21:24:18 deraadt Exp $	*/

/*
 * Copyright (c) 2007 Federico G. Schwindt <fgsch@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for
 * any purpose with or without fee is hereby granted, provided that
 * the above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
 * OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/ioctl.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <machine/openpromio.h>

#include "defs.h"

extern  char *path_openprom;

static void
op_print(struct opiocdesc *opio, int depth)
{
	char *p;
	int i, multi, special;
	uint32_t cell;

	opio->op_name[opio->op_namelen] = '\0';
	printf("%*s%s: ", depth * 4, " ", opio->op_name);
	if (opio->op_buflen > 0) {
		opio->op_buf[opio->op_buflen] = '\0';
		multi = special = 0;

		/*
		 * On macppc we have string-values properties that end
		 * with multiple NUL characters, and the serial number
		 * has them embedded within the string.
		 */
		if (opio->op_buf[0] != '\0') {
			for (i = 0; i < opio->op_buflen; i++) {
				p = &opio->op_buf[i];
				if (*p >= ' ' && *p <= '~')
					continue;
				if (*p == '\0') {
					if (i + 1 < opio->op_buflen)
						p++;
					if (*p >= ' ' && *p <= '~') {
						special = multi;
						continue;
					}
					if (*p == '\0') {
						multi = 1;
						continue;
					}
				}

				special = 1;
				break;
			}
		} else {
			if (opio->op_buflen > 1)
				special = 1;
		}

		if (special && strcmp(opio->op_name, "serial-number") != 0) {
			for (i = 0; opio->op_buflen - i >= sizeof(int);
			    i += sizeof(int)) {
				if (i)
					printf(".");
				cell = *(uint32_t *)&opio->op_buf[i];
				printf("%08x", betoh32(cell));
			}
			if (i < opio->op_buflen) {
				if (i)
					printf(".");
				for (; i < opio->op_buflen; i++) {
					printf("%02x",
					    *(u_char *)&opio->op_buf[i]);
				}
			}
		} else {
			for (i = 0; i < opio->op_buflen;
			    i += strlen(&opio->op_buf[i]) + 1) {
				if (i && strlen(&opio->op_buf[i]) == 0)
					continue;
				if (i)
					printf(" + ");
				printf("'%s'", &opio->op_buf[i]);
			}
		}
	} else if(opio->op_buflen < 0)
		printf("too large");
	printf("\n");
}

void
op_nodes(int fd, int node, int depth)
{
	char op_buf[BUFSIZE * 8];
	char op_name[BUFSIZE];
	struct opiocdesc opio;

	memset(op_name, 0, sizeof(op_name));
	opio.op_nodeid = node;
	opio.op_buf = op_buf;
	opio.op_name = op_name;

	if (!node) {
		if (ioctl(fd, OPIOCGETNEXT, &opio) == -1)
			err(1, "OPIOCGETNEXT");
		node = opio.op_nodeid;
	} else
		printf("\n%*s", depth * 4, " ");

	printf("Node 0x%x\n", node);

	for (;;) {
		opio.op_buflen = sizeof(op_buf);
		opio.op_namelen = sizeof(op_name);

		/* Get the next property. */
		if (ioctl(fd, OPIOCNEXTPROP, &opio) == -1)
			err(1, "OPIOCNEXTPROP");

		op_buf[opio.op_buflen] = '\0';
		(void)strlcpy(op_name, op_buf, sizeof(op_name));
		opio.op_namelen = strlen(op_name);

		/* If it's the last, punt. */
		if (opio.op_namelen == 0)
			break;

		bzero(op_buf, sizeof(op_buf));
		opio.op_buflen = sizeof(op_buf);

		/* And its value. */
		if (ioctl(fd, OPIOCGET, &opio) == -1) {
			if (errno != ENOMEM)
				err(1, "OPIOCGET");

			opio.op_buflen = -1;	/* for op_print */
		}

		op_print(&opio, depth + 1);
	}

	/* Get next child. */
	if (ioctl(fd, OPIOCGETCHILD, &opio) == -1)
		err(1, "OPIOCGETCHILD");
	if (opio.op_nodeid)
		op_nodes(fd, opio.op_nodeid, depth + 1);

	/* Get next node/sibling. */
	opio.op_nodeid = node;
	if (ioctl(fd, OPIOCGETNEXT, &opio) == -1)
		err(1, "OPIOCGETNEXT");
	if (opio.op_nodeid)
		op_nodes(fd, opio.op_nodeid, depth);
}

void
op_tree(void)
{
	int fd;

	if ((fd = open(path_openprom, O_RDONLY)) == -1)
		err(1, "open: %s", path_openprom);
	op_nodes(fd, 0, 0);
	(void)close(fd);
}
