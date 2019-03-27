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

#include "common.h"
#include <sys/time.h>

#define	MILLION 1000000
#define	THOUSAND 1000
#define	SEC_TO_MS(t) ((t) * THOUSAND)	/* Convert seconds to milliseconds. */
#define	SEC_TO_US(t) ((t) * MILLION)	/* Convert seconds to microseconds. */
#define	MS_TO_US(t)  ((t) * THOUSAND)	/* Convert milliseconds to microseconds. */
#define	US_TO_NS(t)  ((t) * THOUSAND)	/* Convert microseconds to nanoseconds. */

int kqfd;

/* Get the current time with microsecond precision. Used for
 * sub-second timing to make some timer tests run faster.
 */
static long
now(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return SEC_TO_US(tv.tv_sec) + tv.tv_usec;
}

/* Sleep for a given number of milliseconds. The timeout is assumed to
 * be less than 1 second.
 */
void
mssleep(int t)
{
    struct timespec stime = {
        .tv_sec = 0,
        .tv_nsec = US_TO_NS(MS_TO_US(t)),
    };

    nanosleep(&stime, NULL);
}

/* Sleep for a given number of microseconds. The timeout is assumed to
 * be less than 1 second.
 */
void
ussleep(int t)
{
    struct timespec stime = {
        .tv_sec = 0,
        .tv_nsec = US_TO_NS(t),
    };

    nanosleep(&stime, NULL);
}

void
test_kevent_timer_add(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, EV_ADD)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD, 0, 1000, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    success();
}

void
test_kevent_timer_del(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, EV_DELETE)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    test_no_kevents();

    success();
}

void
test_kevent_timer_get(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, wait)";
    struct kevent kev;

    test_begin(test_id);

    EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD, 0, 1000, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    kev.flags |= EV_CLEAR;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    EV_SET(&kev, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    success();
}

static void
test_oneshot(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, EV_ONESHOT)";
    struct kevent kev;

    test_begin(test_id);

    test_no_kevents();

    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 500,NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Retrieve the event */
    kev.flags = EV_ADD | EV_CLEAR | EV_ONESHOT;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Check if the event occurs again */
    sleep(3);
    test_no_kevents();


    success();
}

static void
test_periodic(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, periodic)";
    struct kevent kev;

    test_begin(test_id);

    test_no_kevents();

    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD, 0, 1000,NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Retrieve the event */
    kev.flags = EV_ADD | EV_CLEAR;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Check if the event occurs again */
    sleep(1);
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Delete the event */
    kev.flags = EV_DELETE;
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    success();
}

static void
disable_and_enable(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, EV_DISABLE and EV_ENABLE)";
    struct kevent kev;

    test_begin(test_id);

    test_no_kevents();

    /* Add the watch and immediately disable it */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 2000,NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);
    kev.flags = EV_DISABLE;
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);
    test_no_kevents();

    /* Re-enable and check again */
    kev.flags = EV_ENABLE;
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    kev.flags = EV_ADD | EV_CLEAR | EV_ONESHOT;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    success();
}

static void
test_abstime(void)
{
    const char *test_id = "kevent(EVFILT_TIMER, EV_ONESHOT, NOTE_ABSTIME)";
    struct kevent kev;
    time_t start;
    time_t stop;
    const int timeout = 3;

    test_begin(test_id);

    test_no_kevents();

    start = time(NULL);
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
      NOTE_ABSTIME | NOTE_SECONDS, start + timeout, NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Retrieve the event */
    kev.flags = EV_ADD | EV_ONESHOT;
    kev.data = 1;
    kev.fflags = 0;
    kevent_cmp(&kev, kevent_get(kqfd));
    stop = time(NULL);
    if (stop < start + timeout)
        err(1, "too early %jd %jd", (intmax_t)stop, (intmax_t)(start + timeout));

    /* Check if the event occurs again */
    sleep(3);
    test_no_kevents();

    success();
}

static void
test_update(void)
{
    const char *test_id = "kevent(EVFILT_TIMER (UPDATE), EV_ADD | EV_ONESHOT)";
    struct kevent kev;
    long elapsed;
    long start;

    test_begin(test_id);

    test_no_kevents();

    /* First set the timer to 1 second */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
        NOTE_USECONDS, SEC_TO_US(1), (void *)1);
    start = now();
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Now reduce the timer to 1 ms */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
        NOTE_USECONDS, MS_TO_US(1), (void *)2);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Wait for the event */
    kev.flags |= EV_CLEAR;
    kev.fflags &= ~NOTE_USECONDS;
    kev.data = 1;
    kevent_cmp(&kev, kevent_get(kqfd));
    elapsed = now() - start;

    /* Check that the timer expired after at least 1 ms, but less than
     * 1 second. This check is to make sure that the original 1 second
     * timeout was not used.
     */
    printf("timer expired after %ld us\n", elapsed);
    if (elapsed < MS_TO_US(1))
        errx(1, "early timer expiration: %ld us", elapsed);
    if (elapsed > SEC_TO_US(1))
        errx(1, "late timer expiration: %ld us", elapsed);

    success();
}

static void
test_update_equal(void)
{
    const char *test_id = "kevent(EVFILT_TIMER (UPDATE=), EV_ADD | EV_ONESHOT)";
    struct kevent kev;
    long elapsed;
    long start;

    test_begin(test_id);

    test_no_kevents();

    /* First set the timer to 1 ms */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
        NOTE_USECONDS, MS_TO_US(1), NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Sleep for a significant fraction of the timeout. */
    ussleep(600);
    
    /* Now re-add the timer with the same parameters */
    start = now();
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Wait for the event */
    kev.flags |= EV_CLEAR;
    kev.fflags &= ~NOTE_USECONDS;
    kev.data = 1;
    kevent_cmp(&kev, kevent_get(kqfd));
    elapsed = now() - start;

    /* Check that the timer expired after at least 1 ms. This check is
     * to make sure that the timer re-started and that the event is
     * not from the original add of the timer.
     */
    printf("timer expired after %ld us\n", elapsed);
    if (elapsed < MS_TO_US(1))
        errx(1, "early timer expiration: %ld us", elapsed);

    success();
}

static void
test_update_expired(void)
{
    const char *test_id = "kevent(EVFILT_TIMER (UPDATE EXP), EV_ADD | EV_ONESHOT)";
    struct kevent kev;
    long elapsed;
    long start;

    test_begin(test_id);

    test_no_kevents();

    /* Set the timer to 1ms */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
        NOTE_USECONDS, MS_TO_US(1), NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Wait for 2 ms to give the timer plenty of time to expire. */
    mssleep(2);
    
    /* Now re-add the timer */
    start = now();
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Wait for the event */
    kev.flags |= EV_CLEAR;
    kev.fflags &= ~NOTE_USECONDS;
    kev.data = 1;
    kevent_cmp(&kev, kevent_get(kqfd));
    elapsed = now() - start;
    
    /* Check that the timer expired after at least 1 ms.  This check
     * is to make sure that the timer re-started and that the event is
     * not from the original add (and expiration) of the timer.
     */
    printf("timer expired after %ld us\n", elapsed);
    if (elapsed < MS_TO_US(1))
        errx(1, "early timer expiration: %ld us", elapsed);

    /* Make sure the re-added timer does not fire. In other words,
     * test that the event received above was the only event from the
     * add and re-add of the timer.
     */
    mssleep(2);
    test_no_kevents();

    success();
}

static void
test_update_periodic(void)
{
    const char *test_id = "kevent(EVFILT_TIMER (UPDATE), periodic)";
    struct kevent kev;
    long elapsed;
    long start;
    long stop;

    test_begin(test_id);

    test_no_kevents();

    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD, 0, SEC_TO_MS(1), NULL);
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Retrieve the event */
    kev.flags = EV_ADD | EV_CLEAR;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Check if the event occurs again */
    sleep(1);
    kevent_cmp(&kev, kevent_get(kqfd));

    /* Re-add with new timeout. */
    EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD, 0, SEC_TO_MS(2), NULL);
    start = now();
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    /* Retrieve the event */
    kev.flags = EV_ADD | EV_CLEAR;
    kev.data = 1; 
    kevent_cmp(&kev, kevent_get(kqfd));

    stop = now();
    elapsed = stop - start;

    /* Check that the timer expired after at least 2 ms.
     */
    printf("timer expired after %ld us\n", elapsed);
    if (elapsed < MS_TO_US(2))
        errx(1, "early timer expiration: %ld us", elapsed);

    /* Delete the event */
    kev.flags = EV_DELETE;
    if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
        err(1, "%s", test_id);

    success();
}

static void
test_update_timing(void)
{
#define	MIN_SLEEP 500
#define	MAX_SLEEP 1500
    const char *test_id = "kevent(EVFILT_TIMER (UPDATE TIMING), EV_ADD | EV_ONESHOT)";
    struct kevent kev;
    int iteration;
    int sleeptime;
    long elapsed;
    long start;
    long stop;

    test_begin(test_id);

    test_no_kevents();

    /* Re-try the update tests with a variety of delays between the
     * original timer activation and the update of the timer. The goal
     * is to show that in all cases the only timer event that is
     * received is from the update and not the original timer add.
     */
    for (sleeptime = MIN_SLEEP, iteration = 1;
         sleeptime < MAX_SLEEP;
         ++sleeptime, ++iteration) {

        /* First set the timer to 1 ms */
        EV_SET(&kev, vnode_fd, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
            NOTE_USECONDS, MS_TO_US(1), NULL);
        if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
            err(1, "%s", test_id);

        /* Delay; the delay ranges from less than to greater than the
         * timer period.
         */
        ussleep(sleeptime);
    
        /* Now re-add the timer with the same parameters */
        start = now();
        if (kevent(kqfd, &kev, 1, NULL, 0, NULL) < 0)
            err(1, "%s", test_id);

        /* Wait for the event */
        kev.flags |= EV_CLEAR;
        kev.fflags &= ~NOTE_USECONDS;
        kev.data = 1;
        kevent_cmp(&kev, kevent_get(kqfd));
        stop = now();
        elapsed = stop - start;

        /* Check that the timer expired after at least 1 ms. This
         * check is to make sure that the timer re-started and that
         * the event is not from the original add of the timer.
         */
        if (elapsed < MS_TO_US(1))
            errx(1, "early timer expiration: %ld us", elapsed);

        /* Make sure the re-added timer does not fire. In other words,
         * test that the event received above was the only event from
         * the add and re-add of the timer.
         */
        mssleep(2);
        test_no_kevents_quietly();
    }

    success();
}

void
test_evfilt_timer()
{
    kqfd = kqueue();
    test_kevent_timer_add();
    test_kevent_timer_del();
    test_kevent_timer_get();
    test_oneshot();
    test_periodic();
    test_abstime();
    test_update();
    test_update_equal();
    test_update_expired();
    test_update_timing();
    test_update_periodic();
    disable_and_enable();
    close(kqfd);
}
