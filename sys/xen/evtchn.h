/******************************************************************************
 * evtchn.h
 * 
 * Interface to /dev/xen/evtchn.
 * 
 * Copyright (c) 2003-2005, K A Fraser
 * 
 * This file may be distributed separately from the Linux kernel, or
 * incorporated into other software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef __XEN_EVTCHN_H__
#define __XEN_EVTCHN_H__

/*
 * Bind a fresh port to VIRQ @virq.
 */
#define IOCTL_EVTCHN_BIND_VIRQ				\
	_IOWR('E', 4, struct ioctl_evtchn_bind_virq)
struct ioctl_evtchn_bind_virq {
	unsigned int virq;
	unsigned int port;
};

/*
 * Bind a fresh port to remote <@remote_domain, @remote_port>.
 */
#define IOCTL_EVTCHN_BIND_INTERDOMAIN			\
	_IOWR('E', 5, struct ioctl_evtchn_bind_interdomain)
struct ioctl_evtchn_bind_interdomain {
	unsigned int remote_domain, remote_port;
	unsigned int port;
};

/*
 * Allocate a fresh port for binding to @remote_domain.
 */
#define IOCTL_EVTCHN_BIND_UNBOUND_PORT			\
	_IOWR('E', 6, struct ioctl_evtchn_bind_unbound_port)
struct ioctl_evtchn_bind_unbound_port {
	unsigned int remote_domain;
	unsigned int port;
};

/*
 * Unbind previously allocated @port.
 */
#define IOCTL_EVTCHN_UNBIND				\
	_IOW('E', 7, struct ioctl_evtchn_unbind)
struct ioctl_evtchn_unbind {
	unsigned int port;
};

/*
 * Send event to previously allocated @port.
 */
#define IOCTL_EVTCHN_NOTIFY				\
	_IOW('E', 8, struct ioctl_evtchn_notify)
struct ioctl_evtchn_notify {
	unsigned int port;
};

/* Clear and reinitialise the event buffer. Clear error condition. */
#define IOCTL_EVTCHN_RESET				\
	_IO('E', 9)

#endif /* __XEN_EVTCHN_H__ */
