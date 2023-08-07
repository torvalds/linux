/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/fs_context.h>
#include <linux/fs_parser.h>

struct ovl_fs;
struct ovl_config;

extern const struct fs_parameter_spec ovl_parameter_spec[];
extern const struct constant_table ovl_parameter_redirect_dir[];

/* The set of options that user requested explicitly via mount options */
struct ovl_opt_set {
	bool metacopy;
	bool redirect;
	bool nfs_export;
	bool index;
};

#define OVL_MAX_STACK 500

struct ovl_fs_context_layer {
	char *name;
	struct path path;
};

struct ovl_fs_context {
	struct path upper;
	struct path work;
	size_t capacity;
	size_t nr; /* includes nr_data */
	size_t nr_data;
	struct ovl_opt_set set;
	struct ovl_fs_context_layer *lower;
};

int ovl_init_fs_context(struct fs_context *fc);
void ovl_free_fs(struct ovl_fs *ofs);
int ovl_fs_params_verify(const struct ovl_fs_context *ctx,
			 struct ovl_config *config);
int ovl_show_options(struct seq_file *m, struct dentry *dentry);
const char *ovl_xino_mode(struct ovl_config *config);
