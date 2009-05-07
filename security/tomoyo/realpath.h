/*
 * security/tomoyo/realpath.h
 *
 * Get the canonicalized absolute pathnames. The basis for TOMOYO.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
 */

#ifndef _SECURITY_TOMOYO_REALPATH_H
#define _SECURITY_TOMOYO_REALPATH_H

struct path;
struct tomoyo_path_info;
struct tomoyo_io_buffer;

/* Convert binary string to ascii string. */
int tomoyo_encode(char *buffer, int buflen, const char *str);

/* Returns realpath(3) of the given pathname but ignores chroot'ed root. */
int tomoyo_realpath_from_path2(struct path *path, char *newname,
			       int newname_len);

/*
 * Returns realpath(3) of the given pathname but ignores chroot'ed root.
 * These functions use tomoyo_alloc(), so the caller must call tomoyo_free()
 * if these functions didn't return NULL.
 */
char *tomoyo_realpath(const char *pathname);
/*
 * Same with tomoyo_realpath() except that it doesn't follow the final symlink.
 */
char *tomoyo_realpath_nofollow(const char *pathname);
/* Same with tomoyo_realpath() except that the pathname is already solved. */
char *tomoyo_realpath_from_path(struct path *path);

/*
 * Allocate memory for ACL entry.
 * The RAM is chunked, so NEVER try to kfree() the returned pointer.
 */
void *tomoyo_alloc_element(const unsigned int size);

/*
 * Keep the given name on the RAM.
 * The RAM is shared, so NEVER try to modify or kfree() the returned name.
 */
const struct tomoyo_path_info *tomoyo_save_name(const char *name);

/* Allocate memory for temporary use (e.g. permission checks). */
void *tomoyo_alloc(const size_t size);

/* Free memory allocated by tomoyo_alloc(). */
void tomoyo_free(const void *p);

/* Check for memory usage. */
int tomoyo_read_memory_counter(struct tomoyo_io_buffer *head);

/* Set memory quota. */
int tomoyo_write_memory_quota(struct tomoyo_io_buffer *head);

/* Initialize realpath related code. */
void __init tomoyo_realpath_init(void);

#endif /* !defined(_SECURITY_TOMOYO_REALPATH_H) */
