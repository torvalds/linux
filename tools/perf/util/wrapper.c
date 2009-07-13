/*
 * Various trivial helper wrappers around standard functions
 */
#include "cache.h"

/*
 * There's no pack memory to release - but stay close to the Git
 * version so wrap this away:
 */
static inline void release_pack_memory(size_t size __used, int flag __used)
{
}

char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret) {
		release_pack_memory(strlen(str) + 1, -1);
		ret = strdup(str);
		if (!ret)
			die("Out of memory, strdup failed");
	}
	return ret;
}

void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret) {
		release_pack_memory(size, -1);
		ret = malloc(size);
		if (!ret && !size)
			ret = malloc(1);
		if (!ret)
			die("Out of memory, malloc failed");
	}
#ifdef XMALLOC_POISON
	memset(ret, 0xA5, size);
#endif
	return ret;
}

/*
 * xmemdupz() allocates (len + 1) bytes of memory, duplicates "len" bytes of
 * "data" to the allocated memory, zero terminates the allocated memory,
 * and returns a pointer to the allocated memory. If the allocation fails,
 * the program dies.
 */
void *xmemdupz(const void *data, size_t len)
{
	char *p = xmalloc(len + 1);
	memcpy(p, data, len);
	p[len] = '\0';
	return p;
}

char *xstrndup(const char *str, size_t len)
{
	char *p = memchr(str, '\0', len);

	return xmemdupz(str, p ? (size_t)(p - str) : len);
}

void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret) {
		release_pack_memory(size, -1);
		ret = realloc(ptr, size);
		if (!ret && !size)
			ret = realloc(ptr, 1);
		if (!ret)
			die("Out of memory, realloc failed");
	}
	return ret;
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret) {
		release_pack_memory(nmemb * size, -1);
		ret = calloc(nmemb, size);
		if (!ret && (!nmemb || !size))
			ret = calloc(1, 1);
		if (!ret)
			die("Out of memory, calloc failed");
	}
	return ret;
}

void *xmmap(void *start, size_t length,
	int prot, int flags, int fd, off_t offset)
{
	void *ret = mmap(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED) {
		if (!length)
			return NULL;
		release_pack_memory(length, fd);
		ret = mmap(start, length, prot, flags, fd, offset);
		if (ret == MAP_FAILED)
			die("Out of memory? mmap failed: %s", strerror(errno));
	}
	return ret;
}

/*
 * xread() is the same a read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

/*
 * xwrite() is the same a write(), but it automatically restarts write()
 * operations with a recoverable error (EAGAIN and EINTR). xwrite() DOES NOT
 * GUARANTEE that "len" bytes is written even if the operation is successful.
 */
ssize_t xwrite(int fd, const void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = write(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

ssize_t read_in_full(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded <= 0)
			return total ? total : loaded;
		count -= loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

ssize_t write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t written = xwrite(fd, p, count);
		if (written < 0)
			return -1;
		if (!written) {
			errno = ENOSPC;
			return -1;
		}
		count -= written;
		p += written;
		total += written;
	}

	return total;
}

int xdup(int fd)
{
	int ret = dup(fd);
	if (ret < 0)
		die("dup failed: %s", strerror(errno));
	return ret;
}

FILE *xfdopen(int fd, const char *mode)
{
	FILE *stream = fdopen(fd, mode);
	if (stream == NULL)
		die("Out of memory? fdopen failed: %s", strerror(errno));
	return stream;
}

int xmkstemp(char *template)
{
	int fd;

	fd = mkstemp(template);
	if (fd < 0)
		die("Unable to create temporary file: %s", strerror(errno));
	return fd;
}
