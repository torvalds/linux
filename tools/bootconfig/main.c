// SPDX-License-Identifier: GPL-2.0
/*
 * Boot config tool for initrd image
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <endian.h>

#include <linux/bootconfig.h>

#define pr_err(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

static int xbc_show_value(struct xbc_node *node, bool semicolon)
{
	const char *val, *eol;
	char q;
	int i = 0;

	eol = semicolon ? ";\n" : "\n";
	xbc_array_for_each_value(node, val) {
		if (strchr(val, '"'))
			q = '\'';
		else
			q = '"';
		printf("%c%s%c%s", q, val, q, xbc_node_is_array(node) ? ", " : eol);
		i++;
	}
	return i;
}

static void xbc_show_compact_tree(void)
{
	struct xbc_node *node, *cnode = NULL, *vnode;
	int depth = 0, i;

	node = xbc_root_node();
	while (node && xbc_node_is_key(node)) {
		for (i = 0; i < depth; i++)
			printf("\t");
		if (!cnode)
			cnode = xbc_node_get_child(node);
		while (cnode && xbc_node_is_key(cnode) && !cnode->next) {
			vnode = xbc_node_get_child(cnode);
			/*
			 * If @cnode has value and subkeys, this
			 * should show it as below.
			 *
			 * key(@node) {
			 *      key(@cnode) = value;
			 *      key(@cnode) {
			 *          subkeys;
			 *      }
			 * }
			 */
			if (vnode && xbc_node_is_value(vnode) && vnode->next)
				break;
			printf("%s.", xbc_node_get_data(node));
			node = cnode;
			cnode = vnode;
		}
		if (cnode && xbc_node_is_key(cnode)) {
			printf("%s {\n", xbc_node_get_data(node));
			depth++;
			node = cnode;
			cnode = NULL;
			continue;
		} else if (cnode && xbc_node_is_value(cnode)) {
			printf("%s = ", xbc_node_get_data(node));
			xbc_show_value(cnode, true);
			/*
			 * If @node has value and subkeys, continue
			 * looping on subkeys with same node.
			 */
			if (cnode->next) {
				cnode = xbc_node_get_next(cnode);
				continue;
			}
		} else {
			printf("%s;\n", xbc_node_get_data(node));
		}
		cnode = NULL;

		if (node->next) {
			node = xbc_node_get_next(node);
			continue;
		}
		while (!node->next) {
			node = xbc_node_get_parent(node);
			if (!node)
				return;
			if (!xbc_node_get_child(node)->next)
				continue;
			if (depth) {
				depth--;
				for (i = 0; i < depth; i++)
					printf("\t");
				printf("}\n");
			}
		}
		node = xbc_node_get_next(node);
	}
}

static void xbc_show_list(void)
{
	char key[XBC_KEYLEN_MAX];
	struct xbc_node *leaf;
	const char *val;
	int ret;

	xbc_for_each_key_value(leaf, val) {
		ret = xbc_node_compose_key(leaf, key, XBC_KEYLEN_MAX);
		if (ret < 0) {
			fprintf(stderr, "Failed to compose key %d\n", ret);
			break;
		}
		printf("%s = ", key);
		if (!val || val[0] == '\0') {
			printf("\"\"\n");
			continue;
		}
		xbc_show_value(xbc_node_get_child(leaf), false);
	}
}

#define PAGE_SIZE	4096

static int load_xbc_fd(int fd, char **buf, int size)
{
	int ret;

	*buf = malloc(size + 1);
	if (!*buf)
		return -ENOMEM;

	ret = read(fd, *buf, size);
	if (ret < 0)
		return -errno;
	(*buf)[size] = '\0';

	return ret;
}

/* Return the read size or -errno */
static int load_xbc_file(const char *path, char **buf)
{
	struct stat stat;
	int fd, ret;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	ret = fstat(fd, &stat);
	if (ret < 0)
		return -errno;

	ret = load_xbc_fd(fd, buf, stat.st_size);

	close(fd);

	return ret;
}

static int pr_errno(const char *msg, int err)
{
	pr_err("%s: %d\n", msg, err);
	return err;
}

static int load_xbc_from_initrd(int fd, char **buf)
{
	struct stat stat;
	int ret;
	uint32_t size = 0, csum = 0, rcsum;
	char magic[BOOTCONFIG_MAGIC_LEN];
	const char *msg;

	ret = fstat(fd, &stat);
	if (ret < 0)
		return -errno;

	if (stat.st_size < 8 + BOOTCONFIG_MAGIC_LEN)
		return 0;

	if (lseek(fd, -BOOTCONFIG_MAGIC_LEN, SEEK_END) < 0)
		return pr_errno("Failed to lseek for magic", -errno);

	if (read(fd, magic, BOOTCONFIG_MAGIC_LEN) < 0)
		return pr_errno("Failed to read", -errno);

	/* Check the bootconfig magic bytes */
	if (memcmp(magic, BOOTCONFIG_MAGIC, BOOTCONFIG_MAGIC_LEN) != 0)
		return 0;

	if (lseek(fd, -(8 + BOOTCONFIG_MAGIC_LEN), SEEK_END) < 0)
		return pr_errno("Failed to lseek for size", -errno);

	if (read(fd, &size, sizeof(uint32_t)) < 0)
		return pr_errno("Failed to read size", -errno);
	size = le32toh(size);

	if (read(fd, &csum, sizeof(uint32_t)) < 0)
		return pr_errno("Failed to read checksum", -errno);
	csum = le32toh(csum);

	/* Wrong size error  */
	if (stat.st_size < size + 8 + BOOTCONFIG_MAGIC_LEN) {
		pr_err("bootconfig size is too big\n");
		return -E2BIG;
	}

	if (lseek(fd, stat.st_size - (size + 8 + BOOTCONFIG_MAGIC_LEN),
		  SEEK_SET) < 0)
		return pr_errno("Failed to lseek", -errno);

	ret = load_xbc_fd(fd, buf, size);
	if (ret < 0)
		return ret;

	/* Wrong Checksum */
	rcsum = xbc_calc_checksum(*buf, size);
	if (csum != rcsum) {
		pr_err("checksum error: %d != %d\n", csum, rcsum);
		return -EINVAL;
	}

	ret = xbc_init(*buf, size, &msg, NULL);
	/* Wrong data */
	if (ret < 0) {
		pr_err("parse error: %s.\n", msg);
		return ret;
	}

	return size;
}

static void show_xbc_error(const char *data, const char *msg, int pos)
{
	int lin = 1, col, i;

	if (pos < 0) {
		pr_err("Error: %s.\n", msg);
		return;
	}

	/* Note that pos starts from 0 but lin and col should start from 1. */
	col = pos + 1;
	for (i = 0; i < pos; i++) {
		if (data[i] == '\n') {
			lin++;
			col = pos - i;
		}
	}
	pr_err("Parse Error: %s at %d:%d\n", msg, lin, col);

}

static int init_xbc_with_error(char *buf, int len)
{
	char *copy = strdup(buf);
	const char *msg;
	int ret, pos;

	if (!copy)
		return -ENOMEM;

	ret = xbc_init(buf, len, &msg, &pos);
	if (ret < 0)
		show_xbc_error(copy, msg, pos);
	free(copy);

	return ret;
}

static int show_xbc(const char *path, bool list)
{
	int ret, fd;
	char *buf = NULL;
	struct stat st;

	ret = stat(path, &st);
	if (ret < 0) {
		ret = -errno;
		pr_err("Failed to stat %s: %d\n", path, ret);
		return ret;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		ret = -errno;
		pr_err("Failed to open initrd %s: %d\n", path, ret);
		return ret;
	}

	ret = load_xbc_from_initrd(fd, &buf);
	close(fd);
	if (ret < 0) {
		pr_err("Failed to load a boot config from initrd: %d\n", ret);
		goto out;
	}
	/* Assume a bootconfig file if it is enough small */
	if (ret == 0 && st.st_size <= XBC_DATA_MAX) {
		ret = load_xbc_file(path, &buf);
		if (ret < 0) {
			pr_err("Failed to load a boot config: %d\n", ret);
			goto out;
		}
		if (init_xbc_with_error(buf, ret) < 0)
			goto out;
	}
	if (list)
		xbc_show_list();
	else
		xbc_show_compact_tree();
	ret = 0;
out:
	free(buf);

	return ret;
}

static int delete_xbc(const char *path)
{
	struct stat stat;
	int ret = 0, fd, size;
	char *buf = NULL;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		pr_err("Failed to open initrd %s: %d\n", path, ret);
		return ret;
	}

	size = load_xbc_from_initrd(fd, &buf);
	if (size < 0) {
		ret = size;
		pr_err("Failed to load a boot config from initrd: %d\n", ret);
	} else if (size > 0) {
		ret = fstat(fd, &stat);
		if (!ret)
			ret = ftruncate(fd, stat.st_size
					- size - 8 - BOOTCONFIG_MAGIC_LEN);
		if (ret)
			ret = -errno;
	} /* Ignore if there is no boot config in initrd */

	close(fd);
	free(buf);

	return ret;
}

static int apply_xbc(const char *path, const char *xbc_path)
{
	char *buf, *data, *p;
	size_t total_size;
	struct stat stat;
	const char *msg;
	uint32_t size, csum;
	int pos, pad;
	int ret, fd;

	ret = load_xbc_file(xbc_path, &buf);
	if (ret < 0) {
		pr_err("Failed to load %s : %d\n", xbc_path, ret);
		return ret;
	}
	size = strlen(buf) + 1;
	csum = xbc_calc_checksum(buf, size);

	/* Backup the bootconfig data */
	data = calloc(size + BOOTCONFIG_ALIGN +
		      sizeof(uint32_t) + sizeof(uint32_t) + BOOTCONFIG_MAGIC_LEN, 1);
	if (!data)
		return -ENOMEM;
	memcpy(data, buf, size);

	/* Check the data format */
	ret = xbc_init(buf, size, &msg, &pos);
	if (ret < 0) {
		show_xbc_error(data, msg, pos);
		free(data);
		free(buf);

		return ret;
	}
	printf("Apply %s to %s\n", xbc_path, path);
	xbc_get_info(&ret, NULL);
	printf("\tNumber of nodes: %d\n", ret);
	printf("\tSize: %u bytes\n", (unsigned int)size);
	printf("\tChecksum: %d\n", (unsigned int)csum);

	/* TODO: Check the options by schema */
	xbc_exit();
	free(buf);

	/* Remove old boot config if exists */
	ret = delete_xbc(path);
	if (ret < 0) {
		pr_err("Failed to delete previous boot config: %d\n", ret);
		free(data);
		return ret;
	}

	/* Apply new one */
	fd = open(path, O_RDWR | O_APPEND);
	if (fd < 0) {
		ret = -errno;
		pr_err("Failed to open %s: %d\n", path, ret);
		free(data);
		return ret;
	}
	/* TODO: Ensure the @path is initramfs/initrd image */
	if (fstat(fd, &stat) < 0) {
		ret = -errno;
		pr_err("Failed to get the size of %s\n", path);
		goto out;
	}

	/* To align up the total size to BOOTCONFIG_ALIGN, get padding size */
	total_size = stat.st_size + size + sizeof(uint32_t) * 2 + BOOTCONFIG_MAGIC_LEN;
	pad = ((total_size + BOOTCONFIG_ALIGN - 1) & (~BOOTCONFIG_ALIGN_MASK)) - total_size;
	size += pad;

	/* Add a footer */
	p = data + size;
	*(uint32_t *)p = htole32(size);
	p += sizeof(uint32_t);

	*(uint32_t *)p = htole32(csum);
	p += sizeof(uint32_t);

	memcpy(p, BOOTCONFIG_MAGIC, BOOTCONFIG_MAGIC_LEN);
	p += BOOTCONFIG_MAGIC_LEN;

	total_size = p - data;

	ret = write(fd, data, total_size);
	if (ret < total_size) {
		if (ret < 0)
			ret = -errno;
		pr_err("Failed to apply a boot config: %d\n", ret);
		if (ret >= 0)
			goto out_rollback;
	} else
		ret = 0;

out:
	close(fd);
	free(data);

	return ret;

out_rollback:
	/* Map the partial write to -ENOSPC */
	if (ret >= 0)
		ret = -ENOSPC;
	if (ftruncate(fd, stat.st_size) < 0) {
		ret = -errno;
		pr_err("Failed to rollback the write error: %d\n", ret);
		pr_err("The initrd %s may be corrupted. Recommend to rebuild.\n", path);
	}
	goto out;
}

static int usage(void)
{
	printf("Usage: bootconfig [OPTIONS] <INITRD>\n"
		"Or     bootconfig <CONFIG>\n"
		" Apply, delete or show boot config to initrd.\n"
		" Options:\n"
		"		-a <config>: Apply boot config to initrd\n"
		"		-d : Delete boot config file from initrd\n"
		"		-l : list boot config in initrd or file\n\n"
		" If no option is given, show the bootconfig in the given file.\n");
	return -1;
}

int main(int argc, char **argv)
{
	char *path = NULL;
	char *apply = NULL;
	bool delete = false, list = false;
	int opt;

	while ((opt = getopt(argc, argv, "hda:l")) != -1) {
		switch (opt) {
		case 'd':
			delete = true;
			break;
		case 'a':
			apply = optarg;
			break;
		case 'l':
			list = true;
			break;
		case 'h':
		default:
			return usage();
		}
	}

	if ((apply && delete) || (delete && list) || (apply && list)) {
		pr_err("Error: You can give one of -a, -d or -l at once.\n");
		return usage();
	}

	if (optind >= argc) {
		pr_err("Error: No initrd is specified.\n");
		return usage();
	}

	path = argv[optind];

	if (apply)
		return apply_xbc(path, apply);
	else if (delete)
		return delete_xbc(path);

	return show_xbc(path, list);
}
