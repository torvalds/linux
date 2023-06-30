// SPDX-License-Identifier: GPL-2.0-only

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>
#include "overlayfs.h"

static ssize_t ovl_parse_param_split_lowerdirs(char *str)
{
	ssize_t nr_layers = 1, nr_colons = 0;
	char *s, *d;

	for (s = d = str;; s++, d++) {
		if (*s == '\\') {
			s++;
		} else if (*s == ':') {
			bool next_colon = (*(s + 1) == ':');

			nr_colons++;
			if (nr_colons == 2 && next_colon) {
				pr_err("only single ':' or double '::' sequences of unescaped colons in lowerdir mount option allowed.\n");
				return -EINVAL;
			}
			/* count layers, not colons */
			if (!next_colon)
				nr_layers++;

			*d = '\0';
			continue;
		}

		*d = *s;
		if (!*s) {
			/* trailing colons */
			if (nr_colons) {
				pr_err("unescaped trailing colons in lowerdir mount option.\n");
				return -EINVAL;
			}
			break;
		}
		nr_colons = 0;
	}

	return nr_layers;
}

static int ovl_mount_dir_noesc(const char *name, struct path *path)
{
	int err = -EINVAL;

	if (!*name) {
		pr_err("empty lowerdir\n");
		goto out;
	}
	err = kern_path(name, LOOKUP_FOLLOW, path);
	if (err) {
		pr_err("failed to resolve '%s': %i\n", name, err);
		goto out;
	}
	err = -EINVAL;
	if (ovl_dentry_weird(path->dentry)) {
		pr_err("filesystem on '%s' not supported\n", name);
		goto out_put;
	}
	if (!d_is_dir(path->dentry)) {
		pr_err("'%s' not a directory\n", name);
		goto out_put;
	}
	return 0;

out_put:
	path_put_init(path);
out:
	return err;
}

static void ovl_unescape(char *s)
{
	char *d = s;

	for (;; s++, d++) {
		if (*s == '\\')
			s++;
		*d = *s;
		if (!*s)
			break;
	}
}

static int ovl_mount_dir(const char *name, struct path *path)
{
	int err = -ENOMEM;
	char *tmp = kstrdup(name, GFP_KERNEL);

	if (tmp) {
		ovl_unescape(tmp);
		err = ovl_mount_dir_noesc(tmp, path);

		if (!err && path->dentry->d_flags & DCACHE_OP_REAL) {
			pr_err("filesystem on '%s' not supported as upperdir\n",
			       tmp);
			path_put_init(path);
			err = -EINVAL;
		}
		kfree(tmp);
	}
	return err;
}

int ovl_parse_param_upperdir(const char *name, struct fs_context *fc,
			     bool workdir)
{
	int err;
	struct ovl_fs *ofs = fc->s_fs_info;
	struct ovl_config *config = &ofs->config;
	struct ovl_fs_context *ctx = fc->fs_private;
	struct path path;
	char *dup;

	err = ovl_mount_dir(name, &path);
	if (err)
		return err;

	/*
	 * Check whether upper path is read-only here to report failures
	 * early. Don't forget to recheck when the superblock is created
	 * as the mount attributes could change.
	 */
	if (__mnt_is_readonly(path.mnt)) {
		path_put(&path);
		return -EINVAL;
	}

	dup = kstrdup(name, GFP_KERNEL);
	if (!dup) {
		path_put(&path);
		return -ENOMEM;
	}

	if (workdir) {
		kfree(config->workdir);
		config->workdir = dup;
		path_put(&ctx->work);
		ctx->work = path;
	} else {
		kfree(config->upperdir);
		config->upperdir = dup;
		path_put(&ctx->upper);
		ctx->upper = path;
	}
	return 0;
}

void ovl_parse_param_drop_lowerdir(struct ovl_fs_context *ctx)
{
	for (size_t nr = 0; nr < ctx->nr; nr++) {
		path_put(&ctx->lower[nr].path);
		kfree(ctx->lower[nr].name);
		ctx->lower[nr].name = NULL;
	}
	ctx->nr = 0;
	ctx->nr_data = 0;
}

/*
 * Parse lowerdir= mount option:
 *
 * (1) lowerdir=/lower1:/lower2:/lower3::/data1::/data2
 *     Set "/lower1", "/lower2", and "/lower3" as lower layers and
 *     "/data1" and "/data2" as data lower layers. Any existing lower
 *     layers are replaced.
 * (2) lowerdir=:/lower4
 *     Append "/lower4" to current stack of lower layers. This requires
 *     that there already is at least one lower layer configured.
 * (3) lowerdir=::/lower5
 *     Append data "/lower5" as data lower layer. This requires that
 *     there's at least one regular lower layer present.
 */
int ovl_parse_param_lowerdir(const char *name, struct fs_context *fc)
{
	int err;
	struct ovl_fs_context *ctx = fc->fs_private;
	struct ovl_fs_context_layer *l;
	char *dup = NULL, *dup_iter;
	ssize_t nr_lower = 0, nr = 0, nr_data = 0;
	bool append = false, data_layer = false;

	/*
	 * Ensure we're backwards compatible with mount(2)
	 * by allowing relative paths.
	 */

	/* drop all existing lower layers */
	if (!*name) {
		ovl_parse_param_drop_lowerdir(ctx);
		return 0;
	}

	if (strncmp(name, "::", 2) == 0) {
		/*
		 * This is a data layer.
		 * There must be at least one regular lower layer
		 * specified.
		 */
		if (ctx->nr == 0) {
			pr_err("data lower layers without regular lower layers not allowed");
			return -EINVAL;
		}

		/* Skip the leading "::". */
		name += 2;
		data_layer = true;
		/*
		 * A data layer is automatically an append as there
		 * must've been at least one regular lower layer.
		 */
		append = true;
	} else if (*name == ':') {
		/*
		 * This is a regular lower layer.
		 * If users want to append a layer enforce that they
		 * have already specified a first layer before. It's
		 * better to be strict.
		 */
		if (ctx->nr == 0) {
			pr_err("cannot append layer if no previous layer has been specified");
			return -EINVAL;
		}

		/*
		 * Once a sequence of data layers has started regular
		 * lower layers are forbidden.
		 */
		if (ctx->nr_data > 0) {
			pr_err("regular lower layers cannot follow data lower layers");
			return -EINVAL;
		}

		/* Skip the leading ":". */
		name++;
		append = true;
	}

	dup = kstrdup(name, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	err = -EINVAL;
	nr_lower = ovl_parse_param_split_lowerdirs(dup);
	if (nr_lower < 0)
		goto out_err;

	if ((nr_lower > OVL_MAX_STACK) ||
	    (append && (size_add(ctx->nr, nr_lower) > OVL_MAX_STACK))) {
		pr_err("too many lower directories, limit is %d\n", OVL_MAX_STACK);
		goto out_err;
	}

	if (!append)
		ovl_parse_param_drop_lowerdir(ctx);

	/*
	 * (1) append
	 *
	 * We want nr <= nr_lower <= capacity We know nr > 0 and nr <=
	 * capacity. If nr == 0 this wouldn't be append. If nr +
	 * nr_lower is <= capacity then nr <= nr_lower <= capacity
	 * already holds. If nr + nr_lower exceeds capacity, we realloc.
	 *
	 * (2) replace
	 *
	 * Ensure we're backwards compatible with mount(2) which allows
	 * "lowerdir=/a:/b:/c,lowerdir=/d:/e:/f" causing the last
	 * specified lowerdir mount option to win.
	 *
	 * We want nr <= nr_lower <= capacity We know either (i) nr == 0
	 * or (ii) nr > 0. We also know nr_lower > 0. The capacity
	 * could've been changed multiple times already so we only know
	 * nr <= capacity. If nr + nr_lower > capacity we realloc,
	 * otherwise nr <= nr_lower <= capacity holds already.
	 */
	nr_lower += ctx->nr;
	if (nr_lower > ctx->capacity) {
		err = -ENOMEM;
		l = krealloc_array(ctx->lower, nr_lower, sizeof(*ctx->lower),
				   GFP_KERNEL_ACCOUNT);
		if (!l)
			goto out_err;

		ctx->lower = l;
		ctx->capacity = nr_lower;
	}

	/*
	 *   (3) By (1) and (2) we know nr <= nr_lower <= capacity.
	 *   (4) If ctx->nr == 0 => replace
	 *       We have verified above that the lowerdir mount option
	 *       isn't an append, i.e., the lowerdir mount option
	 *       doesn't start with ":" or "::".
	 * (4.1) The lowerdir mount options only contains regular lower
	 *       layers ":".
	 *       => Nothing to verify.
	 * (4.2) The lowerdir mount options contains regular ":" and
	 *       data "::" layers.
	 *       => We need to verify that data lower layers "::" aren't
	 *          followed by regular ":" lower layers
	 *   (5) If ctx->nr > 0 => append
	 *       We know that there's at least one regular layer
	 *       otherwise we would've failed when parsing the previous
	 *       lowerdir mount option.
	 * (5.1) The lowerdir mount option is a regular layer ":" append
	 *       => We need to verify that no data layers have been
	 *          specified before.
	 * (5.2) The lowerdir mount option is a data layer "::" append
	 *       We know that there's at least one regular layer or
	 *       other data layers. => There's nothing to verify.
	 */
	dup_iter = dup;
	for (nr = ctx->nr; nr < nr_lower; nr++) {
		l = &ctx->lower[nr];
		memset(l, 0, sizeof(*l));

		err = ovl_mount_dir_noesc(dup_iter, &l->path);
		if (err)
			goto out_put;

		err = -ENOMEM;
		l->name = kstrdup(dup_iter, GFP_KERNEL_ACCOUNT);
		if (!l->name)
			goto out_put;

		if (data_layer)
			nr_data++;

		/* Calling strchr() again would overrun. */
		if ((nr + 1) == nr_lower)
			break;

		err = -EINVAL;
		dup_iter = strchr(dup_iter, '\0') + 1;
		if (*dup_iter) {
			/*
			 * This is a regular layer so we require that
			 * there are no data layers.
			 */
			if ((ctx->nr_data + nr_data) > 0) {
				pr_err("regular lower layers cannot follow data lower layers");
				goto out_put;
			}

			data_layer = false;
			continue;
		}

		/* This is a data lower layer. */
		data_layer = true;
		dup_iter++;
	}
	ctx->nr = nr_lower;
	ctx->nr_data += nr_data;
	kfree(dup);
	return 0;

out_put:
	/*
	 * We know nr >= ctx->nr < nr_lower. If we failed somewhere
	 * we want to undo until nr == ctx->nr. This is correct for
	 * both ctx->nr == 0 and ctx->nr > 0.
	 */
	for (; nr >= ctx->nr; nr--) {
		l = &ctx->lower[nr];
		kfree(l->name);
		l->name = NULL;
		path_put(&l->path);

		/* don't overflow */
		if (nr == 0)
			break;
	}

out_err:
	kfree(dup);

	/* Intentionally don't realloc to a smaller size. */
	return err;
}
