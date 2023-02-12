/*
 * Linux Security Module interfaces
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2015 Intel Corporation.
 * Copyright (C) 2015 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2016 Mellanox Techonologies
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_LSM_HOOKS_H
#define __LINUX_LSM_HOOKS_H

#include <linux/security.h>
#include <linux/init.h>
#include <linux/rculist.h>

/**
 * union security_list_options - Linux Security Module hook function list
 *
 * Security hooks for socket operations.
 *
 * @socket_create:
 *	Check permissions prior to creating a new socket.
 *	@family contains the requested protocol family.
 *	@type contains the requested communications type.
 *	@protocol contains the requested protocol.
 *	@kern set to 1 if a kernel socket.
 *	Return 0 if permission is granted.
 * @socket_post_create:
 *	This hook allows a module to update or allocate a per-socket security
 *	structure. Note that the security field was not added directly to the
 *	socket structure, but rather, the socket security information is stored
 *	in the associated inode.  Typically, the inode alloc_security hook will
 *	allocate and attach security information to
 *	SOCK_INODE(sock)->i_security.  This hook may be used to update the
 *	SOCK_INODE(sock)->i_security field with additional information that
 *	wasn't available when the inode was allocated.
 *	@sock contains the newly created socket structure.
 *	@family contains the requested protocol family.
 *	@type contains the requested communications type.
 *	@protocol contains the requested protocol.
 *	@kern set to 1 if a kernel socket.
 *	Return 0 if permission is granted.
 * @socket_socketpair:
 *	Check permissions before creating a fresh pair of sockets.
 *	@socka contains the first socket structure.
 *	@sockb contains the second socket structure.
 *	Return 0 if permission is granted and the connection was established.
 * @socket_bind:
 *	Check permission before socket protocol layer bind operation is
 *	performed and the socket @sock is bound to the address specified in the
 *	@address parameter.
 *	@sock contains the socket structure.
 *	@address contains the address to bind to.
 *	@addrlen contains the length of address.
 *	Return 0 if permission is granted.
 * @socket_connect:
 *	Check permission before socket protocol layer connect operation
 *	attempts to connect socket @sock to a remote address, @address.
 *	@sock contains the socket structure.
 *	@address contains the address of remote endpoint.
 *	@addrlen contains the length of address.
 *	Return 0 if permission is granted.
 * @socket_listen:
 *	Check permission before socket protocol layer listen operation.
 *	@sock contains the socket structure.
 *	@backlog contains the maximum length for the pending connection queue.
 *	Return 0 if permission is granted.
 * @socket_accept:
 *	Check permission before accepting a new connection.  Note that the new
 *	socket, @newsock, has been created and some information copied to it,
 *	but the accept operation has not actually been performed.
 *	@sock contains the listening socket structure.
 *	@newsock contains the newly created server socket for connection.
 *	Return 0 if permission is granted.
 * @socket_sendmsg:
 *	Check permission before transmitting a message to another socket.
 *	@sock contains the socket structure.
 *	@msg contains the message to be transmitted.
 *	@size contains the size of message.
 *	Return 0 if permission is granted.
 * @socket_recvmsg:
 *	Check permission before receiving a message from a socket.
 *	@sock contains the socket structure.
 *	@msg contains the message structure.
 *	@size contains the size of message structure.
 *	@flags contains the operational flags.
 *	Return 0 if permission is granted.
 * @socket_getsockname:
 *	Check permission before the local address (name) of the socket object
 *	@sock is retrieved.
 *	@sock contains the socket structure.
 *	Return 0 if permission is granted.
 * @socket_getpeername:
 *	Check permission before the remote address (name) of a socket object
 *	@sock is retrieved.
 *	@sock contains the socket structure.
 *	Return 0 if permission is granted.
 * @socket_getsockopt:
 *	Check permissions before retrieving the options associated with socket
 *	@sock.
 *	@sock contains the socket structure.
 *	@level contains the protocol level to retrieve option from.
 *	@optname contains the name of option to retrieve.
 *	Return 0 if permission is granted.
 * @socket_setsockopt:
 *	Check permissions before setting the options associated with socket
 *	@sock.
 *	@sock contains the socket structure.
 *	@level contains the protocol level to set options for.
 *	@optname contains the name of the option to set.
 *	Return 0 if permission is granted.
 * @socket_shutdown:
 *	Checks permission before all or part of a connection on the socket
 *	@sock is shut down.
 *	@sock contains the socket structure.
 *	@how contains the flag indicating how future sends and receives
 *	are handled.
 *	Return 0 if permission is granted.
 * @socket_sock_rcv_skb:
 *	Check permissions on incoming network packets.  This hook is distinct
 *	from Netfilter's IP input hooks since it is the first time that the
 *	incoming sk_buff @skb has been associated with a particular socket, @sk.
 *	Must not sleep inside this hook because some callers hold spinlocks.
 *	@sk contains the sock (not socket) associated with the incoming sk_buff.
 *	@skb contains the incoming network data.
 *	Return 0 if permission is granted.
 * @socket_getpeersec_stream:
 *	This hook allows the security module to provide peer socket security
 *	state for unix or connected tcp sockets to userspace via getsockopt
 *	SO_GETPEERSEC.  For tcp sockets this can be meaningful if the
 *	socket is associated with an ipsec SA.
 *	@sock is the local socket.
 *	@optval memory where the security state is to be copied.
 *	@optlen memory where the module should copy the actual length
 *	of the security state.
 *	@len as input is the maximum length to copy to userspace provided
 *	by the caller.
 *	Return 0 if all is well, otherwise, typical getsockopt return
 *	values.
 * @socket_getpeersec_dgram:
 *	This hook allows the security module to provide peer socket security
 *	state for udp sockets on a per-packet basis to userspace via
 *	getsockopt SO_GETPEERSEC. The application must first have indicated
 *	the IP_PASSSEC option via getsockopt. It can then retrieve the
 *	security state returned by this hook for a packet via the SCM_SECURITY
 *	ancillary message type.
 *	@sock contains the peer socket. May be NULL.
 *	@skb is the sk_buff for the packet being queried. May be NULL.
 *	@secid pointer to store the secid of the packet.
 *	Return 0 on success, error on failure.
 * @sk_alloc_security:
 *	Allocate and attach a security structure to the sk->sk_security field,
 *	which is used to copy security attributes between local stream sockets.
 *	Return 0 on success, error on failure.
 * @sk_free_security:
 *	Deallocate security structure.
 * @sk_clone_security:
 *	Clone/copy security structure.
 * @sk_getsecid:
 *	Retrieve the LSM-specific secid for the sock to enable caching
 *	of network authorizations.
 * @sock_graft:
 *	Sets the socket's isec sid to the sock's sid.
 * @inet_conn_request:
 *	Sets the openreq's sid to socket's sid with MLS portion taken
 *	from peer sid.
 *	Return 0 if permission is granted.
 * @inet_csk_clone:
 *	Sets the new child socket's sid to the openreq sid.
 * @inet_conn_established:
 *	Sets the connection's peersid to the secmark on skb.
 * @secmark_relabel_packet:
 *	Check if the process should be allowed to relabel packets to
 *	the given secid.
 *	Return 0 if permission is granted.
 * @secmark_refcount_inc:
 *	Tells the LSM to increment the number of secmark labeling rules loaded.
 * @secmark_refcount_dec:
 *	Tells the LSM to decrement the number of secmark labeling rules loaded.
 * @req_classify_flow:
 *	Sets the flow's sid to the openreq sid.
 * @tun_dev_alloc_security:
 *	This hook allows a module to allocate a security structure for a TUN
 *	device.
 *	@security pointer to a security structure pointer.
 *	Returns a zero on success, negative values on failure.
 * @tun_dev_free_security:
 *	This hook allows a module to free the security structure for a TUN
 *	device.
 *	@security pointer to the TUN device's security structure.
 * @tun_dev_create:
 *	Check permissions prior to creating a new TUN device.
 *	Return 0 if permission is granted.
 * @tun_dev_attach_queue:
 *	Check permissions prior to attaching to a TUN device queue.
 *	@security pointer to the TUN device's security structure.
 *	Return 0 if permission is granted.
 * @tun_dev_attach:
 *	This hook can be used by the module to update any security state
 *	associated with the TUN device's sock structure.
 *	@sk contains the existing sock structure.
 *	@security pointer to the TUN device's security structure.
 *	Return 0 if permission is granted.
 * @tun_dev_open:
 *	This hook can be used by the module to update any security state
 *	associated with the TUN device's security structure.
 *	@security pointer to the TUN devices's security structure.
 *	Return 0 if permission is granted.
 *
 * Security hooks for SCTP
 *
 * @sctp_assoc_request:
 *	Passes the @asoc and @chunk->skb of the association INIT packet to
 *	the security module.
 *	@asoc pointer to sctp association structure.
 *	@skb pointer to skbuff of association packet.
 *	Return 0 on success, error on failure.
 * @sctp_bind_connect:
 *	Validiate permissions required for each address associated with sock
 *	@sk. Depending on @optname, the addresses will be treated as either
 *	for a connect or bind service. The @addrlen is calculated on each
 *	ipv4 and ipv6 address using sizeof(struct sockaddr_in) or
 *	sizeof(struct sockaddr_in6).
 *	@sk pointer to sock structure.
 *	@optname name of the option to validate.
 *	@address list containing one or more ipv4/ipv6 addresses.
 *	@addrlen total length of address(s).
 *	Return 0 on success, error on failure.
 * @sctp_sk_clone:
 *	Called whenever a new socket is created by accept(2) (i.e. a TCP
 *	style socket) or when a socket is 'peeled off' e.g userspace
 *	calls sctp_peeloff(3).
 *	@asoc pointer to current sctp association structure.
 *	@sk pointer to current sock structure.
 *	@newsk pointer to new sock structure.
 * @sctp_assoc_established:
 *	Passes the @asoc and @chunk->skb of the association COOKIE_ACK packet
 *	to the security module.
 *	@asoc pointer to sctp association structure.
 *	@skb pointer to skbuff of association packet.
 *	Return 0 if permission is granted.
 *
 * Security hooks for Infiniband
 *
 * @ib_pkey_access:
 *	Check permission to access a pkey when modifing a QP.
 *	@subnet_prefix the subnet prefix of the port being used.
 *	@pkey the pkey to be accessed.
 *	@sec pointer to a security structure.
 *	Return 0 if permission is granted.
 * @ib_endport_manage_subnet:
 *	Check permissions to send and receive SMPs on a end port.
 *	@dev_name the IB device name (i.e. mlx4_0).
 *	@port_num the port number.
 *	@sec pointer to a security structure.
 *	Return 0 if permission is granted.
 * @ib_alloc_security:
 *	Allocate a security structure for Infiniband objects.
 *	@sec pointer to a security structure pointer.
 *	Returns 0 on success, non-zero on failure.
 * @ib_free_security:
 *	Deallocate an Infiniband security structure.
 *	@sec contains the security structure to be freed.
 *
 * Security hooks for XFRM operations.
 *
 * @xfrm_policy_alloc_security:
 *	@ctxp is a pointer to the xfrm_sec_ctx being added to Security Policy
 *	Database used by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level policy update program (e.g., setkey).
 *	@gfp is to specify the context for the allocation.
 *	Allocate a security structure to the xp->security field; the security
 *	field is initialized to NULL when the xfrm_policy is allocated.
 *	Return 0 if operation was successful (memory to allocate, legal
 *	context).
 * @xfrm_policy_clone_security:
 *	@old_ctx contains an existing xfrm_sec_ctx.
 *	@new_ctxp contains a new xfrm_sec_ctx being cloned from old.
 *	Allocate a security structure in new_ctxp that contains the
 *	information from the old_ctx structure.
 *	Return 0 if operation was successful (memory to allocate).
 * @xfrm_policy_free_security:
 *	@ctx contains the xfrm_sec_ctx.
 *	Deallocate xp->security.
 * @xfrm_policy_delete_security:
 *	@ctx contains the xfrm_sec_ctx.
 *	Authorize deletion of xp->security.
 *	Return 0 if permission is granted.
 * @xfrm_state_alloc:
 *	@x contains the xfrm_state being added to the Security Association
 *	Database by the XFRM system.
 *	@sec_ctx contains the security context information being provided by
 *	the user-level SA generation program (e.g., setkey or racoon).
 *	Allocate a security structure to the x->security field; the security
 *	field is initialized to NULL when the xfrm_state is allocated. Set the
 *	context to correspond to sec_ctx. Return 0 if operation was successful
 *	(memory to allocate, legal context).
 * @xfrm_state_alloc_acquire:
 *	@x contains the xfrm_state being added to the Security Association
 *	Database by the XFRM system.
 *	@polsec contains the policy's security context.
 *	@secid contains the secid from which to take the mls portion of the
 *	context.
 *	Allocate a security structure to the x->security field; the security
 *	field is initialized to NULL when the xfrm_state is allocated. Set the
 *	context to correspond to secid. Return 0 if operation was successful
 *	(memory to allocate, legal context).
 * @xfrm_state_free_security:
 *	@x contains the xfrm_state.
 *	Deallocate x->security.
 * @xfrm_state_delete_security:
 *	@x contains the xfrm_state.
 *	Authorize deletion of x->security.
 *	Return 0 if permission is granted.
 * @xfrm_policy_lookup:
 *	@ctx contains the xfrm_sec_ctx for which the access control is being
 *	checked.
 *	@fl_secid contains the flow security label that is used to authorize
 *	access to the policy xp.
 *	@dir contains the direction of the flow (input or output).
 *	Check permission when a flow selects a xfrm_policy for processing
 *	XFRMs on a packet.  The hook is called when selecting either a
 *	per-socket policy or a generic xfrm policy.
 *	Return 0 if permission is granted, -ESRCH otherwise, or -errno
 *	on other errors.
 * @xfrm_state_pol_flow_match:
 *	@x contains the state to match.
 *	@xp contains the policy to check for a match.
 *	@flic contains the flowi_common struct to check for a match.
 *	Return 1 if there is a match.
 * @xfrm_decode_session:
 *	@skb points to skb to decode.
 *	@secid points to the flow key secid to set.
 *	@ckall says if all xfrms used should be checked for same secid.
 *	Return 0 if ckall is zero or all xfrms used have the same secid.
 *
 * Security hooks affecting all Key Management operations
 *
 * @key_alloc:
 *	Permit allocation of a key and assign security data. Note that key does
 *	not have a serial number assigned at this point.
 *	@key points to the key.
 *	@flags is the allocation flags.
 *	Return 0 if permission is granted, -ve error otherwise.
 * @key_free:
 *	Notification of destruction; free security data.
 *	@key points to the key.
 *	No return value.
 * @key_permission:
 *	See whether a specific operational right is granted to a process on a
 *	key.
 *	@key_ref refers to the key (key pointer + possession attribute bit).
 *	@cred points to the credentials to provide the context against which to
 *	evaluate the security data on the key.
 *	@perm describes the combination of permissions required of this key.
 *	Return 0 if permission is granted, -ve error otherwise.
 * @key_getsecurity:
 *	Get a textual representation of the security context attached to a key
 *	for the purposes of honouring KEYCTL_GETSECURITY.  This function
 *	allocates the storage for the NUL-terminated string and the caller
 *	should free it.
 *	@key points to the key to be queried.
 *	@_buffer points to a pointer that should be set to point to the
 *	resulting string (if no label or an error occurs).
 *	Return the length of the string (including terminating NUL) or -ve if
 *	an error.
 *	May also return 0 (and a NULL buffer pointer) if there is no label.
 *
 * Security hooks affecting all System V IPC operations.
 *
 * @ipc_permission:
 *	Check permissions for access to IPC
 *	@ipcp contains the kernel IPC permission structure.
 *	@flag contains the desired (requested) permission set.
 *	Return 0 if permission is granted.
 * @ipc_getsecid:
 *	Get the secid associated with the ipc object.
 *	@ipcp contains the kernel IPC permission structure.
 *	@secid contains a pointer to the location where result will be saved.
 *	In case of failure, @secid will be set to zero.
 *
 * Security hooks for individual messages held in System V IPC message queues
 *
 * @msg_msg_alloc_security:
 *	Allocate and attach a security structure to the msg->security field.
 *	The security field is initialized to NULL when the structure is first
 *	created.
 *	@msg contains the message structure to be modified.
 *	Return 0 if operation was successful and permission is granted.
 * @msg_msg_free_security:
 *	Deallocate the security structure for this message.
 *	@msg contains the message structure to be modified.
 *
 * Security hooks for System V IPC Message Queues
 *
 * @msg_queue_alloc_security:
 *	Allocate and attach a security structure to the
 *	@perm->security field. The security field is initialized to
 *	NULL when the structure is first created.
 *	@perm contains the IPC permissions of the message queue.
 *	Return 0 if operation was successful and permission is granted.
 * @msg_queue_free_security:
 *	Deallocate security field @perm->security for the message queue.
 *	@perm contains the IPC permissions of the message queue.
 * @msg_queue_associate:
 *	Check permission when a message queue is requested through the
 *	msgget system call. This hook is only called when returning the
 *	message queue identifier for an existing message queue, not when a
 *	new message queue is created.
 *	@perm contains the IPC permissions of the message queue.
 *	@msqflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgctl:
 *	Check permission when a message control operation specified by @cmd
 *	is to be performed on the message queue with permissions @perm.
 *	The @perm may be NULL, e.g. for IPC_INFO or MSG_INFO.
 *	@perm contains the IPC permissions of the msg queue. May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @msg_queue_msgsnd:
 *	Check permission before a message, @msg, is enqueued on the message
 *	queue with permissions @perm.
 *	@perm contains the IPC permissions of the message queue.
 *	@msg contains the message to be enqueued.
 *	@msqflg contains operational flags.
 *	Return 0 if permission is granted.
 * @msg_queue_msgrcv:
 *	Check permission before a message, @msg, is removed from the message
 *	queue. The @target task structure contains a pointer to the
 *	process that will be receiving the message (not equal to the current
 *	process when inline receives are being performed).
 *	@perm contains the IPC permissions of the message queue.
 *	@msg contains the message destination.
 *	@target contains the task structure for recipient process.
 *	@type contains the type of message requested.
 *	@mode contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Shared Memory Segments
 *
 * @shm_alloc_security:
 *	Allocate and attach a security structure to the @perm->security
 *	field. The security field is initialized to NULL when the structure is
 *	first created.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	Return 0 if operation was successful and permission is granted.
 * @shm_free_security:
 *	Deallocate the security structure @perm->security for the memory segment.
 *	@perm contains the IPC permissions of the shared memory structure.
 * @shm_associate:
 *	Check permission when a shared memory region is requested through the
 *	shmget system call. This hook is only called when returning the shared
 *	memory region identifier for an existing region, not when a new shared
 *	memory region is created.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@shmflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @shm_shmctl:
 *	Check permission when a shared memory control operation specified by
 *	@cmd is to be performed on the shared memory region with permissions @perm.
 *	The @perm may be NULL, e.g. for IPC_INFO or SHM_INFO.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @shm_shmat:
 *	Check permissions prior to allowing the shmat system call to attach the
 *	shared memory segment with permissions @perm to the data segment of the
 *	calling process. The attaching address is specified by @shmaddr.
 *	@perm contains the IPC permissions of the shared memory structure.
 *	@shmaddr contains the address to attach memory region to.
 *	@shmflg contains the operational flags.
 *	Return 0 if permission is granted.
 *
 * Security hooks for System V Semaphores
 *
 * @sem_alloc_security:
 *	Allocate and attach a security structure to the @perm->security
 *	field. The security field is initialized to NULL when the structure is
 *	first created.
 *	@perm contains the IPC permissions of the semaphore.
 *	Return 0 if operation was successful and permission is granted.
 * @sem_free_security:
 *	Deallocate security structure @perm->security for the semaphore.
 *	@perm contains the IPC permissions of the semaphore.
 * @sem_associate:
 *	Check permission when a semaphore is requested through the semget
 *	system call. This hook is only called when returning the semaphore
 *	identifier for an existing semaphore, not when a new one must be
 *	created.
 *	@perm contains the IPC permissions of the semaphore.
 *	@semflg contains the operation control flags.
 *	Return 0 if permission is granted.
 * @sem_semctl:
 *	Check permission when a semaphore operation specified by @cmd is to be
 *	performed on the semaphore. The @perm may be NULL, e.g. for
 *	IPC_INFO or SEM_INFO.
 *	@perm contains the IPC permissions of the semaphore. May be NULL.
 *	@cmd contains the operation to be performed.
 *	Return 0 if permission is granted.
 * @sem_semop:
 *	Check permissions before performing operations on members of the
 *	semaphore set. If the @alter flag is nonzero, the semaphore set
 *	may be modified.
 *	@perm contains the IPC permissions of the semaphore.
 *	@sops contains the operations to perform.
 *	@nsops contains the number of operations to perform.
 *	@alter contains the flag indicating whether changes are to be made.
 *	Return 0 if permission is granted.
 *
 * @binder_set_context_mgr:
 *	Check whether @mgr is allowed to be the binder context manager.
 *	@mgr contains the struct cred for the current binder process.
 *	Return 0 if permission is granted.
 * @binder_transaction:
 *	Check whether @from is allowed to invoke a binder transaction call
 *	to @to.
 *	@from contains the struct cred for the sending process.
 *	@to contains the struct cred for the receiving process.
 *	Return 0 if permission is granted.
 * @binder_transfer_binder:
 *	Check whether @from is allowed to transfer a binder reference to @to.
 *	@from contains the struct cred for the sending process.
 *	@to contains the struct cred for the receiving process.
 *	Return 0 if permission is granted.
 * @binder_transfer_file:
 *	Check whether @from is allowed to transfer @file to @to.
 *	@from contains the struct cred for the sending process.
 *	@file contains the struct file being transferred.
 *	@to contains the struct cred for the receiving process.
 *	Return 0 if permission is granted.
 *
 * @ptrace_access_check:
 *	Check permission before allowing the current process to trace the
 *	@child process.
 *	Security modules may also want to perform a process tracing check
 *	during an execve in the set_security or apply_creds hooks of
 *	tracing check during an execve in the bprm_set_creds hook of
 *	binprm_security_ops if the process is being traced and its security
 *	attributes would be changed by the execve.
 *	@child contains the task_struct structure for the target process.
 *	@mode contains the PTRACE_MODE flags indicating the form of access.
 *	Return 0 if permission is granted.
 * @ptrace_traceme:
 *	Check that the @parent process has sufficient permission to trace the
 *	current process before allowing the current process to present itself
 *	to the @parent process for tracing.
 *	@parent contains the task_struct structure for debugger process.
 *	Return 0 if permission is granted.
 * @capget:
 *	Get the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  The hook may also perform permission checking to
 *	determine if the current process is allowed to see the capability sets
 *	of the @target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if the capability sets were successfully obtained.
 * @capset:
 *	Set the @effective, @inheritable, and @permitted capability sets for
 *	the current process.
 *	@new contains the new credentials structure for target process.
 *	@old contains the current credentials structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 and update @new if permission is granted.
 * @capable:
 *	Check whether the @tsk process has the @cap capability in the indicated
 *	credentials.
 *	@cred contains the credentials to use.
 *	@ns contains the user namespace we want the capability in.
 *	@cap contains the capability <include/linux/capability.h>.
 *	@opts contains options for the capable check <include/linux/security.h>.
 *	Return 0 if the capability is granted for @tsk.
 * @quotactl:
 *	Check whether the quotactl syscall is allowed for this @sb.
 *	Return 0 if permission is granted.
 * @quota_on:
 *	Check whether QUOTAON is allowed for this @dentry.
 *	Return 0 if permission is granted.
 * @syslog:
 *	Check permission before accessing the kernel message ring or changing
 *	logging to the console.
 *	See the syslog(2) manual page for an explanation of the @type values.
 *	@type contains the SYSLOG_ACTION_* constant from
 *	<include/linux/syslog.h>.
 *	Return 0 if permission is granted.
 * @settime:
 *	Check permission to change the system time.
 *	struct timespec64 is defined in <include/linux/time64.h> and timezone
 *	is defined in <include/linux/time.h>
 *	@ts contains new time.
 *	@tz contains new timezone.
 *	Return 0 if permission is granted.
 * @vm_enough_memory:
 *	Check permissions for allocating a new virtual mapping.
 *	@mm contains the mm struct it is being added to.
 *	@pages contains the number of pages.
 *	Return 0 if permission is granted by the LSM infrastructure to the
 *	caller. If all LSMs return a positive value, __vm_enough_memory() will
 *	be called with cap_sys_admin set. If at least one LSM returns 0 or
 *	negative, __vm_enough_memory() will be called with cap_sys_admin
 *	cleared.
 *
 * @ismaclabel:
 *	Check if the extended attribute specified by @name
 *	represents a MAC label. Returns 1 if name is a MAC
 *	attribute otherwise returns 0.
 *	@name full extended attribute name to check against
 *	LSM as a MAC label.
 *
 * @secid_to_secctx:
 *	Convert secid to security context.  If secdata is NULL the length of
 *	the result will be returned in seclen, but no secdata will be returned.
 *	This does mean that the length could change between calls to check the
 *	length and the next call which actually allocates and returns the
 *	secdata.
 *	@secid contains the security ID.
 *	@secdata contains the pointer that stores the converted security
 *	context.
 *	@seclen pointer which contains the length of the data.
 *	Return 0 on success, error on failure.
 * @secctx_to_secid:
 *	Convert security context to secid.
 *	@secid contains the pointer to the generated security ID.
 *	@secdata contains the security context.
 *	Return 0 on success, error on failure.
 *
 * @release_secctx:
 *	Release the security context.
 *	@secdata contains the security context.
 *	@seclen contains the length of the security context.
 *
 * Security hooks for Audit
 *
 * @audit_rule_init:
 *	Allocate and initialize an LSM audit rule structure.
 *	@field contains the required Audit action.
 *	Fields flags are defined in <include/linux/audit.h>
 *	@op contains the operator the rule uses.
 *	@rulestr contains the context where the rule will be applied to.
 *	@lsmrule contains a pointer to receive the result.
 *	Return 0 if @lsmrule has been successfully set,
 *	-EINVAL in case of an invalid rule.
 *
 * @audit_rule_known:
 *	Specifies whether given @krule contains any fields related to
 *	current LSM.
 *	@krule contains the audit rule of interest.
 *	Return 1 in case of relation found, 0 otherwise.
 *
 * @audit_rule_match:
 *	Determine if given @secid matches a rule previously approved
 *	by @audit_rule_known.
 *	@secid contains the security id in question.
 *	@field contains the field which relates to current LSM.
 *	@op contains the operator that will be used for matching.
 *	@lrule points to the audit rule that will be checked against.
 *	Return 1 if secid matches the rule, 0 if it does not, -ERRNO on failure.
 *
 * @audit_rule_free:
 *	Deallocate the LSM audit rule structure previously allocated by
 *	audit_rule_init.
 *	@lsmrule contains the allocated rule.
 *
 * @inode_invalidate_secctx:
 *	Notify the security module that it must revalidate the security context
 *	of an inode.
 *
 * @inode_notifysecctx:
 *	Notify the security module of what the security context of an inode
 *	should be.  Initializes the incore security context managed by the
 *	security module for this inode.  Example usage:  NFS client invokes
 *	this hook to initialize the security context in its incore inode to the
 *	value provided by the server for the file when the server returned the
 *	file's attributes to the client.
 *	Must be called with inode->i_mutex locked.
 *	@inode we wish to set the security context of.
 *	@ctx contains the string which we wish to set in the inode.
 *	@ctxlen contains the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * @inode_setsecctx:
 *	Change the security context of an inode.  Updates the
 *	incore security context managed by the security module and invokes the
 *	fs code as needed (via __vfs_setxattr_noperm) to update any backing
 *	xattrs that represent the context.  Example usage:  NFS server invokes
 *	this hook to change the security context in its incore inode and on the
 *	backing filesystem to a value provided by the client on a SETATTR
 *	operation.
 *	Must be called with inode->i_mutex locked.
 *	@dentry contains the inode we wish to set the security context of.
 *	@ctx contains the string which we wish to set in the inode.
 *	@ctxlen contains the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * @inode_getsecctx:
 *	On success, returns 0 and fills out @ctx and @ctxlen with the security
 *	context for the given @inode.
 *	@inode we wish to get the security context of.
 *	@ctx is a pointer in which to place the allocated security context.
 *	@ctxlen points to the place to put the length of @ctx.
 *	Return 0 on success, error on failure.
 *
 * Security hooks for the general notification queue:
 *
 * @post_notification:
 *	Check to see if a watch notification can be posted to a particular
 *	queue.
 *	@w_cred: The credentials of the whoever set the watch.
 *	@cred: The event-triggerer's credentials.
 *	@n: The notification being posted.
 *	Return 0 if permission is granted.
 *
 * @watch_key:
 *	Check to see if a process is allowed to watch for event notifications
 *	from a key or keyring.
 *	@key: The key to watch.
 *	Return 0 if permission is granted.
 *
 * Security hooks for using the eBPF maps and programs functionalities through
 * eBPF syscalls.
 *
 * @bpf:
 *	Do a initial check for all bpf syscalls after the attribute is copied
 *	into the kernel. The actual security module can implement their own
 *	rules to check the specific cmd they need.
 *	Return 0 if permission is granted.
 *
 * @bpf_map:
 *	Do a check when the kernel generate and return a file descriptor for
 *	eBPF maps.
 *	@map: bpf map that we want to access.
 *	@mask: the access flags.
 *	Return 0 if permission is granted.
 *
 * @bpf_prog:
 *	Do a check when the kernel generate and return a file descriptor for
 *	eBPF programs.
 *	@prog: bpf prog that userspace want to use.
 *	Return 0 if permission is granted.
 *
 * @bpf_map_alloc_security:
 *	Initialize the security field inside bpf map.
 *	Return 0 on success, error on failure.
 *
 * @bpf_map_free_security:
 *	Clean up the security information stored inside bpf map.
 *
 * @bpf_prog_alloc_security:
 *	Initialize the security field inside bpf program.
 *	Return 0 on success, error on failure.
 *
 * @bpf_prog_free_security:
 *	Clean up the security information stored inside bpf prog.
 *
 * @locked_down:
 *	Determine whether a kernel feature that potentially enables arbitrary
 *	code execution in kernel space should be permitted.
 *	@what: kernel feature being accessed.
 *	Return 0 if permission is granted.
 *
 * Security hooks for perf events
 *
 * @perf_event_open:
 *	Check whether the @type of perf_event_open syscall is allowed.
 *	Return 0 if permission is granted.
 * @perf_event_alloc:
 *	Allocate and save perf_event security info.
 *	Return 0 on success, error on failure.
 * @perf_event_free:
 *	Release (free) perf_event security info.
 * @perf_event_read:
 *	Read perf_event security info if allowed.
 *	Return 0 if permission is granted.
 * @perf_event_write:
 *	Write perf_event security info if allowed.
 *	Return 0 if permission is granted.
 *
 * Security hooks for io_uring
 *
 * @uring_override_creds:
 *	Check if the current task, executing an io_uring operation, is allowed
 *	to override it's credentials with @new.
 *	@new: the new creds to use.
 *	Return 0 if permission is granted.
 *
 * @uring_sqpoll:
 *	Check whether the current task is allowed to spawn a io_uring polling
 *	thread (IORING_SETUP_SQPOLL).
 *	Return 0 if permission is granted.
 *
 * @uring_cmd:
 *	Check whether the file_operations uring_cmd is allowed to run.
 *	Return 0 if permission is granted.
 *
 */
union security_list_options {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) RET (*NAME)(__VA_ARGS__);
	#include "lsm_hook_defs.h"
	#undef LSM_HOOK
};

struct security_hook_heads {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) struct hlist_head NAME;
	#include "lsm_hook_defs.h"
	#undef LSM_HOOK
} __randomize_layout;

/*
 * Security module hook list structure.
 * For use with generic list macros for common operations.
 */
struct security_hook_list {
	struct hlist_node		list;
	struct hlist_head		*head;
	union security_list_options	hook;
	const char			*lsm;
} __randomize_layout;

/*
 * Security blob size or offset data.
 */
struct lsm_blob_sizes {
	int	lbs_cred;
	int	lbs_file;
	int	lbs_inode;
	int	lbs_superblock;
	int	lbs_ipc;
	int	lbs_msg_msg;
	int	lbs_task;
};

/*
 * LSM_RET_VOID is used as the default value in LSM_HOOK definitions for void
 * LSM hooks (in include/linux/lsm_hook_defs.h).
 */
#define LSM_RET_VOID ((void) 0)

/*
 * Initializing a security_hook_list structure takes
 * up a lot of space in a source file. This macro takes
 * care of the common case and reduces the amount of
 * text involved.
 */
#define LSM_HOOK_INIT(HEAD, HOOK) \
	{ .head = &security_hook_heads.HEAD, .hook = { .HEAD = HOOK } }

extern struct security_hook_heads security_hook_heads;
extern char *lsm_names;

extern void security_add_hooks(struct security_hook_list *hooks, int count,
				const char *lsm);

#define LSM_FLAG_LEGACY_MAJOR	BIT(0)
#define LSM_FLAG_EXCLUSIVE	BIT(1)

enum lsm_order {
	LSM_ORDER_FIRST = -1,	/* This is only for capabilities. */
	LSM_ORDER_MUTABLE = 0,
};

struct lsm_info {
	const char *name;	/* Required. */
	enum lsm_order order;	/* Optional: default is LSM_ORDER_MUTABLE */
	unsigned long flags;	/* Optional: flags describing LSM */
	int *enabled;		/* Optional: controlled by CONFIG_LSM */
	int (*init)(void);	/* Required. */
	struct lsm_blob_sizes *blobs; /* Optional: for blob sharing. */
};

extern struct lsm_info __start_lsm_info[], __end_lsm_info[];
extern struct lsm_info __start_early_lsm_info[], __end_early_lsm_info[];

#define DEFINE_LSM(lsm)							\
	static struct lsm_info __lsm_##lsm				\
		__used __section(".lsm_info.init")			\
		__aligned(sizeof(unsigned long))

#define DEFINE_EARLY_LSM(lsm)						\
	static struct lsm_info __early_lsm_##lsm			\
		__used __section(".early_lsm_info.init")		\
		__aligned(sizeof(unsigned long))

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
/*
 * Assuring the safety of deleting a security module is up to
 * the security module involved. This may entail ordering the
 * module's hook list in a particular way, refusing to disable
 * the module once a policy is loaded or any number of other
 * actions better imagined than described.
 *
 * The name of the configuration option reflects the only module
 * that currently uses the mechanism. Any developer who thinks
 * disabling their module is a good idea needs to be at least as
 * careful as the SELinux team.
 */
static inline void security_delete_hooks(struct security_hook_list *hooks,
						int count)
{
	int i;

	for (i = 0; i < count; i++)
		hlist_del_rcu(&hooks[i].list);
}
#endif /* CONFIG_SECURITY_SELINUX_DISABLE */

/* Currently required to handle SELinux runtime hook disable. */
#ifdef CONFIG_SECURITY_WRITABLE_HOOKS
#define __lsm_ro_after_init
#else
#define __lsm_ro_after_init	__ro_after_init
#endif /* CONFIG_SECURITY_WRITABLE_HOOKS */

extern int lsm_inode_alloc(struct inode *inode);

#endif /* ! __LINUX_LSM_HOOKS_H */
