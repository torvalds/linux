#ifndef _SCSI_SCSI_CMND_H
#define _SCSI_SCSI_CMND_H

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/types.h>

struct request;
struct scatterlist;
struct scsi_device;
struct scsi_request;


/* embedded in scsi_cmnd */
struct scsi_pointer {
	char *ptr;		/* data pointer */
	int this_residual;	/* left in this buffer */
	struct scatterlist *buffer;	/* which buffer */
	int buffers_residual;	/* how many buffers left */

        dma_addr_t dma_handle;

	volatile int Status;
	volatile int Message;
	volatile int have_data_in;
	volatile int sent_command;
	volatile int phase;
};

struct scsi_cmnd {
	int     sc_magic;

	struct scsi_device *device;
	unsigned short state;
	unsigned short owner;
	struct scsi_request *sc_request;

	struct list_head list;  /* scsi_cmnd participates in queue lists */

	struct list_head eh_entry; /* entry for the host eh_cmd_q */
	int eh_state;		/* Used for state tracking in error handlr */
	int eh_eflags;		/* Used by error handlr */
	void (*done) (struct scsi_cmnd *);	/* Mid-level done function */

	/*
	 * A SCSI Command is assigned a nonzero serial_number when internal_cmnd
	 * passes it to the driver's queue command function.  The serial_number
	 * is cleared when scsi_done is entered indicating that the command has
	 * been completed.  If a timeout occurs, the serial number at the moment
	 * of timeout is copied into serial_number_at_timeout.  By subsequently
	 * comparing the serial_number and serial_number_at_timeout fields
	 * during abort or reset processing, we can detect whether the command
	 * has already completed.  This also detects cases where the command has
	 * completed and the SCSI Command structure has already being reused
	 * for another command, so that we can avoid incorrectly aborting or
	 * resetting the new command.
	 * The serial number is only unique per host.
	 */
	unsigned long serial_number;
	unsigned long serial_number_at_timeout;

	int retries;
	int allowed;
	int timeout_per_command;
	int timeout_total;
	int timeout;

	/*
	 * We handle the timeout differently if it happens when a reset, 
	 * abort, etc are in process. 
	 */
	unsigned volatile char internal_timeout;

	unsigned char cmd_len;
	unsigned char old_cmd_len;
	enum dma_data_direction sc_data_direction;
	enum dma_data_direction sc_old_data_direction;

	/* These elements define the operation we are about to perform */
#define MAX_COMMAND_SIZE	16
	unsigned char cmnd[MAX_COMMAND_SIZE];
	unsigned request_bufflen;	/* Actual request size */

	struct timer_list eh_timeout;	/* Used to time out the command. */
	void *request_buffer;		/* Actual requested buffer */

	/* These elements define the operation we ultimately want to perform */
	unsigned char data_cmnd[MAX_COMMAND_SIZE];
	unsigned short old_use_sg;	/* We save  use_sg here when requesting
					 * sense info */
	unsigned short use_sg;	/* Number of pieces of scatter-gather */
	unsigned short sglist_len;	/* size of malloc'd scatter-gather list */
	unsigned short abort_reason;	/* If the mid-level code requests an
					 * abort, this is the reason. */
	unsigned bufflen;	/* Size of data buffer */
	void *buffer;		/* Data buffer */

	unsigned underflow;	/* Return error if less than
				   this amount is transferred */
	unsigned old_underflow;	/* save underflow here when reusing the
				 * command for error handling */

	unsigned transfersize;	/* How much we are guaranteed to
				   transfer with each SCSI transfer
				   (ie, between disconnect / 
				   reconnects.   Probably == sector
				   size */

	int resid;		/* Number of bytes requested to be
				   transferred less actual number
				   transferred (0 if not supported) */

	struct request *request;	/* The command we are
				   	   working on */

#define SCSI_SENSE_BUFFERSIZE 	96
	unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];		/* obtained by REQUEST SENSE
						 * when CHECK CONDITION is
						 * received on original command 
						 * (auto-sense) */

	/* Low-level done function - can be used by low-level driver to point
	 *        to completion function.  Not used by mid/upper level code. */
	void (*scsi_done) (struct scsi_cmnd *);

	/*
	 * The following fields can be written to by the host specific code. 
	 * Everything else should be left alone. 
	 */
	struct scsi_pointer SCp;	/* Scratchpad used by some host adapters */

	unsigned char *host_scribble;	/* The host adapter is allowed to
					   * call scsi_malloc and get some memory
					   * and hang it here.     The host adapter
					   * is also expected to call scsi_free
					   * to release this memory.  (The memory
					   * obtained by scsi_malloc is guaranteed
					   * to be at an address < 16Mb). */

	int result;		/* Status code from lower level driver */

	unsigned char tag;	/* SCSI-II queued command tag */
	unsigned long pid;	/* Process ID, starts at 0. Unique per host. */
};

/*
 * These are the values that scsi_cmd->state can take.
 */
#define SCSI_STATE_TIMEOUT         0x1000
#define SCSI_STATE_FINISHED        0x1001
#define SCSI_STATE_FAILED          0x1002
#define SCSI_STATE_QUEUED          0x1003
#define SCSI_STATE_UNUSED          0x1006
#define SCSI_STATE_DISCONNECTING   0x1008
#define SCSI_STATE_INITIALIZING    0x1009
#define SCSI_STATE_BHQUEUE         0x100a
#define SCSI_STATE_MLQUEUE         0x100b


extern struct scsi_cmnd *scsi_get_command(struct scsi_device *, int);
extern void scsi_put_command(struct scsi_cmnd *);
extern void scsi_io_completion(struct scsi_cmnd *, unsigned int, unsigned int);
extern void scsi_finish_command(struct scsi_cmnd *cmd);

#endif /* _SCSI_SCSI_CMND_H */
