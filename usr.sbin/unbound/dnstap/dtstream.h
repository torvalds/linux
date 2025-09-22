/*
 * dnstap/dtstream.h - Frame Streams thread for unbound DNSTAP
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file
 *
 * An implementation of the Frame Streams data transport protocol for
 * the Unbound DNSTAP message logging facility.
 */

#ifndef DTSTREAM_H
#define DTSTREAM_H

#include "util/locks.h"
struct dt_msg_entry;
struct dt_io_list_item;
struct dt_io_thread;
struct config_file;
struct comm_base;

/**
 * A message buffer with dnstap messages queued up.  It is per-worker.
 * It has locks to synchronize.  If the buffer is full, a new message
 * cannot be added and is discarded.  A thread reads the messages and sends
 * them.
 */
struct dt_msg_queue {
	/** lock of the buffer structure.  Hold this lock to add or remove
	 * entries to the buffer.  Release it so that other threads can also
	 * put messages to log, or a message can be taken out to send away
	 * by the writer thread.
	 */
	lock_basic_type lock;
	/** the maximum size of the buffer, in bytes */
	size_t maxsize;
	/** current size of the buffer, in bytes.  data bytes of messages.
	 * If a new message make it more than maxsize, the buffer is full */
	size_t cursize;
	/** number of messages in the queue */
	int msgcount;
	/** list of messages.  The messages are added to the back and taken
	 * out from the front. */
	struct dt_msg_entry* first, *last;
	/** reference to the io thread to wakeup */
	struct dt_io_thread* dtio;
	/** the wakeup timer for dtio, on worker event base */
	struct comm_timer* wakeup_timer;
};

/**
 * An entry in the dt_msg_queue. contains one DNSTAP message.
 * It is malloced.
 */
struct dt_msg_entry {
	/** next in the list. */
	struct dt_msg_entry* next;
	/** the buffer with the data to send, an encoded DNSTAP message */
	void* buf;
	/** the length to send. */
	size_t len;
};

/**
 * Containing buffer and counter for reading DNSTAP frames.
 */
struct dt_frame_read_buf {
	/** Buffer containing frame, except length counter(s). */
	void* buf;
	/** Number of bytes written to buffer. */
	size_t buf_count;
	/** Capacity of the buffer. */
	size_t buf_cap;

	/** Frame length field. Will contain the 2nd length field for control
	 * frames. */
	uint32_t frame_len;
	/** Number of bytes that have been written to the frame_length field. */
	size_t frame_len_done;

	/** Set to 1 if this is a control frame, 0 otherwise (ie data frame). */
	int control_frame;
};

/**
 * IO thread that reads from the queues and writes them.
 */
struct dt_io_thread {
	/** the thread number for the dtio thread,
	 * must be first to cast thread arg to int* in checklock code. */
	int threadnum;
	/** event base, for event handling */
	void* event_base;
	/** list of queues that is registered to get written */
	struct dt_io_list_item* io_list;
	/** iterator point in the io_list, to pick from them in a
	 * round-robin fashion, instead of only from the first when busy.
	 * if NULL it means start at the start of the list. */
	struct dt_io_list_item* io_list_iter;
	/** thread id, of the io thread */
	ub_thread_type tid;
	/** if the io processing has started */
	int started;
	/** ssl context for the io thread, for tls connections. type SSL_CTX* */
	void* ssl_ctx;
	/** if SNI will be used for TLS connections. */
	int tls_use_sni;

	/** file descriptor that the thread writes to */
	int fd;
	/** event structure that the thread uses */
	void* event;
	/** the event is added */
	int event_added;
	/** event added is a write event */
	int event_added_is_write;
	/** check for nonblocking connect errors on fd */
	int check_nb_connect;
	/** ssl for current connection, type SSL* */
	void* ssl;
	/** true if the handshake for SSL is done, 0 if not */
	int ssl_handshake_done;
	/** true if briefly the SSL wants a read event, 0 if not.
	 * This happens during negotiation, we then do not want to write,
	 * but wait for a read event. */
	int ssl_brief_read;
	/** true if SSL_read is waiting for a write event. Set back to 0 after
	 * single write event is handled. */
	int ssl_brief_write;

	/** the buffer that currently getting written, or NULL if no
	 * (partial) message written now */
	void* cur_msg;
	/** length of the current message */
	size_t cur_msg_len;
	/** number of bytes written for the current message */
	size_t cur_msg_done;
	/** number of bytes of the length that have been written,
	 * for the current message length that precedes the frame */
	size_t cur_msg_len_done;

	/** lock on wakeup_timer_enabled */
	lock_basic_type wakeup_timer_lock;
	/** if wakeup timer is enabled in some thread */
	int wakeup_timer_enabled;
	/** command pipe that stops the pipe if closed.  Used to quit
	 * the program. [0] is read, [1] is written to. */
	int commandpipe[2];
	/** the event to listen to the commandpipe */
	void* command_event;
	/** the io thread wants to exit */
	int want_to_exit;

	/** in stop flush, this is nonNULL and references the stop_ev */
	void* stop_flush_event;

	/** the timer event for connection retries */
	void* reconnect_timer;
	/** if the reconnect timer is added to the event base */
	int reconnect_is_added;
	/** the current reconnection timeout, it is increased with
	 * exponential backoff, in msec */
	int reconnect_timeout;

	/** If the log server is connected to over unix domain sockets,
	 * eg. a file is named that is created to log onto. */
	int upstream_is_unix;
	/** if the log server is connected to over TCP.  The ip address and
	 * port are used */
	int upstream_is_tcp;
	/** if the log server is connected to over TLS.  ip address, port,
	 * and client certificates can be used for authentication. */
	int upstream_is_tls;

	/** Perform bidirectional Frame Streams handshake before sending
	 * messages. */
	int is_bidirectional;
	/** Set if the READY control frame has been sent. */
	int ready_frame_sent;
	/** Set if valid ACCEPT frame is received. */
	int accept_frame_received;
	/** (partially) read frame */
	struct dt_frame_read_buf read_frame;

	/** the file path for unix socket (or NULL) */
	char* socket_path;
	/** the ip address and port number (or NULL) */
	char* ip_str;
	/** is the TLS upstream authenticated by name, if nonNULL,
	 * we use the same cert bundle as used by other TLS streams. */
	char* tls_server_name;
	/** are client certificates in use */
	int use_client_certs;
	/** client cert files: the .key file */
	char* client_key_file;
	/** client cert files: the .pem file */
	char* client_cert_file;
};

/**
 * IO thread list of queues list item
 * lists a worker queue that should be looked at and sent to the log server.
 */
struct dt_io_list_item {
	/** next in the list of buffers to inspect */
	struct dt_io_list_item* next;
	/** buffer of this worker */
	struct dt_msg_queue* queue;
};

/**
 * Create new (empty) worker message queue. Limit set to default on max.
 * @param base: event base for wakeup timer.
 * @return NULL on malloc failure or a new queue (not locked).
 */
struct dt_msg_queue* dt_msg_queue_create(struct comm_base* base);

/**
 * Delete a worker message queue.  It has to be unlinked from access,
 * so it can be deleted without lock worries.  The queue is emptied (deleted).
 * @param mq: message queue.
 */
void dt_msg_queue_delete(struct dt_msg_queue* mq);

/**
 * Submit a message to the queue.  The queue is locked by the routine,
 * the message is inserted, and then the queue is unlocked so the
 * message can be picked up by the writer thread.
 * @param mq: message queue.
 * @param buf: buffer with message (dnstap contents).
 * 	The buffer must have been malloced by caller.  It is linked in
 * 	the queue, and is free()d after use.  If the routine fails
 * 	the buffer is freed as well (and nothing happens, the item
 * 	could not be logged).
 * @param len: length of buffer.
 */
void dt_msg_queue_submit(struct dt_msg_queue* mq, void* buf, size_t len);

/** timer callback to wakeup dtio thread to process messages */
void mq_wakeup_cb(void* arg);

/**
 * Create IO thread.
 * @return new io thread object. not yet started. or NULL malloc failure.
 */
struct dt_io_thread* dt_io_thread_create(void);

/**
 * Delete the IO thread structure.
 * @param dtio: the io thread that is deleted.  It must not be running.
 */
void dt_io_thread_delete(struct dt_io_thread* dtio);

/**
 * Apply config to the dtio thread
 * @param dtio: io thread, not yet started.
 * @param cfg: config file struct.
 * @return false on malloc failure.
 */
int dt_io_thread_apply_cfg(struct dt_io_thread* dtio,
	struct config_file *cfg);

/**
 * Register a msg queue to the io thread.  It will be polled to see if
 * there are messages and those then get removed and sent, when the thread
 * is running.
 * @param dtio: the io thread.
 * @param mq: message queue to register.
 * @return false on failure (malloc failure).
 */
int dt_io_thread_register_queue(struct dt_io_thread* dtio,
	struct dt_msg_queue* mq);

/**
 * Unregister queue from io thread.
 * @param dtio: the io thread.
 * @param mq: message queue.
 */
void dt_io_thread_unregister_queue(struct dt_io_thread* dtio,
        struct dt_msg_queue* mq);

/**
 * Start the io thread
 * @param dtio: the io thread.
 * @param event_base_nothr: the event base to attach the events to, in case
 * 	we are running without threads.  With threads, this is ignored
 * 	and a thread is started to process the dnstap log messages.
 * @param numworkers: number of worker threads.  The dnstap io thread is
 * 	that number +1 as the threadnumber (in logs).
 * @return false on failure.
 */
int dt_io_thread_start(struct dt_io_thread* dtio, void* event_base_nothr,
	int numworkers);

/** 
 * Stop the io thread
 * @param dtio: the io thread.
 */
void dt_io_thread_stop(struct dt_io_thread* dtio);

/** callback for the dnstap reconnect, to start reconnecting to output */
void dtio_reconnect_timeout_cb(int fd, short bits, void* arg);

/** callback for the dnstap events, to write to the output */
void dtio_output_cb(int fd, short bits, void* arg);

/** callback for the dnstap commandpipe, to stop the dnstap IO */
void dtio_cmd_cb(int fd, short bits, void* arg);

/** callback for the timer when the thread stops and wants to finish up */
void dtio_stop_timer_cb(int fd, short bits, void* arg);

/** callback for the output when the thread stops and wants to finish up */
void dtio_stop_ev_cb(int fd, short bits, void* arg);

/** callback for unbound-dnstap-socket */
void dtio_tap_callback(int fd, short bits, void* arg);

/** callback for unbound-dnstap-socket */
void dtio_mainfdcallback(int fd, short bits, void* arg);

#endif /* DTSTREAM_H */
