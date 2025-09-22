/*	$OpenBSD: ldomd.c,v 1.11 2021/10/24 21:24:18 deraadt Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
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
#include <sys/ioctl.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "ds.h"
#include "hvctl.h"
#include "mdesc.h"
#include "ldom_util.h"
#include "ldomd.h"

TAILQ_HEAD(guest_head, guest) guests;

void add_guest(struct md_node *);
void map_domain_services(struct md *);

void frag_init(void);
void add_frag_mblock(struct md_node *);
void add_frag(uint64_t);
void delete_frag(uint64_t);
uint64_t alloc_frag(void);

void hv_update_md(struct guest *guest);
void hv_open(void);
void hv_close(void);
void hv_read(uint64_t, void *, size_t);
void hv_write(uint64_t, void *, size_t);

int hvctl_seq = 1;
int hvctl_fd;

void *hvmd_buf;
size_t hvmd_len;
struct md *hvmd;
uint64_t hv_mdpa;

__dead void	usage(void);
void	logit(int, const char *, ...);
void	vlog(int, const char *, va_list);

void
log_init(int n_debug)
{
	extern char *__progname;

	debug = n_debug;

	if (!debug)
		openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	tzset();
}

void
fatal(const char *emsg)
{
	if (errno)
		logit(LOG_CRIT, "fatal: %s: %s\n", emsg, strerror(errno));
	else
		logit(LOG_CRIT, "fatal: %s\n", emsg);

	exit(EXIT_FAILURE);
}

void
logit(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(pri, fmt, ap);
	va_end(ap);
}

void
vlog(int pri, const char *fmt, va_list ap)
{
	char *nfmt;

	if (debug) {
		/* best effort in out of mem situations */
		if (asprintf(&nfmt, "%s\n", fmt) == -1) {
			vfprintf(stderr, fmt, ap);
			fprintf(stderr, "\n");
		} else {
			vfprintf(stderr, nfmt, ap);
			free(nfmt);
		}
		fflush(stderr);
	} else
		vsyslog(pri, fmt, ap);
}

int
main(int argc, char **argv)
{
	struct hvctl_msg msg;
	ssize_t nbytes;
	struct md_header hdr;
	struct md_node *node;
	struct md_prop *prop;
	struct guest *guest;
	int debug = 0;
	int ch;
	int i;

	log_init(1);

	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (!debug)
		if (daemon(0, 0))
			fatal("daemon");

	log_init(debug);

	hv_open();

	/*
	 * Request config.
	 */
	bzero(&msg, sizeof(msg));
	msg.hdr.op = HVCTL_OP_GET_HVCONFIG;
	msg.hdr.seq = hvctl_seq++;
	nbytes = write(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("write");

	bzero(&msg, sizeof(msg));
	nbytes = read(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("read");

	hv_mdpa = msg.msg.hvcnf.hvmdp;
	hv_read(hv_mdpa, &hdr, sizeof(hdr));
	hvmd_len = sizeof(hdr) + hdr.node_blk_sz + hdr.name_blk_sz +
	    hdr.data_blk_sz;
	hvmd_buf = xmalloc(hvmd_len);
	hv_read(hv_mdpa, hvmd_buf, hvmd_len);

	hvmd = md_ingest(hvmd_buf, hvmd_len);
	node = md_find_node(hvmd, "guests");
	TAILQ_INIT(&guests);
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			add_guest(prop->d.arc.node);
	}

	frag_init();

	TAILQ_FOREACH(guest, &guests, link) {
		struct ds_conn *dc;
		char path[PATH_MAX];

		if (strcmp(guest->name, "primary") == 0)
			continue;

		snprintf(path, sizeof(path), "/dev/ldom-%s", guest->name);
		dc = ds_conn_open(path, guest);
		ds_conn_register_service(dc, &var_config_service);
	}

	hv_close();

	/*
	 * Open all virtual disk server port device files.  As long as
	 * we keep these device files open, the corresponding virtual
	 * disks will be available to the guest domains.  For now we
	 * just keep them open until we exit, so there is not reason
	 * to keep track of the file descriptors.
	 */
	for (i = 0; i < 256; i++) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "/dev/vdsp%d", i);
		if (open(path, O_RDWR) == -1)
			break;
	}

	ds_conn_serve();

	exit(EXIT_SUCCESS);
}

void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-d]\n", __progname);
	exit(EXIT_FAILURE);
}

void
add_guest(struct md_node *node)
{
	struct guest *guest;
	struct md_header hdr;
	void *buf;
	size_t len;

	guest = xmalloc(sizeof(*guest));

	if (!md_get_prop_str(hvmd, node, "name", &guest->name))
		goto free;
	if (!md_get_prop_val(hvmd, node, "gid", &guest->gid))
		goto free;
	if (!md_get_prop_val(hvmd, node, "mdpa", &guest->mdpa))
		goto free;

	hv_read(guest->mdpa, &hdr, sizeof(hdr));
	len = sizeof(hdr) + hdr.node_blk_sz + hdr.name_blk_sz +
	    hdr.data_blk_sz;
	buf = xmalloc(len);
	hv_read(guest->mdpa, buf, len);

	guest->node = node;
	guest->md = md_ingest(buf, len);
	if (strcmp(guest->name, "primary") == 0)
		map_domain_services(guest->md);

	TAILQ_INSERT_TAIL(&guests, guest, link);
	return;

free:
	free(guest);
}

void
map_domain_services(struct md *md)
{
	struct md_node *node;
	const char *name;
	char source[64];
	char target[64];
	int unit = 0;

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, "virtual-device-port") != 0)
			continue;

		if (!md_get_prop_str(md, node, "vldc-svc-name", &name))
			continue;

		if (strncmp(name, "ldom-", 5) != 0 ||
		    strcmp(name, "ldom-primary") == 0)
			continue;

		snprintf(source, sizeof(source), "/dev/ldom%d", unit++);
		snprintf(target, sizeof(target), "/dev/%s", name);
		unlink(target);
		symlink(source, target);
	}
}

struct frag {
	TAILQ_ENTRY(frag) link;
	uint64_t base;
};

TAILQ_HEAD(frag_head, frag) free_frags;

uint64_t fragsize;

void
frag_init(void)
{
	struct md_node *node;
	struct md_prop *prop;

	node = md_find_node(hvmd, "frag_space");
	md_get_prop_val(hvmd, node, "fragsize", &fragsize);
	TAILQ_INIT(&free_frags);
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			add_frag_mblock(prop->d.arc.node);
	}
}

void
add_frag_mblock(struct md_node *node)
{
	uint64_t base, size;
	struct guest *guest;

	md_get_prop_val(hvmd, node, "base", &base);
	md_get_prop_val(hvmd, node, "size", &size);
	while (size > fragsize) {
		add_frag(base);
		size -= fragsize;
		base += fragsize;
	}

	delete_frag(hv_mdpa);
	TAILQ_FOREACH(guest, &guests, link)
		delete_frag(guest->mdpa);
}

void
add_frag(uint64_t base)
{
	struct frag *frag;

	frag = xmalloc(sizeof(*frag));
	frag->base = base;
	TAILQ_INSERT_TAIL(&free_frags, frag, link);
}

void
delete_frag(uint64_t base)
{
	struct frag *frag;
	struct frag *tmp;

	TAILQ_FOREACH_SAFE(frag, &free_frags, link, tmp) {
		if (frag->base == base) {
			TAILQ_REMOVE(&free_frags, frag, link);
			free(frag);
		}
	}
}

uint64_t
alloc_frag(void)
{
	struct frag *frag;
	uint64_t base;

	frag = TAILQ_FIRST(&free_frags);
	if (frag == NULL)
		return -1;

	TAILQ_REMOVE(&free_frags, frag, link);
	base = frag->base;
	free(frag);

	return base;
}

void
hv_update_md(struct guest *guest)
{
	struct hvctl_msg msg;
	size_t nbytes;
	void *buf;
	size_t size;
	uint64_t mdpa;

	hv_open();

	mdpa = alloc_frag();
	size = md_exhume(guest->md, &buf);
	hv_write(mdpa, buf, size);
	add_frag(guest->mdpa);
	guest->mdpa = mdpa;
	free(buf);

	md_set_prop_val(hvmd, guest->node, "mdpa", guest->mdpa);

	mdpa = alloc_frag();
	size = md_exhume(hvmd, &buf);
	hv_write(mdpa, buf, size);
	add_frag(hv_mdpa);
	hv_mdpa = mdpa;
	free(buf);

	/* Update config.  */
	bzero(&msg, sizeof(msg));
	msg.hdr.op = HVCTL_OP_RECONFIGURE;
	msg.hdr.seq = hvctl_seq++;
	msg.msg.reconfig.guestid = -1;
	msg.msg.reconfig.hvmdp = hv_mdpa;
	nbytes = write(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("write");

	bzero(&msg, sizeof(msg));
	nbytes = read(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("read");

	hv_close();

	if (msg.hdr.status != HVCTL_ST_OK)
		logit(LOG_CRIT, "reconfigure failed: %d", msg.hdr.status);
}

void
hv_open(void)
{
	struct hvctl_msg msg;
	ssize_t nbytes;
	uint64_t code;

	hvctl_fd = open("/dev/hvctl", O_RDWR);
	if (hvctl_fd == -1)
		fatal("cannot open /dev/hvctl");

	/*
	 * Say "Hello".
	 */
	bzero(&msg, sizeof(msg));
	msg.hdr.op = HVCTL_OP_HELLO;
	msg.hdr.seq = hvctl_seq++;
	msg.msg.hello.major = 1;
	nbytes = write(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("write");

	bzero(&msg, sizeof(msg));
	nbytes = read(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("read");

	code = msg.msg.clnge.code ^ 0xbadbeef20;

	/*
	 * Respond to challenge.
	 */
	bzero(&msg, sizeof(msg));
	msg.hdr.op = HVCTL_OP_RESPONSE;
	msg.hdr.seq = hvctl_seq++;
	msg.msg.clnge.code = code ^ 0x12cafe42a;
	nbytes = write(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("write");

	bzero(&msg, sizeof(msg));
	nbytes = read(hvctl_fd, &msg, sizeof(msg));
	if (nbytes != sizeof(msg))
		fatal("read");
}

void
hv_close(void)
{
	close(hvctl_fd);
	hvctl_fd = -1;
}

void
hv_read(uint64_t addr, void *buf, size_t len)
{
	struct hv_io hi;

	hi.hi_cookie = addr;
	hi.hi_addr = buf;
	hi.hi_len = len;

	if (ioctl(hvctl_fd, HVIOCREAD, &hi) == -1)
		fatal("ioctl");
}

void
hv_write(uint64_t addr, void *buf, size_t len)
{
	struct hv_io hi;

	hi.hi_cookie = addr;
	hi.hi_addr = buf;
	hi.hi_len = len;

	if (ioctl(hvctl_fd, HVIOCWRITE, &hi) == -1)
		fatal("ioctl");
}
