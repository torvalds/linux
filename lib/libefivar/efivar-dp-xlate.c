/*-
 * Copyright (c) 2017 Netflix, Inc.
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
#include <sys/ucred.h>
#include <sys/mount.h>

#undef MAX
#undef MIN

#include <assert.h>
#include <efivar.h>
#include <errno.h>
#include <libgeom.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>

#include "efichar.h"

#include "efi-osdep.h"
#include "efivar-dp.h"

#include "uefi-dplib.h"

#define MAX_DP_SANITY	4096		/* Biggest device path in bytes */
#define MAX_DP_TEXT_LEN	4096		/* Longest string rep of dp */

#define	G_PART	"PART"
#define	G_LABEL "LABEL"
#define G_DISK	"DISK"

static const char *
geom_pp_attr(struct gmesh *mesh, struct gprovider *pp, const char *attr)
{
	struct gconfig *conf;

	LIST_FOREACH(conf, &pp->lg_config, lg_config) {
		if (strcmp(conf->lg_name, attr) != 0)
			continue;
		return (conf->lg_val);
	}
	return (NULL);
}

static struct gprovider *
find_provider_by_efimedia(struct gmesh *mesh, const char *efimedia)
{
	struct gclass *classp;
	struct ggeom *gp;
	struct gprovider *pp;
	const char *val;

	/*
	 * Find the partition class so we can search it...
	 */
	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		if (strcasecmp(classp->lg_name, G_PART) == 0)
			break;
	}
	if (classp == NULL)
		return (NULL);

	/*
	 * Each geom will have a number of providers, search each
	 * one of them for the efimedia that matches.
	 */
	/* XXX just used gpart class since I know it's the only one, but maybe I should search all classes */
	LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
		LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
			val = geom_pp_attr(mesh, pp, "efimedia");
			if (val == NULL)
				continue;
			if (strcasecmp(efimedia, val) == 0)
				return (pp);
		}
	}

	return (NULL);
}

static struct gprovider *
find_provider_by_name(struct gmesh *mesh, const char *name)
{
	struct gclass *classp;
	struct ggeom *gp;
	struct gprovider *pp;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class) {
		LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
			LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
				if (strcmp(pp->lg_name, name) == 0)
					return (pp);
			}
		}
	}

	return (NULL);
}


static int
efi_hd_to_unix(struct gmesh *mesh, const_efidp dp, char **dev, char **relpath, char **abspath)
{
	int rv = 0, n, i;
	const_efidp media, file, walker;
	size_t len, mntlen;
	char buf[MAX_DP_TEXT_LEN];
	char *pwalk;
	struct gprovider *pp, *provider;
	struct gconsumer *cp;
	struct statfs *mnt;

	walker = media = dp;

	/*
	 * Now, we can either have a filepath node next, or the end.
	 * Otherwise, it's an error.
	 */
	walker = (const_efidp)NextDevicePathNode(walker);
	if ((uintptr_t)walker - (uintptr_t)dp > MAX_DP_SANITY)
		return (EINVAL);
	if (DevicePathType(walker) ==  MEDIA_DEVICE_PATH &&
	    DevicePathSubType(walker) == MEDIA_FILEPATH_DP)
		file = walker;
	else if (DevicePathType(walker) == MEDIA_DEVICE_PATH &&
	    DevicePathType(walker) == END_DEVICE_PATH_TYPE)
		file = NULL;
	else
		return (EINVAL);

	/*
	 * Format this node. We're going to look for it as a efimedia
	 * attribute of some geom node. Once we find that node, we use it
	 * as the device it comes from, at least provisionally.
	 */
	len = efidp_format_device_path_node(buf, sizeof(buf), media);
	if (len > sizeof(buf))
		return (EINVAL);

	pp = find_provider_by_efimedia(mesh, buf);
	if (pp == NULL) {
		rv = ENOENT;
		goto errout;
	}

	*dev = strdup(pp->lg_name);
	if (*dev == NULL) {
		rv = ENOMEM;
		goto errout;
	}

	/*
	 * No file specified, just return the device. Don't even look
	 * for a mountpoint. XXX Sane?
	 */
	if (file == NULL)
		goto errout;

	/*
	 * Now extract the relative path. The next node in the device path should
	 * be a filesystem node. If not, we have issues.
	 */
	*relpath = efidp_extract_file_path(file);
	if (*relpath == NULL) {
		rv = ENOMEM;
		goto errout;
	}
	for (pwalk = *relpath; *pwalk; pwalk++)
		if (*pwalk == '\\')
			*pwalk = '/';

	/*
	 * To find the absolute path, we have to look for where we're mounted.
	 * We only look a little hard, since looking too hard can come up with
	 * false positives (imagine a graid, one of whose devices is *dev).
	 */
	n = getfsstat(NULL, 0, MNT_NOWAIT) + 1;
	if (n < 0) {
		rv = errno;
		goto errout;
	}
	mntlen = sizeof(struct statfs) * n;
	mnt = malloc(mntlen);
	n = getfsstat(mnt, mntlen, MNT_NOWAIT);
	if (n < 0) {
		rv = errno;
		goto errout;
	}
	provider = pp;
	for (i = 0; i < n; i++) {
		/*
		 * Skip all pseudo filesystems. This also skips the real filesytsem
		 * of ZFS. There's no EFI designator for ZFS in the standard, so
		 * we'll need to invent one, but its decoding will be handled in
		 * a separate function.
		 */
		if (mnt[i].f_mntfromname[0] != '/')
			continue;

		/*
		 * First see if it is directly attached
		 */
		if (strcmp(provider->lg_name, mnt[i].f_mntfromname + 5) == 0)
			break;

		/*
		 * Next see if it is attached via one of the physical disk's
		 * labels.
		 */
		LIST_FOREACH(cp, &provider->lg_consumers, lg_consumer) {
			pp = cp->lg_provider;
			if (strcmp(pp->lg_geom->lg_class->lg_name, G_LABEL) != 0)
				continue;
			if (strcmp(g_device_path(pp->lg_name), mnt[i].f_mntfromname) == 0)
				goto break2;
		}
		/* Not the one, try the next mount point */
	}
break2:

	/*
	 * No mountpoint found, no absolute path possible
	 */
	if (i >= n)
		goto errout;

	/*
	 * Construct absolute path and we're finally done.
	 */
	if (strcmp(mnt[i].f_mntonname, "/") == 0)
		asprintf(abspath, "/%s", *relpath);
	else
		asprintf(abspath, "%s/%s", mnt[i].f_mntonname, *relpath);

errout:
	if (rv != 0) {
		free(*dev);
		*dev = NULL;
		free(*relpath);
		*relpath = NULL;
	}
	return (rv);
}

/*
 * Translate the passed in device_path to a unix path via the following
 * algorithm.
 *
 * If dp, dev or path NULL, return EDOOFUS. XXX wise?
 *
 * Set *path = NULL; *dev = NULL;
 *
 * Walk through the device_path until we find either a media device path.
 * Return EINVAL if not found. Return EINVAL if walking dp would
 * land us more than sanity size away from the start (4k).
 *
 * If we find a media descriptor, we search through the geom mesh to see if we
 * can find a matching node. If no match is found in the mesh that matches,
 * return ENXIO.
 *
 * Once we find a matching node, we search to see if there is a filesystem
 * mounted on it. If we find nothing, then search each of the devices that are
 * mounted to see if we can work up the geom tree to find the matching node. if
 * we still can't find anything, *dev = sprintf("/dev/%s", provider_name
 * of the original node we found), but return ENOTBLK.
 *
 * Record the dev of the mountpoint in *dev.
 *
 * Once we find something, check to see if the next node in the device path is
 * the end of list. If so, return the mountpoint.
 *
 * If the next node isn't a File path node, return EFTYPE.
 *
 * Extract the path from the File path node(s). translate any \ file separators
 * to /. Append the result to the mount point. Copy the resulting path into
 * *path.  Stat that path. If it is not found, return the errorr from stat.
 *
 * Finally, check to make sure the resulting path is still on the same
 * device. If not, return ENODEV.
 *
 * Otherwise return 0.
 *
 * The dev or full path that's returned is malloced, so needs to be freed when
 * the caller is done about it. Unlike many other functions, we can return data
 * with an error code, so pay attention.
 */
int
efivar_device_path_to_unix_path(const_efidp dp, char **dev, char **relpath, char **abspath)
{
	const_efidp walker;
	struct gmesh mesh;
	int rv = 0;

	/*
	 * Sanity check args, fail early
	 */
	if (dp == NULL || dev == NULL || relpath == NULL || abspath == NULL)
		return (EDOOFUS);

	*dev = NULL;
	*relpath = NULL;
	*abspath = NULL;

	/*
	 * Find the first media device path we can. If we go too far,
	 * assume the passed in device path is bogus. If we hit the end
	 * then we didn't find a media device path, so signal that error.
	 */
	walker = dp;
	while (DevicePathType(walker) != MEDIA_DEVICE_PATH &&
	    DevicePathType(walker) != END_DEVICE_PATH_TYPE) {
		walker = (const_efidp)NextDevicePathNode(walker);
		if ((uintptr_t)walker - (uintptr_t)dp > MAX_DP_SANITY)
			return (EINVAL);
	}
	if (DevicePathType(walker) !=  MEDIA_DEVICE_PATH)
		return (EINVAL);

	/*
	 * There's several types of media paths. We're only interested in the
	 * hard disk path, as it's really the only relevant one to booting. The
	 * CD path just might also be relevant, and would be easy to add, but
	 * isn't supported. A file path too is relevant, but at this stage, it's
	 * premature because we're trying to translate a specification for a device
	 * and path on that device into a unix path, or at the very least, a
	 * geom device : path-on-device.
	 *
	 * Also, ZFS throws a bit of a monkey wrench in here since it doesn't have
	 * a device path type (it creates a new virtual device out of one or more
	 * storage devices).
	 *
	 * For all of them, we'll need to know the geoms, so allocate / free the
	 * geom mesh here since it's safer than doing it in each sub-function
	 * which may have many error exits.
	 */
	if (geom_gettree(&mesh))
		return (ENOMEM);

	rv = EINVAL;
	if (DevicePathSubType(walker) == MEDIA_HARDDRIVE_DP)
		rv = efi_hd_to_unix(&mesh, walker, dev, relpath, abspath);
#ifdef notyet
	else if (is_cdrom_device(walker))
		rv = efi_cdrom_to_unix(&mesh, walker, dev, relpath, abspath);
	else if (is_floppy_device(walker))
		rv = efi_floppy_to_unix(&mesh, walker, dev, relpath, abspath);
	else if (is_zpool_device(walker))
		rv = efi_zpool_to_unix(&mesh, walker, dev, relpath, abspath);
#endif
	geom_deletetree(&mesh);

	return (rv);
}

/*
 * Construct the EFI path to a current unix path as follows.
 *
 * The path may be of one of three forms:
 *	1) /path/to/file -- full path to a file. The file need not be present,
 *		but /path/to must be. It must reside on a local filesystem
 *		mounted on a GPT or MBR partition.
 *	2) //path/to/file -- Shorthand for 'On the EFI partition, \path\to\file'
 *		where 'The EFI Partition' is a partiton that's type is 'efi'
 *		on the same disk that / is mounted from. If there are multiple
 *		or no 'efi' parittions on that disk, or / isn't on a disk that
 *		we can trace back to a physical device, an error will result
 *	3) [/dev/]geom-name:/path/to/file -- Use the specified partition
 *		(and it must be a GPT or MBR partition) with the specified
 *		path. The latter is not authenticated.
 * all path forms translate any \ characters to / before further processing.
 * When a file path node is created, all / characters are translated back
 * to \.
 *
 * For paths of the first form:
 *	find where the filesystem is mount (either the file directly, or
 *		its parent directory).
 *	translate any logical device name (eg lable) to a physical one
 *	If not possible, return ENXIO
 *	If the physical path is unsupported (Eg not on a GPT or MBR disk),
 *		return ENXIO
 *	Create a media device path node.
 *	append the relative path from the mountpoint to the media device node
 * 		as a file path.
 *
 * For paths matching the second form:
 *	find the EFI partition corresponding to the root fileystem.
 *	If none found, return ENXIO
 *	Create a media device path node for the found partition
 *	Append a File Path to the end for the rest of the file.
 *
 * For paths of the third form
 *	Translate the geom-name passed in into a physical partition
 *		name.
 *	Return ENXIO if the translation fails
 *	Make a media device path for it
 *	append the part after the : as a File path node.
 */

static char *
path_to_file_dp(const char *relpath)
{
	char *rv;

	asprintf(&rv, "File(%s)", relpath);
	return rv;
}

static char *
find_geom_efi_on_root(struct gmesh *mesh)
{
	struct statfs buf;
	const char *dev;
	struct gprovider *pp;
//	struct ggeom *disk;
	struct gconsumer *cp;

	/*
	 * Find /'s geom. Assume it's mounted on /dev/ and filter out all the
	 * filesystems that aren't.
	 */
	if (statfs("/", &buf) != 0)
		return (NULL);
	dev = buf.f_mntfromname;
	if (*dev != '/' || strncmp(dev, _PATH_DEV, sizeof(_PATH_DEV) - 1) != 0)
		return (NULL);
	dev += sizeof(_PATH_DEV) -1;
	pp = find_provider_by_name(mesh, dev);
	if (pp == NULL)
		return (NULL);

	/*
	 * If the provider is a LABEL, find it's outer PART class, if any. We
	 * only operate on partitions.
	 */
	if (strcmp(pp->lg_geom->lg_class->lg_name, G_LABEL) == 0) {
		LIST_FOREACH(cp, &pp->lg_consumers, lg_consumer) {
			if (strcmp(cp->lg_provider->lg_geom->lg_class->lg_name, G_PART) == 0) {
				pp = cp->lg_provider;
				break;
			}
		}
	}
	if (strcmp(pp->lg_geom->lg_class->lg_name, G_PART) != 0)
		return (NULL);

#if 0
	/* This doesn't work because we can't get the data to walk UP the tree it seems */

	/*
	 * Now that we've found the PART that we have mounted as root, find the
	 * first efi typed partition that's a peer, if any.
	 */
	LIST_FOREACH(cp, &pp->lg_consumers, lg_consumer) {
		if (strcmp(cp->lg_provider->lg_geom->lg_class->lg_name, G_DISK) == 0) {
			disk = cp->lg_provider->lg_geom;
			break;
		}
	}
	if (disk == NULL)	/* This is very bad -- old nested partitions -- no support ? */
		return (NULL);
#endif

#if 0
	/* This doesn't work because we can't get the data to walk UP the tree it seems */

	/*
	 * With the disk provider, we can look for its consumers to see if any are the proper type.
	 */
	LIST_FOREACH(pp, &disk->lg_consumer, lg_consumer) {
		type = geom_pp_attr(mesh, pp, "type");
		if (type == NULL)
			continue;
		if (strcmp(type, "efi") != 0)
			continue;
		efimedia = geom_pp_attr(mesh, pp, "efimedia");
		if (efimedia == NULL)
			return (NULL);
		return strdup(efimedia);
	}
#endif
	return (NULL);
}


static char *
find_geom_efimedia(struct gmesh *mesh, const char *dev)
{
	struct gprovider *pp;
	const char *efimedia;

	pp = find_provider_by_name(mesh, dev);
	if (pp == NULL)
		return (NULL);
	efimedia = geom_pp_attr(mesh, pp, "efimedia");
	if (efimedia == NULL)
		return (NULL);
	return strdup(efimedia);
}

static int
build_dp(const char *efimedia, const char *relpath, efidp *dp)
{
	char *fp, *dptxt = NULL, *cp, *rp;
	int rv = 0;
	efidp out = NULL;
	size_t len;

	rp = strdup(relpath);
	for (cp = rp; *cp; cp++)
		if (*cp == '/')
			*cp = '\\';
	fp = path_to_file_dp(rp);
	free(rp);
	if (fp == NULL) {
		rv = ENOMEM;
		goto errout;
	}

	asprintf(&dptxt, "%s/%s", efimedia, fp);
	out = malloc(8192);
	len = efidp_parse_device_path(dptxt, out, 8192);
	if (len > 8192) {
		rv = ENOMEM;
		goto errout;
	}
	if (len == 0) {
		rv = EINVAL;
		goto errout;
	}

	*dp = out;
errout:
	if (rv) {
		free(out);
	}
	free(dptxt);
	free(fp);

	return rv;
}

/* Handles //path/to/file */
/*
 * Which means: find the disk that has /. Then look for a EFI partition
 * and use that for the efimedia and /path/to/file as relative to that.
 * Not sure how ZFS will work here since we can't easily make the leap
 * to the geom from the zpool.
 */
static int
efipart_to_dp(struct gmesh *mesh, char *path, efidp *dp)
{
	char *efimedia = NULL;
	int rv;

	efimedia = find_geom_efi_on_root(mesh);
#ifdef notyet
	if (efimedia == NULL)
		efimedia = find_efi_on_zfsroot(dev);
#endif
	if (efimedia == NULL) {
		rv = ENOENT;
		goto errout;
	}

	rv = build_dp(efimedia, path + 1, dp);
errout:
	free(efimedia);

	return rv;
}

/* Handles [/dev/]geom:[/]path/to/file */
/* Handles zfs-dataset:[/]path/to/file (this may include / ) */
static int
dev_path_to_dp(struct gmesh *mesh, char *path, efidp *dp)
{
	char *relpath, *dev, *efimedia = NULL;
	int rv = 0;

	relpath = strchr(path, ':');
	assert(relpath != NULL);
	*relpath++ = '\0';

	dev = path;
	if (strncmp(dev, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		dev += sizeof(_PATH_DEV) -1;

	efimedia = find_geom_efimedia(mesh, dev);
#ifdef notyet
	if (efimedia == NULL)
		find_zfs_efi_media(dev);
#endif
	if (efimedia == NULL) {
		rv = ENOENT;
		goto errout;
	}
	rv = build_dp(efimedia, relpath, dp);
errout:
	free(efimedia);

	return rv;
}

/* Handles /path/to/file */
static int
path_to_dp(struct gmesh *mesh, char *path, efidp *dp)
{
	struct statfs buf;
	char *rp = NULL, *ep, *dev, *efimedia = NULL;
	int rv = 0;

	rp = realpath(path, NULL);
	if (rp == NULL) {
		rv = errno;
		goto errout;
	}

	if (statfs(rp, &buf) != 0) {
		rv = errno;
		goto errout;
	}

	dev = buf.f_mntfromname;
	if (strncmp(dev, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		dev += sizeof(_PATH_DEV) -1;
	ep = rp + strlen(buf.f_mntonname);

	efimedia = find_geom_efimedia(mesh, dev);
#ifdef notyet
	if (efimedia == NULL)
		find_zfs_efi_media(dev);
#endif
	if (efimedia == NULL) {
		rv = ENOENT;
		goto errout;
	}

	rv = build_dp(efimedia, ep, dp);
errout:
	free(efimedia);
	free(rp);
	if (rv != 0) {
		free(*dp);
		*dp = NULL;
	}
	return (rv);
}

int
efivar_unix_path_to_device_path(const char *path, efidp *dp)
{
	char *modpath = NULL, *cp;
	int rv = ENOMEM;
	struct gmesh mesh;

	/*
	 * Fail early for clearly bogus things
	 */
	if (path == NULL || dp == NULL)
		return (EDOOFUS);

	/*
	 * We'll need the goem mesh to grovel through it to find the
	 * efimedia attribute for any devices we find. Grab it here
	 * and release it to simplify the error paths out of the
	 * subordinate functions
	 */
	if (geom_gettree(&mesh))
		return (errno);

	/*
	 * Convert all \ to /. We'll convert them back again when
	 * we encode the file. Boot loaders are expected to cope.
	 */
	modpath = strdup(path);
	if (modpath == NULL)
		goto out;
	for (cp = modpath; *cp; cp++)
		if (*cp == '\\')
			*cp = '/';

	if (modpath[0] == '/' && modpath[1] == '/')	/* Handle //foo/bar/baz */
		rv = efipart_to_dp(&mesh, modpath, dp);
	else if (strchr(modpath, ':'))			/* Handle dev:/bar/baz */
		rv = dev_path_to_dp(&mesh, modpath, dp);
	else						/* Handle /a/b/c */
		rv = path_to_dp(&mesh, modpath, dp);

out:
	geom_deletetree(&mesh);
	free(modpath);

	return (rv);
}
