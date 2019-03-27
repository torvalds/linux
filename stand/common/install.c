/*-
 * Copyright (c) 2008-2014, Juniper Networks, Inc.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <stand.h>
#include <net.h>
#include <string.h>

#include "bootstrap.h"

extern struct in_addr servip;

extern int pkgfs_init(const char *, struct fs_ops *);
extern void pkgfs_cleanup(void);

COMMAND_SET(install, "install", "install software package", command_install);

static char *inst_kernel;
static char **inst_modules;
static char *inst_rootfs;
static char *inst_loader_rc;

static int
setpath(char **what, char *val)
{
	char *path;
	size_t len;
	int rel;

	len = strlen(val) + 1;
	rel = (val[0] != '/') ? 1 : 0;
	path = malloc(len + rel);
	if (path == NULL)
		return (ENOMEM);
	path[0] = '/';
	strcpy(path + rel, val);

	*what = path;
	return (0);
}

static int
setmultipath(char ***what, char *val)
{
	char *s, *v;
	int count, error, idx;

	count = 0;
	v = val;
	do {
		count++;
		s = strchr(v, ',');
		v = (s == NULL) ? NULL : s + 1;
	} while (v != NULL);

	*what = calloc(count + 1, sizeof(char *));
	if (*what == NULL)
		return (ENOMEM);

	for (idx = 0; idx < count; idx++) {
		s = strchr(val, ',');
		if (s != NULL)
			*s++ = '\0';
		error = setpath(*what + idx, val);
		if (error)
			return (error);
		val = s;
	}

	return (0);
}

static int
read_metatags(int fd)
{
	char buf[1024];
	char *p, *tag, *val;
	ssize_t fsize;
	int error;

	fsize = read(fd, buf, sizeof(buf));
	if (fsize == -1)
		return (errno);

	/*
	 * Assume that if we read a whole buffer worth of data, we
	 * haven't read the entire file. In other words, the buffer
	 * size must always be larger than the file size. That way
	 * we can append a '\0' and use standard string operations.
	 * Return an error if this is not possible.
	 */
	if (fsize == sizeof(buf))
		return (ENOMEM);

	buf[fsize] = '\0';
	error = 0;
	tag = buf;
	while (!error && *tag != '\0') {
		val = strchr(tag, '=');
		if (val == NULL) {
			error = EINVAL;
			break;
		}
		*val++ = '\0';
		p = strchr(val, '\n');
		if (p == NULL) {
			error = EINVAL;
			break;
		}
		*p++ = '\0';

		if (strcmp(tag, "KERNEL") == 0)
			error = setpath(&inst_kernel, val);
		else if (strcmp(tag, "MODULES") == 0)
			error = setmultipath(&inst_modules, val);
		else if (strcmp(tag, "ROOTFS") == 0)
			error = setpath(&inst_rootfs, val);
		else if (strcmp(tag, "LOADER_RC") == 0)
			error = setpath(&inst_loader_rc, val);

		tag = p;
	}

	return (error);
}

static void
cleanup(void)
{
	u_int i;

	if (inst_kernel != NULL) {
		free(inst_kernel);
		inst_kernel = NULL;
	}
	if (inst_modules != NULL) {
		i = 0;
		while (inst_modules[i] != NULL)
			free(inst_modules[i++]);
		free(inst_modules);
		inst_modules = NULL;
	}
	if (inst_rootfs != NULL) {
		free(inst_rootfs);
		inst_rootfs = NULL;
	}
	if (inst_loader_rc != NULL) {
		free(inst_loader_rc);
		inst_loader_rc = NULL;
	}
	pkgfs_cleanup();
}

/*
 * usage: install URL
 * where: URL = (tftp|file)://[host]/<package>
 */
static int
install(char *pkgname)
{
	static char buf[256];
	struct fs_ops *proto;
	struct preloaded_file *fp;
	char *s, *currdev;
	const char *devname;
	int error, fd, i, local;

	s = strstr(pkgname, "://");
	if (s == NULL)
		goto invalid_url;

	i = s - pkgname;
	if (i == 4 && !strncasecmp(pkgname, "tftp", i)) {
		devname = "net0";
		proto = &tftp_fsops;
		local = 0;
	} else if (i == 4 && !strncasecmp(pkgname, "file", i)) {
		currdev = getenv("currdev");
		if (currdev != NULL && strcmp(currdev, "pxe0:") == 0) {
			devname = "pxe0";
			proto = NULL;
		} else {
			devname = "disk1";
			proto = &dosfs_fsops;
		}
		local = 1;
	} else
		goto invalid_url;

	s += 3;
	if (*s == '\0')
		goto invalid_url;

	if (*s != '/' ) {
		if (local)
			goto invalid_url;

		pkgname = strchr(s, '/');
		if (pkgname == NULL)
			goto invalid_url;

		*pkgname = '\0';
		servip.s_addr = inet_addr(s);
		if (servip.s_addr == htonl(INADDR_NONE))
			goto invalid_url;

		setenv("serverip", inet_ntoa(servip), 1);

		*pkgname = '/';
	} else
		pkgname = s;

	if (strlen(devname) + strlen(pkgname) + 2 > sizeof(buf)) {
		command_errmsg = "package name too long";
		return (CMD_ERROR);
	}
	sprintf(buf, "%s:%s", devname, pkgname);
	setenv("install_package", buf, 1);

	error = pkgfs_init(buf, proto);
	if (error) {
		command_errmsg = "cannot open package";
		goto fail;
	}

	/*
	 * Point of no return: unload anything that may have been
	 * loaded and prune the environment from harmful variables.
	 */
	unload();
	unsetenv("vfs.root.mountfrom");

	/*
	 * read the metatags file.
	 */
	fd = open("/metatags", O_RDONLY);
	if (fd != -1) {
		error = read_metatags(fd);
		close(fd);
		if (error) {
			command_errmsg = "cannot load metatags";
			goto fail;
		}
	}

	s = (inst_kernel == NULL) ? "/kernel" : inst_kernel;
	error = mod_loadkld(s, 0, NULL);
	if (error) {
		command_errmsg = "cannot load kernel from package";
		goto fail;
	}

	/* If there is a loader.rc in the package, execute it */
	s = (inst_loader_rc == NULL) ? "/loader.rc" : inst_loader_rc;
	fd = open(s, O_RDONLY);
	if (fd != -1) {
		close(fd);
		error = inter_include(s);
		if (error == CMD_ERROR)
			goto fail;
	}

	i = 0;
	while (inst_modules != NULL && inst_modules[i] != NULL) {
		error = mod_loadkld(inst_modules[i], 0, NULL);
		if (error) {
			command_errmsg = "cannot load module(s) from package";
			goto fail;
		}
		i++;
	}

	s = (inst_rootfs == NULL) ? "/install.iso" : inst_rootfs;
	if (file_loadraw(s, "mfs_root", 1) == NULL) {
		error = errno;
		command_errmsg = "cannot load root file system";
		goto fail;
	}

	cleanup();

	fp = file_findfile(NULL, NULL);
	if (fp != NULL)
		file_formats[fp->f_loader]->l_exec(fp);
	error = CMD_ERROR;
	command_errmsg = "unable to start installation";

 fail:
	sprintf(buf, "%s (error %d)", command_errmsg, error);
	cleanup();
	unload();
	exclusive_file_system = NULL;
	command_errmsg = buf;	/* buf is static. */
	return (CMD_ERROR);

 invalid_url:
	command_errmsg = "invalid URL";
	return (CMD_ERROR);
}

static int
command_install(int argc, char *argv[])
{
	int argidx;

	unsetenv("install_format");

	argidx = 1;
	while (1) {
		if (argc == argidx) {
			command_errmsg =
			    "usage: install [--format] <URL>";
			return (CMD_ERROR);
		}
		if (!strcmp(argv[argidx], "--format")) {
			setenv("install_format", "yes", 1);
			argidx++;
			continue;
		}
		break;
	}

	return (install(argv[argidx]));
}
