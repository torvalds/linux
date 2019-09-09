/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * ipmi.h
 *
 * MontaVista IPMI interface
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 */

#ifndef _UAPI__LINUX_IPMI_H
#define _UAPI__LINUX_IPMI_H

#include <linux/ipmi_msgdefs.h>
#include <linux/compiler.h>

/*
 * This file describes an interface to an IPMI driver.  You have to
 * have a fairly good understanding of IPMI to use this, so go read
 * the specs first before actually trying to do anything.
 *
 * With that said, this driver provides a multi-user interface to the
 * IPMI driver, and it allows multiple IPMI physical interfaces below
 * the driver.  The physical interfaces bind as a lower layer on the
 * driver.  They appear as interfaces to the application using this
 * interface.
 *
 * Multi-user means that multiple applications may use the driver,
 * send commands, receive responses, etc.  The driver keeps track of
 * commands the user sends and tracks the responses.  The responses
 * will go back to the application that send the command.  If the
 * response doesn't come back in time, the driver will return a
 * timeout error response to the application.  Asynchronous events
 * from the BMC event queue will go to all users bound to the driver.
 * The incoming event queue in the BMC will automatically be flushed
 * if it becomes full and it is queried once a second to see if
 * anything is in it.  Incoming commands to the driver will get
 * delivered as commands.
 */

/*
 * This is an overlay for all the address types, so it's easy to
 * determine the actual address type.  This is kind of like addresses
 * work for sockets.
 */
#define IPMI_MAX_ADDR_SIZE 32
struct ipmi_addr {
	 /* Try to take these from the "Channel Medium Type" table
	    in section 6.5 of the IPMI 1.5 manual. */
	int   addr_type;
	short channel;
	char  data[IPMI_MAX_ADDR_SIZE];
};

/*
 * When the address is not used, the type will be set to this value.
 * The channel is the BMC's channel number for the channel (usually
 * 0), or IPMC_BMC_CHANNEL if communicating directly with the BMC.
 */
#define IPMI_SYSTEM_INTERFACE_ADDR_TYPE	0x0c
struct ipmi_system_interface_addr {
	int           addr_type;
	short         channel;
	unsigned char lun;
};

/* An IPMB Address. */
#define IPMI_IPMB_ADDR_TYPE		0x01
/* Used for broadcast get device id as described in section 17.9 of the
   IPMI 1.5 manual. */
#define IPMI_IPMB_BROADCAST_ADDR_TYPE	0x41
struct ipmi_ipmb_addr {
	int           addr_type;
	short         channel;
	unsigned char slave_addr;
	unsigned char lun;
};

/*
 * A LAN Address.  This is an address to/from a LAN interface bridged
 * by the BMC, not an address actually out on the LAN.
 *
 * A conscious decision was made here to deviate slightly from the IPMI
 * spec.  We do not use rqSWID and rsSWID like it shows in the
 * message.  Instead, we use remote_SWID and local_SWID.  This means
 * that any message (a request or response) from another device will
 * always have exactly the same address.  If you didn't do this,
 * requests and responses from the same device would have different
 * addresses, and that's not too cool.
 *
 * In this address, the remote_SWID is always the SWID the remote
 * message came from, or the SWID we are sending the message to.
 * local_SWID is always our SWID.  Note that having our SWID in the
 * message is a little weird, but this is required.
 */
#define IPMI_LAN_ADDR_TYPE		0x04
struct ipmi_lan_addr {
	int           addr_type;
	short         channel;
	unsigned char privilege;
	unsigned char session_handle;
	unsigned char remote_SWID;
	unsigned char local_SWID;
	unsigned char lun;
};


/*
 * Channel for talking directly with the BMC.  When using this
 * channel, This is for the system interface address type only.  FIXME
 * - is this right, or should we use -1?
 */
#define IPMI_BMC_CHANNEL  0xf
#define IPMI_NUM_CHANNELS 0x10

/*
 * Used to signify an "all channel" bitmask.  This is more than the
 * actual number of channels because this is used in userland and
 * will cover us if the number of channels is extended.
 */
#define IPMI_CHAN_ALL     (~0)


/*
 * A raw IPMI message without any addressing.  This covers both
 * commands and responses.  The completion code is always the first
 * byte of data in the response (as the spec shows the messages laid
 * out).
 */
struct ipmi_msg {
	unsigned char  netfn;
	unsigned char  cmd;
	unsigned short data_len;
	unsigned char  __user *data;
};

struct kernel_ipmi_msg {
	unsigned char  netfn;
	unsigned char  cmd;
	unsigned short data_len;
	unsigned char  *data;
};

/*
 * Various defines that are useful for IPMI applications.
 */
#define IPMI_INVALID_CMD_COMPLETION_CODE	0xC1
#define IPMI_TIMEOUT_COMPLETION_CODE		0xC3
#define IPMI_UNKNOWN_ERR_COMPLETION_CODE	0xff


/*
 * Receive types for messages coming from the receive interface.  This
 * is used for the receive in-kernel interface and in the receive
 * IOCTL.
 *
 * The "IPMI_RESPONSE_RESPNOSE_TYPE" is a little strange sounding, but
 * it allows you to get the message results when you send a response
 * message.
 */
#define IPMI_RESPONSE_RECV_TYPE		1 /* A response to a command */
#define IPMI_ASYNC_EVENT_RECV_TYPE	2 /* Something from the event queue */
#define IPMI_CMD_RECV_TYPE		3 /* A command from somewhere else */
#define IPMI_RESPONSE_RESPONSE_TYPE	4 /* The response for
					      a sent response, giving any
					      error status for sending the
					      response.  When you send a
					      response message, this will
					      be returned. */
#define IPMI_OEM_RECV_TYPE		5 /* The response for OEM Channels */

/* Note that async events and received commands do not have a completion
   code as the first byte of the incoming data, unlike a response. */


/*
 * Modes for ipmi_set_maint_mode() and the userland IOCTL.  The AUTO
 * setting is the default and means it will be set on certain
 * commands.  Hard setting it on and off will override automatic
 * operation.
 */
#define IPMI_MAINTENANCE_MODE_AUTO	0
#define IPMI_MAINTENANCE_MODE_OFF	1
#define IPMI_MAINTENANCE_MODE_ON	2



/*
 * The userland interface
 */

/*
 * The userland interface for the IPMI driver is a standard character
 * device, with each instance of an interface registered as a minor
 * number under the major character device.
 *
 * The read and write calls do not work, to get messages in and out
 * requires ioctl calls because of the complexity of the data.  select
 * and poll do work, so you can wait for input using the file
 * descriptor, you just can use read to get it.
 *
 * In general, you send a command down to the interface and receive
 * responses back.  You can use the msgid value to correlate commands
 * and responses, the driver will take care of figuring out which
 * incoming messages are for which command and find the proper msgid
 * value to report.  You will only receive reponses for commands you
 * send.  Asynchronous events, however, go to all open users, so you
 * must be ready to handle these (or ignore them if you don't care).
 *
 * The address type depends upon the channel type.  When talking
 * directly to the BMC (IPMC_BMC_CHANNEL), the address is ignored
 * (IPMI_UNUSED_ADDR_TYPE).  When talking to an IPMB channel, you must
 * supply a valid IPMB address with the addr_type set properly.
 *
 * When talking to normal channels, the driver takes care of the
 * details of formatting and sending messages on that channel.  You do
 * not, for instance, have to format a send command, you just send
 * whatever command you want to the channel, the driver will create
 * the send command, automatically issue receive command and get even
 * commands, and pass those up to the proper user.
 */


/* The magic IOCTL value for this interface. */
#define IPMI_IOC_MAGIC 'i'


/* Messages sent to the interface are this format. */
struct ipmi_req {
	unsigned char __user *addr; /* Address to send the message to. */
	unsigned int  addr_len;

	long    msgid; /* The sequence number for the message.  This
			  exact value will be reported back in the
			  response to this request if it is a command.
			  If it is a response, this will be used as
			  the sequence value for the response.  */

	struct ipmi_msg msg;
};
/*
 * Send a message to the interfaces.  error values are:
 *   - EFAULT - an address supplied was invalid.
 *   - EINVAL - The address supplied was not valid, or the command
 *              was not allowed.
 *   - EMSGSIZE - The message to was too large.
 *   - ENOMEM - Buffers could not be allocated for the command.
 */
#define IPMICTL_SEND_COMMAND		_IOR(IPMI_IOC_MAGIC, 13,	\
					     struct ipmi_req)

/* Messages sent to the interface with timing parameters are this
   format. */
struct ipmi_req_settime {
	struct ipmi_req req;

	/* See ipmi_request_settime() above for details on these
	   values. */
	int          retries;
	unsigned int retry_time_ms;
};
/*
 * Send a message to the interfaces with timing parameters.  error values
 * are:
 *   - EFAULT - an address supplied was invalid.
 *   - EINVAL - The address supplied was not valid, or the command
 *              was not allowed.
 *   - EMSGSIZE - The message to was too large.
 *   - ENOMEM - Buffers could not be allocated for the command.
 */
#define IPMICTL_SEND_COMMAND_SETTIME	_IOR(IPMI_IOC_MAGIC, 21,	\
					     struct ipmi_req_settime)

/* Messages received from the interface are this format. */
struct ipmi_recv {
	int     recv_type; /* Is this a command, response or an
			      asyncronous event. */

	unsigned char __user *addr;    /* Address the message was from is put
				   here.  The caller must supply the
				   memory. */
	unsigned int  addr_len; /* The size of the address buffer.
				   The caller supplies the full buffer
				   length, this value is updated to
				   the actual message length when the
				   message is received. */

	long    msgid; /* The sequence number specified in the request
			  if this is a response.  If this is a command,
			  this will be the sequence number from the
			  command. */

	struct ipmi_msg msg; /* The data field must point to a buffer.
				The data_size field must be set to the
				size of the message buffer.  The
				caller supplies the full buffer
				length, this value is updated to the
				actual message length when the message
				is received. */
};

/*
 * Receive a message.  error values:
 *  - EAGAIN - no messages in the queue.
 *  - EFAULT - an address supplied was invalid.
 *  - EINVAL - The address supplied was not valid.
 *  - EMSGSIZE - The message to was too large to fit into the message buffer,
 *               the message will be left in the buffer. */
#define IPMICTL_RECEIVE_MSG		_IOWR(IPMI_IOC_MAGIC, 12,	\
					      struct ipmi_recv)

/*
 * Like RECEIVE_MSG, but if the message won't fit in the buffer, it
 * will truncate the contents instead of leaving the data in the
 * buffer.
 */
#define IPMICTL_RECEIVE_MSG_TRUNC	_IOWR(IPMI_IOC_MAGIC, 11,	\
					      struct ipmi_recv)

/* Register to get commands from other entities on this interface. */
struct ipmi_cmdspec {
	unsigned char netfn;
	unsigned char cmd;
};

/*
 * Register to receive a specific command.  error values:
 *   - EFAULT - an address supplied was invalid.
 *   - EBUSY - The netfn/cmd supplied was already in use.
 *   - ENOMEM - could not allocate memory for the entry.
 */
#define IPMICTL_REGISTER_FOR_CMD	_IOR(IPMI_IOC_MAGIC, 14,	\
					     struct ipmi_cmdspec)
/*
 * Unregister a registered command.  error values:
 *  - EFAULT - an address supplied was invalid.
 *  - ENOENT - The netfn/cmd was not found registered for this user.
 */
#define IPMICTL_UNREGISTER_FOR_CMD	_IOR(IPMI_IOC_MAGIC, 15,	\
					     struct ipmi_cmdspec)

/*
 * Register to get commands from other entities on specific channels.
 * This way, you can only listen on specific channels, or have messages
 * from some channels go to one place and other channels to someplace
 * else.  The chans field is a bitmask, (1 << channel) for each channel.
 * It may be IPMI_CHAN_ALL for all channels.
 */
struct ipmi_cmdspec_chans {
	unsigned int netfn;
	unsigned int cmd;
	unsigned int chans;
};

/*
 * Register to receive a specific command on specific channels.  error values:
 *   - EFAULT - an address supplied was invalid.
 *   - EBUSY - One of the netfn/cmd/chans supplied was already in use.
 *   - ENOMEM - could not allocate memory for the entry.
 */
#define IPMICTL_REGISTER_FOR_CMD_CHANS	_IOR(IPMI_IOC_MAGIC, 28,	\
					     struct ipmi_cmdspec_chans)
/*
 * Unregister some netfn/cmd/chans.  error values:
 *  - EFAULT - an address supplied was invalid.
 *  - ENOENT - None of the netfn/cmd/chans were found registered for this user.
 */
#define IPMICTL_UNREGISTER_FOR_CMD_CHANS _IOR(IPMI_IOC_MAGIC, 29,	\
					     struct ipmi_cmdspec_chans)

/*
 * Set whether this interface receives events.  Note that the first
 * user registered for events will get all pending events for the
 * interface.  error values:
 *  - EFAULT - an address supplied was invalid.
 */
#define IPMICTL_SET_GETS_EVENTS_CMD	_IOR(IPMI_IOC_MAGIC, 16, int)

/*
 * Set and get the slave address and LUN that we will use for our
 * source messages.  Note that this affects the interface, not just
 * this user, so it will affect all users of this interface.  This is
 * so some initialization code can come in and do the OEM-specific
 * things it takes to determine your address (if not the BMC) and set
 * it for everyone else.  You should probably leave the LUN alone.
 */
struct ipmi_channel_lun_address_set {
	unsigned short channel;
	unsigned char  value;
};
#define IPMICTL_SET_MY_CHANNEL_ADDRESS_CMD \
	_IOR(IPMI_IOC_MAGIC, 24, struct ipmi_channel_lun_address_set)
#define IPMICTL_GET_MY_CHANNEL_ADDRESS_CMD \
	_IOR(IPMI_IOC_MAGIC, 25, struct ipmi_channel_lun_address_set)
#define IPMICTL_SET_MY_CHANNEL_LUN_CMD \
	_IOR(IPMI_IOC_MAGIC, 26, struct ipmi_channel_lun_address_set)
#define IPMICTL_GET_MY_CHANNEL_LUN_CMD \
	_IOR(IPMI_IOC_MAGIC, 27, struct ipmi_channel_lun_address_set)
/* Legacy interfaces, these only set IPMB 0. */
#define IPMICTL_SET_MY_ADDRESS_CMD	_IOR(IPMI_IOC_MAGIC, 17, unsigned int)
#define IPMICTL_GET_MY_ADDRESS_CMD	_IOR(IPMI_IOC_MAGIC, 18, unsigned int)
#define IPMICTL_SET_MY_LUN_CMD		_IOR(IPMI_IOC_MAGIC, 19, unsigned int)
#define IPMICTL_GET_MY_LUN_CMD		_IOR(IPMI_IOC_MAGIC, 20, unsigned int)

/*
 * Get/set the default timing values for an interface.  You shouldn't
 * generally mess with these.
 */
struct ipmi_timing_parms {
	int          retries;
	unsigned int retry_time_ms;
};
#define IPMICTL_SET_TIMING_PARMS_CMD	_IOR(IPMI_IOC_MAGIC, 22, \
					     struct ipmi_timing_parms)
#define IPMICTL_GET_TIMING_PARMS_CMD	_IOR(IPMI_IOC_MAGIC, 23, \
					     struct ipmi_timing_parms)

/*
 * Set the maintenance mode.  See ipmi_set_maintenance_mode() above
 * for a description of what this does.
 */
#define IPMICTL_GET_MAINTENANCE_MODE_CMD	_IOR(IPMI_IOC_MAGIC, 30, int)
#define IPMICTL_SET_MAINTENANCE_MODE_CMD	_IOW(IPMI_IOC_MAGIC, 31, int)

#endif /* _UAPI__LINUX_IPMI_H */
