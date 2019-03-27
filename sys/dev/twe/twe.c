/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * Driver for the 3ware Escalade family of IDE RAID controllers.
 */

#include <dev/twe/twe_compat.h>
#include <dev/twe/twereg.h>
#include <dev/twe/tweio.h>
#include <dev/twe/twevar.h>
#define TWE_DEFINE_TABLES
#include <dev/twe/twe_tables.h>

/*
 * Command submission.
 */
static int	twe_get_param_1(struct twe_softc *sc, int table_id, int param_id, u_int8_t *result);
static int	twe_get_param_2(struct twe_softc *sc, int table_id, int param_id, u_int16_t *result);
static int	twe_get_param_4(struct twe_softc *sc, int table_id, int param_id, u_int32_t *result);
static void	*twe_get_param(struct twe_softc *sc, int table_id, int parameter_id, size_t size, 
					       void (* func)(struct twe_request *tr));
#ifdef TWE_SHUTDOWN_NOTIFICATION
static int	twe_set_param_1(struct twe_softc *sc, int table_id, int param_id, u_int8_t value);
#endif
#if 0
static int	twe_set_param_2(struct twe_softc *sc, int table_id, int param_id, u_int16_t value);
static int	twe_set_param_4(struct twe_softc *sc, int table_id, int param_id, u_int32_t value);
#endif
static int	twe_set_param(struct twe_softc *sc, int table_id, int param_id, int param_size, 
					      void *data);
static int	twe_init_connection(struct twe_softc *sc, int mode);
static int	twe_wait_request(struct twe_request *tr);
static int	twe_immediate_request(struct twe_request *tr, int usetmp);
static void	twe_completeio(struct twe_request *tr);
static void	twe_reset(struct twe_softc *sc);
static int	twe_add_unit(struct twe_softc *sc, int unit);
static int	twe_del_unit(struct twe_softc *sc, int unit);

/*
 * Command I/O to controller.
 */
static void	twe_done(struct twe_softc *sc, int startio);
static void	twe_complete(struct twe_softc *sc);
static int	twe_wait_status(struct twe_softc *sc, u_int32_t status, int timeout);
static int	twe_drain_response_queue(struct twe_softc *sc);
static int	twe_check_bits(struct twe_softc *sc, u_int32_t status_reg);
static int	twe_soft_reset(struct twe_softc *sc);

/*
 * Interrupt handling.
 */
static void	twe_host_intr(struct twe_softc *sc);
static void	twe_attention_intr(struct twe_softc *sc);
static void	twe_command_intr(struct twe_softc *sc);

/*
 * Asynchronous event handling.
 */
static int	twe_fetch_aen(struct twe_softc *sc);
static void	twe_handle_aen(struct twe_request *tr);
static void	twe_enqueue_aen(struct twe_softc *sc, u_int16_t aen);
static u_int16_t	twe_dequeue_aen(struct twe_softc *sc);
static int	twe_drain_aen_queue(struct twe_softc *sc);
static int	twe_find_aen(struct twe_softc *sc, u_int16_t aen);

/*
 * Command buffer management.
 */
static int	twe_get_request(struct twe_softc *sc, struct twe_request **tr);
static void	twe_release_request(struct twe_request *tr);

/*
 * Debugging.
 */
static char 	*twe_format_aen(struct twe_softc *sc, u_int16_t aen);
static int	twe_report_request(struct twe_request *tr);
static void	twe_panic(struct twe_softc *sc, char *reason);

/********************************************************************************
 ********************************************************************************
                                                                Public Interfaces
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Initialise the controller, set up driver data structures.
 */
int
twe_setup(struct twe_softc *sc)
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    u_int32_t		status_reg;
    int			i;

    debug_called(4);

    /*
     * Initialise request queues.
     */
    twe_initq_free(sc);
    twe_initq_bio(sc);
    twe_initq_ready(sc);
    twe_initq_busy(sc);
    twe_initq_complete(sc);
    sc->twe_wait_aen = -1;

    /*
     * Allocate request structures up front.
     */
    for (i = 0; i < TWE_Q_LENGTH; i++) {
	if ((tr = twe_allocate_request(sc, i)) == NULL)
	    return(ENOMEM);
	/*
	 * Set global defaults that won't change.
	 */
	cmd = TWE_FIND_COMMAND(tr);
	cmd->generic.host_id = sc->twe_host_id;		/* controller-assigned host ID */
	cmd->generic.request_id = i;			/* our index number */
	sc->twe_lookup[i] = tr;

	/*
	 * Put command onto the freelist.
	 */
	TWE_IO_LOCK(sc);
	twe_release_request(tr);
	TWE_IO_UNLOCK(sc);
    }
    TWE_IO_LOCK(sc);

    /*
     * Check status register for errors, clear them.
     */
    status_reg = TWE_STATUS(sc);
    twe_check_bits(sc, status_reg);

    /*
     * Wait for the controller to come ready.
     */
    if (twe_wait_status(sc, TWE_STATUS_MICROCONTROLLER_READY, 60)) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "microcontroller not ready\n");
	return(ENXIO);
    }

    /*
     * Disable interrupts from the card.
     */
    twe_disable_interrupts(sc);

    /*
     * Soft reset the controller, look for the AEN acknowledging the reset,
     * check for errors, drain the response queue.
     */
    for (i = 0; i < TWE_MAX_RESET_TRIES; i++) {

	if (i > 0)
	    twe_printf(sc, "reset %d failed, trying again\n", i);
	
	if (!twe_soft_reset(sc))
	    break;			/* reset process complete */
    }
    TWE_IO_UNLOCK(sc);
    /* did we give up? */
    if (i >= TWE_MAX_RESET_TRIES) {
	twe_printf(sc, "can't initialise controller, giving up\n");
	return(ENXIO);
    }

    return(0);
}

static int
twe_add_unit(struct twe_softc *sc, int unit)
{
    struct twe_drive		*dr;
    int				table, error = 0;
    u_int16_t			dsize;
    TWE_Param			*drives = NULL, *param = NULL;
    TWE_Array_Descriptor	*ud;

    TWE_CONFIG_ASSERT_LOCKED(sc);
    if (unit < 0 || unit > TWE_MAX_UNITS)
	return (EINVAL);

    /*
     * The controller is in a safe state, so try to find drives attached to it.
     */
    TWE_IO_LOCK(sc);
    if ((drives = twe_get_param(sc, TWE_PARAM_UNITSUMMARY, TWE_PARAM_UNITSUMMARY_Status,
				TWE_MAX_UNITS, NULL)) == NULL) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "can't detect attached units\n");
	return (EIO);
    }

    dr = &sc->twe_drive[unit];
    /* check that the drive is online */
    if (!(drives->data[unit] & TWE_PARAM_UNITSTATUS_Online)) {
	TWE_IO_UNLOCK(sc);
	error = ENXIO;
	goto out;
    }

    table = TWE_PARAM_UNITINFO + unit;

    if (twe_get_param_4(sc, table, TWE_PARAM_UNITINFO_Capacity, &dr->td_size)) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "error fetching capacity for unit %d\n", unit);
	error = EIO;
	goto out;
    }
    if (twe_get_param_1(sc, table, TWE_PARAM_UNITINFO_Status, &dr->td_state)) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "error fetching state for unit %d\n", unit);
	error = EIO;
	goto out;
    }
    if (twe_get_param_2(sc, table, TWE_PARAM_UNITINFO_DescriptorSize, &dsize)) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "error fetching descriptor size for unit %d\n", unit);
	error = EIO;
	goto out;
    }
    if ((param = twe_get_param(sc, table, TWE_PARAM_UNITINFO_Descriptor, dsize - 3, NULL)) == NULL) {
	TWE_IO_UNLOCK(sc);
	twe_printf(sc, "error fetching descriptor for unit %d\n", unit);
	error = EIO;
	goto out;
    }
    ud = (TWE_Array_Descriptor *)param->data;
    dr->td_type = ud->configuration;
    dr->td_stripe = ud->stripe_size;

    /* build synthetic geometry as per controller internal rules */
    if (dr->td_size > 0x200000) {
	dr->td_heads = 255;
	dr->td_sectors = 63;
    } else {
	dr->td_heads = 64;
	dr->td_sectors = 32;
    }
    dr->td_cylinders = dr->td_size / (dr->td_heads * dr->td_sectors);
    dr->td_twe_unit = unit;
    TWE_IO_UNLOCK(sc);

    error = twe_attach_drive(sc, dr);

out:
    if (param != NULL)
	free(param, M_DEVBUF);
    if (drives != NULL)
	free(drives, M_DEVBUF);
    return (error);
}

static int
twe_del_unit(struct twe_softc *sc, int unit)
{
    int error;

    TWE_CONFIG_ASSERT_LOCKED(sc);
    if (unit < 0 || unit >= TWE_MAX_UNITS)
	return (ENXIO);

    if (sc->twe_drive[unit].td_disk == NULL)
	return (ENXIO);

    error = twe_detach_drive(sc, unit);
    return (error);
}

/********************************************************************************
 * Locate disk devices and attach children to them.
 */
void
twe_init(struct twe_softc *sc)
{
    int 		i;

    /*
     * Scan for drives
     */
    TWE_CONFIG_LOCK(sc);
    for (i = 0; i < TWE_MAX_UNITS; i++)
	twe_add_unit(sc, i);
    TWE_CONFIG_UNLOCK(sc);

    /*
     * Initialise connection with controller.
     */
    TWE_IO_LOCK(sc);
    twe_init_connection(sc, TWE_INIT_MESSAGE_CREDITS);

#ifdef TWE_SHUTDOWN_NOTIFICATION
    /*
     * Tell the controller we support shutdown notification.
     */
    twe_set_param_1(sc, TWE_PARAM_FEATURES, TWE_PARAM_FEATURES_DriverShutdown, 1);
#endif

    /* 
     * Mark controller up and ready to run.
     */
    sc->twe_state &= ~TWE_STATE_SHUTDOWN;

    /*
     * Finally enable interrupts.
     */
    twe_enable_interrupts(sc);
    TWE_IO_UNLOCK(sc);
}

/********************************************************************************
 * Stop the controller
 */
void
twe_deinit(struct twe_softc *sc)
{
    /*
     * Mark the controller as shutting down, and disable any further interrupts.
     */
    TWE_IO_ASSERT_LOCKED(sc);
    sc->twe_state |= TWE_STATE_SHUTDOWN;
    twe_disable_interrupts(sc);

#ifdef TWE_SHUTDOWN_NOTIFICATION
    /*
     * Disconnect from the controller
     */
    twe_init_connection(sc, TWE_SHUTDOWN_MESSAGE_CREDITS);
#endif
}

/*******************************************************************************
 * Take an interrupt, or be poked by other code to look for interrupt-worthy
 * status.
 */
void
twe_intr(struct twe_softc *sc)
{
    u_int32_t		status_reg;

    debug_called(4);

    /*
     * Collect current interrupt status.
     */
    status_reg = TWE_STATUS(sc);
    twe_check_bits(sc, status_reg);

    /*
     * Dispatch based on interrupt status
     */
    if (status_reg & TWE_STATUS_HOST_INTERRUPT)
	twe_host_intr(sc);
    if (status_reg & TWE_STATUS_ATTENTION_INTERRUPT)
	twe_attention_intr(sc);
    if (status_reg & TWE_STATUS_COMMAND_INTERRUPT)
	twe_command_intr(sc);
    if (status_reg & TWE_STATUS_RESPONSE_INTERRUPT)
	twe_done(sc, 1);
};

/********************************************************************************
 * Pull as much work off the softc's work queue as possible and give it to the
 * controller.
 */
void
twe_startio(struct twe_softc *sc)
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    struct bio		*bp;
    int			error;

    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);
    if (sc->twe_state & (TWE_STATE_CTLR_BUSY | TWE_STATE_FRZN))
	return;

    /* spin until something prevents us from doing any work */
    for (;;) {

	/* try to get a command that's already ready to go */
	tr = twe_dequeue_ready(sc);

	/* build a command from an outstanding bio */
	if (tr == NULL) {
	    
	    /* get a command to handle the bio with */
	    if (twe_get_request(sc, &tr))
		break;

	    /* see if there's work to be done */
	    if ((bp = twe_dequeue_bio(sc)) == NULL) {
		twe_release_request(tr);
		break;
	    }

	    /* connect the bio to the command */
	    tr->tr_complete = twe_completeio;
	    tr->tr_private = bp;
	    tr->tr_data = bp->bio_data;
	    tr->tr_length = bp->bio_bcount;
	    cmd = TWE_FIND_COMMAND(tr);
	    if (bp->bio_cmd == BIO_READ) {
		tr->tr_flags |= TWE_CMD_DATAIN;
		cmd->io.opcode = TWE_OP_READ;
	    } else {
		tr->tr_flags |= TWE_CMD_DATAOUT;
		cmd->io.opcode = TWE_OP_WRITE;
	    }
	
	    /* build a suitable I/O command (assumes 512-byte rounded transfers) */
	    cmd->io.size = 3;
	    cmd->io.unit = *(int *)(bp->bio_driver1);
	    cmd->io.block_count = (tr->tr_length + TWE_BLOCK_SIZE - 1) / TWE_BLOCK_SIZE;
	    cmd->io.lba = bp->bio_pblkno;
	}
	
	/* did we find something to do? */
	if (tr == NULL)
	    break;
	
	/* try to map and submit the command to controller */
	error = twe_map_request(tr);

	if (error != 0) {
	    if (error == EBUSY)
		break;
	    tr->tr_status = TWE_CMD_ERROR;
	    if (tr->tr_private != NULL) {
		bp = (struct bio *)(tr->tr_private);
		bp->bio_error = error;
		bp->bio_flags |= BIO_ERROR;
		tr->tr_private = NULL;
		twed_intr(bp);
	        twe_release_request(tr);
	    } else if (tr->tr_flags & TWE_CMD_SLEEPER)
		wakeup_one(tr); /* wakeup the sleeping owner */
	}
    }
}

/********************************************************************************
 * Write blocks from memory to disk, for system crash dumps.
 */
int
twe_dump_blocks(struct twe_softc *sc, int unit,	u_int32_t lba, void *data, int nblks)
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    int			error;

    if (twe_get_request(sc, &tr))
	return(ENOMEM);

    tr->tr_data = data;
    tr->tr_status = TWE_CMD_SETUP;
    tr->tr_length = nblks * TWE_BLOCK_SIZE;
    tr->tr_flags = TWE_CMD_DATAOUT;

    cmd = TWE_FIND_COMMAND(tr);
    cmd->io.opcode = TWE_OP_WRITE;
    cmd->io.size = 3;
    cmd->io.unit = unit;
    cmd->io.block_count = nblks;
    cmd->io.lba = lba;

    error = twe_immediate_request(tr, 0);
    if (error == 0)
	if (twe_report_request(tr))
	    error = EIO;
    twe_release_request(tr);
    return(error);
}

/********************************************************************************
 * Handle controller-specific control operations.
 */
int
twe_ioctl(struct twe_softc *sc, u_long ioctlcmd, void *addr)
{
    struct twe_usercommand	*tu = (struct twe_usercommand *)addr;
    struct twe_paramcommand	*tp = (struct twe_paramcommand *)addr;
    struct twe_drivecommand	*td = (struct twe_drivecommand *)addr;
    union twe_statrequest	*ts = (union twe_statrequest *)addr;
    TWE_Param			*param;
    TWE_Command			*cmd;
    void			*data;
    u_int16_t			*aen_code = (u_int16_t *)addr;
    struct twe_request		*tr;
    u_int8_t			srid;
    int				error;
    size_t			tr_length;

    error = 0;
    switch(ioctlcmd) {
	/* handle a command from userspace */
    case TWEIO_COMMAND:
	/*
	 * if there's a data buffer, allocate and copy it in.
	 * Must be in multiplied of 512 bytes.
	 */
	tr_length = roundup2(tu->tu_size, 512);
	if (tr_length > 0) {
	    data = malloc(tr_length, M_DEVBUF, M_WAITOK);
	    error = copyin(tu->tu_data, data, tu->tu_size);
	    if (error) {
		free(data, M_DEVBUF);
		break;
	    }
	} else
	    data = NULL;

	/* get a request */
	TWE_IO_LOCK(sc);
	while (twe_get_request(sc, &tr))
	    mtx_sleep(sc, &sc->twe_io_lock, PPAUSE, "twioctl", hz);

	/*
	 * Save the command's request ID, copy the user-supplied command in,
	 * restore the request ID.
	 */
	cmd = TWE_FIND_COMMAND(tr);
	srid = cmd->generic.request_id;
	bcopy(&tu->tu_command, cmd, sizeof(TWE_Command));
	cmd->generic.request_id = srid;

	tr->tr_length = tr_length;
	tr->tr_data = data;
	if (tr->tr_length > 0) {
	    tr->tr_flags |= TWE_CMD_DATAIN | TWE_CMD_DATAOUT;
	}

	/* run the command */
	error = twe_wait_request(tr);
	TWE_IO_UNLOCK(sc);
	if (error)
	    goto cmd_done;

	/* copy the command out again */
	bcopy(cmd, &tu->tu_command, sizeof(TWE_Command));
	
	/* if there was a data buffer, copy it out */
	if (tr->tr_length > 0)
	    error = copyout(tr->tr_data, tu->tu_data, tu->tu_size);

    cmd_done:
	/* free resources */
	if (tr->tr_data != NULL)
	    free(tr->tr_data, M_DEVBUF);
	TWE_IO_LOCK(sc);
	twe_release_request(tr);
	TWE_IO_UNLOCK(sc);

	break;

	/* fetch statistics counter */
    case TWEIO_STATS:
	switch (ts->ts_item) {
#ifdef TWE_PERFORMANCE_MONITOR
	case TWEQ_FREE:
	case TWEQ_BIO:
	case TWEQ_READY:
	case TWEQ_BUSY:
	case TWEQ_COMPLETE:
	    TWE_IO_LOCK(sc);
	    bcopy(&sc->twe_qstat[ts->ts_item], &ts->ts_qstat, sizeof(struct twe_qstat));
	    TWE_IO_UNLOCK(sc);
	    break;
#endif
	default:
	    error = ENOENT;
	    break;
	}
	break;

	/* poll for an AEN */
    case TWEIO_AEN_POLL:
	TWE_IO_LOCK(sc);
	*aen_code = twe_dequeue_aen(sc);
	TWE_IO_UNLOCK(sc);
	break;

	/* wait for another AEN to show up */
    case TWEIO_AEN_WAIT:
	TWE_IO_LOCK(sc);
	while ((*aen_code = twe_dequeue_aen(sc)) == TWE_AEN_QUEUE_EMPTY) {
	    error = mtx_sleep(&sc->twe_aen_queue, &sc->twe_io_lock, PRIBIO | PCATCH,
		"tweaen", 0);
	    if (error == EINTR)
		break;
	}
	TWE_IO_UNLOCK(sc);
	break;

    case TWEIO_GET_PARAM:
	TWE_IO_LOCK(sc);
	param = twe_get_param(sc, tp->tp_table_id, tp->tp_param_id, tp->tp_size, NULL);
	TWE_IO_UNLOCK(sc);
	if (param == NULL) {
	    twe_printf(sc, "TWEIO_GET_PARAM failed for 0x%x/0x%x/%d\n", 
		       tp->tp_table_id, tp->tp_param_id, tp->tp_size);
	    error = EINVAL;
	} else {
	    if (param->parameter_size_bytes > tp->tp_size) {
		twe_printf(sc, "TWEIO_GET_PARAM parameter too large (%d > %d)\n",	
			   param->parameter_size_bytes, tp->tp_size);
		error = EFAULT;
	    } else {
		error = copyout(param->data, tp->tp_data, param->parameter_size_bytes);
	    }
	    free(param, M_DEVBUF);
	}
	break;

    case TWEIO_SET_PARAM:
	data = malloc(tp->tp_size, M_DEVBUF, M_WAITOK);
	error = copyin(tp->tp_data, data, tp->tp_size);
	if (error == 0) {
	    TWE_IO_LOCK(sc);
	    error = twe_set_param(sc, tp->tp_table_id, tp->tp_param_id, tp->tp_size, data);
	    TWE_IO_UNLOCK(sc);
	}
	free(data, M_DEVBUF);
	break;

    case TWEIO_RESET:
	TWE_IO_LOCK(sc);
	twe_reset(sc);
	TWE_IO_UNLOCK(sc);
	break;

    case TWEIO_ADD_UNIT:
	TWE_CONFIG_LOCK(sc);
	error = twe_add_unit(sc, td->td_unit);
	TWE_CONFIG_UNLOCK(sc);
	break;

    case TWEIO_DEL_UNIT:
	TWE_CONFIG_LOCK(sc);
	error = twe_del_unit(sc, td->td_unit);
	TWE_CONFIG_UNLOCK(sc);
	break;

	/* XXX implement ATA PASSTHROUGH */

	/* nothing we understand */
    default:	
	error = ENOTTY;
    }

    return(error);
}

/********************************************************************************
 * Enable the useful interrupts from the controller.
 */
void
twe_enable_interrupts(struct twe_softc *sc)
{
    sc->twe_state |= TWE_STATE_INTEN;
    TWE_CONTROL(sc, 
	       TWE_CONTROL_CLEAR_ATTENTION_INTERRUPT |
	       TWE_CONTROL_UNMASK_RESPONSE_INTERRUPT |
	       TWE_CONTROL_ENABLE_INTERRUPTS);
}

/********************************************************************************
 * Disable interrupts from the controller.
 */
void
twe_disable_interrupts(struct twe_softc *sc)
{
    TWE_CONTROL(sc, TWE_CONTROL_DISABLE_INTERRUPTS);
    sc->twe_state &= ~TWE_STATE_INTEN;
}

/********************************************************************************
 ********************************************************************************
                                                               Command Submission
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Read integer parameter table entries.
 */
static int
twe_get_param_1(struct twe_softc *sc, int table_id, int param_id, u_int8_t *result)
{
    TWE_Param	*param;

    if ((param = twe_get_param(sc, table_id, param_id, 1, NULL)) == NULL)
	return(ENOENT);
    *result = *(u_int8_t *)param->data;
    free(param, M_DEVBUF);
    return(0);
}

static int
twe_get_param_2(struct twe_softc *sc, int table_id, int param_id, u_int16_t *result)
{
    TWE_Param	*param;

    if ((param = twe_get_param(sc, table_id, param_id, 2, NULL)) == NULL)
	return(ENOENT);
    *result = *(u_int16_t *)param->data;
    free(param, M_DEVBUF);
    return(0);
}

static int
twe_get_param_4(struct twe_softc *sc, int table_id, int param_id, u_int32_t *result)
{
    TWE_Param	*param;

    if ((param = twe_get_param(sc, table_id, param_id, 4, NULL)) == NULL)
	return(ENOENT);
    *result = *(u_int32_t *)param->data;
    free(param, M_DEVBUF);
    return(0);
}

/********************************************************************************
 * Perform a TWE_OP_GET_PARAM command.  If a callback function is provided, it
 * will be called with the command when it's completed.  If no callback is 
 * provided, we will wait for the command to complete and then return just the data.
 * The caller is responsible for freeing the data when done with it.
 */
static void *
twe_get_param(struct twe_softc *sc, int table_id, int param_id, size_t param_size, 
	      void (* func)(struct twe_request *tr))
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    TWE_Param		*param;
    int			error;

    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);
    tr = NULL;
    param = NULL;

    /* get a command */
    if (twe_get_request(sc, &tr))
	goto err;

    /* get a buffer */
    if ((param = (TWE_Param *)malloc(TWE_SECTOR_SIZE, M_DEVBUF, M_NOWAIT)) == NULL)
	goto err;
    tr->tr_data = param;
    tr->tr_length = TWE_SECTOR_SIZE;
    tr->tr_flags = TWE_CMD_DATAIN | TWE_CMD_DATAOUT;

    /* build the command for the controller */
    cmd = TWE_FIND_COMMAND(tr);
    cmd->param.opcode = TWE_OP_GET_PARAM;
    cmd->param.size = 2;
    cmd->param.unit = 0;
    cmd->param.param_count = 1;

    /* fill in the outbound parameter data */
    param->table_id = table_id;
    param->parameter_id = param_id;
    param->parameter_size_bytes = param_size;

    /* submit the command and either wait or let the callback handle it */
    if (func == NULL) {
	/* XXX could use twe_wait_request here if interrupts were enabled? */
	error = twe_immediate_request(tr, 1 /* usetmp */);
	if (error == 0) {
	    if (twe_report_request(tr))
		goto err;
	} else {
	    goto err;
	}
	twe_release_request(tr);
	return(param);
    } else {
	tr->tr_complete = func;
	error = twe_map_request(tr);
	if ((error == 0) || (error == EBUSY))
	    return(func);
    }

    /* something failed */
err:
    debug(1, "failed");
    if (tr != NULL)
	twe_release_request(tr);
    if (param != NULL)
	free(param, M_DEVBUF);
    return(NULL);
}

/********************************************************************************
 * Set integer parameter table entries.
 */
#ifdef TWE_SHUTDOWN_NOTIFICATION
static int
twe_set_param_1(struct twe_softc *sc, int table_id, int param_id, u_int8_t value)
{
    return(twe_set_param(sc, table_id, param_id, sizeof(value), &value));
}
#endif

#if 0
static int
twe_set_param_2(struct twe_softc *sc, int table_id, int param_id, u_int16_t value)
{
    return(twe_set_param(sc, table_id, param_id, sizeof(value), &value));
}

static int
twe_set_param_4(struct twe_softc *sc, int table_id, int param_id, u_int32_t value)
{
    return(twe_set_param(sc, table_id, param_id, sizeof(value), &value));
}
#endif

/********************************************************************************
 * Perform a TWE_OP_SET_PARAM command, returns nonzero on error.
 */
static int
twe_set_param(struct twe_softc *sc, int table_id, int param_id, int param_size, void *data)
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    TWE_Param		*param;
    int			error;

    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);
    tr = NULL;
    param = NULL;
    error = ENOMEM;

    /* get a command */
    if (twe_get_request(sc, &tr))
	goto out;

    /* get a buffer */
    if ((param = (TWE_Param *)malloc(TWE_SECTOR_SIZE, M_DEVBUF, M_NOWAIT)) == NULL)
	goto out;
    tr->tr_data = param;
    tr->tr_length = TWE_SECTOR_SIZE;
    tr->tr_flags = TWE_CMD_DATAIN | TWE_CMD_DATAOUT;

    /* build the command for the controller */
    cmd = TWE_FIND_COMMAND(tr);
    cmd->param.opcode = TWE_OP_SET_PARAM;
    cmd->param.size = 2;
    cmd->param.unit = 0;
    cmd->param.param_count = 1;

    /* fill in the outbound parameter data */
    param->table_id = table_id;
    param->parameter_id = param_id;
    param->parameter_size_bytes = param_size;
    bcopy(data, param->data, param_size);

    /* XXX could use twe_wait_request here if interrupts were enabled? */
    error = twe_immediate_request(tr, 1 /* usetmp */);
    if (error == 0) {
	if (twe_report_request(tr))
	    error = EIO;
    }

out:
    if (tr != NULL)
	twe_release_request(tr);
    if (param != NULL)
	free(param, M_DEVBUF);
    return(error);
}

/********************************************************************************
 * Perform a TWE_OP_INIT_CONNECTION command, returns nonzero on error.
 *
 * Typically called with interrupts disabled.
 */
static int
twe_init_connection(struct twe_softc *sc, int mode)
{
    struct twe_request	*tr;
    TWE_Command		*cmd;
    int			error;
    
    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);

    /* get a command */
    if (twe_get_request(sc, &tr))
	return(0);

    /* build the command */
    cmd = TWE_FIND_COMMAND(tr);
    cmd->initconnection.opcode = TWE_OP_INIT_CONNECTION;
    cmd->initconnection.size = 3;
    cmd->initconnection.host_id = 0;
    cmd->initconnection.message_credits = mode;
    cmd->initconnection.response_queue_pointer = 0;

    /* submit the command */
    error = twe_immediate_request(tr, 0 /* usetmp */);
    twe_release_request(tr);

    if (mode == TWE_INIT_MESSAGE_CREDITS)
	sc->twe_host_id = cmd->initconnection.host_id;
    return(error);
}

/********************************************************************************
 * Start the command (tr) and sleep waiting for it to complete.
 *
 * Successfully completed commands are dequeued.
 */
static int
twe_wait_request(struct twe_request *tr)
{

    debug_called(4);

    TWE_IO_ASSERT_LOCKED(tr->tr_sc);
    tr->tr_flags |= TWE_CMD_SLEEPER;
    tr->tr_status = TWE_CMD_BUSY;
    twe_enqueue_ready(tr);
    twe_startio(tr->tr_sc);
    while (tr->tr_status == TWE_CMD_BUSY)
	mtx_sleep(tr, &tr->tr_sc->twe_io_lock, PRIBIO, "twewait", 0);
    
    return(tr->tr_status != TWE_CMD_COMPLETE);
}

/********************************************************************************
 * Start the command (tr) and busy-wait for it to complete.
 * This should only be used when interrupts are actually disabled (although it
 * will work if they are not).
 */
static int
twe_immediate_request(struct twe_request *tr, int usetmp)
{
    struct twe_softc *sc;
    int		error;
    int		count = 0;

    debug_called(4);

    sc = tr->tr_sc;

    if (usetmp && (tr->tr_data != NULL)) {
	tr->tr_flags |= TWE_CMD_IMMEDIATE;
	if (tr->tr_length > DFLTPHYS)
	    return (EINVAL);
	bcopy(tr->tr_data, sc->twe_immediate, tr->tr_length);
    }
    tr->tr_status = TWE_CMD_BUSY;
    if ((error = twe_map_request(tr)) != 0)
	if (error != EBUSY)
	    return(error);

    /* Wait up to 5 seconds for the command to complete */
    while ((count++ < 5000) && (tr->tr_status == TWE_CMD_BUSY)){
	DELAY(1000);
	twe_done(sc, 1);
    }
    if (usetmp && (tr->tr_data != NULL))
	bcopy(sc->twe_immediate, tr->tr_data, tr->tr_length);

    return(tr->tr_status != TWE_CMD_COMPLETE);
}

/********************************************************************************
 * Handle completion of an I/O command.
 */
static void
twe_completeio(struct twe_request *tr)
{
    TWE_Command		*cmd = TWE_FIND_COMMAND(tr);
    struct twe_softc	*sc = tr->tr_sc;
    struct bio		*bp = tr->tr_private;

    debug_called(4);

    if (tr->tr_status == TWE_CMD_COMPLETE) {

	if (cmd->generic.status)
	    if (twe_report_request(tr)) {
		bp->bio_error = EIO;
		bp->bio_flags |= BIO_ERROR;
	    }

    } else {
	twe_panic(sc, "twe_completeio on incomplete command");
    }
    tr->tr_private = NULL;
    twed_intr(bp);
    twe_release_request(tr);
}

/********************************************************************************
 * Reset the controller and pull all the active commands back onto the ready
 * queue.  Used to restart a controller that's exhibiting bad behaviour.
 */
static void
twe_reset(struct twe_softc *sc)
{
    struct twe_request	*tr;
    int			i;

    /*
     * Sleep for a short period to allow AENs to be signalled.
     */
    mtx_sleep(sc, &sc->twe_io_lock, PRIBIO, "twereset", hz);

    /*
     * Disable interrupts from the controller, and mask any accidental entry
     * into our interrupt handler.
     */
    twe_printf(sc, "controller reset in progress...\n");
    twe_disable_interrupts(sc);

    /*
     * Try to soft-reset the controller.
     */
    for (i = 0; i < TWE_MAX_RESET_TRIES; i++) {

	if (i > 0)
	    twe_printf(sc, "reset %d failed, trying again\n", i);
	
	if (!twe_soft_reset(sc))
	    break;			/* reset process complete */
    }
    /* did we give up? */
    if (i >= TWE_MAX_RESET_TRIES) {
	twe_printf(sc, "can't reset controller, giving up\n");
	goto out;
    }

    /*
     * Move all of the commands that were busy back to the ready queue.
     */
    i = 0;
    while ((tr = twe_dequeue_busy(sc)) != NULL) {
	twe_enqueue_ready(tr);
	i++;
    }

    /*
     * Kick the controller to start things going again, then re-enable interrupts.
     */
    twe_startio(sc);
    twe_printf(sc, "controller reset done, %d commands restarted\n", i);

out:
    twe_enable_interrupts(sc);
}

/********************************************************************************
 ********************************************************************************
                                                        Command I/O to Controller
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Try to deliver (tr) to the controller.
 *
 * Can be called at any interrupt level, with or without interrupts enabled.
 */
int
twe_start(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;
    TWE_Command		*cmd;
    int			i;
    u_int32_t		status_reg;

    debug_called(4);

    if (!dumping)
	TWE_IO_ASSERT_LOCKED(sc);

    /* mark the command as currently being processed */
    tr->tr_status = TWE_CMD_BUSY;
    cmd = TWE_FIND_COMMAND(tr);

    /* 
     * Spin briefly waiting for the controller to come ready 
     *
     * XXX it might be more efficient to return EBUSY immediately
     *     and let the command be rescheduled.
     */
    for (i = 100000; (i > 0); i--) {
	
	/* check to see if we can post a command */
	status_reg = TWE_STATUS(sc);
	twe_check_bits(sc, status_reg);

	if (!(status_reg & TWE_STATUS_COMMAND_QUEUE_FULL)) {
	    twe_enqueue_busy(tr);

	    TWE_COMMAND_QUEUE(sc, TWE_FIND_COMMANDPHYS(tr));

	    /* move command to work queue */
#ifdef TWE_DEBUG
	    if (tr->tr_complete != NULL) {
		debug(3, "queued request %d with callback %p", cmd->generic.request_id, tr->tr_complete);
	    } else if (tr->tr_flags & TWE_CMD_SLEEPER) {
		debug(3, "queued request %d with wait channel %p", cmd->generic.request_id, tr);
	    } else {
		debug(3, "queued request %d for polling caller", cmd->generic.request_id);
	    }
#endif
	    return(0);
	} else if (!(status_reg & TWE_STATUS_RESPONSE_QUEUE_EMPTY) && i > 1)
	    twe_done(sc, 0);
    }

    /* 
     * We couldn't get the controller to take the command; try submitting it again later.
     * This should only happen if something is wrong with the controller, or if we have
     * overestimated the number of commands it can accept.  (Should we actually reject
     * the command at this point?)
     */
    return(EBUSY);
}

/********************************************************************************
 * Poll the controller (sc) for completed commands.
 *
 * Can be called at any interrupt level, with or without interrupts enabled.
 */
static void
twe_done(struct twe_softc *sc, int startio)
{
    TWE_Response_Queue	rq;
    TWE_Command		*cmd;
    struct twe_request	*tr;
    int			found;
    u_int32_t		status_reg;
    
    debug_called(5);

    /* loop collecting completed commands */
    found = 0;
    for (;;) {
	status_reg = TWE_STATUS(sc);
	twe_check_bits(sc, status_reg);		/* XXX should this fail? */

	if (!(status_reg & TWE_STATUS_RESPONSE_QUEUE_EMPTY)) {
	    found = 1;
	    rq = TWE_RESPONSE_QUEUE(sc);
	    tr = sc->twe_lookup[rq.u.response_id];	/* find command */
	    cmd = TWE_FIND_COMMAND(tr);
	    if (tr->tr_status != TWE_CMD_BUSY)
		twe_printf(sc, "completion event for nonbusy command\n");
	    tr->tr_status = TWE_CMD_COMPLETE;
	    debug(3, "completed request id %d with status %d", 
		  cmd->generic.request_id, cmd->generic.status);
	    /* move to completed queue */
	    twe_remove_busy(tr);
	    twe_enqueue_complete(tr);
	    sc->twe_state &= ~TWE_STATE_CTLR_BUSY;
	} else {
	    break;					/* no response ready */
	}
    }

    /* if we've completed any commands, try posting some more */
    if (found && startio)
	twe_startio(sc);

    /* handle completion and timeouts */
    twe_complete(sc);		/* XXX use deferred completion? */
}

/********************************************************************************
 * Perform post-completion processing for commands on (sc).
 *
 * This is split from twe_done as it can be safely deferred and run at a lower
 * priority level should facilities for such a thing become available.
 */
static void
twe_complete(struct twe_softc *sc) 
{
    struct twe_request	*tr;
    
    debug_called(5);

    /*
     * Pull commands off the completed list, dispatch them appropriately
     */
    while ((tr = twe_dequeue_complete(sc)) != NULL) {
	/* unmap the command's data buffer */
	twe_unmap_request(tr);

	/* dispatch to suit command originator */
	if (tr->tr_complete != NULL) {		/* completion callback */
	    debug(2, "call completion handler %p", tr->tr_complete);
	    tr->tr_complete(tr);

	} else if (tr->tr_flags & TWE_CMD_SLEEPER) {	/* caller is asleep waiting */
	    debug(2, "wake up command owner on %p", tr);
	    wakeup_one(tr);

	} else {					/* caller is polling command */
	    debug(2, "command left for owner");
	}
    }   
}

/********************************************************************************
 * Wait for (status) to be set in the controller status register for up to
 * (timeout) seconds.  Returns 0 if found, nonzero if we time out.
 *
 * Note: this busy-waits, rather than sleeping, since we may be called with
 * eg. clock interrupts masked.
 */
static int
twe_wait_status(struct twe_softc *sc, u_int32_t status, int timeout)
{
    time_t	expiry;
    u_int32_t	status_reg;

    debug_called(4);

    expiry = time_second + timeout;

    do {
	status_reg = TWE_STATUS(sc);
	if (status_reg & status)	/* got the required bit(s)? */
	    return(0);
	DELAY(100000);
    } while (time_second <= expiry);

    return(1);
}

/********************************************************************************
 * Drain the response queue, which may contain responses to commands we know
 * nothing about.
 */
static int
twe_drain_response_queue(struct twe_softc *sc)
{
    TWE_Response_Queue	rq;
    u_int32_t		status_reg;

    debug_called(4);

    for (;;) {				/* XXX give up eventually? */
	status_reg = TWE_STATUS(sc);
	if (twe_check_bits(sc, status_reg))
	    return(1);
	if (status_reg & TWE_STATUS_RESPONSE_QUEUE_EMPTY)
	    return(0);
	rq = TWE_RESPONSE_QUEUE(sc);
    }
}

/********************************************************************************
 * Soft-reset the controller
 */
static int
twe_soft_reset(struct twe_softc *sc)
{
    u_int32_t		status_reg;

    debug_called(2);

    TWE_IO_ASSERT_LOCKED(sc);
    TWE_SOFT_RESET(sc);

    if (twe_wait_status(sc, TWE_STATUS_ATTENTION_INTERRUPT, 30)) {
	twe_printf(sc, "no attention interrupt\n");
	return(1);
    }
    TWE_CONTROL(sc, TWE_CONTROL_CLEAR_ATTENTION_INTERRUPT);
    if (twe_drain_aen_queue(sc)) {
	twe_printf(sc, "can't drain AEN queue\n");
	return(1);
    }
    if (twe_find_aen(sc, TWE_AEN_SOFT_RESET)) {
	twe_printf(sc, "reset not reported\n");
	return(1);
    }
    status_reg = TWE_STATUS(sc);
    if (TWE_STATUS_ERRORS(status_reg) || twe_check_bits(sc, status_reg)) {
	twe_printf(sc, "controller errors detected\n");
	return(1);
    }
    if (twe_drain_response_queue(sc)) {
	twe_printf(sc, "can't drain response queue\n");
	return(1);
    }
    return(0);
}

/********************************************************************************
 ********************************************************************************
                                                               Interrupt Handling
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Host interrupt.
 *
 * XXX what does this mean?
 */
static void
twe_host_intr(struct twe_softc *sc)
{
    debug_called(4);

    twe_printf(sc, "host interrupt\n");
    TWE_CONTROL(sc, TWE_CONTROL_CLEAR_HOST_INTERRUPT);
}

/********************************************************************************
 * Attention interrupt.
 *
 * Signalled when the controller has one or more AENs for us.
 */
static void
twe_attention_intr(struct twe_softc *sc)
{
    debug_called(4);

    /* instigate a poll for AENs */
    if (twe_fetch_aen(sc)) {
	twe_printf(sc, "error polling for signalled AEN\n");
    } else {
	TWE_CONTROL(sc, TWE_CONTROL_CLEAR_ATTENTION_INTERRUPT);
    }
}

/********************************************************************************
 * Command interrupt.
 *
 * Signalled when the controller can handle more commands.
 */
static void
twe_command_intr(struct twe_softc *sc)
{
    debug_called(4);

    /*
     * We don't use this, rather we try to submit commands when we receive
     * them, and when other commands have completed.  Mask it so we don't get
     * another one.
     */
    TWE_CONTROL(sc, TWE_CONTROL_MASK_COMMAND_INTERRUPT);
}

/********************************************************************************
 ********************************************************************************
                                                      Asynchronous Event Handling
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Request an AEN from the controller.
 */
static int
twe_fetch_aen(struct twe_softc *sc)
{

    debug_called(4);

    if ((twe_get_param(sc, TWE_PARAM_AEN, TWE_PARAM_AEN_UnitCode, 2, twe_handle_aen)) == NULL)
	return(EIO);
    return(0);
}

/********************************************************************************
 * Handle an AEN returned by the controller.
 */
static void
twe_handle_aen(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;
    TWE_Param		*param;
    u_int16_t		aen;

    debug_called(4);

    /* XXX check for command success somehow? */

    param = (TWE_Param *)tr->tr_data;
    aen = *(u_int16_t *)(param->data);

    free(tr->tr_data, M_DEVBUF);
    twe_release_request(tr);
    twe_enqueue_aen(sc, aen);

    /* XXX poll for more AENs? */
}

/********************************************************************************
 * Pull AENs out of the controller and park them in the queue, in a context where
 * interrupts aren't active.  Return nonzero if we encounter any errors in the
 * process of obtaining all the available AENs.
 */
static int
twe_drain_aen_queue(struct twe_softc *sc)
{
    u_int16_t	aen;

    TWE_IO_ASSERT_LOCKED(sc);
    for (;;) {
	if (twe_get_param_2(sc, TWE_PARAM_AEN, TWE_PARAM_AEN_UnitCode, &aen))
	    return(1);
	if (aen == TWE_AEN_QUEUE_EMPTY)
	    return(0);
	twe_enqueue_aen(sc, aen);
    }
}

/********************************************************************************
 * Push an AEN that we've received onto the queue.
 *
 * Note that we have to lock this against reentrance, since it may be called
 * from both interrupt and non-interrupt context.
 *
 * If someone is waiting for the AEN we have, wake them up.
 */
static void
twe_enqueue_aen(struct twe_softc *sc, u_int16_t aen)
{
    char	*msg;
    int		next, nextnext;

    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);
    if ((msg = twe_format_aen(sc, aen)) != NULL)
	twe_printf(sc, "AEN: <%s>\n", msg);

    /* enqueue the AEN */
    next = ((sc->twe_aen_head + 1) % TWE_Q_LENGTH);
    nextnext = ((sc->twe_aen_head + 2) % TWE_Q_LENGTH);
    
    /* check to see if this is the last free slot, and subvert the AEN if it is */
    if (nextnext == sc->twe_aen_tail)
	aen = TWE_AEN_QUEUE_FULL;

    /* look to see if there's room for this AEN */
    if (next != sc->twe_aen_tail) {
	sc->twe_aen_queue[sc->twe_aen_head] = aen;
	sc->twe_aen_head = next;
    }

    /* wake up anyone asleep on the queue */
    wakeup(&sc->twe_aen_queue);

    /* anyone looking for this AEN? */
    if (sc->twe_wait_aen == aen) {
	sc->twe_wait_aen = -1;
	wakeup(&sc->twe_wait_aen);
    }
}

/********************************************************************************
 * Pop an AEN off the queue, or return -1 if there are none left.
 *
 * We are more or less interrupt-safe, so don't block interrupts.
 */
static u_int16_t
twe_dequeue_aen(struct twe_softc *sc)
{
    u_int16_t	result;
    
    debug_called(4);

    TWE_IO_ASSERT_LOCKED(sc);
    if (sc->twe_aen_tail == sc->twe_aen_head) {
	result = TWE_AEN_QUEUE_EMPTY;
    } else {
	result = sc->twe_aen_queue[sc->twe_aen_tail];
	sc->twe_aen_tail = ((sc->twe_aen_tail + 1) % TWE_Q_LENGTH);
    }
    return(result);
}

/********************************************************************************
 * Check to see if the requested AEN is in the queue.
 *
 * XXX we could probably avoid masking interrupts here
 */
static int
twe_find_aen(struct twe_softc *sc, u_int16_t aen)
{
    int		i, missing;

    missing = 1;
    for (i = sc->twe_aen_tail; (i != sc->twe_aen_head) && missing; i = (i + 1) % TWE_Q_LENGTH) {
	if (sc->twe_aen_queue[i] == aen)
	    missing = 0;
    }
    return(missing);
}


#if 0	/* currently unused */
/********************************************************************************
 * Sleep waiting for at least (timeout) seconds until we see (aen) as 
 * requested.  Returns nonzero on timeout or failure.
 *
 * XXX: this should not be used in cases where there may be more than one sleeper
 *      without a mechanism for registering multiple sleepers.
 */
static int
twe_wait_aen(struct twe_softc *sc, int aen, int timeout)
{
    time_t	expiry;
    int		found;

    debug_called(4);

    expiry = time_second + timeout;
    found = 0;

    sc->twe_wait_aen = aen;
    do {
	twe_fetch_aen(sc);
	mtx_sleep(&sc->twe_wait_aen, &sc->twe_io_lock, PZERO, "twewaen", hz);
	if (sc->twe_wait_aen == -1)
	    found = 1;
    } while ((time_second <= expiry) && !found);
    return(!found);
}
#endif

/********************************************************************************
 ********************************************************************************
                                                        Command Buffer Management
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Get a new command buffer.
 *
 * This will return NULL if all command buffers are in use.
 */
static int
twe_get_request(struct twe_softc *sc, struct twe_request **tr)
{
    TWE_Command		*cmd;
    debug_called(4);

    if (!dumping)
	TWE_IO_ASSERT_LOCKED(sc);

    /* try to reuse an old buffer */
    *tr = twe_dequeue_free(sc);

    /* initialise some fields to their defaults */
    if (*tr != NULL) {
	cmd = TWE_FIND_COMMAND(*tr);
	(*tr)->tr_data = NULL;
	(*tr)->tr_private = NULL;
	(*tr)->tr_status = TWE_CMD_SETUP;		/* command is in setup phase */
	(*tr)->tr_flags = 0;
	(*tr)->tr_complete = NULL;
	cmd->generic.status = 0;			/* before submission to controller */
	cmd->generic.flags = 0;				/* not used */
    }
    return(*tr == NULL);
}

/********************************************************************************
 * Release a command buffer for reuse.
 *
 */
static void
twe_release_request(struct twe_request *tr)
{
    debug_called(4);

    if (!dumping)
	TWE_IO_ASSERT_LOCKED(tr->tr_sc);
    if (tr->tr_private != NULL)
	twe_panic(tr->tr_sc, "tr_private != NULL");
    twe_enqueue_free(tr);
}

/********************************************************************************
 ********************************************************************************
                                                                        Debugging
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Print some information about the controller
 */
void
twe_describe_controller(struct twe_softc *sc)
{
    TWE_Param		*p[6];
    u_int8_t		ports;
    u_int32_t		size;
    int			i;

    debug_called(2);

    TWE_IO_LOCK(sc);

    /* get the port count */
    twe_get_param_1(sc, TWE_PARAM_CONTROLLER, TWE_PARAM_CONTROLLER_PortCount, &ports);

    /* get version strings */
    p[0] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_FW,   16, NULL);
    p[1] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_BIOS, 16, NULL);
    if (p[0] && p[1])
	 twe_printf(sc, "%d ports, Firmware %.16s, BIOS %.16s\n", ports, p[0]->data, p[1]->data);

    if (bootverbose) {
	p[2] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_Mon,  16, NULL);
	p[3] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_PCB,  8, NULL);
	p[4] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_ATA,  8, NULL);
	p[5] = twe_get_param(sc, TWE_PARAM_VERSION, TWE_PARAM_VERSION_PCI,  8, NULL);

	if (p[2] && p[3] && p[4] && p[5])
	    twe_printf(sc, "Monitor %.16s, PCB %.8s, Achip %.8s, Pchip %.8s\n", p[2]->data, p[3]->data,
		p[4]->data, p[5]->data);
	if (p[2])
	    free(p[2], M_DEVBUF);
	if (p[3])
	    free(p[3], M_DEVBUF);
	if (p[4])
	    free(p[4], M_DEVBUF);
	if (p[5])
	    free(p[5], M_DEVBUF);
    }
    if (p[0])
	free(p[0], M_DEVBUF);
    if (p[1])
	free(p[1], M_DEVBUF);

    /* print attached drives */
    if (bootverbose) {
	p[0] = twe_get_param(sc, TWE_PARAM_DRIVESUMMARY, TWE_PARAM_DRIVESUMMARY_Status, 16, NULL);
	for (i = 0; i < ports; i++) {
	    if (p[0]->data[i] != TWE_PARAM_DRIVESTATUS_Present)
		continue;
	    twe_get_param_4(sc, TWE_PARAM_DRIVEINFO + i, TWE_PARAM_DRIVEINFO_Size, &size);
	    p[1] = twe_get_param(sc, TWE_PARAM_DRIVEINFO + i, TWE_PARAM_DRIVEINFO_Model, 40, NULL);
	    if (p[1] != NULL) {
		twe_printf(sc, "port %d: %.40s %dMB\n", i, p[1]->data, size / 2048);
		free(p[1], M_DEVBUF);
	    } else {
		twe_printf(sc, "port %d, drive status unavailable\n", i);
	    }
	}
	if (p[0])
	    free(p[0], M_DEVBUF);
    }
    TWE_IO_UNLOCK(sc);
}

/********************************************************************************
 * Look up a text description of a numeric code and return a pointer to same.
 */
char *
twe_describe_code(struct twe_code_lookup *table, u_int32_t code)
{
    int         i;

    for (i = 0; table[i].string != NULL; i++)
	if (table[i].code == code)
	    return(table[i].string);
    return(table[i+1].string);
}

/********************************************************************************
 * Complain if the status bits aren't what we're expecting.
 *
 * Rate-limit the complaints to at most one of each every five seconds, but
 * always return the correct status.
 */
static int
twe_check_bits(struct twe_softc *sc, u_int32_t status_reg)
{
    int			result;
    static time_t	lastwarn[2] = {0, 0};

    /*
     * This can be a little problematic, as twe_panic may call twe_reset if 
     * TWE_DEBUG is not set, which will call us again as part of the soft reset.
     */
    if ((status_reg & TWE_STATUS_PANIC_BITS) != 0) {
	twe_printf(sc, "FATAL STATUS BIT(S) %b\n", status_reg & TWE_STATUS_PANIC_BITS,
		   TWE_STATUS_BITS_DESCRIPTION);
	twe_panic(sc, "fatal status bits");
    }

    result = 0;
    if ((status_reg & TWE_STATUS_EXPECTED_BITS) != TWE_STATUS_EXPECTED_BITS) {
	if (time_second > (lastwarn[0] + 5)) {
	    twe_printf(sc, "missing expected status bit(s) %b\n", ~status_reg & TWE_STATUS_EXPECTED_BITS, 
		       TWE_STATUS_BITS_DESCRIPTION);
	    lastwarn[0] = time_second;
	}
	result = 1;
    }

    if ((status_reg & TWE_STATUS_UNEXPECTED_BITS) != 0) {
	if (time_second > (lastwarn[1] + 5)) {
	    twe_printf(sc, "unexpected status bit(s) %b\n", status_reg & TWE_STATUS_UNEXPECTED_BITS, 
		       TWE_STATUS_BITS_DESCRIPTION);
	    lastwarn[1] = time_second;
	}
	result = 1;
	if (status_reg & TWE_STATUS_PCI_PARITY_ERROR) {
	    twe_printf(sc, "PCI parity error: Reseat card, move card or buggy device present.\n");
	    twe_clear_pci_parity_error(sc);
	}
	if (status_reg & TWE_STATUS_PCI_ABORT) {
	    twe_printf(sc, "PCI abort, clearing.\n");
	    twe_clear_pci_abort(sc);
	}
    }

    return(result);
}	

/********************************************************************************
 * Return a string describing (aen).
 *
 * The low 8 bits of the aen are the code, the high 8 bits give the unit number
 * where an AEN is specific to a unit.
 *
 * Note that we could expand this routine to handle eg. up/downgrading the status
 * of a drive if we had some idea of what the drive's initial status was.
 */

static char *
twe_format_aen(struct twe_softc *sc, u_int16_t aen)
{
    device_t	child;
    char	*code, *msg;

    code = twe_describe_code(twe_table_aen, TWE_AEN_CODE(aen));
    msg = code + 2;

    switch (*code) {
    case 'q':
	if (!bootverbose)
	    return(NULL);
	/* FALLTHROUGH */
    case 'a':
	return(msg);

    case 'c':
	if ((child = sc->twe_drive[TWE_AEN_UNIT(aen)].td_disk) != NULL) {
	    snprintf(sc->twe_aen_buf, sizeof(sc->twe_aen_buf), "twed%d: %s",
		device_get_unit(child), msg);
	} else {
	    snprintf(sc->twe_aen_buf, sizeof(sc->twe_aen_buf),
		"twe%d: %s for unknown unit %d", device_get_unit(sc->twe_dev),
		msg, TWE_AEN_UNIT(aen));
	}
	return(sc->twe_aen_buf);

    case 'p':
	snprintf(sc->twe_aen_buf, sizeof(sc->twe_aen_buf),
	    "twe%d: port %d: %s", device_get_unit(sc->twe_dev),
	    TWE_AEN_UNIT(aen), msg);
	return(sc->twe_aen_buf);

	
    case 'x':
    default:
	break;
    }
    snprintf(sc->twe_aen_buf, sizeof(sc->twe_aen_buf), "unknown AEN 0x%x", aen);
    return(sc->twe_aen_buf);
}

/********************************************************************************
 * Print a diagnostic if the status of the command warrants it, and return
 * either zero (command was ok) or nonzero (command failed).
 */
static int
twe_report_request(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;
    TWE_Command		*cmd = TWE_FIND_COMMAND(tr);
    int			result = 0;

    /*
     * Check the command status value and handle accordingly.
     */
    if (cmd->generic.status == TWE_STATUS_RESET) {
	/*
	 * The status code 0xff requests a controller reset.
	 */
	twe_printf(sc, "command returned with controller reset request\n");
	twe_reset(sc);
	result = 1;
    } else if (cmd->generic.status > TWE_STATUS_FATAL) {
	/*
	 * Fatal errors that don't require controller reset.
	 *
	 * We know a few special flags values.
	 */
	switch (cmd->generic.flags) {
	case 0x1b:
	    device_printf(sc->twe_drive[cmd->generic.unit].td_disk,
			  "drive timeout");
	    break;
	case 0x51:
	    device_printf(sc->twe_drive[cmd->generic.unit].td_disk,
			  "unrecoverable drive error");
	    break;
	default:
	    device_printf(sc->twe_drive[cmd->generic.unit].td_disk,
			  "controller error - %s (flags = 0x%x)\n",
			  twe_describe_code(twe_table_status, cmd->generic.status),
			  cmd->generic.flags);
	    result = 1;
	}
    } else if (cmd->generic.status > TWE_STATUS_WARNING) {
	/*
	 * Warning level status.
	 */
	device_printf(sc->twe_drive[cmd->generic.unit].td_disk,
		      "warning - %s (flags = 0x%x)\n",
		      twe_describe_code(twe_table_status, cmd->generic.status),
		      cmd->generic.flags);
    } else if (cmd->generic.status > 0x40) {
	/*
	 * Info level status.
	 */
	device_printf(sc->twe_drive[cmd->generic.unit].td_disk,
		      "attention - %s (flags = 0x%x)\n",
		      twe_describe_code(twe_table_status, cmd->generic.status),
		      cmd->generic.flags);
    }
    
    return(result);
}

/********************************************************************************
 * Print some controller state to aid in debugging error/panic conditions
 */
void
twe_print_controller(struct twe_softc *sc)
{
    u_int32_t		status_reg;

    status_reg = TWE_STATUS(sc);
    twe_printf(sc, "status   %b\n", status_reg, TWE_STATUS_BITS_DESCRIPTION);
    twe_printf(sc, "          current  max    min\n");
    twe_printf(sc, "free      %04d     %04d   %04d\n",
	sc->twe_qstat[TWEQ_FREE].q_length, sc->twe_qstat[TWEQ_FREE].q_max, sc->twe_qstat[TWEQ_FREE].q_min);

    twe_printf(sc, "ready     %04d     %04d   %04d\n",
	sc->twe_qstat[TWEQ_READY].q_length, sc->twe_qstat[TWEQ_READY].q_max, sc->twe_qstat[TWEQ_READY].q_min);

    twe_printf(sc, "busy      %04d     %04d   %04d\n",
	sc->twe_qstat[TWEQ_BUSY].q_length, sc->twe_qstat[TWEQ_BUSY].q_max, sc->twe_qstat[TWEQ_BUSY].q_min);

    twe_printf(sc, "complete  %04d     %04d   %04d\n",
	sc->twe_qstat[TWEQ_COMPLETE].q_length, sc->twe_qstat[TWEQ_COMPLETE].q_max, sc->twe_qstat[TWEQ_COMPLETE].q_min);

    twe_printf(sc, "bioq      %04d     %04d   %04d\n",
	sc->twe_qstat[TWEQ_BIO].q_length, sc->twe_qstat[TWEQ_BIO].q_max, sc->twe_qstat[TWEQ_BIO].q_min);

    twe_printf(sc, "AEN queue head %d  tail %d\n", sc->twe_aen_head, sc->twe_aen_tail);
}	

static void
twe_panic(struct twe_softc *sc, char *reason)
{
    twe_print_controller(sc);
#ifdef TWE_DEBUG
    panic(reason);
#else
    twe_reset(sc);
#endif
}

#if 0
/********************************************************************************
 * Print a request/command in human-readable format.
 */
static void
twe_print_request(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;
    TWE_Command	*cmd = TWE_FIND_COMMAND(tr);
    int		i;

    twe_printf(sc, "CMD: request_id %d  opcode <%s>  size %d  unit %d  host_id %d\n", 
	       cmd->generic.request_id, twe_describe_code(twe_table_opcode, cmd->generic.opcode), cmd->generic.size, 
	       cmd->generic.unit, cmd->generic.host_id);
    twe_printf(sc, " status %d  flags 0x%x  count %d  sgl_offset %d\n", 
	       cmd->generic.status, cmd->generic.flags, cmd->generic.count, cmd->generic.sgl_offset);

    switch(cmd->generic.opcode) {	/* XXX add more opcodes? */
    case TWE_OP_READ:
    case TWE_OP_WRITE:
	twe_printf(sc, " lba %d\n", cmd->io.lba);
	for (i = 0; (i < TWE_MAX_SGL_LENGTH) && (cmd->io.sgl[i].length != 0); i++)
	    twe_printf(sc, "  %d: 0x%x/%d\n", 
		       i, cmd->io.sgl[i].address, cmd->io.sgl[i].length);
	break;

    case TWE_OP_GET_PARAM:
    case TWE_OP_SET_PARAM:
	for (i = 0; (i < TWE_MAX_SGL_LENGTH) && (cmd->param.sgl[i].length != 0); i++)
	    twe_printf(sc, "  %d: 0x%x/%d\n", 
		       i, cmd->param.sgl[i].address, cmd->param.sgl[i].length);
	break;

    case TWE_OP_INIT_CONNECTION:
	twe_printf(sc, " response queue pointer 0x%x\n", 
		   cmd->initconnection.response_queue_pointer);
	break;

    default:
	break;
    }
    twe_printf(sc, " tr_command %p/0x%x  tr_data %p/0x%x,%d\n", 
	       tr, TWE_FIND_COMMANDPHYS(tr), tr->tr_data, tr->tr_dataphys, tr->tr_length);
    twe_printf(sc, " tr_status %d  tr_flags 0x%x  tr_complete %p  tr_private %p\n", 
	       tr->tr_status, tr->tr_flags, tr->tr_complete, tr->tr_private);
}

#endif
