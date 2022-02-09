// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include "bpf_preload.h"
#include "iterators/iterators.lskel.h"

static struct bpf_link *maps_link, *progs_link;
static struct iterators_bpf *skel;

static void free_links_and_skel(void)
{
	if (!IS_ERR_OR_NULL(maps_link))
		bpf_link_put(maps_link);
	if (!IS_ERR_OR_NULL(progs_link))
		bpf_link_put(progs_link);
	iterators_bpf__destroy(skel);
}

static int preload(struct bpf_preload_info *obj)
{
	strlcpy(obj[0].link_name, "maps.debug", sizeof(obj[0].link_name));
	obj[0].link = maps_link;
	strlcpy(obj[1].link_name, "progs.debug", sizeof(obj[1].link_name));
	obj[1].link = progs_link;
	return 0;
}

static struct bpf_preload_ops ops = {
	.preload = preload,
	.owner = THIS_MODULE,
};

static int load_skel(void)
{
	int err;

	skel = iterators_bpf__open();
	if (!skel)
		return -ENOMEM;
	err = iterators_bpf__load(skel);
	if (err)
		goto out;
	err = iterators_bpf__attach(skel);
	if (err)
		goto out;
	maps_link = bpf_link_get_from_fd(skel->links.dump_bpf_map_fd);
	if (IS_ERR(maps_link)) {
		err = PTR_ERR(maps_link);
		goto out;
	}
	progs_link = bpf_link_get_from_fd(skel->links.dump_bpf_prog_fd);
	if (IS_ERR(progs_link)) {
		err = PTR_ERR(progs_link);
		goto out;
	}
	return 0;
out:
	free_links_and_skel();
	return err;
}

static int __init load(void)
{
	int err;

	err = load_skel();
	if (err)
		return err;
	bpf_preload_ops = &ops;
	return err;
}

static void __exit fini(void)
{
	bpf_preload_ops = NULL;
	free_links_and_skel();
}
late_initcall(load);
module_exit(fini);
MODULE_LICENSE("GPL");
