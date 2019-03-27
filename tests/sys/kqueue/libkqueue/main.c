/*
 * Copyright (c) 2009 Mark Heily <mark@heily.com>
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include "config.h"
#include "common.h"

int testnum = 1;
char *cur_test_id = NULL;
int kqfd;

extern void test_evfilt_read();
extern void test_evfilt_signal();
extern void test_evfilt_vnode();
extern void test_evfilt_timer();
extern void test_evfilt_proc();
#if HAVE_EVFILT_USER
extern void test_evfilt_user();
#endif

/* Checks if any events are pending, which is an error. */
void 
test_no_kevents(void)
{
    int nfds;
    struct timespec timeo;
    struct kevent kev;
    char *kev_str;

    puts("confirming that there are no events pending");
    memset(&timeo, 0, sizeof(timeo));
    nfds = kevent(kqfd, NULL, 0, &kev, 1, &timeo);
    if (nfds != 0) {
        puts("\nUnexpected event:");
        kev_str = kevent_to_str(&kev);
        puts(kev_str);
        free(kev_str);
        errx(1, "%d event(s) pending, but none expected:", nfds);
    }
}

/* Checks if any events are pending, which is an error. Do not print
 * out anything unless events are found.
*/
void 
test_no_kevents_quietly(void)
{
    int nfds;
    struct timespec timeo;
    struct kevent kev;
    char *kev_str;

    memset(&timeo, 0, sizeof(timeo));
    nfds = kevent(kqfd, NULL, 0, &kev, 1, &timeo);
    if (nfds != 0) {
        puts("\nUnexpected event:");
        kev_str = kevent_to_str(&kev);
        puts(kev_str);
        free(kev_str);
        errx(1, "%d event(s) pending, but none expected:", nfds);
    }
}

/* Retrieve a single kevent */
struct kevent *
kevent_get(int kqfd)
{
    int nfds;
    struct kevent *kev;

    if ((kev = calloc(1, sizeof(*kev))) == NULL)
        err(1, "out of memory");
    
    nfds = kevent(kqfd, NULL, 0, kev, 1, NULL);
    if (nfds < 1)
        err(1, "kevent(2)");

    return (kev);
}

/* Retrieve a single kevent, specifying a maximum time to wait for it. */
struct kevent *
kevent_get_timeout(int kqfd, int seconds)
{
    int nfds;
    struct kevent *kev;
    struct timespec timeout = {seconds, 0};

    if ((kev = calloc(1, sizeof(*kev))) == NULL)
        err(1, "out of memory");
    
    nfds = kevent(kqfd, NULL, 0, kev, 1, &timeout);
    if (nfds < 0) {
        err(1, "kevent(2)");
    } else if (nfds == 0) {
        free(kev);
        kev = NULL;
    }

    return (kev);
}

char *
kevent_fflags_dump(struct kevent *kev)
{
    char *buf;

#define KEVFFL_DUMP(attrib) \
    if (kev->fflags & attrib) \
        strncat(buf, #attrib" ", 64);

    if ((buf = calloc(1, 1024)) == NULL)
        abort();

    /* Not every filter has meaningful fflags */
    if (kev->filter == EVFILT_PROC) {
        snprintf(buf, 1024, "fflags = %x (", kev->fflags);
        KEVFFL_DUMP(NOTE_EXIT);
        KEVFFL_DUMP(NOTE_FORK);
        KEVFFL_DUMP(NOTE_EXEC);
        KEVFFL_DUMP(NOTE_CHILD);
        KEVFFL_DUMP(NOTE_TRACKERR);
        KEVFFL_DUMP(NOTE_TRACK);
        buf[strlen(buf) - 1] = ')';
    } else if (kev->filter == EVFILT_PROCDESC) {
        snprintf(buf, 1024, "fflags = %x (", kev->fflags);
        KEVFFL_DUMP(NOTE_EXIT);
        KEVFFL_DUMP(NOTE_FORK);
        KEVFFL_DUMP(NOTE_EXEC);
        buf[strlen(buf) - 1] = ')';
    } else if (kev->filter == EVFILT_VNODE) {
        snprintf(buf, 1024, "fflags = %x (", kev->fflags);
        KEVFFL_DUMP(NOTE_DELETE);
        KEVFFL_DUMP(NOTE_WRITE);
        KEVFFL_DUMP(NOTE_EXTEND);
#if HAVE_NOTE_TRUNCATE
        KEVFFL_DUMP(NOTE_TRUNCATE);
#endif
        KEVFFL_DUMP(NOTE_ATTRIB);
        KEVFFL_DUMP(NOTE_LINK);
        KEVFFL_DUMP(NOTE_RENAME);
#if HAVE_NOTE_REVOKE
        KEVFFL_DUMP(NOTE_REVOKE);
#endif
        buf[strlen(buf) - 1] = ')';
    } else {
        snprintf(buf, 1024, "fflags = %x", kev->fflags);
    }

    return (buf);
}

char *
kevent_flags_dump(struct kevent *kev)
{
    char *buf;

#define KEVFL_DUMP(attrib) \
    if (kev->flags & attrib) \
        strncat(buf, #attrib" ", 64);

    if ((buf = calloc(1, 1024)) == NULL)
        abort();

    snprintf(buf, 1024, "flags = %d (", kev->flags);
    KEVFL_DUMP(EV_ADD);
    KEVFL_DUMP(EV_ENABLE);
    KEVFL_DUMP(EV_DISABLE);
    KEVFL_DUMP(EV_DELETE);
    KEVFL_DUMP(EV_ONESHOT);
    KEVFL_DUMP(EV_CLEAR);
    KEVFL_DUMP(EV_EOF);
    KEVFL_DUMP(EV_ERROR);
#if HAVE_EV_DISPATCH
    KEVFL_DUMP(EV_DISPATCH);
#endif
#if HAVE_EV_RECEIPT
    KEVFL_DUMP(EV_RECEIPT);
#endif
    buf[strlen(buf) - 1] = ')';

    return (buf);
}

/* Copied from ../kevent.c kevent_dump() and improved */
char *
kevent_to_str(struct kevent *kev)
{
    char buf[512];
    char *flags_str = kevent_flags_dump(kev);
    char *fflags_str = kevent_fflags_dump(kev);

    snprintf(&buf[0], sizeof(buf), 
            "[ident=%ju, filter=%d, %s, %s, data=%jd, udata=%p, "
            "ext=[%jx %jx %jx %jx]",
            (uintmax_t) kev->ident,
            kev->filter,
            flags_str,
            fflags_str,
            (uintmax_t)kev->data,
            kev->udata,
            (uintmax_t)kev->ext[0],
            (uintmax_t)kev->ext[1],
            (uintmax_t)kev->ext[2],
            (uintmax_t)kev->ext[3]);

    free(flags_str);
    free(fflags_str);
    
    return (strdup(buf));
}

void
kevent_add(int kqfd, struct kevent *kev, 
        uintptr_t ident,
        short     filter,
        u_short   flags,
        u_int     fflags,
        intptr_t  data,
        void      *udata)
{
    char *kev_str;
    
    EV_SET(kev, ident, filter, flags, fflags, data, NULL);    
    if (kevent(kqfd, kev, 1, NULL, 0, NULL) < 0) {
        kev_str = kevent_to_str(kev);
        printf("Unable to add the following kevent:\n%s\n",
                kev_str);
        free(kev_str);
        err(1, "kevent(): %s", strerror(errno));
    }
}

void
kevent_cmp(struct kevent *k1, struct kevent *k2)
{
    char *kev1_str;
    char *kev2_str;
    
/* XXX-
   Workaround for inconsistent implementation of kevent(2) 
 */
#ifdef __FreeBSD__
    if (k1->flags & EV_ADD)
        k2->flags |= EV_ADD;
#endif
    if (k1->ident != k2->ident || k1->filter != k2->filter ||
      k1->flags != k2->flags || k1->fflags != k2->fflags ||
      k1->data != k2->data || k1->udata != k2->udata ||
      k1->ext[0] != k2->ext[0] || k1->ext[1] != k2->ext[1] ||
      k1->ext[0] != k2->ext[2] || k1->ext[0] != k2->ext[3]) {
        kev1_str = kevent_to_str(k1);
        kev2_str = kevent_to_str(k2);
        printf("kevent_cmp: mismatch:\n  %s !=\n  %s\n", 
               kev1_str, kev2_str);
        free(kev1_str);
        free(kev2_str);
        abort();
    }
}

void
test_begin(const char *func)
{
    if (cur_test_id)
        free(cur_test_id);
    cur_test_id = strdup(func);
    if (!cur_test_id)
        err(1, "strdup failed");

    printf("\n\nTest %d: %s\n", testnum++, func);
}

void
success(void)
{
    printf("%-70s %s\n", cur_test_id, "passed");
    free(cur_test_id);
    cur_test_id = NULL;
}

void
test_kqueue(void)
{
    test_begin("kqueue()");
    if ((kqfd = kqueue()) < 0)
        err(1, "kqueue()");
    test_no_kevents();
    success();
}

void
test_kqueue_close(void)
{
    test_begin("close(kq)");
    if (close(kqfd) < 0)
        err(1, "close()");
    success();
}

int 
main(int argc, char **argv)
{
    int test_proc = 1;
    int test_socket = 1;
    int test_signal = 1;
    int test_vnode = 1;
    int test_timer = 1;
#ifdef __FreeBSD__
    int test_user = 1;
#else
    /* XXX-FIXME temporary */
    int test_user = 0;
#endif

    while (argc) {
        if (strcmp(argv[0], "--no-proc") == 0)
            test_proc = 0;
        if (strcmp(argv[0], "--no-socket") == 0)
            test_socket = 0;
        if (strcmp(argv[0], "--no-timer") == 0)
            test_timer = 0;
        if (strcmp(argv[0], "--no-signal") == 0)
            test_signal = 0;
        if (strcmp(argv[0], "--no-vnode") == 0)
            test_vnode = 0;
        if (strcmp(argv[0], "--no-user") == 0)
            test_user = 0;
        argv++;
        argc--;
    }

    /*
     * Some tests fork.  If output is fully buffered,
     * the children inherit some buffered data and flush
     * it when they exit, causing some data to be printed twice.
     * Use line buffering to avoid this problem.
     */
    setlinebuf(stdout);
    setlinebuf(stderr);

    test_kqueue();
    test_kqueue_close();

    if (test_socket) 
        test_evfilt_read();
    if (test_signal) 
        test_evfilt_signal();
    if (test_vnode) 
        test_evfilt_vnode();
#if HAVE_EVFILT_USER
    if (test_user) 
        test_evfilt_user();
#endif
    if (test_timer) 
        test_evfilt_timer();
    if (test_proc) 
        test_evfilt_proc();

    printf("\n---\n"
            "+OK All %d tests completed.\n", testnum - 1);
    return (0);
}
