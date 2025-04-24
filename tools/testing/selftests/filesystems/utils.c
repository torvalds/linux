// SPDX-License-Identifier: GPL-2.0
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <grp.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>

#include "utils.h"

#define MAX_USERNS_LEVEL 32

#define syserror(format, ...)                           \
	({                                              \
		fprintf(stderr, "%m - " format "\n", ##__VA_ARGS__); \
		(-errno);                               \
	})

#define syserror_set(__ret__, format, ...)                    \
	({                                                    \
		typeof(__ret__) __internal_ret__ = (__ret__); \
		errno = labs(__ret__);                        \
		fprintf(stderr, "%m - " format "\n", ##__VA_ARGS__);       \
		__internal_ret__;                             \
	})

#define STRLITERALLEN(x) (sizeof(""x"") - 1)

#define INTTYPE_TO_STRLEN(type)             \
	(2 + (sizeof(type) <= 1             \
		  ? 3                       \
		  : sizeof(type) <= 2       \
			? 5                 \
			: sizeof(type) <= 4 \
			      ? 10          \
			      : sizeof(type) <= 8 ? 20 : sizeof(int[-2 * (sizeof(type) > 8)])))

#define list_for_each(__iterator, __list) \
	for (__iterator = (__list)->next; __iterator != __list; __iterator = __iterator->next)

typedef enum idmap_type_t {
	ID_TYPE_UID,
	ID_TYPE_GID
} idmap_type_t;

struct id_map {
	idmap_type_t map_type;
	__u32 nsid;
	__u32 hostid;
	__u32 range;
};

struct list {
	void *elem;
	struct list *next;
	struct list *prev;
};

struct userns_hierarchy {
	int fd_userns;
	int fd_event;
	unsigned int level;
	struct list id_map;
};

static inline void list_init(struct list *list)
{
	list->elem = NULL;
	list->next = list->prev = list;
}

static inline int list_empty(const struct list *list)
{
	return list == list->next;
}

static inline void __list_add(struct list *new, struct list *prev, struct list *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add_tail(struct list *head, struct list *list)
{
	__list_add(list, head->prev, head);
}

static inline void list_del(struct list *list)
{
	struct list *next, *prev;

	next = list->next;
	prev = list->prev;
	next->prev = prev;
	prev->next = next;
}

static ssize_t read_nointr(int fd, void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

#define __STACK_SIZE (8 * 1024 * 1024)
static pid_t do_clone(int (*fn)(void *), void *arg, int flags)
{
	void *stack;

	stack = malloc(__STACK_SIZE);
	if (!stack)
		return -ENOMEM;

#ifdef __ia64__
	return __clone2(fn, stack, __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#else
	return clone(fn, stack + __STACK_SIZE, flags | SIGCHLD, arg, NULL);
#endif
}

static int get_userns_fd_cb(void *data)
{
	for (;;)
		pause();
	_exit(0);
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static int write_id_mapping(idmap_type_t map_type, pid_t pid, const char *buf, size_t buf_size)
{
	int fd = -EBADF, setgroups_fd = -EBADF;
	int fret = -1;
	int ret;
	char path[STRLITERALLEN("/proc/") + INTTYPE_TO_STRLEN(pid_t) +
		  STRLITERALLEN("/setgroups") + 1];

	if (geteuid() != 0 && map_type == ID_TYPE_GID) {
		ret = snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
		if (ret < 0 || ret >= sizeof(path))
			goto out;

		setgroups_fd = open(path, O_WRONLY | O_CLOEXEC);
		if (setgroups_fd < 0 && errno != ENOENT) {
			syserror("Failed to open \"%s\"", path);
			goto out;
		}

		if (setgroups_fd >= 0) {
			ret = write_nointr(setgroups_fd, "deny\n", STRLITERALLEN("deny\n"));
			if (ret != STRLITERALLEN("deny\n")) {
				syserror("Failed to write \"deny\" to \"/proc/%d/setgroups\"", pid);
				goto out;
			}
		}
	}

	ret = snprintf(path, sizeof(path), "/proc/%d/%cid_map", pid, map_type == ID_TYPE_UID ? 'u' : 'g');
	if (ret < 0 || ret >= sizeof(path))
		goto out;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		syserror("Failed to open \"%s\"", path);
		goto out;
	}

	ret = write_nointr(fd, buf, buf_size);
	if (ret != buf_size) {
		syserror("Failed to write %cid mapping to \"%s\"",
			 map_type == ID_TYPE_UID ? 'u' : 'g', path);
		goto out;
	}

	fret = 0;
out:
	close(fd);
	close(setgroups_fd);

	return fret;
}

static int map_ids_from_idmap(struct list *idmap, pid_t pid)
{
	int fill, left;
	char mapbuf[4096] = {};
	bool had_entry = false;
	idmap_type_t map_type, u_or_g;

	if (list_empty(idmap))
		return 0;

	for (map_type = ID_TYPE_UID, u_or_g = 'u';
	     map_type <= ID_TYPE_GID; map_type++, u_or_g = 'g') {
		char *pos = mapbuf;
		int ret;
		struct list *iterator;


		list_for_each(iterator, idmap) {
			struct id_map *map = iterator->elem;
			if (map->map_type != map_type)
				continue;

			had_entry = true;

			left = 4096 - (pos - mapbuf);
			fill = snprintf(pos, left, "%u %u %u\n", map->nsid, map->hostid, map->range);
			/*
			 * The kernel only takes <= 4k for writes to
			 * /proc/<pid>/{g,u}id_map
			 */
			if (fill <= 0 || fill >= left)
				return syserror_set(-E2BIG, "Too many %cid mappings defined", u_or_g);

			pos += fill;
		}
		if (!had_entry)
			continue;

		ret = write_id_mapping(map_type, pid, mapbuf, pos - mapbuf);
		if (ret < 0)
			return syserror("Failed to write mapping: %s", mapbuf);

		memset(mapbuf, 0, sizeof(mapbuf));
	}

	return 0;
}

static int get_userns_fd_from_idmap(struct list *idmap)
{
	int ret;
	pid_t pid;
	char path_ns[STRLITERALLEN("/proc/") + INTTYPE_TO_STRLEN(pid_t) +
		     STRLITERALLEN("/ns/user") + 1];

	pid = do_clone(get_userns_fd_cb, NULL, CLONE_NEWUSER | CLONE_NEWNS);
	if (pid < 0)
		return -errno;

	ret = map_ids_from_idmap(idmap, pid);
	if (ret < 0)
		return ret;

	ret = snprintf(path_ns, sizeof(path_ns), "/proc/%d/ns/user", pid);
	if (ret < 0 || (size_t)ret >= sizeof(path_ns))
		ret = -EIO;
	else
		ret = open(path_ns, O_RDONLY | O_CLOEXEC | O_NOCTTY);

	(void)kill(pid, SIGKILL);
	(void)wait_for_pid(pid);
	return ret;
}

int get_userns_fd(unsigned long nsid, unsigned long hostid, unsigned long range)
{
	struct list head, uid_mapl, gid_mapl;
	struct id_map uid_map = {
		.map_type	= ID_TYPE_UID,
		.nsid		= nsid,
		.hostid		= hostid,
		.range		= range,
	};
	struct id_map gid_map = {
		.map_type	= ID_TYPE_GID,
		.nsid		= nsid,
		.hostid		= hostid,
		.range		= range,
	};

	list_init(&head);
	uid_mapl.elem = &uid_map;
	gid_mapl.elem = &gid_map;
	list_add_tail(&head, &uid_mapl);
	list_add_tail(&head, &gid_mapl);

	return get_userns_fd_from_idmap(&head);
}

bool switch_ids(uid_t uid, gid_t gid)
{
	if (setgroups(0, NULL))
		return syserror("failure: setgroups");

	if (setresgid(gid, gid, gid))
		return syserror("failure: setresgid");

	if (setresuid(uid, uid, uid))
		return syserror("failure: setresuid");

	/* Ensure we can access proc files from processes we can ptrace. */
	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0))
		return syserror("failure: make dumpable");

	return true;
}

static int create_userns_hierarchy(struct userns_hierarchy *h);

static int userns_fd_cb(void *data)
{
	struct userns_hierarchy *h = data;
	char c;
	int ret;

	ret = read_nointr(h->fd_event, &c, 1);
	if (ret < 0)
		return syserror("failure: read from socketpair");

	/* Only switch ids if someone actually wrote a mapping for us. */
	if (c == '1') {
		if (!switch_ids(0, 0))
			return syserror("failure: switch ids to 0");
	}

	ret = write_nointr(h->fd_event, "1", 1);
	if (ret < 0)
		return syserror("failure: write to socketpair");

	ret = create_userns_hierarchy(++h);
	if (ret < 0)
		return syserror("failure: userns level %d", h->level);

	return 0;
}

static int create_userns_hierarchy(struct userns_hierarchy *h)
{
	int fret = -1;
	char c;
	int fd_socket[2];
	int fd_userns = -EBADF, ret = -1;
	ssize_t bytes;
	pid_t pid;
	char path[256];

	if (h->level == MAX_USERNS_LEVEL)
		return 0;

	ret = socketpair(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fd_socket);
	if (ret < 0)
		return syserror("failure: create socketpair");

	/* Note the CLONE_FILES | CLONE_VM when mucking with fds and memory. */
	h->fd_event = fd_socket[1];
	pid = do_clone(userns_fd_cb, h, CLONE_NEWUSER | CLONE_FILES | CLONE_VM);
	if (pid < 0) {
		syserror("failure: userns level %d", h->level);
		goto out_close;
	}

	ret = map_ids_from_idmap(&h->id_map, pid);
	if (ret < 0) {
		kill(pid, SIGKILL);
		syserror("failure: writing id mapping for userns level %d for %d", h->level, pid);
		goto out_wait;
	}

	if (!list_empty(&h->id_map))
		bytes = write_nointr(fd_socket[0], "1", 1); /* Inform the child we wrote a mapping. */
	else
		bytes = write_nointr(fd_socket[0], "0", 1); /* Inform the child we didn't write a mapping. */
	if (bytes < 0) {
		kill(pid, SIGKILL);
		syserror("failure: write to socketpair");
		goto out_wait;
	}

	/* Wait for child to set*id() and become dumpable. */
	bytes = read_nointr(fd_socket[0], &c, 1);
	if (bytes < 0) {
		kill(pid, SIGKILL);
		syserror("failure: read from socketpair");
		goto out_wait;
	}

	snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	fd_userns = open(path, O_RDONLY | O_CLOEXEC);
	if (fd_userns < 0) {
		kill(pid, SIGKILL);
		syserror("failure: open userns level %d for %d", h->level, pid);
		goto out_wait;
	}

	fret = 0;

out_wait:
	if (!wait_for_pid(pid) && !fret) {
		h->fd_userns = fd_userns;
		fd_userns = -EBADF;
	}

out_close:
	if (fd_userns >= 0)
		close(fd_userns);
	close(fd_socket[0]);
	close(fd_socket[1]);
	return fret;
}

/* caps_down - lower all effective caps */
int caps_down(void)
{
	bool fret = false;
	cap_t caps = NULL;
	int ret = -1;

	caps = cap_get_proc();
	if (!caps)
		goto out;

	ret = cap_clear_flag(caps, CAP_EFFECTIVE);
	if (ret)
		goto out;

	ret = cap_set_proc(caps);
	if (ret)
		goto out;

	fret = true;

out:
	cap_free(caps);
	return fret;
}

/* cap_down - lower an effective cap */
int cap_down(cap_value_t down)
{
	bool fret = false;
	cap_t caps = NULL;
	cap_value_t cap = down;
	int ret = -1;

	caps = cap_get_proc();
	if (!caps)
		goto out;

	ret = cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, 0);
	if (ret)
		goto out;

	ret = cap_set_proc(caps);
	if (ret)
		goto out;

	fret = true;

out:
	cap_free(caps);
	return fret;
}
