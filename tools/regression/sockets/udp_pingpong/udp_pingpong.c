/*-
 * Copyright (c) 2017 Maksym Sobolyev <sobomax@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The test that setups two processes A and B and make A sending
 * B UDP packet(s) and B send it back. The time of sending is recorded
 * in the payload and time of the arrival is either determined by
 * reading clock after recv() completes or using kernel-supplied
 * via recvmsg(). End-to-end time t(A->B->A) is then calculated
 * and compared against time for both t(A->B) + t(B->A) to make
 * sure it makes sense.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define	NPKTS		1000
#define	PKT_SIZE	128
/* Timeout to receive pong on the side A, 100ms */
#define SRECV_TIMEOUT	(1 * 100)
/*
 * Timeout to receive ping on the side B. 4x as large as on the side A,
 * so that in the case of packet loss the side A will have a chance to
 * realize that and send few more before B bails out.
 */
#define RRECV_TIMEOUT	(SRECV_TIMEOUT * 4)
#define MIN_NRECV	((NPKTS * 99) / 100) /* 99% */

//#define	SIMULATE_PLOSS

struct trip_ts {
    struct timespec sent;
    struct timespec recvd;
};

struct test_pkt {
    int pnum;
    struct trip_ts tss[2];
    int lost;
    unsigned char data[PKT_SIZE];
};

struct test_ctx {
    const char *name;
    int fds[2];
    struct pollfd pfds[2];
    union {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;
    } sin[2];
    struct test_pkt test_pkts[NPKTS];
    int nsent;
    int nrecvd;
    clockid_t clock;
    int use_recvmsg;
    int ts_type;
};

struct rtt {
    struct timespec a2b;
    struct timespec b2a;
    struct timespec e2e;
    struct timespec a2b_b2a;
};

#define SEC(x)		((x)->tv_sec)
#define NSEC(x)		((x)->tv_nsec)
#define NSEC_MAX	1000000000L
#define NSEC_IN_USEC	1000L

#define timeval2timespec(tv, ts)                                   \
    do {                                                           \
        SEC(ts) = (tv)->tv_sec;                                    \
        NSEC(ts) = (tv)->tv_usec * NSEC_IN_USEC;                   \
    } while (0);

static const struct timespec zero_ts;
/* 0.01s, should be more than enough for the loopback communication  */
static const struct timespec max_ts = {.tv_nsec = (NSEC_MAX / 100)};

enum ts_types {TT_TIMESTAMP = -2, TT_BINTIME = -1,
  TT_REALTIME_MICRO = SO_TS_REALTIME_MICRO, TT_TS_BINTIME = SO_TS_BINTIME,
  TT_REALTIME = SO_TS_REALTIME, TT_MONOTONIC = SO_TS_MONOTONIC};

static clockid_t
get_clock_type(struct test_ctx *tcp)
{
    switch (tcp->ts_type) {
    case TT_TIMESTAMP:
    case TT_BINTIME:
    case TT_REALTIME_MICRO:
    case TT_TS_BINTIME:
    case TT_REALTIME:
        return (CLOCK_REALTIME);

    case TT_MONOTONIC:
        return (CLOCK_MONOTONIC);
    }
    abort();
}

static int
get_scm_type(struct test_ctx *tcp)
{
    switch (tcp->ts_type) {
    case TT_TIMESTAMP:
    case TT_REALTIME_MICRO:
        return (SCM_TIMESTAMP);

    case TT_BINTIME:
    case TT_TS_BINTIME:
        return (SCM_BINTIME);

    case TT_REALTIME:
        return (SCM_REALTIME);

    case TT_MONOTONIC:
        return (SCM_MONOTONIC);
    }
    abort();
}

static size_t
get_scm_size(struct test_ctx *tcp)
{
    switch (tcp->ts_type) {
    case TT_TIMESTAMP:
    case TT_REALTIME_MICRO:
        return (sizeof(struct timeval));

    case TT_BINTIME:
    case TT_TS_BINTIME:
        return (sizeof(struct bintime));

    case TT_REALTIME:
    case TT_MONOTONIC:
        return (sizeof(struct timespec));
    }
    abort();
}

static void
setup_ts_sockopt(struct test_ctx *tcp, int fd)
{
    int rval, oname1, oname2, sval1, sval2;

    oname1 = SO_TIMESTAMP;
    oname2 = -1;
    sval2 = -1;

    switch (tcp->ts_type) {
    case TT_REALTIME_MICRO:
    case TT_TS_BINTIME:
    case TT_REALTIME:
    case TT_MONOTONIC:
        oname2 = SO_TS_CLOCK;
        sval2 = tcp->ts_type;
        break;

    case TT_TIMESTAMP:
        break;

    case TT_BINTIME:
        oname1 = SO_BINTIME;
        break;

    default:
        abort();
    }

    sval1 = 1;
    rval = setsockopt(fd, SOL_SOCKET, oname1, &sval1,
      sizeof(sval1));
    if (rval != 0) {
        err(1, "%s: setup_udp: setsockopt(%d, %d, 1)", tcp->name,
          fd, oname1);
    }
    if (oname2 == -1)
        return;
    rval = setsockopt(fd, SOL_SOCKET, oname2, &sval2,
      sizeof(sval2));
    if (rval != 0) {
        err(1, "%s: setup_udp: setsockopt(%d, %d, %d)",
          tcp->name, fd, oname2, sval2);
    }
}


static void
setup_udp(struct test_ctx *tcp)
{
    int i;
    socklen_t sin_len, af_len;

    af_len = sizeof(tcp->sin[0].v4);
    for (i = 0; i < 2; i++) {
        tcp->sin[i].v4.sin_len = af_len;
        tcp->sin[i].v4.sin_family = AF_INET;
        tcp->sin[i].v4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tcp->fds[i] = socket(PF_INET, SOCK_DGRAM, 0);
        if (tcp->fds[i] < 0)
            err(1, "%s: setup_udp: socket", tcp->name);
        if (bind(tcp->fds[i], (struct sockaddr *)&tcp->sin[i], af_len) < 0)
            err(1, "%s: setup_udp: bind(%s, %d)", tcp->name,
              inet_ntoa(tcp->sin[i].v4.sin_addr), 0);
        sin_len = af_len;
        if (getsockname(tcp->fds[i], (struct sockaddr *)&tcp->sin[i], &sin_len) < 0)
            err(1, "%s: setup_udp: getsockname(%d)", tcp->name, tcp->fds[i]);
        if (tcp->use_recvmsg != 0) {
            setup_ts_sockopt(tcp, tcp->fds[i]);
        }

        tcp->pfds[i].fd = tcp->fds[i];
        tcp->pfds[i].events = POLLIN;
    }

    if (connect(tcp->fds[0], (struct sockaddr *)&tcp->sin[1], af_len) < 0)
        err(1, "%s: setup_udp: connect(%s, %d)", tcp->name,
          inet_ntoa(tcp->sin[1].v4.sin_addr), ntohs(tcp->sin[1].v4.sin_port));
    if (connect(tcp->fds[1], (struct sockaddr *)&tcp->sin[0], af_len) < 0)
        err(1, "%s: setup_udp: connect(%s, %d)", tcp->name,
          inet_ntoa(tcp->sin[0].v4.sin_addr), ntohs(tcp->sin[0].v4.sin_port));
}

static char *
inet_ntoa6(const void *sin6_addr)
{
    static char straddr[INET6_ADDRSTRLEN];

    inet_ntop(AF_INET6, sin6_addr, straddr, sizeof(straddr));
    return (straddr);
}

static void
setup_udp6(struct test_ctx *tcp)
{
    int i;
    socklen_t sin_len, af_len;

    af_len = sizeof(tcp->sin[0].v6);
    for (i = 0; i < 2; i++) {
        tcp->sin[i].v6.sin6_len = af_len;
        tcp->sin[i].v6.sin6_family = AF_INET6;
        tcp->sin[i].v6.sin6_addr = in6addr_loopback;
        tcp->fds[i] = socket(PF_INET6, SOCK_DGRAM, 0);
        if (tcp->fds[i] < 0)
            err(1, "%s: setup_udp: socket", tcp->name);
        if (bind(tcp->fds[i], (struct sockaddr *)&tcp->sin[i], af_len) < 0)
            err(1, "%s: setup_udp: bind(%s, %d)", tcp->name,
              inet_ntoa6(&tcp->sin[i].v6.sin6_addr), 0);
        sin_len = af_len;
        if (getsockname(tcp->fds[i], (struct sockaddr *)&tcp->sin[i], &sin_len) < 0)
            err(1, "%s: setup_udp: getsockname(%d)", tcp->name, tcp->fds[i]);
        if (tcp->use_recvmsg != 0) {
            setup_ts_sockopt(tcp, tcp->fds[i]);
        }

        tcp->pfds[i].fd = tcp->fds[i];
        tcp->pfds[i].events = POLLIN;
    }

    if (connect(tcp->fds[0], (struct sockaddr *)&tcp->sin[1], af_len) < 0)
        err(1, "%s: setup_udp: connect(%s, %d)", tcp->name,
          inet_ntoa6(&tcp->sin[1].v6.sin6_addr),
          ntohs(tcp->sin[1].v6.sin6_port));
    if (connect(tcp->fds[1], (struct sockaddr *)&tcp->sin[0], af_len) < 0)
        err(1, "%s: setup_udp: connect(%s, %d)", tcp->name,
          inet_ntoa6(&tcp->sin[0].v6.sin6_addr),
          ntohs(tcp->sin[0].v6.sin6_port));
}

static void
teardown_udp(struct test_ctx *tcp)
{

    close(tcp->fds[0]);
    close(tcp->fds[1]);
}

static void
send_pkt(struct test_ctx *tcp, int pnum, int fdidx, const char *face)
{
    ssize_t r;
    size_t slen;

    slen = sizeof(tcp->test_pkts[pnum]);
    clock_gettime(get_clock_type(tcp), &tcp->test_pkts[pnum].tss[fdidx].sent);
    r = send(tcp->fds[fdidx], &tcp->test_pkts[pnum], slen, 0);
    if (r < 0) {
        err(1, "%s: %s: send(%d)", tcp->name, face, tcp->fds[fdidx]);
    }
    if (r < (ssize_t)slen) {
        errx(1, "%s: %s: send(%d): short send", tcp->name, face,
          tcp->fds[fdidx]);
    }
    tcp->nsent += 1;
}

#define PDATA(tcp, i) ((tcp)->test_pkts[(i)].data)

static void
hdr_extract_ts(struct test_ctx *tcp, struct msghdr *mhp, struct timespec *tp)
{
    int scm_type;
    size_t scm_size;
    union {
        struct timespec ts;
        struct bintime bt;
        struct timeval tv;
    } tdata;
    struct cmsghdr *cmsg;

    scm_type = get_scm_type(tcp);
    scm_size = get_scm_size(tcp);
    for (cmsg = CMSG_FIRSTHDR(mhp); cmsg != NULL;
      cmsg = CMSG_NXTHDR(mhp, cmsg)) {
        if ((cmsg->cmsg_level == SOL_SOCKET) &&
          (cmsg->cmsg_type == scm_type)) {
            memcpy(&tdata, CMSG_DATA(cmsg), scm_size);
            break;
        }
    }
    if (cmsg == NULL) {
        abort();
    }
    switch (tcp->ts_type) {
    case TT_REALTIME:
    case TT_MONOTONIC:
        *tp = tdata.ts;
        break;

    case TT_TIMESTAMP:
    case TT_REALTIME_MICRO:
        timeval2timespec(&tdata.tv, tp);
        break;

    case TT_BINTIME:
    case TT_TS_BINTIME:
        bintime2timespec(&tdata.bt, tp);
        break;

    default:
        abort();
    }
}

static void
recv_pkt_recvmsg(struct test_ctx *tcp, int fdidx, const char *face, void *buf,
  size_t rlen, struct timespec *tp)
{
    /* We use a union to make sure hdr is aligned */
    union {
        struct cmsghdr hdr;
        unsigned char buf[CMSG_SPACE(1024)];
    } cmsgbuf;
    struct msghdr msg;
    struct iovec iov;
    ssize_t rval;

    memset(&msg, '\0', sizeof(msg));
    iov.iov_base = buf;
    iov.iov_len = rlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf.buf;
    msg.msg_controllen = sizeof(cmsgbuf.buf);

    rval = recvmsg(tcp->fds[fdidx], &msg, 0);
    if (rval < 0) {
        err(1, "%s: %s: recvmsg(%d)", tcp->name, face, tcp->fds[fdidx]);
    }
    if (rval < (ssize_t)rlen) {
        errx(1, "%s: %s: recvmsg(%d): short recv", tcp->name, face,
          tcp->fds[fdidx]);
    }

    hdr_extract_ts(tcp, &msg, tp);
}

static void
recv_pkt_recv(struct test_ctx *tcp, int fdidx, const char *face, void *buf,
  size_t rlen, struct timespec *tp)
{
    ssize_t rval;

    rval = recv(tcp->fds[fdidx], buf, rlen, 0);
    clock_gettime(get_clock_type(tcp), tp);
    if (rval < 0) {
        err(1, "%s: %s: recv(%d)", tcp->name, face, tcp->fds[fdidx]);
    }
    if (rval < (ssize_t)rlen) {
        errx(1, "%s: %s: recv(%d): short recv", tcp->name, face,
            tcp->fds[fdidx]);
    }
}

static int
recv_pkt(struct test_ctx *tcp, int fdidx, const char *face, int tout)
{
    int pr;
    struct test_pkt recv_buf;
    size_t rlen;

    pr = poll(&tcp->pfds[fdidx], 1, tout);
    if (pr < 0) {
        err(1, "%s: %s: poll(%d)", tcp->name, face, tcp->fds[fdidx]);
    }
    if (pr == 0) {
        return (-1);
    }
    if(tcp->pfds[fdidx].revents != POLLIN) {
        errx(1, "%s: %s: poll(%d): unexpected result", tcp->name, face,
          tcp->fds[fdidx]);
    }
    rlen = sizeof(recv_buf);
    if (tcp->use_recvmsg == 0) {
        recv_pkt_recv(tcp, fdidx, face, &recv_buf, rlen,
          &recv_buf.tss[fdidx].recvd);
    } else {
        recv_pkt_recvmsg(tcp, fdidx, face, &recv_buf, rlen,
          &recv_buf.tss[fdidx].recvd);
    }
    if (recv_buf.pnum < 0 || recv_buf.pnum >= NPKTS ||
      memcmp(recv_buf.data, PDATA(tcp, recv_buf.pnum), PKT_SIZE) != 0) {
        errx(1, "%s: %s: recv(%d): corrupted data, packet %d", tcp->name,
          face, tcp->fds[fdidx], recv_buf.pnum);
    }
    tcp->nrecvd += 1;
    memcpy(tcp->test_pkts[recv_buf.pnum].tss, recv_buf.tss,
      sizeof(recv_buf.tss));
    tcp->test_pkts[recv_buf.pnum].lost = 0;
    return (recv_buf.pnum);
}

static void
test_server(struct test_ctx *tcp)
{
    int i, j;

    for (i = 0; i < NPKTS; i++) {
        send_pkt(tcp, i, 0, __FUNCTION__);
        j = recv_pkt(tcp, 0, __FUNCTION__, SRECV_TIMEOUT);
        if (j < 0) {
            warnx("packet %d is lost", i);
            /* timeout */
            continue;
        }
    }
}

static void
test_client(struct test_ctx *tcp)
{
    int i, j;

    for (i = 0; i < NPKTS; i++) {
        j = recv_pkt(tcp, 1, __FUNCTION__, RRECV_TIMEOUT);
        if (j < 0) {
            /* timeout */
            return;
        }
#if defined(SIMULATE_PLOSS)
        if ((i % 99) == 0) {
            warnx("dropping packet %d", i);
            continue;
        }
#endif
        send_pkt(tcp, j, 1, __FUNCTION__);
    }
}

static void
calc_rtt(struct test_pkt *tpp, struct rtt *rttp)
{

    timespecsub(&tpp->tss[1].recvd, &tpp->tss[0].sent, &rttp->a2b);
    timespecsub(&tpp->tss[0].recvd, &tpp->tss[1].sent, &rttp->b2a);
    timespecadd(&rttp->a2b, &rttp->b2a, &rttp->a2b_b2a);
    timespecsub(&tpp->tss[0].recvd, &tpp->tss[0].sent, &rttp->e2e);
}

static void
test_run(int ts_type, int use_ipv6, int use_recvmsg, const char *name)
{
    struct test_ctx test_ctx;
    pid_t pid, cpid;
    int i, j, status;

    printf("Testing %s via %s: ", name, (use_ipv6 == 0) ? "IPv4" : "IPv6");
    fflush(stdout);
    bzero(&test_ctx, sizeof(test_ctx));
    test_ctx.name = name;
    test_ctx.use_recvmsg = use_recvmsg;
    test_ctx.ts_type = ts_type;
    if (use_ipv6 == 0) {
        setup_udp(&test_ctx);
    } else {
        setup_udp6(&test_ctx);
    }
    for (i = 0; i < NPKTS; i++) {
        test_ctx.test_pkts[i].pnum = i;
        test_ctx.test_pkts[i].lost = 1;
        for (j = 0; j < PKT_SIZE; j++) {
            test_ctx.test_pkts[i].data[j] = (unsigned char)random();
        }
    }
    cpid = fork();
    if (cpid < 0) {
        err(1, "%s: fork()", test_ctx.name);
    }
    if (cpid == 0) {
        test_client(&test_ctx);
        exit(0);
    }
    test_server(&test_ctx);
    pid = waitpid(cpid, &status, 0);
    if (pid == (pid_t)-1) {
        err(1, "%s: waitpid(%d)", test_ctx.name, cpid);
    }

    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            errx(1, "client exit status is %d",
              WEXITSTATUS(status));
        }
    } else {
        if (WIFSIGNALED(status))
            errx(1, "abnormal termination of client, signal %d%s",
              WTERMSIG(status), WCOREDUMP(status) ?
              " (core file generated)" : "");
        else
            errx(1, "termination of client, unknown status");
    }
    if (test_ctx.nrecvd < MIN_NRECV) {
        errx(1, "packet loss is too high %d received out of %d, min %d",
          test_ctx.nrecvd, test_ctx.nsent, MIN_NRECV);
    }
    for (i = 0; i < NPKTS; i++) {
        struct rtt rtt;
        if (test_ctx.test_pkts[i].lost != 0) {
            continue;
        }
        calc_rtt(&test_ctx.test_pkts[i], &rtt);
        if (!timespeccmp(&rtt.e2e, &rtt.a2b_b2a, >))
            errx(1, "end-to-end trip time is too small");
        if (!timespeccmp(&rtt.e2e, &max_ts, <))
            errx(1, "end-to-end trip time is too large");
        if (!timespeccmp(&rtt.a2b, &zero_ts, >))
            errx(1, "A2B trip time is not positive");
        if (!timespeccmp(&rtt.b2a, &zero_ts, >))
            errx(1, "B2A trip time is not positive");
    }
    teardown_udp(&test_ctx);
}

int
main(void)
{
    int i;

    srandomdev();

    for (i = 0; i < 2; i++) {
        test_run(0, i, 0, "send()/recv()");
        printf("OK\n");
        test_run(TT_TIMESTAMP, i, 1,
          "send()/recvmsg(), setsockopt(SO_TIMESTAMP, 1)");
        printf("OK\n");
        if (i == 0) {
            test_run(TT_BINTIME, i, 1,
              "send()/recvmsg(), setsockopt(SO_BINTIME, 1)");
            printf("OK\n");
        }
        test_run(TT_REALTIME_MICRO, i, 1,
          "send()/recvmsg(), setsockopt(SO_TS_CLOCK, SO_TS_REALTIME_MICRO)");
        printf("OK\n");
        test_run(TT_TS_BINTIME, i, 1,
          "send()/recvmsg(), setsockopt(SO_TS_CLOCK, SO_TS_BINTIME)");
        printf("OK\n");
        test_run(TT_REALTIME, i, 1,
          "send()/recvmsg(), setsockopt(SO_TS_CLOCK, SO_TS_REALTIME)");
        printf("OK\n");
        test_run(TT_MONOTONIC, i, 1,
          "send()/recvmsg(), setsockopt(SO_TS_CLOCK, SO_TS_MONOTONIC)");
        printf("OK\n");
    }
    exit(0);
}
