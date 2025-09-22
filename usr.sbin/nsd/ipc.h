/*
 * ipc.h - Interprocess communication routines. Handlers read and write.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef NSD_IPC_H
#define NSD_IPC_H

#include "netio.h"
struct buffer;
struct nsd;
struct nsd_child;
struct xfrd_tcp;
struct xfrd_state;
struct nsdst;
struct event;

/*
 * Data for the server_main IPC handler 
 * Used by parent side to listen to children, and write to children.
 */
struct main_ipc_handler_data
{
	struct nsd	*nsd;
	struct nsd_child *child;
};

/*
 * Data for ipc handler, nsd and a conn for reading ipc msgs.
 * Used by children to listen to parent. 
 * Used by parent to listen to xfrd.
 */
struct ipc_handler_conn_data
{
	struct nsd	*nsd;
	struct xfrd_tcp	*conn;
};

/*
 * Routine used by server_main.
 * Handle a command received from the xfrdaemon processes.
 */
void parent_handle_xfrd_command(netio_type *netio,
	netio_handler_type *handler, netio_event_types_type event_types);

/*
 * Routine used by server_main.
 * Handle a command received from the reload process.
 */
void parent_handle_reload_command(netio_type *netio,
	netio_handler_type *handler, netio_event_types_type event_types);

/*
 * Routine used by server_main.
 * Handle a command received from the children processes.
 * Send commands and forwarded xfrd packets when writable.
 */
void parent_handle_child_command(netio_type *netio,
	netio_handler_type *handler, netio_event_types_type event_types);

/*
 * Routine used by server_child.
 * Handle a command received from the parent process.
 */
void child_handle_parent_command(int fd, short event, void* arg);

/*
 * Routine used by xfrd
 * Handle interprocess communication with parent process, read and write.
 */
void xfrd_handle_ipc(int fd, short event, void* arg);

/* receive incoming notifies received by and from the serve processes */
void xfrd_handle_notify(int fd, short event, void* arg);

/* check if all children have exited in an orderly fashion and set mode */
void parent_check_all_children_exited(struct nsd* nsd);

/** add stats to total */
void stats_add(struct nsdst* total, struct nsdst* s);
/** subtract stats from total */
void stats_subtract(struct nsdst* total, struct nsdst* s);

/** set event to listen to given mode, no timeout, must be added already */
void ipc_xfrd_set_listening(struct xfrd_state* xfrd, short mode);

#endif /* NSD_IPC_H */
