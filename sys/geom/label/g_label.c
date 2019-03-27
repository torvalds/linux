/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include "opt_geom.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/ctype.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/sbuf.h>
#include <sys/stddef.h>
#include <sys/sysctl.h>
#include <geom/geom.h>
#include <geom/geom_slice.h>
#include <geom/label/g_label.h>

FEATURE(geom_label, "GEOM labeling support");

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, label, CTLFLAG_RW, 0, "GEOM_LABEL stuff");
u_int g_label_debug = 0;
SYSCTL_UINT(_kern_geom_label, OID_AUTO, debug, CTLFLAG_RWTUN, &g_label_debug, 0,
    "Debug level");

static int g_label_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);
static int g_label_destroy(struct g_geom *gp, boolean_t force);
static struct g_geom *g_label_taste(struct g_class *mp, struct g_provider *pp,
    int flags __unused);
static void g_label_config(struct gctl_req *req, struct g_class *mp,
    const char *verb);

struct g_class g_label_class = {
	.name = G_LABEL_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_label_config,
	.taste = g_label_taste,
	.destroy_geom = g_label_destroy_geom
};

/*
 * To add a new file system where you want to look for volume labels,
 * you have to:
 * 1. Add a file g_label_<file system>.c which implements labels recognition.
 * 2. Add an 'extern const struct g_label_desc g_label_<file system>;' into
 *    g_label.h file.
 * 3. Add an element to the table below '&g_label_<file system>,'.
 * 4. Add your file to sys/conf/files.
 * 5. Add your file to sys/modules/geom/geom_label/Makefile.
 * 6. Add your file system to manual page sbin/geom/class/label/glabel.8.
 */
const struct g_label_desc *g_labels[] = {
	&g_label_gpt,
	&g_label_gpt_uuid,
#ifdef GEOM_LABEL
	&g_label_ufs_id,
	&g_label_ufs_volume,
	&g_label_iso9660,
	&g_label_msdosfs,
	&g_label_ext2fs,
	&g_label_reiserfs,
	&g_label_ntfs,
	&g_label_disk_ident,
	&g_label_flashmap,
#endif
	NULL
};

void
g_label_rtrim(char *label, size_t size)
{
	ptrdiff_t i;

	for (i = size - 1; i >= 0; i--) {
		if (label[i] == '\0')
			continue;
		else if (label[i] == ' ')
			label[i] = '\0';
		else
			break;
	}
}

static int
g_label_destroy_geom(struct gctl_req *req __unused, struct g_class *mp,
    struct g_geom *gp __unused)
{

	/*
	 * XXX: Unloading a class which is using geom_slice:1.56 is currently
	 * XXX: broken, so we deny unloading when we have geoms.
	 */
	return (EOPNOTSUPP);
}

static void
g_label_orphan(struct g_consumer *cp)
{

	G_LABEL_DEBUG(1, "Label %s removed.",
	    LIST_FIRST(&cp->geom->provider)->name);
	g_slice_orphan(cp);
}

static void
g_label_spoiled(struct g_consumer *cp)
{

	G_LABEL_DEBUG(1, "Label %s removed.",
	    LIST_FIRST(&cp->geom->provider)->name);
	g_slice_spoiled(cp);
}

static void
g_label_resize(struct g_consumer *cp)
{

	G_LABEL_DEBUG(1, "Label %s resized.",
	    LIST_FIRST(&cp->geom->provider)->name);

	g_slice_config(cp->geom, 0, G_SLICE_CONFIG_FORCE, (off_t)0,
	    cp->provider->mediasize, cp->provider->sectorsize, "notused");
}

static int
g_label_is_name_ok(const char *label)
{
	const char *s;

	/* Check if the label starts from ../ */
	if (strncmp(label, "../", 3) == 0)
		return (0);
	/* Check if the label contains /../ */
	if (strstr(label, "/../") != NULL)
		return (0);
	/* Check if the label ends at ../ */
	if ((s = strstr(label, "/..")) != NULL && s[3] == '\0')
		return (0);
	return (1);
}

static void
g_label_mangle_name(char *label, size_t size)
{
	struct sbuf *sb;
	const u_char *c;

	sb = sbuf_new(NULL, NULL, size, SBUF_FIXEDLEN);
	for (c = label; *c != '\0'; c++) {
		if (!isprint(*c) || isspace(*c) || *c =='"' || *c == '%')
			sbuf_printf(sb, "%%%02X", *c);
		else
			sbuf_putc(sb, *c);
	}
	if (sbuf_finish(sb) != 0)
		label[0] = '\0';
	else
		strlcpy(label, sbuf_data(sb), size);
	sbuf_delete(sb);
}

static struct g_geom *
g_label_create(struct gctl_req *req, struct g_class *mp, struct g_provider *pp,
    const char *label, const char *dir, off_t mediasize)
{
	struct g_geom *gp;
	struct g_provider *pp2;
	struct g_consumer *cp;
	char name[64];

	g_topology_assert();

	if (!g_label_is_name_ok(label)) {
		G_LABEL_DEBUG(0, "%s contains suspicious label, skipping.",
		    pp->name);
		G_LABEL_DEBUG(1, "%s suspicious label is: %s", pp->name, label);
		if (req != NULL)
			gctl_error(req, "Label name %s is invalid.", label);
		return (NULL);
	}
	gp = NULL;
	cp = NULL;
	if (snprintf(name, sizeof(name), "%s/%s", dir, label) >= sizeof(name)) {
		if (req != NULL)
			gctl_error(req, "Label name %s is too long.", label);
		return (NULL);
	}
	LIST_FOREACH(gp, &mp->geom, geom) {
		pp2 = LIST_FIRST(&gp->provider);
		if (pp2 == NULL)
			continue;
		if ((pp2->flags & G_PF_ORPHAN) != 0)
			continue;
		if (strcmp(pp2->name, name) == 0) {
			G_LABEL_DEBUG(1, "Label %s(%s) already exists (%s).",
			    label, name, pp->name);
			if (req != NULL) {
				gctl_error(req, "Provider %s already exists.",
				    name);
			}
			return (NULL);
		}
	}
	gp = g_slice_new(mp, 1, pp, &cp, NULL, 0, NULL);
	if (gp == NULL) {
		G_LABEL_DEBUG(0, "Cannot create slice %s.", label);
		if (req != NULL)
			gctl_error(req, "Cannot create slice %s.", label);
		return (NULL);
	}
	gp->orphan = g_label_orphan;
	gp->spoiled = g_label_spoiled;
	gp->resize = g_label_resize;
	g_access(cp, -1, 0, 0);
	g_slice_config(gp, 0, G_SLICE_CONFIG_SET, (off_t)0, mediasize,
	    pp->sectorsize, "%s", name);
	G_LABEL_DEBUG(1, "Label for provider %s is %s.", pp->name, name);
	return (gp);
}

static int
g_label_destroy(struct g_geom *gp, boolean_t force)
{
	struct g_provider *pp;

	g_topology_assert();
	pp = LIST_FIRST(&gp->provider);
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_LABEL_DEBUG(0, "Provider %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_LABEL_DEBUG(1,
			    "Provider %s is still open (r%dw%de%d).", pp->name,
			    pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	} else if (pp != NULL)
		G_LABEL_DEBUG(1, "Label %s removed.", pp->name);
	g_slice_spoiled(LIST_FIRST(&gp->consumer));
	return (0);
}

static int
g_label_read_metadata(struct g_consumer *cp, struct g_label_metadata *md)
{
	struct g_provider *pp;
	u_char *buf;
	int error;

	g_topology_assert();

	pp = cp->provider;
	g_topology_unlock();
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize, pp->sectorsize,
	    &error);
	g_topology_lock();
	if (buf == NULL)
		return (error);
	/* Decode metadata. */
	label_metadata_decode(buf, md);
	g_free(buf);

	return (0);
}

static void
g_label_orphan_taste(struct g_consumer *cp __unused)
{

	KASSERT(1 == 0, ("%s called?", __func__));
}

static void
g_label_start_taste(struct bio *bp __unused)
{

	KASSERT(1 == 0, ("%s called?", __func__));
}

static int
g_label_access_taste(struct g_provider *pp __unused, int dr __unused,
    int dw __unused, int de __unused)
{

	KASSERT(1 == 0, ("%s called", __func__));
	return (EOPNOTSUPP);
}

static struct g_geom *
g_label_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_label_metadata md;
	struct g_consumer *cp;
	struct g_geom *gp;
	int i;

	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	g_topology_assert();

	G_LABEL_DEBUG(2, "Tasting %s.", pp->name);

	/* Skip providers that are already open for writing. */
	if (pp->acw > 0)
		return (NULL);

	if (strcmp(pp->geom->class->name, mp->name) == 0)
		return (NULL);

	gp = g_new_geomf(mp, "label:taste");
	gp->start = g_label_start_taste;
	gp->access = g_label_access_taste;
	gp->orphan = g_label_orphan_taste;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	if (g_access(cp, 1, 0, 0) != 0)
		goto end;
	do {
		if (g_label_read_metadata(cp, &md) != 0)
			break;
		if (strcmp(md.md_magic, G_LABEL_MAGIC) != 0)
			break;
		if (md.md_version > G_LABEL_VERSION) {
			printf("geom_label.ko module is too old to handle %s.\n",
			    pp->name);
			break;
		}

		/*
		 * Backward compatibility:
		 */
		/*
		 * There was no md_provsize field in earlier versions of
		 * metadata.
		 */
		if (md.md_version < 2)
			md.md_provsize = pp->mediasize;

		if (md.md_provsize != pp->mediasize)
			break;

		g_label_create(NULL, mp, pp, md.md_label, G_LABEL_DIR,
		    pp->mediasize - pp->sectorsize);
	} while (0);
	for (i = 0; g_labels[i] != NULL; i++) {
		char label[128];

		if (g_labels[i]->ld_enabled == 0)
			continue;
		g_topology_unlock();
		g_labels[i]->ld_taste(cp, label, sizeof(label));
		g_label_mangle_name(label, sizeof(label));
		g_topology_lock();
		if (label[0] == '\0')
			continue;
		g_label_create(NULL, mp, pp, label, g_labels[i]->ld_dir,
		    pp->mediasize);
	}
	g_access(cp, -1, 0, 0);
end:
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	return (NULL);
}

static void
g_label_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	struct g_provider *pp;
	const char *name;
	int *nargs;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	/*
	 * arg1 is the name of provider.
	 */
	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg%d' argument", 1);
		return;
	}
	if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
		name += strlen("/dev/");
	pp = g_provider_by_name(name);
	if (pp == NULL) {
		G_LABEL_DEBUG(1, "Provider %s is invalid.", name);
		gctl_error(req, "Provider %s is invalid.", name);
		return;
	}
	/*
	 * arg0 is the label.
	 */
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%d' argument", 0);
		return;
	}
	g_label_create(req, mp, pp, name, G_LABEL_DIR, pp->mediasize);
}

static const char *
g_label_skip_dir(const char *name)
{
	char path[64];
	u_int i;

	if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
		name += strlen("/dev/");
	if (strncmp(name, G_LABEL_DIR "/", strlen(G_LABEL_DIR "/")) == 0)
		name += strlen(G_LABEL_DIR "/");
	for (i = 0; g_labels[i] != NULL; i++) {
		snprintf(path, sizeof(path), "%s/", g_labels[i]->ld_dir);
		if (strncmp(name, path, strlen(path)) == 0) {
			name += strlen(path);
			break;
		}
	}
	return (name);
}

static struct g_geom *
g_label_find_geom(struct g_class *mp, const char *name)
{
	struct g_geom *gp;
	struct g_provider *pp;
	const char *pname;

	name = g_label_skip_dir(name);
	LIST_FOREACH(gp, &mp->geom, geom) {
		pp = LIST_FIRST(&gp->provider);
		pname = g_label_skip_dir(pp->name);
		if (strcmp(pname, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
g_label_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	int *nargs, *force, error, i;
	struct g_geom *gp;
	const char *name;
	char param[16];

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs <= 0) {
		gctl_error(req, "Missing device(s).");
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force == NULL) {
		gctl_error(req, "No 'force' argument");
		return;
	}

	for (i = 0; i < *nargs; i++) {
		snprintf(param, sizeof(param), "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%d' argument", i);
			return;
		}
		gp = g_label_find_geom(mp, name);
		if (gp == NULL) {
			G_LABEL_DEBUG(1, "Label %s is invalid.", name);
			gctl_error(req, "Label %s is invalid.", name);
			return;
		}
		error = g_label_destroy(gp, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy label %s (error=%d).",
			    LIST_FIRST(&gp->provider)->name, error);
			return;
		}
	}
}

static void
g_label_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_LABEL_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_label_ctl_create(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0 ||
	    strcmp(verb, "stop") == 0) {
		g_label_ctl_destroy(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

DECLARE_GEOM_CLASS(g_label_class, g_label);
MODULE_VERSION(geom_label, 0);
