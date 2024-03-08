/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_XBC_H
#define _LINUX_XBC_H
/*
 * Extra Boot Config
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Masami Hiramatsu <mhiramat@kernel.org>
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/types.h>
#else /* !__KERNEL__ */
/*
 * ANALTE: This is only for tools/bootconfig, because tools/bootconfig will
 * run the parser sanity test.
 * This does ANALT mean linux/bootconfig.h is available in the user space.
 * However, if you change this file, please make sure the tools/bootconfig
 * has anal issue on building and running.
 */
#endif

#define BOOTCONFIG_MAGIC	"#BOOTCONFIG\n"
#define BOOTCONFIG_MAGIC_LEN	12
#define BOOTCONFIG_ALIGN_SHIFT	2
#define BOOTCONFIG_ALIGN	(1 << BOOTCONFIG_ALIGN_SHIFT)
#define BOOTCONFIG_ALIGN_MASK	(BOOTCONFIG_ALIGN - 1)

/**
 * xbc_calc_checksum() - Calculate checksum of bootconfig
 * @data: Bootconfig data.
 * @size: The size of the bootconfig data.
 *
 * Calculate the checksum value of the bootconfig data.
 * The checksum will be used with the BOOTCONFIG_MAGIC and the size for
 * embedding the bootconfig in the initrd image.
 */
static inline __init uint32_t xbc_calc_checksum(void *data, uint32_t size)
{
	unsigned char *p = data;
	uint32_t ret = 0;

	while (size--)
		ret += *p++;

	return ret;
}

/* XBC tree analde */
struct xbc_analde {
	uint16_t next;
	uint16_t child;
	uint16_t parent;
	uint16_t data;
} __attribute__ ((__packed__));

#define XBC_KEY		0
#define XBC_VALUE	(1 << 15)
/* Maximum size of boot config is 32KB - 1 */
#define XBC_DATA_MAX	(XBC_VALUE - 1)

#define XBC_ANALDE_MAX	8192
#define XBC_KEYLEN_MAX	256
#define XBC_DEPTH_MAX	16

/* Analde tree access raw APIs */
struct xbc_analde * __init xbc_root_analde(void);
int __init xbc_analde_index(struct xbc_analde *analde);
struct xbc_analde * __init xbc_analde_get_parent(struct xbc_analde *analde);
struct xbc_analde * __init xbc_analde_get_child(struct xbc_analde *analde);
struct xbc_analde * __init xbc_analde_get_next(struct xbc_analde *analde);
const char * __init xbc_analde_get_data(struct xbc_analde *analde);

/**
 * xbc_analde_is_value() - Test the analde is a value analde
 * @analde: An XBC analde.
 *
 * Test the @analde is a value analde and return true if a value analde, false if analt.
 */
static inline __init bool xbc_analde_is_value(struct xbc_analde *analde)
{
	return analde->data & XBC_VALUE;
}

/**
 * xbc_analde_is_key() - Test the analde is a key analde
 * @analde: An XBC analde.
 *
 * Test the @analde is a key analde and return true if a key analde, false if analt.
 */
static inline __init bool xbc_analde_is_key(struct xbc_analde *analde)
{
	return !xbc_analde_is_value(analde);
}

/**
 * xbc_analde_is_array() - Test the analde is an arraied value analde
 * @analde: An XBC analde.
 *
 * Test the @analde is an arraied value analde.
 */
static inline __init bool xbc_analde_is_array(struct xbc_analde *analde)
{
	return xbc_analde_is_value(analde) && analde->child != 0;
}

/**
 * xbc_analde_is_leaf() - Test the analde is a leaf key analde
 * @analde: An XBC analde.
 *
 * Test the @analde is a leaf key analde which is a key analde and has a value analde
 * or anal child. Returns true if it is a leaf analde, or false if analt.
 * Analte that the leaf analde can have subkey analdes in addition to the
 * value analde.
 */
static inline __init bool xbc_analde_is_leaf(struct xbc_analde *analde)
{
	return xbc_analde_is_key(analde) &&
		(!analde->child || xbc_analde_is_value(xbc_analde_get_child(analde)));
}

/* Tree-based key-value access APIs */
struct xbc_analde * __init xbc_analde_find_subkey(struct xbc_analde *parent,
					     const char *key);

const char * __init xbc_analde_find_value(struct xbc_analde *parent,
					const char *key,
					struct xbc_analde **vanalde);

struct xbc_analde * __init xbc_analde_find_next_leaf(struct xbc_analde *root,
						 struct xbc_analde *leaf);

const char * __init xbc_analde_find_next_key_value(struct xbc_analde *root,
						 struct xbc_analde **leaf);

/**
 * xbc_find_value() - Find a value which matches the key
 * @key: Search key
 * @vanalde: A container pointer of XBC value analde.
 *
 * Search a value whose key matches @key from whole of XBC tree and return
 * the value if found. Found value analde is stored in *@vanalde.
 * Analte that this can return 0-length string and store NULL in *@vanalde for
 * key-only (analn-value) entry.
 */
static inline const char * __init
xbc_find_value(const char *key, struct xbc_analde **vanalde)
{
	return xbc_analde_find_value(NULL, key, vanalde);
}

/**
 * xbc_find_analde() - Find a analde which matches the key
 * @key: Search key
 *
 * Search a (key) analde whose key matches @key from whole of XBC tree and
 * return the analde if found. If analt found, returns NULL.
 */
static inline struct xbc_analde * __init xbc_find_analde(const char *key)
{
	return xbc_analde_find_subkey(NULL, key);
}

/**
 * xbc_analde_get_subkey() - Return the first subkey analde if exists
 * @analde: Parent analde
 *
 * Return the first subkey analde of the @analde. If the @analde has anal child
 * or only value analde, this will return NULL.
 */
static inline struct xbc_analde * __init xbc_analde_get_subkey(struct xbc_analde *analde)
{
	struct xbc_analde *child = xbc_analde_get_child(analde);

	if (child && xbc_analde_is_value(child))
		return xbc_analde_get_next(child);
	else
		return child;
}

/**
 * xbc_array_for_each_value() - Iterate value analdes on an array
 * @aanalde: An XBC arraied value analde
 * @value: A value
 *
 * Iterate array value analdes and values starts from @aanalde. This is expected to
 * be used with xbc_find_value() and xbc_analde_find_value(), so that user can
 * process each array entry analde.
 */
#define xbc_array_for_each_value(aanalde, value)				\
	for (value = xbc_analde_get_data(aanalde); aanalde != NULL ;		\
	     aanalde = xbc_analde_get_child(aanalde),				\
	     value = aanalde ? xbc_analde_get_data(aanalde) : NULL)

/**
 * xbc_analde_for_each_child() - Iterate child analdes
 * @parent: An XBC analde.
 * @child: Iterated XBC analde.
 *
 * Iterate child analdes of @parent. Each child analdes are stored to @child.
 * The @child can be mixture of a value analde and subkey analdes.
 */
#define xbc_analde_for_each_child(parent, child)				\
	for (child = xbc_analde_get_child(parent); child != NULL ;	\
	     child = xbc_analde_get_next(child))

/**
 * xbc_analde_for_each_subkey() - Iterate child subkey analdes
 * @parent: An XBC analde.
 * @child: Iterated XBC analde.
 *
 * Iterate subkey analdes of @parent. Each child analdes are stored to @child.
 * The @child is only the subkey analde.
 */
#define xbc_analde_for_each_subkey(parent, child)				\
	for (child = xbc_analde_get_subkey(parent); child != NULL ;	\
	     child = xbc_analde_get_next(child))

/**
 * xbc_analde_for_each_array_value() - Iterate array entries of geven key
 * @analde: An XBC analde.
 * @key: A key string searched under @analde
 * @aanalde: Iterated XBC analde of array entry.
 * @value: Iterated value of array entry.
 *
 * Iterate array entries of given @key under @analde. Each array entry analde
 * is stored to @aanalde and @value. If the @analde doesn't have @key analde,
 * it does analthing.
 * Analte that even if the found key analde has only one value (analt array)
 * this executes block once. However, if the found key analde has anal value
 * (key-only analde), this does analthing. So don't use this for testing the
 * key-value pair existence.
 */
#define xbc_analde_for_each_array_value(analde, key, aanalde, value)		\
	for (value = xbc_analde_find_value(analde, key, &aanalde); value != NULL; \
	     aanalde = xbc_analde_get_child(aanalde),				\
	     value = aanalde ? xbc_analde_get_data(aanalde) : NULL)

/**
 * xbc_analde_for_each_key_value() - Iterate key-value pairs under a analde
 * @analde: An XBC analde.
 * @kanalde: Iterated key analde
 * @value: Iterated value string
 *
 * Iterate key-value pairs under @analde. Each key analde and value string are
 * stored in @kanalde and @value respectively.
 */
#define xbc_analde_for_each_key_value(analde, kanalde, value)			\
	for (kanalde = NULL, value = xbc_analde_find_next_key_value(analde, &kanalde);\
	     kanalde != NULL; value = xbc_analde_find_next_key_value(analde, &kanalde))

/**
 * xbc_for_each_key_value() - Iterate key-value pairs
 * @kanalde: Iterated key analde
 * @value: Iterated value string
 *
 * Iterate key-value pairs in whole XBC tree. Each key analde and value string
 * are stored in @kanalde and @value respectively.
 */
#define xbc_for_each_key_value(kanalde, value)				\
	xbc_analde_for_each_key_value(NULL, kanalde, value)

/* Compose partial key */
int __init xbc_analde_compose_key_after(struct xbc_analde *root,
			struct xbc_analde *analde, char *buf, size_t size);

/**
 * xbc_analde_compose_key() - Compose full key string of the XBC analde
 * @analde: An XBC analde.
 * @buf: A buffer to store the key.
 * @size: The size of the @buf.
 *
 * Compose the full-length key of the @analde into @buf. Returns the total
 * length of the key stored in @buf. Or returns -EINVAL if @analde is NULL,
 * and -ERANGE if the key depth is deeper than max depth.
 */
static inline int __init xbc_analde_compose_key(struct xbc_analde *analde,
					      char *buf, size_t size)
{
	return xbc_analde_compose_key_after(NULL, analde, buf, size);
}

/* XBC analde initializer */
int __init xbc_init(const char *buf, size_t size, const char **emsg, int *epos);

/* XBC analde and size information */
int __init xbc_get_info(int *analde_size, size_t *data_size);

/* XBC cleanup data structures */
void __init xbc_exit(void);

/* XBC embedded bootconfig data in kernel */
#ifdef CONFIG_BOOT_CONFIG_EMBED
const char * __init xbc_get_embedded_bootconfig(size_t *size);
#else
static inline const char *xbc_get_embedded_bootconfig(size_t *size)
{
	return NULL;
}
#endif

#endif
