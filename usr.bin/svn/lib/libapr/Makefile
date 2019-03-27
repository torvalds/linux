# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

INTERNALLIB=	yes
LIB=		apr
SRCS=		apr_cpystrn.c apr_fnmatch.c apr_getpass.c apr_hash.c apr_skiplist.c \
		apr_pools.c apr_random.c apr_snprintf.c apr_strings.c \
		apr_strnatcmp.c apr_strtok.c apr_tables.c buffer.c \
		builtins.c charset.c common.c copy.c dir.c dso.c env.c \
		epoll.c errorcodes.c fileacc.c filedup.c filepath.c \
		filepath_util.c filestat.c flock.c fullrw.c getopt.c \
		global_mutex.c groupinfo.c ia32.c inet_ntop.c inet_pton.c \
		kqueue.c mktemp.c mmap.c multicast.c mutex.c open.c \
		otherchild.c pipe.c poll.c pollcb.c pollset.c port.c \
		ppc.c proc.c proc_mutex.c procsup.c rand.c readwrite.c \
		s390.c seek.c select.c sendrecv.c sha2.c sha2_glue.c \
		shm.c signals.c sockaddr.c socket_util.c sockets.c \
		sockopt.c solaris.c start.c tempdir.c thread.c thread_cond.c \
		thread_mutex.c thread_rwlock.c threadpriv.c time.c \
		timestr.c userinfo.c version.c waitio.c z_asio.c

.PATH:		${APR}/atomic/unix ${APR}/dso/unix ${APR}/file_io/unix \
		${APR}/locks/unix ${APR}/memory/unix ${APR}/misc/unix \
		${APR}/mmap/unix ${APR}/network_io/unix ${APR}/passwd \
		${APR}/poll/unix ${APR}/random/unix ${APR}/shmem/unix \
		${APR}/strings ${APR}/support/unix ${APR}/tables \
		${APR}/threadproc/unix ${APR}/time/unix ${APR}/user/unix \
		${APR}/include

CFLAGS+= 	-DHAVE_CONFIG_H -I${.CURDIR} \
		-I${APR}/include/arch/unix -I${APR}/include

.include <bsd.lib.mk>
