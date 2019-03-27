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

#include <sys/stat.h>

#include <err.h>

#include "config.h"
#include "common.h"

static int sigusr1_caught = 0;

int kqfd;

static void
sig_handler(int signum)
{
    sigusr1_caught = 1;
}

static void
add_and_delete(void)
{
    struct kevent kev;
    pid_t pid;

    /* Create a child that waits to be killed and then exits */
    pid = fork();
    if (pid == 0) {
        struct stat s;
        if (fstat(kqfd, &s) != -1)
            errx(1, "kqueue inherited across fork! (%s() at %s:%d)",
                __func__, __FILE__, __LINE__);

        pause();
        exit(2);
    }
    printf(" -- child created (pid %d)\n", (int) pid);

    test_begin("kevent(EVFILT_PROC, EV_ADD)");

    test_no_kevents();
    kevent_add(kqfd, &kev, pid, EVFILT_PROC, EV_ADD, 0, 0, NULL);
    test_no_kevents();

    success();

    test_begin("kevent(EVFILT_PROC, EV_DELETE)");

    sleep(1);
    test_no_kevents();
    kevent_add(kqfd, &kev, pid, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
    if (kill(pid, SIGKILL) < 0)
        err(1, "kill");
    sleep(1);
    test_no_kevents();

    success();

}

static void
proc_track(int sleep_time)
{
    char test_id[64];
    struct kevent kev;
    pid_t pid;
    int pipe_fd[2];
    ssize_t result;

    snprintf(test_id, sizeof(test_id),
             "kevent(EVFILT_PROC, NOTE_TRACK); sleep %d", sleep_time);
    test_begin(test_id);
    test_no_kevents();

    if (pipe(pipe_fd)) {
        err(1, "pipe (parent) failed! (%s() at %s:%d)",
            __func__, __FILE__, __LINE__);
    }

    /* Create a child to track. */
    pid = fork();
    if (pid == 0) { /* Child */
        pid_t grandchild = -1;

        /*
         * Give the parent a chance to start tracking us.
         */
        result = read(pipe_fd[1], test_id, 1);
        if (result != 1) {
            err(1, "read from pipe in child failed! (ret %zd) (%s() at %s:%d)",
                result, __func__, __FILE__, __LINE__);
        }

        /*
         * Spawn a grandchild that will immediately exit. If the kernel has bug
         * 180385, the parent will see a kevent with both NOTE_CHILD and
         * NOTE_EXIT. If that bug is fixed, it will see two separate kevents
         * for those notes. Note that this triggers the conditions for
         * detecting the bug quite reliably on a 1 CPU system (or if the test
         * process is restricted to a single CPU), but may not trigger it on a
         * multi-CPU system.
         */
        grandchild = fork();
        if (grandchild == 0) { /* Grandchild */
            if (sleep_time) sleep(sleep_time);
            exit(1);
        } else if (grandchild == -1) { /* Error */
            err(1, "fork (grandchild) failed! (%s() at %s:%d)",
                __func__, __FILE__, __LINE__);
        } else { /* Child (Grandchild Parent) */
            printf(" -- grandchild created (pid %d)\n", (int) grandchild);
        }
        if (sleep_time) sleep(sleep_time);
        exit(0);
    } else if (pid == -1) { /* Error */
        err(1, "fork (child) failed! (%s() at %s:%d)",
            __func__, __FILE__, __LINE__);
    }
    
    printf(" -- child created (pid %d)\n", (int) pid);

    kevent_add(kqfd, &kev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE,
               NOTE_TRACK | NOTE_EXEC | NOTE_EXIT | NOTE_FORK,
               0, NULL);

    printf(" -- tracking child (pid %d)\n", (int) pid);

    /* Now that we're tracking the child, tell it to proceed. */
    result = write(pipe_fd[0], test_id, 1);
    if (result != 1) {
        err(1, "write to pipe in parent failed! (ret %zd) (%s() at %s:%d)",
            result, __func__, __FILE__, __LINE__);
    }

    /*
     * Several events should be received:
     *  - NOTE_FORK (from child)
     *  - NOTE_CHILD (from grandchild)
     *  - NOTE_EXIT (from grandchild)
     *  - NOTE_EXIT (from child)
     *
     * The NOTE_FORK and NOTE_EXIT from the child could be combined into a
     * single event, but the NOTE_CHILD and NOTE_EXIT from the grandchild must
     * not be combined.
     *
     * The loop continues until no events are received within a 5 second
     * period, at which point it is assumed that no more will be coming. The
     * loop is deliberately designed to attempt to get events even after all
     * the expected ones are received in case some spurious events are
     * generated as well as the expected ones.
     */
    {
        int child_exit = 0;
        int child_fork = 0;
        int gchild_exit = 0;
        int gchild_note = 0;
        pid_t gchild_pid = -1;
        int done = 0;
        char *kev_str;
        
        while (!done)
        {
            int handled = 0;
            struct kevent *kevp;

            kevp = kevent_get_timeout(kqfd, 5);
            if (kevp == NULL) {
                done = 1;
            } else {
                kev_str = kevent_to_str(kevp);
                printf(" -- Received kevent: %s\n", kev_str);
                free(kev_str);
            
                if ((kevp->fflags & NOTE_CHILD) && (kevp->fflags & NOTE_EXIT)) {
                    errx(1, "NOTE_CHILD and NOTE_EXIT in same kevent: %s", kevent_to_str(kevp));
                }
            
                if (kevp->fflags & NOTE_CHILD) {
                    if (kevp->data == pid) {
                        if (!gchild_note) {
                            ++gchild_note;
                            gchild_pid = kevp->ident;
                            ++handled;
                        } else {
                            errx(1, "Spurious NOTE_CHILD: %s", kevent_to_str(kevp));
                        }
                    }
                }

                if (kevp->fflags & NOTE_EXIT) {
                    if ((kevp->ident == pid) && (!child_exit)) {
                        ++child_exit;
                        ++handled;
                    } else if ((kevp->ident == gchild_pid) && (!gchild_exit)) {
                        ++gchild_exit;
                        ++handled;
                    } else {
                        errx(1, "Spurious NOTE_EXIT: %s", kevent_to_str(kevp));
                    }
                }

                if (kevp->fflags & NOTE_FORK) {
                    if ((kevp->ident == pid) && (!child_fork)) {
                        ++child_fork;
                        ++handled;
                    } else {
                        errx(1, "Spurious NOTE_FORK: %s", kevent_to_str(kevp));
                    }
                }

                if (!handled) {
                    errx(1, "Spurious kevent: %s", kevent_to_str(kevp));
                }

                free(kevp);
            }
        }
        
        /* Make sure all expected events were received. */
        if (child_exit && child_fork && gchild_exit && gchild_note) {
            printf(" -- Received all expected events.\n");
        } else {
            errx(1, "Did not receive all expected events.");
        }
    }

    success();
}

#ifdef TODO
static void
event_trigger(void)
{
    struct kevent kev;
    pid_t pid;

    test_begin("kevent(EVFILT_PROC, wait)");

    /* Create a child that waits to be killed and then exits */
    pid = fork();
    if (pid == 0) {
        pause();
        printf(" -- child caught signal, exiting\n");
        exit(2);
    }
    printf(" -- child created (pid %d)\n", (int) pid);

    test_no_kevents();
    kevent_add(kqfd, &kev, pid, EVFILT_PROC, EV_ADD, 0, 0, NULL);

    /* Cause the child to exit, then retrieve the event */
    printf(" -- killing process %d\n", (int) pid);
    if (kill(pid, SIGUSR1) < 0)
        err(1, "kill");
    kevent_cmp(&kev, kevent_get(kqfd));
    test_no_kevents();

    success();
}

void
test_kevent_signal_disable(void)
{
    const char *test_id = "kevent(EVFILT_SIGNAL, EV_DISABLE)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, SIGUSR1, EVFILT_SIGNAL, EV_DISABLE, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Block SIGUSR1, then send it to ourselves */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        err(1, "sigprocmask");
    if (kill(getpid(), SIGKILL) < 0)
        err(1, "kill");

    test_no_kevents();

    success();
}

void
test_kevent_signal_enable(void)
{
    const char *test_id = "kevent(EVFILT_SIGNAL, EV_ENABLE)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, SIGUSR1, EVFILT_SIGNAL, EV_ENABLE, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Block SIGUSR1, then send it to ourselves */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        err(1, "sigprocmask");
    if (kill(getpid(), SIGUSR1) < 0)
        err(1, "kill");

    kev.flags = EV_ADD | EV_CLEAR;
#if LIBKQUEUE
    kev.data = 1; /* WORKAROUND */
#else
    kev.data = 2; // one extra time from test_kevent_signal_disable()
#endif
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Delete the watch */
    kev.flags = EV_DELETE;
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    success();
}

void
test_kevent_signal_del(void)
{
    const char *test_id = "kevent(EVFILT_SIGNAL, EV_DELETE)";
    struct kevent kev;

    test_begin(test_id);

    /* Delete the kevent */
    EV_SET(&kev, SIGUSR1, EVFILT_SIGNAL, EV_DELETE, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Block SIGUSR1, then send it to ourselves */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        err(1, "sigprocmask");
    if (kill(getpid(), SIGUSR1) < 0)
        err(1, "kill");

    test_no_kevents();
    success();
}

void
test_kevent_signal_oneshot(void)
{
    const char *test_id = "kevent(EVFILT_SIGNAL, EV_ONESHOT)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, SIGUSR1, EVFILT_SIGNAL, EV_ADD | EV_ONESHOT, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Block SIGUSR1, then send it to ourselves */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        err(1, "sigprocmask");
    if (kill(getpid(), SIGUSR1) < 0)
        err(1, "kill");

    kev.flags |= EV_CLEAR;
    kev.data = 1;
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Send another one and make sure we get no events */
    if (kill(getpid(), SIGUSR1) < 0)
        err(1, "kill");
    test_no_kevents();

    success();
}
#endif

void
test_evfilt_proc()
{
    kqfd = kqueue();

    signal(SIGUSR1, sig_handler);

    add_and_delete();
    proc_track(0); /* Run without sleeping before children exit. */
    proc_track(1); /* Sleep a bit in the children before exiting. */

#if TODO
    event_trigger();
#endif

    signal(SIGUSR1, SIG_DFL);

#if TODO
    test_kevent_signal_add();
    test_kevent_signal_del();
    test_kevent_signal_get();
    test_kevent_signal_disable();
    test_kevent_signal_enable();
    test_kevent_signal_oneshot();
#endif
    close(kqfd);
}
