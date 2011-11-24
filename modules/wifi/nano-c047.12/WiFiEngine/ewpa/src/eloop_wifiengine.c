/*
 * Event loop - empty template (basic structure, but no OS specific operations)
 * Copyright (c) 2002-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"


#define static

struct eloop_sock {
	int sock;
	void *eloop_data;
	void *user_data;
	void (*handler)(int sock, void *eloop_ctx, void *sock_ctx);
};

struct eloop_timeout {
	struct os_time time;
	void *eloop_data;
	void *user_data;
	void (*handler)(void *eloop_ctx, void *sock_ctx);
	struct eloop_timeout *next;
};

struct eloop_signal {
	int sig;
	void *user_data;
	void (*handler)(int sig, void *eloop_ctx, void *signal_ctx);
	int signaled;
};

struct eloop_data {
	void *user_data;

	int max_sock, reader_count;
	struct eloop_sock *readers;

	struct eloop_timeout *timeout;
	struct eloop_timeout *registered_timeout;

	driver_timer_id_t timer_id;

	int signal_count;
	struct eloop_signal *signals;
	int signaled;
	int pending_terminate;

	int terminate;
	int reader_table_changed;
};

static struct eloop_data eloop;


int eloop_init(void *user_data)
{
	memset(&eloop, 0, sizeof(eloop));
	eloop.user_data = user_data;
	DriverEnvironment_GetNewTimer(&eloop.timer_id, TRUE);
	return 0;
}


int eloop_register_read_sock(int sock,
			     void (*handler)(int sock, void *eloop_ctx,
					     void *sock_ctx),
			     void *eloop_data, void *user_data)
{
	struct eloop_sock *tmp;

	tmp = (struct eloop_sock *)
		os_realloc(eloop.readers,
			(eloop.reader_count + 1) * sizeof(struct eloop_sock));
	if (tmp == NULL)
		return -1;

	tmp[eloop.reader_count].sock = sock;
	tmp[eloop.reader_count].eloop_data = eloop_data;
	tmp[eloop.reader_count].user_data = user_data;
	tmp[eloop.reader_count].handler = handler;
	eloop.reader_count++;
	eloop.readers = tmp;
	if (sock > eloop.max_sock)
		eloop.max_sock = sock;
	eloop.reader_table_changed = 1;

	return 0;
}


void eloop_unregister_read_sock(int sock)
{
	int i;

	if (eloop.readers == NULL || eloop.reader_count == 0)
		return;

	for (i = 0; i < eloop.reader_count; i++) {
		if (eloop.readers[i].sock == sock)
			break;
	}
	if (i == eloop.reader_count)
		return;
	if (i != eloop.reader_count - 1) {
		memmove(&eloop.readers[i], &eloop.readers[i + 1],
			(eloop.reader_count - i - 1) *
			sizeof(struct eloop_sock));
	}
	eloop.reader_count--;
	eloop.reader_table_changed = 1;
}


static void eloop_reschedule(void);


/* Simulate os_get_time with the internal tick counter, this does not
 * provide a correct time stamp, but we really only care about a
 * monotonically increasing time here. 
 *
 */
/* XXX this does not belong here */
static void os_get_time_mono(struct os_time *t)
{
#ifdef __rtke__
	/* This will wrap after about 15 months. */
	unsigned int ticks = DriverEnvironment_GetTicks();
	unsigned int H, L;

	/* avoid overflow by performing calculation in multiple
	   steps */
	H = ticks / 100000;
	L = ticks % 100000;
	t->sec = H * 923;
	t->usec = L * 9230;
	t->sec += t->usec / 1000000;
	t->usec %= 1000000;
#else
	/* Assuming driver_msec_t is a 32-bit type, this will wrap
	 * after 49 days, or whenever _msec wraps. */
	driver_msec_t msec = DriverEnvironment_GetTimestamp_msec();

	t->usec = msec % 1000;
	t->sec = msec - t->usec;
	t->sec /= 1000;
	t->usec *= 1000;
#endif
}


static int eloop_timer(void *data, size_t len)
{
	struct os_time now;
	os_get_time_mono(&now);
	while(eloop.timeout != NULL &&
	      !os_time_before(&now, &eloop.timeout->time)) {
		struct eloop_timeout *tmp;
		tmp = eloop.timeout;
		eloop.timeout = eloop.timeout->next;
		eloop.registered_timeout = NULL;
		(*tmp->handler)(tmp->eloop_data, tmp->user_data);
		os_free(tmp);
	}
	eloop_reschedule();

	return 0;
}

static void
eloop_reschedule(void)
{
	if(eloop.timeout != NULL && 
	   eloop.timeout != eloop.registered_timeout) {
		struct os_time delay, now;
		long msec = 0;
		os_get_time_mono(&now);
		if(os_time_before(&now, &eloop.timeout->time)) {
			os_time_sub(&eloop.timeout->time, &now, &delay);
			msec = delay.sec * 1000;
			msec += delay.usec / 1000;
		}
		if(msec <= 0)
			msec = 1;

		DriverEnvironment_RegisterTimerCallback(msec,
							eloop.timer_id,
							eloop_timer,
							FALSE);
		eloop.registered_timeout = eloop.timeout;
	}
							
}

int eloop_register_timeout(unsigned int secs, unsigned int usecs,
			   void (*handler)(void *eloop_ctx, void *timeout_ctx),
			   void *eloop_data, void *user_data)
{
	struct eloop_timeout *timeout, *tmp, **prev;

	timeout = (struct eloop_timeout *) os_malloc(sizeof(*timeout));
	if (timeout == NULL)
		return -1;
	os_get_time_mono(&timeout->time);
	timeout->time.sec += secs;
	timeout->time.usec += usecs;
	while (timeout->time.usec >= 1000000) {
		timeout->time.sec++;
		timeout->time.usec -= 1000000;
	}
	timeout->eloop_data = eloop_data;
	timeout->user_data = user_data;
	timeout->handler = handler;
	timeout->next = NULL;
	
	prev = &eloop.timeout;
	tmp = eloop.timeout;
	while(tmp != NULL) {
		if(os_time_before(&timeout->time, &tmp->time))
			break;
		prev = &tmp->next;
		tmp = tmp->next;
	}
	
	timeout->next = tmp;
	*prev = timeout;

	eloop_reschedule();

	return 0;
}


int eloop_cancel_timeout(void (*handler)(void *eloop_ctx, void *sock_ctx),
			 void *eloop_data, void *user_data)
{
	struct eloop_timeout *timeout, *prev, *next;
	int removed = 0;

	prev = NULL;
	timeout = eloop.timeout;
	while (timeout != NULL) {
		next = timeout->next;

		if (timeout->handler == handler &&
		    (timeout->eloop_data == eloop_data ||
		     eloop_data == ELOOP_ALL_CTX) &&
		    (timeout->user_data == user_data ||
		     user_data == ELOOP_ALL_CTX)) {
			if (prev == NULL)
				eloop.timeout = next;
			else
				prev->next = next;
			os_free(timeout);
			removed++;
		} else
			prev = timeout;

		timeout = next;
	}

	eloop_reschedule();

	return removed;
}


/* TODO: replace with suitable signal handler */
#if 0
static void eloop_handle_signal(int sig)
{
	int i;

	eloop.signaled++;
	for (i = 0; i < eloop.signal_count; i++) {
		if (eloop.signals[i].sig == sig) {
			eloop.signals[i].signaled++;
			break;
		}
	}
}
#endif


static void eloop_process_pending_signals(void)
{
	int i;

	if (eloop.signaled == 0)
		return;
	eloop.signaled = 0;

	if (eloop.pending_terminate) {
		eloop.pending_terminate = 0;
	}

	for (i = 0; i < eloop.signal_count; i++) {
		if (eloop.signals[i].signaled) {
			eloop.signals[i].signaled = 0;
			eloop.signals[i].handler(eloop.signals[i].sig,
						 eloop.user_data,
						 eloop.signals[i].user_data);
		}
	}
}


int eloop_register_signal(int sig,
			  void (*handler)(int sig, void *eloop_ctx,
					  void *signal_ctx),
			  void *user_data)
{
	struct eloop_signal *tmp;

	tmp = (struct eloop_signal *)
		os_realloc(eloop.signals,
			(eloop.signal_count + 1) *
			sizeof(struct eloop_signal));
	if (tmp == NULL)
		return -1;

	tmp[eloop.signal_count].sig = sig;
	tmp[eloop.signal_count].user_data = user_data;
	tmp[eloop.signal_count].handler = handler;
	tmp[eloop.signal_count].signaled = 0;
	eloop.signal_count++;
	eloop.signals = tmp;

	/* TODO: register signal handler */

	return 0;
}


int eloop_register_signal_terminate(void (*handler)(int sig, void *eloop_ctx,
						    void *signal_ctx),
				    void *user_data)
{
#if 0
	/* TODO: for example */
	int ret = eloop_register_signal(SIGINT, handler, user_data);
	if (ret == 0)
		ret = eloop_register_signal(SIGTERM, handler, user_data);
	return ret;
#endif
	return 0;
}


int eloop_register_signal_reconfig(void (*handler)(int sig, void *eloop_ctx,
						   void *signal_ctx),
				   void *user_data)
{
#if 0
	/* TODO: for example */
	return eloop_register_signal(SIGHUP, handler, user_data);
#endif
	return 0;
}


void eloop_run(void)
{
	int i;
	struct os_time tv, now;

	while (!eloop.terminate &&
		(eloop.timeout || eloop.reader_count > 0)) {
		if (eloop.timeout) {
			os_get_time_mono(&now);
			if (os_time_before(&now, &eloop.timeout->time))
				os_time_sub(&eloop.timeout->time, &now, &tv);
			else
				tv.sec = tv.usec = 0;
		}

		/*
		 * TODO: wait for any event (read socket ready, timeout (tv),
		 * signal
		 */
		os_sleep(1, 0); /* just a dummy wait for testing */

		eloop_process_pending_signals();

		/* check if some registered timeouts have occurred */
		if (eloop.timeout) {
			struct eloop_timeout *tmp;

			os_get_time_mono(&now);
			if (!os_time_before(&now, &eloop.timeout->time)) {
				tmp = eloop.timeout;
				eloop.timeout = eloop.timeout->next;
				tmp->handler(tmp->eloop_data,
					     tmp->user_data);
				os_free(tmp);
			}

		}

		eloop.reader_table_changed = 0;
		for (i = 0; i < eloop.reader_count; i++) {
			/*
			 * TODO: call each handler that has pending data to
			 * read
			 */
			if (0 /* TODO: eloop.readers[i].sock ready */) {
				eloop.readers[i].handler(
					eloop.readers[i].sock,
					eloop.readers[i].eloop_data,
					eloop.readers[i].user_data);
				if (eloop.reader_table_changed)
					break;
			}
		}
	}
}


void eloop_terminate(void)
{
	eloop.terminate = 1;
}


void eloop_destroy(void)
{
	struct eloop_timeout *timeout, *prev;

	timeout = eloop.timeout;
	while (timeout != NULL) {
		prev = timeout;
		timeout = timeout->next;
		os_free(prev);
	}
	DriverEnvironment_FreeTimer(eloop.timer_id);
	os_free(eloop.readers);
	os_free(eloop.signals);
}


int eloop_terminated(void)
{
	return eloop.terminate;
}


void eloop_wait_for_read_sock(int sock)
{
	/*
	 * TODO: wait for the file descriptor to have something available for
	 * reading
	 */
}


void * eloop_get_user_data(void)
{
	return eloop.user_data;
}
/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
