/*	$OpenBSD: fdt.h,v 1.6 2020/07/18 09:44:59 kettenis Exp $	*/

/*
 * Copyright (c) 2009 Dariusz Swiderski <sfires@sfires.net>
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

struct fdt_head {
	u_int32_t fh_magic;
	u_int32_t fh_size;
	u_int32_t fh_struct_off;
	u_int32_t fh_strings_off;
	u_int32_t fh_reserve_off;
	u_int32_t fh_version;
	u_int32_t fh_comp_ver;     /* last compatible version */
	u_int32_t fh_boot_cpu_id;  /* fh_version >=2 */
	u_int32_t fh_strings_size; /* fh_version >=3 */
	u_int32_t fh_struct_size;  /* fh_version >=17 */
};

struct fdt {
	struct fdt_head *header;
	char		*tree;
	char		*strings;
	char		*memory;
	char		*end;
	int		version;
	int		strings_size;
	int		struct_size;
};

struct fdt_reg {
	uint64_t	addr;
	uint64_t	size;
};

#define FDT_MAGIC	0xd00dfeed
#define FDT_NODE_BEGIN	0x01
#define FDT_NODE_END	0x02
#define FDT_PROPERTY	0x03
#define FDT_NOP		0x04
#define FDT_END		0x09

#define FDT_CODE_VERSION 0x11

int	 fdt_init(void *);
void	 fdt_finalize(void);
size_t	 fdt_get_size(void *);
void	*fdt_next_node(void *);
void	*fdt_child_node(void *);
char	*fdt_node_name(void *);
void	*fdt_find_node(char *);
int	 fdt_node_property(void *, char *, char **);
int	 fdt_node_set_property(void *, char *, void *, int);
int	 fdt_node_add_property(void *, char *, void *, int);
void	*fdt_parent_node(void *);
void	*fdt_find_phandle(uint32_t);
int	 fdt_get_reg(void *, int, struct fdt_reg *);
int	 fdt_is_compatible(void *, const char *);
#ifdef DEBUG
void	*fdt_print_property(void *, int);
void 	 fdt_print_node(void *, int);
void	 fdt_print_tree(void);
#endif
