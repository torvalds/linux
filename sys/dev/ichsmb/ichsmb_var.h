/*-
 * ichsmb_var.h
 *
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 */

#ifndef _DEV_ICHSMB_ICHSMB_VAR_H
#define _DEV_ICHSMB_ICHSMB_VAR_H

#include "smbus_if.h"

/* Per-device private info */
struct ichsmb_softc {

	/* Device/bus stuff */
	device_t		dev;		/* this device */
	device_t		smb;		/* smb device */
	struct resource		*io_res;        /* i/o port resource */
	int			io_rid;         /* i/o port bus id */
	struct resource		*irq_res;       /* interrupt resource */
	int			irq_rid;        /* interrupt bus id */
	void			*irq_handle;    /* handle for interrupt code */

	/* Device state */
	int			ich_cmd;	/* ich command, or -1 */
	int			smb_error;	/* result of smb command */
	int			block_count;	/* count for block read/write */
	int			block_index;	/* index for block read/write */
	u_char			block_write;	/* 0=read, 1=write */
	u_char			block_data[32];	/* block read/write data */
	struct mtx		mutex;		/* device mutex */
};
typedef struct ichsmb_softc *sc_p;

/* SMBus methods */
extern smbus_callback_t	ichsmb_callback;	
extern smbus_quick_t	ichsmb_quick;	
extern smbus_sendb_t	ichsmb_sendb;	
extern smbus_recvb_t	ichsmb_recvb;	
extern smbus_writeb_t	ichsmb_writeb;	
extern smbus_writew_t	ichsmb_writew;	
extern smbus_readb_t	ichsmb_readb;	
extern smbus_readw_t	ichsmb_readw;	
extern smbus_pcall_t	ichsmb_pcall;	
extern smbus_bwrite_t	ichsmb_bwrite;	
extern smbus_bread_t	ichsmb_bread;	

/* Other functions */
extern void	ichsmb_device_intr(void *cookie);
extern void	ichsmb_release_resources(sc_p sc);
extern int	ichsmb_probe(device_t dev);
extern int	ichsmb_attach(device_t dev);
extern int	ichsmb_detach(device_t dev);

#endif /* _DEV_ICHSMB_ICHSMB_VAR_H */

