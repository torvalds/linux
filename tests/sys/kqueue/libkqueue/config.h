/* $FreeBSD$ */

#define HAVE_ERR_H 1
#define HAVE_SYS_EVENT_H 1
#define HAVE_EV_DISPATCH 1
#define HAVE_EV_RECEIPT 1
#undef HAVE_NOTE_TRUNCATE
#define HAVE_EVFILT_TIMER 1
#define HAVE_EVFILT_USER 1
#define PROGRAM "libkqueue-test"
#define VERSION "0.1"
#define TARGET "freebsd"
#define CFLAGS "-g -O0 -Wall -Werror"
