/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libutil.h>
#include <paths.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "ggate.h"


#define	GGATED_EXPORT_FILE	"/etc/gg.exports"

struct ggd_connection {
	off_t		 c_mediasize;
	unsigned	 c_sectorsize;
	unsigned	 c_flags;	/* flags (RO/RW) */
	int		 c_diskfd;
	int		 c_sendfd;
	int		 c_recvfd;
	time_t		 c_birthtime;
	char		*c_path;
	uint64_t	 c_token;
	in_addr_t	 c_srcip;
	LIST_ENTRY(ggd_connection) c_next;
};

struct ggd_request {
	struct g_gate_hdr	 r_hdr;
	char			*r_data;
	TAILQ_ENTRY(ggd_request) r_next;
};
#define	r_cmd		r_hdr.gh_cmd
#define	r_offset	r_hdr.gh_offset
#define	r_length	r_hdr.gh_length
#define	r_error		r_hdr.gh_error

struct ggd_export {
	char		*e_path;	/* path to device/file */
	in_addr_t	 e_ip;		/* remote IP address */
	in_addr_t	 e_mask;	/* IP mask */
	unsigned	 e_flags;	/* flags (RO/RW) */
	SLIST_ENTRY(ggd_export) e_next;
};

static const char *exports_file = GGATED_EXPORT_FILE;
static int got_sighup = 0;
static in_addr_t bindaddr;

static TAILQ_HEAD(, ggd_request) inqueue = TAILQ_HEAD_INITIALIZER(inqueue);
static TAILQ_HEAD(, ggd_request) outqueue = TAILQ_HEAD_INITIALIZER(outqueue);
static pthread_mutex_t inqueue_mtx, outqueue_mtx;
static pthread_cond_t inqueue_cond, outqueue_cond;

static SLIST_HEAD(, ggd_export) exports = SLIST_HEAD_INITIALIZER(exports);
static LIST_HEAD(, ggd_connection) connections = LIST_HEAD_INITIALIZER(connections);

static void *recv_thread(void *arg);
static void *disk_thread(void *arg);
static void *send_thread(void *arg);

static void
usage(void)
{

	fprintf(stderr, "usage: %s [-nv] [-a address] [-F pidfile] [-p port] "
	    "[-R rcvbuf] [-S sndbuf] [exports file]\n", getprogname());
	exit(EXIT_FAILURE);
}

static char *
ip2str(in_addr_t ip)
{
	static char sip[16];

	snprintf(sip, sizeof(sip), "%u.%u.%u.%u",
	    ((ip >> 24) & 0xff),
	    ((ip >> 16) & 0xff),
	    ((ip >> 8) & 0xff),
	    (ip & 0xff));
	return (sip);
}

static in_addr_t
countmask(unsigned m)
{
	in_addr_t mask;

	if (m == 0) {
		mask = 0x0;
	} else {
		mask = 1 << (32 - m);
		mask--;
		mask = ~mask;
	}
	return (mask);
}

static void
line_parse(char *line, unsigned lineno)
{
	struct ggd_export *ex;
	char *word, *path, *sflags;
	unsigned flags, i, vmask;
	in_addr_t ip, mask;

	ip = mask = flags = vmask = 0;
	path = NULL;
	sflags = NULL;

	for (i = 0, word = strtok(line, " \t"); word != NULL;
	    i++, word = strtok(NULL, " \t")) {
		switch (i) {
		case 0: /* IP address or host name */
			ip = g_gate_str2ip(strsep(&word, "/"));
			if (ip == INADDR_NONE) {
				g_gate_xlog("Invalid IP/host name at line %u.",
				    lineno);
			}
			ip = ntohl(ip);
			if (word == NULL)
				vmask = 32;
			else {
				errno = 0;
				vmask = strtoul(word, NULL, 10);
				if (vmask == 0 && errno != 0) {
					g_gate_xlog("Invalid IP mask value at "
					    "line %u.", lineno);
				}
				if ((unsigned)vmask > 32) {
					g_gate_xlog("Invalid IP mask value at line %u.",
					    lineno);
				}
			}
			mask = countmask(vmask);
			break;
		case 1:	/* flags */
			if (strcasecmp("rd", word) == 0 ||
			    strcasecmp("ro", word) == 0) {
				flags = O_RDONLY;
			} else if (strcasecmp("wo", word) == 0) {
				flags = O_WRONLY;
			} else if (strcasecmp("rw", word) == 0) {
				flags = O_RDWR;
			} else {
				g_gate_xlog("Invalid value in flags field at "
				    "line %u.", lineno);
			}
			sflags = word;
			break;
		case 2:	/* path */
			if (strlen(word) >= MAXPATHLEN) {
				g_gate_xlog("Path too long at line %u. ",
				    lineno);
			}
			path = word;
			break;
		default:
			g_gate_xlog("Too many arguments at line %u. ", lineno);
		}
	}
	if (i != 3)
		g_gate_xlog("Too few arguments at line %u.", lineno);

	ex = malloc(sizeof(*ex));
	if (ex == NULL)
		g_gate_xlog("Not enough memory.");
	ex->e_path = strdup(path);
	if (ex->e_path == NULL)
		g_gate_xlog("Not enough memory.");

	/* Made 'and' here. */
	ex->e_ip = (ip & mask);
	ex->e_mask = mask;
	ex->e_flags = flags;

	SLIST_INSERT_HEAD(&exports, ex, e_next);

	g_gate_log(LOG_DEBUG, "Added %s/%u %s %s to exports list.",
	    ip2str(ex->e_ip), vmask, path, sflags);
}

static void
exports_clear(void)
{
	struct ggd_export *ex;

	while (!SLIST_EMPTY(&exports)) {
		ex = SLIST_FIRST(&exports);
		SLIST_REMOVE_HEAD(&exports, e_next);
		free(ex);
	}
}

#define	EXPORTS_LINE_SIZE	2048
static void
exports_get(void)
{
	char buf[EXPORTS_LINE_SIZE], *line;
	unsigned lineno = 0, objs = 0, len;
	FILE *fd;

	exports_clear();

	fd = fopen(exports_file, "r");
	if (fd == NULL) {
		g_gate_xlog("Cannot open exports file (%s): %s.", exports_file,
		    strerror(errno));
	}

	g_gate_log(LOG_INFO, "Reading exports file (%s).", exports_file);

	for (;;) {
		if (fgets(buf, sizeof(buf), fd) == NULL) {
			if (feof(fd))
				break;

			g_gate_xlog("Error while reading exports file: %s.",
			    strerror(errno));
		}

		/* Increase line count. */
		lineno++;

		/* Skip spaces and tabs. */
		for (line = buf; *line == ' ' || *line == '\t'; ++line)
			;

		/* Empty line, comment or empty line at the end of file. */
		if (*line == '\n' || *line == '#' || *line == '\0')
			continue;

		len = strlen(line);
		if (line[len - 1] == '\n') {
			/* Remove new line char. */
			line[len - 1] = '\0';
		} else {
			if (!feof(fd))
				g_gate_xlog("Line %u too long.", lineno);
		}

		line_parse(line, lineno);
		objs++;
	}

	fclose(fd);

	if (objs == 0)
		g_gate_xlog("There are no objects to export.");

	g_gate_log(LOG_INFO, "Exporting %u object(s).", objs);
}

static int
exports_check(struct ggd_export *ex, struct g_gate_cinit *cinit,
    struct ggd_connection *conn)
{
	char ipmask[32]; /* 32 == strlen("xxx.xxx.xxx.xxx/xxx.xxx.xxx.xxx")+1 */
	int error = 0, flags;

	strlcpy(ipmask, ip2str(ex->e_ip), sizeof(ipmask));
	strlcat(ipmask, "/", sizeof(ipmask));
	strlcat(ipmask, ip2str(ex->e_mask), sizeof(ipmask));
	if ((cinit->gc_flags & GGATE_FLAG_RDONLY) != 0) {
		if (ex->e_flags == O_WRONLY) {
			g_gate_log(LOG_WARNING, "Read-only access requested, "
			    "but %s (%s) is exported write-only.", ex->e_path,
			    ipmask);
			return (EPERM);
		} else {
			conn->c_flags |= GGATE_FLAG_RDONLY;
		}
	} else if ((cinit->gc_flags & GGATE_FLAG_WRONLY) != 0) {
		if (ex->e_flags == O_RDONLY) {
			g_gate_log(LOG_WARNING, "Write-only access requested, "
			    "but %s (%s) is exported read-only.", ex->e_path,
			    ipmask);
			return (EPERM);
		} else {
			conn->c_flags |= GGATE_FLAG_WRONLY;
		}
	} else {
		if (ex->e_flags == O_RDONLY) {
			g_gate_log(LOG_WARNING, "Read-write access requested, "
			    "but %s (%s) is exported read-only.", ex->e_path,
			    ipmask);
			return (EPERM);
		} else if (ex->e_flags == O_WRONLY) {
			g_gate_log(LOG_WARNING, "Read-write access requested, "
			    "but %s (%s) is exported write-only.", ex->e_path,
			    ipmask);
			return (EPERM);
		}
	}
	if ((conn->c_flags & GGATE_FLAG_RDONLY) != 0)
		flags = O_RDONLY;
	else if ((conn->c_flags & GGATE_FLAG_WRONLY) != 0)
		flags = O_WRONLY;
	else
		flags = O_RDWR;
	conn->c_diskfd = open(ex->e_path, flags);
	if (conn->c_diskfd == -1) {
		error = errno;
		g_gate_log(LOG_ERR, "Cannot open %s: %s.", ex->e_path,
		    strerror(error));
		return (error);
	}
	return (0);
}

static struct ggd_export *
exports_find(struct sockaddr *s, struct g_gate_cinit *cinit,
    struct ggd_connection *conn)
{
	struct ggd_export *ex;
	in_addr_t ip;
	int error;

	ip = htonl(((struct sockaddr_in *)(void *)s)->sin_addr.s_addr);
	SLIST_FOREACH(ex, &exports, e_next) {
		if ((ip & ex->e_mask) != ex->e_ip) {
			g_gate_log(LOG_DEBUG, "exports[%s]: IP mismatch.",
			    ex->e_path);
			continue;
		}
		if (strcmp(cinit->gc_path, ex->e_path) != 0) {
			g_gate_log(LOG_DEBUG, "exports[%s]: Path mismatch.",
			    ex->e_path);
			continue;
		}
		error = exports_check(ex, cinit, conn);
		if (error == 0)
			return (ex);
		else {
			errno = error;
			return (NULL);
		}
	}
	g_gate_log(LOG_WARNING, "Unauthorized connection from: %s.",
	    ip2str(ip));
	errno = EPERM;
	return (NULL);
}

/*
 * Remove timed out connections.
 */
static void
connection_cleanups(void)
{
	struct ggd_connection *conn, *tconn;
	time_t now;

	time(&now);
	LIST_FOREACH_SAFE(conn, &connections, c_next, tconn) {
		if (now - conn->c_birthtime > 10) {
			LIST_REMOVE(conn, c_next);
			g_gate_log(LOG_NOTICE,
			    "Connection from %s [%s] removed.",
			    ip2str(conn->c_srcip), conn->c_path);
			close(conn->c_diskfd);
			close(conn->c_sendfd);
			close(conn->c_recvfd);
			free(conn->c_path);
			free(conn);
		}
	}
}

static struct ggd_connection *
connection_find(struct g_gate_cinit *cinit)
{
	struct ggd_connection *conn;

	LIST_FOREACH(conn, &connections, c_next) {
		if (conn->c_token == cinit->gc_token)
			break;
	}
	return (conn);
}

static struct ggd_connection *
connection_new(struct g_gate_cinit *cinit, struct sockaddr *s, int sfd)
{
	struct ggd_connection *conn;
	in_addr_t ip;

	/*
	 * First, look for old connections.
	 * We probably should do it every X seconds, but what for?
	 * It is only dangerous if an attacker wants to overload connections
	 * queue, so here is a good place to do the cleanups.
	 */
	connection_cleanups();

	conn = malloc(sizeof(*conn));
	if (conn == NULL)
		return (NULL);
	conn->c_path = strdup(cinit->gc_path);
	if (conn->c_path == NULL) {
		free(conn);
		return (NULL);
	}
	conn->c_token = cinit->gc_token;
	ip = htonl(((struct sockaddr_in *)(void *)s)->sin_addr.s_addr);
	conn->c_srcip = ip;
	conn->c_sendfd = conn->c_recvfd = -1;
	if ((cinit->gc_flags & GGATE_FLAG_SEND) != 0)
		conn->c_sendfd = sfd;
	else
		conn->c_recvfd = sfd;
	conn->c_mediasize = 0;
	conn->c_sectorsize = 0;
	time(&conn->c_birthtime);
	conn->c_flags = cinit->gc_flags;
	LIST_INSERT_HEAD(&connections, conn, c_next);
	g_gate_log(LOG_DEBUG, "Connection created [%s, %s].", ip2str(ip),
	    conn->c_path);
	return (conn);
}

static int
connection_add(struct ggd_connection *conn, struct g_gate_cinit *cinit,
    struct sockaddr *s, int sfd)
{
	in_addr_t ip;

	ip = htonl(((struct sockaddr_in *)(void *)s)->sin_addr.s_addr);
	if ((cinit->gc_flags & GGATE_FLAG_SEND) != 0) {
		if (conn->c_sendfd != -1) {
			g_gate_log(LOG_WARNING,
			    "Send socket already exists [%s, %s].", ip2str(ip),
			    conn->c_path);
			return (EEXIST);
		}
		conn->c_sendfd = sfd;
	} else {
		if (conn->c_recvfd != -1) {
			g_gate_log(LOG_WARNING,
			    "Receive socket already exists [%s, %s].",
			    ip2str(ip), conn->c_path);
			return (EEXIST);
		}
		conn->c_recvfd = sfd;
	}
	g_gate_log(LOG_DEBUG, "Connection added [%s, %s].", ip2str(ip),
	    conn->c_path);
	return (0);
}

/*
 * Remove one socket from the given connection or the whole
 * connection if sfd == -1.
 */
static void
connection_remove(struct ggd_connection *conn)
{

	LIST_REMOVE(conn, c_next);
	g_gate_log(LOG_DEBUG, "Connection removed [%s %s].",
	    ip2str(conn->c_srcip), conn->c_path);
	if (conn->c_sendfd != -1)
		close(conn->c_sendfd);
	if (conn->c_recvfd != -1)
		close(conn->c_recvfd);
	free(conn->c_path);
	free(conn);
}

static int
connection_ready(struct ggd_connection *conn)
{

	return (conn->c_sendfd != -1 && conn->c_recvfd != -1);
}

static void
connection_launch(struct ggd_connection *conn)
{
	pthread_t td;
	int error, pid;

	pid = fork();
	if (pid > 0)
		return;
	else if (pid == -1) {
		g_gate_log(LOG_ERR, "Cannot fork: %s.", strerror(errno));
		return;
	}
	g_gate_log(LOG_DEBUG, "Process created [%s].", conn->c_path);

	/*
	 * Create condition variables and mutexes for in-queue and out-queue
	 * synchronization.
	 */
	error = pthread_mutex_init(&inqueue_mtx, NULL);
	if (error != 0) {
		g_gate_xlog("pthread_mutex_init(inqueue_mtx): %s.",
		    strerror(error));
	}
	error = pthread_cond_init(&inqueue_cond, NULL);
	if (error != 0) {
		g_gate_xlog("pthread_cond_init(inqueue_cond): %s.",
		    strerror(error));
	}
	error = pthread_mutex_init(&outqueue_mtx, NULL);
	if (error != 0) {
		g_gate_xlog("pthread_mutex_init(outqueue_mtx): %s.",
		    strerror(error));
	}
	error = pthread_cond_init(&outqueue_cond, NULL);
	if (error != 0) {
		g_gate_xlog("pthread_cond_init(outqueue_cond): %s.",
		    strerror(error));
	}

	/*
	 * Create threads:
	 * recvtd - thread for receiving I/O request
	 * diskio - thread for doing I/O request
	 * sendtd - thread for sending I/O requests back
	 */
	error = pthread_create(&td, NULL, send_thread, conn);
	if (error != 0) {
		g_gate_xlog("pthread_create(send_thread): %s.",
		    strerror(error));
	}
	error = pthread_create(&td, NULL, recv_thread, conn);
	if (error != 0) {
		g_gate_xlog("pthread_create(recv_thread): %s.",
		    strerror(error));
	}
	disk_thread(conn);
}

static void
sendfail(int sfd, int error, const char *fmt, ...)
{
	struct g_gate_sinit sinit;
	va_list ap;
	ssize_t data;

	memset(&sinit, 0, sizeof(sinit));
	sinit.gs_error = error;
	g_gate_swap2n_sinit(&sinit);
	data = g_gate_send(sfd, &sinit, sizeof(sinit), 0);
	g_gate_swap2h_sinit(&sinit);
	if (data != sizeof(sinit)) {
		g_gate_log(LOG_WARNING, "Cannot send initial packet: %s.",
		    strerror(errno));
		return;
	}
	if (fmt != NULL) {
		va_start(ap, fmt);
		g_gate_vlog(LOG_WARNING, fmt, ap);
		va_end(ap);
	}
}

static void *
malloc_waitok(size_t size)
{
	void *p;

	while ((p = malloc(size)) == NULL) {
		g_gate_log(LOG_DEBUG, "Cannot allocate %zu bytes.", size);
		sleep(1);
	}
	return (p);
}

static void *
recv_thread(void *arg)
{
	struct ggd_connection *conn;
	struct ggd_request *req;
	ssize_t data;
	int error, fd;

	conn = arg;
	g_gate_log(LOG_NOTICE, "%s: started [%s]!", __func__, conn->c_path);
	fd = conn->c_recvfd;
	for (;;) {
		/*
		 * Get header packet.
		 */
		req = malloc_waitok(sizeof(*req));
		data = g_gate_recv(fd, &req->r_hdr, sizeof(req->r_hdr),
		    MSG_WAITALL);
		if (data == 0) {
			g_gate_log(LOG_DEBUG, "Process %u exiting.", getpid());
			exit(EXIT_SUCCESS);
		} else if (data == -1) {
			g_gate_xlog("Error while receiving hdr packet: %s.",
			    strerror(errno));
		} else if (data != sizeof(req->r_hdr)) {
			g_gate_xlog("Malformed hdr packet received.");
		}
		g_gate_log(LOG_DEBUG, "Received hdr packet.");
		g_gate_swap2h_hdr(&req->r_hdr);

		g_gate_log(LOG_DEBUG, "%s: offset=%jd length=%u", __func__,
		    (intmax_t)req->r_offset, (unsigned)req->r_length);

		/*
		 * Allocate memory for data.
		 */
		req->r_data = malloc_waitok(req->r_length);

		/*
		 * Receive data to write for WRITE request.
		 */
		if (req->r_cmd == GGATE_CMD_WRITE) {
			g_gate_log(LOG_DEBUG, "Waiting for %u bytes of data...",
			    req->r_length);
			data = g_gate_recv(fd, req->r_data, req->r_length,
			    MSG_WAITALL);
			if (data == -1) {
				g_gate_xlog("Error while receiving data: %s.",
				    strerror(errno));
			}
		}

		/*
		 * Put the request onto the incoming queue.
		 */
		error = pthread_mutex_lock(&inqueue_mtx);
		assert(error == 0);
		TAILQ_INSERT_TAIL(&inqueue, req, r_next);
		error = pthread_cond_signal(&inqueue_cond);
		assert(error == 0);
		error = pthread_mutex_unlock(&inqueue_mtx);
		assert(error == 0);
	}
}

static void *
disk_thread(void *arg)
{
	struct ggd_connection *conn;
	struct ggd_request *req;
	ssize_t data;
	int error, fd;

	conn = arg;
	g_gate_log(LOG_NOTICE, "%s: started [%s]!", __func__, conn->c_path);
	fd = conn->c_diskfd;
	for (;;) {
		/*
		 * Get a request from the incoming queue.
		 */
		error = pthread_mutex_lock(&inqueue_mtx);
		assert(error == 0);
		while ((req = TAILQ_FIRST(&inqueue)) == NULL) {
			error = pthread_cond_wait(&inqueue_cond, &inqueue_mtx);
			assert(error == 0);
		}
		TAILQ_REMOVE(&inqueue, req, r_next);
		error = pthread_mutex_unlock(&inqueue_mtx);
		assert(error == 0);

		/*
		 * Check the request.
		 */
		assert(req->r_cmd == GGATE_CMD_READ || req->r_cmd == GGATE_CMD_WRITE);
		assert(req->r_offset + req->r_length <= (uintmax_t)conn->c_mediasize);
		assert((req->r_offset % conn->c_sectorsize) == 0);
		assert((req->r_length % conn->c_sectorsize) == 0);

		g_gate_log(LOG_DEBUG, "%s: offset=%jd length=%u", __func__,
		    (intmax_t)req->r_offset, (unsigned)req->r_length);

		/*
		 * Do the request.
		 */
		data = 0;
		switch (req->r_cmd) {
		case GGATE_CMD_READ:
			data = pread(fd, req->r_data, req->r_length,
			    req->r_offset);
			break;
		case GGATE_CMD_WRITE:
			data = pwrite(fd, req->r_data, req->r_length,
			    req->r_offset);
			/* Free data memory here - better sooner. */
			free(req->r_data);
			req->r_data = NULL;
			break;
		}
		if (data != (ssize_t)req->r_length) {
			/* Report short reads/writes as I/O errors. */
			if (errno == 0)
				errno = EIO;
			g_gate_log(LOG_ERR, "Disk error: %s", strerror(errno));
			req->r_error = errno;
			if (req->r_data != NULL) {
				free(req->r_data);
				req->r_data = NULL;
			}
		}

		/*
		 * Put the request onto the outgoing queue.
		 */
		error = pthread_mutex_lock(&outqueue_mtx);
		assert(error == 0);
		TAILQ_INSERT_TAIL(&outqueue, req, r_next);
		error = pthread_cond_signal(&outqueue_cond);
		assert(error == 0);
		error = pthread_mutex_unlock(&outqueue_mtx);
		assert(error == 0);
	}

	/* NOTREACHED */
	return (NULL);
}

static void *
send_thread(void *arg)
{
	struct ggd_connection *conn;
	struct ggd_request *req;
	ssize_t data;
	int error, fd;

	conn = arg;
	g_gate_log(LOG_NOTICE, "%s: started [%s]!", __func__, conn->c_path);
	fd = conn->c_sendfd;
	for (;;) {
		/*
		 * Get a request from the outgoing queue.
		 */
		error = pthread_mutex_lock(&outqueue_mtx);
		assert(error == 0);
		while ((req = TAILQ_FIRST(&outqueue)) == NULL) {
			error = pthread_cond_wait(&outqueue_cond,
			    &outqueue_mtx);
			assert(error == 0);
		}
		TAILQ_REMOVE(&outqueue, req, r_next);
		error = pthread_mutex_unlock(&outqueue_mtx);
		assert(error == 0);

		g_gate_log(LOG_DEBUG, "%s: offset=%jd length=%u", __func__,
		    (intmax_t)req->r_offset, (unsigned)req->r_length);

		/*
		 * Send the request.
		 */
		g_gate_swap2n_hdr(&req->r_hdr);
		if (g_gate_send(fd, &req->r_hdr, sizeof(req->r_hdr), 0) == -1) {
			g_gate_xlog("Error while sending hdr packet: %s.",
			    strerror(errno));
		}
		g_gate_log(LOG_DEBUG, "Sent hdr packet.");
		g_gate_swap2h_hdr(&req->r_hdr);
		if (req->r_data != NULL) {
			data = g_gate_send(fd, req->r_data, req->r_length, 0);
			if (data != (ssize_t)req->r_length) {
				g_gate_xlog("Error while sending data: %s.",
				    strerror(errno));
			}
			g_gate_log(LOG_DEBUG,
			    "Sent %zd bytes (offset=%ju, size=%zu).", data,
			    (uintmax_t)req->r_offset, (size_t)req->r_length);
			free(req->r_data);
		}
		free(req);
	}

	/* NOTREACHED */
	return (NULL);
}

static void
log_connection(struct sockaddr *from)
{
	in_addr_t ip;

	ip = htonl(((struct sockaddr_in *)(void *)from)->sin_addr.s_addr);
	g_gate_log(LOG_INFO, "Connection from: %s.", ip2str(ip));
}

static int
handshake(struct sockaddr *from, int sfd)
{
	struct g_gate_version ver;
	struct g_gate_cinit cinit;
	struct g_gate_sinit sinit;
	struct ggd_connection *conn;
	struct ggd_export *ex;
	ssize_t data;

	log_connection(from);
	/*
	 * Phase 1: Version verification.
	 */
	g_gate_log(LOG_DEBUG, "Receiving version packet.");
	data = g_gate_recv(sfd, &ver, sizeof(ver), MSG_WAITALL);
	g_gate_swap2h_version(&ver);
	if (data != sizeof(ver)) {
		g_gate_log(LOG_WARNING, "Malformed version packet.");
		return (0);
	}
	g_gate_log(LOG_DEBUG, "Version packet received.");
	if (memcmp(ver.gv_magic, GGATE_MAGIC, strlen(GGATE_MAGIC)) != 0) {
		g_gate_log(LOG_WARNING, "Invalid magic field.");
		return (0);
	}
	if (ver.gv_version != GGATE_VERSION) {
		g_gate_log(LOG_WARNING, "Version %u is not supported.",
		    ver.gv_version);
		return (0);
	}
	ver.gv_error = 0;
	g_gate_swap2n_version(&ver);
	data = g_gate_send(sfd, &ver, sizeof(ver), 0);
	g_gate_swap2h_version(&ver);
	if (data == -1) {
		sendfail(sfd, errno, "Error while sending version packet: %s.",
		    strerror(errno));
		return (0);
	}

	/*
	 * Phase 2: Request verification.
	 */
	g_gate_log(LOG_DEBUG, "Receiving initial packet.");
	data = g_gate_recv(sfd, &cinit, sizeof(cinit), MSG_WAITALL);
	g_gate_swap2h_cinit(&cinit);
	if (data != sizeof(cinit)) {
		g_gate_log(LOG_WARNING, "Malformed initial packet.");
		return (0);
	}
	g_gate_log(LOG_DEBUG, "Initial packet received.");
	conn = connection_find(&cinit);
	if (conn != NULL) {
		/*
		 * Connection should already exists.
		 */
		g_gate_log(LOG_DEBUG, "Found existing connection (token=%lu).",
		    (unsigned long)conn->c_token);
		if (connection_add(conn, &cinit, from, sfd) == -1) {
			connection_remove(conn);
			return (0);
		}
	} else {
		/*
		 * New connection, allocate space.
		 */
		conn = connection_new(&cinit, from, sfd);
		if (conn == NULL) {
			sendfail(sfd, ENOMEM,
			    "Cannot allocate new connection.");
			return (0);
		}
		g_gate_log(LOG_DEBUG, "New connection created (token=%lu).",
		    (unsigned long)conn->c_token);
	}

	ex = exports_find(from, &cinit, conn);
	if (ex == NULL) {
		sendfail(sfd, errno, NULL);
		connection_remove(conn);
		return (0);
	}
	if (conn->c_mediasize == 0) {
		conn->c_mediasize = g_gate_mediasize(conn->c_diskfd);
		conn->c_sectorsize = g_gate_sectorsize(conn->c_diskfd);
	}
	sinit.gs_mediasize = conn->c_mediasize;
	sinit.gs_sectorsize = conn->c_sectorsize;
	sinit.gs_error = 0;

	g_gate_log(LOG_DEBUG, "Sending initial packet.");

	g_gate_swap2n_sinit(&sinit);
	data = g_gate_send(sfd, &sinit, sizeof(sinit), 0);
	g_gate_swap2h_sinit(&sinit);
	if (data == -1) {
		sendfail(sfd, errno, "Error while sending initial packet: %s.",
		    strerror(errno));
		return (0);
	}

	if (connection_ready(conn)) {
		connection_launch(conn);
		connection_remove(conn);
	}
	return (1);
}

static void
huphandler(int sig __unused)
{

	got_sighup = 1;
}

int
main(int argc, char *argv[])
{
	const char *ggated_pidfile = _PATH_VARRUN "/ggated.pid";
	struct pidfh *pfh;
	struct sockaddr_in serv;
	struct sockaddr from;
	socklen_t fromlen;
	pid_t otherpid;
	int ch, sfd, tmpsfd;
	unsigned port;

	bindaddr = htonl(INADDR_ANY);
	port = G_GATE_PORT;
	while ((ch = getopt(argc, argv, "a:hnp:F:R:S:v")) != -1) {
		switch (ch) {
		case 'a':
			bindaddr = g_gate_str2ip(optarg);
			if (bindaddr == INADDR_NONE) {
				errx(EXIT_FAILURE,
				    "Invalid IP/host name to bind to.");
			}
			break;
		case 'F':
			ggated_pidfile = optarg;
			break;
		case 'n':
			nagle = 0;
			break;
		case 'p':
			errno = 0;
			port = strtoul(optarg, NULL, 10);
			if (port == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid port.");
			break;
		case 'R':
			errno = 0;
			rcvbuf = strtoul(optarg, NULL, 10);
			if (rcvbuf == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid rcvbuf.");
			break;
		case 'S':
			errno = 0;
			sndbuf = strtoul(optarg, NULL, 10);
			if (sndbuf == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid sndbuf.");
			break;
		case 'v':
			g_gate_verbose++;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argv[0] != NULL)
		exports_file = argv[0];
	exports_get();

	pfh = pidfile_open(ggated_pidfile, 0600, &otherpid);
	if (pfh == NULL) {
		if (errno == EEXIST) {
			errx(EXIT_FAILURE, "Daemon already running, pid: %jd.",
			    (intmax_t)otherpid);
		}
		err(EXIT_FAILURE, "Cannot open/create pidfile");
	}

	if (!g_gate_verbose) {
		/* Run in daemon mode. */
		if (daemon(0, 0) == -1)
			g_gate_xlog("Cannot daemonize: %s", strerror(errno));
	}

	pidfile_write(pfh);

	signal(SIGCHLD, SIG_IGN);

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1)
		g_gate_xlog("Cannot open stream socket: %s.", strerror(errno));
	bzero(&serv, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = bindaddr;
	serv.sin_port = htons(port);

	g_gate_socket_settings(sfd);

	if (bind(sfd, (struct sockaddr *)&serv, sizeof(serv)) == -1)
		g_gate_xlog("bind(): %s.", strerror(errno));
	if (listen(sfd, 5) == -1)
		g_gate_xlog("listen(): %s.", strerror(errno));

	g_gate_log(LOG_INFO, "Listen on port: %d.", port);

	signal(SIGHUP, huphandler);

	for (;;) {
		fromlen = sizeof(from);
		tmpsfd = accept(sfd, &from, &fromlen);
		if (tmpsfd == -1)
			g_gate_xlog("accept(): %s.", strerror(errno));

		if (got_sighup) {
			got_sighup = 0;
			exports_get();
		}

		if (!handshake(&from, tmpsfd))
			close(tmpsfd);
	}
	close(sfd);
	pidfile_remove(pfh);
	exit(EXIT_SUCCESS);
}
