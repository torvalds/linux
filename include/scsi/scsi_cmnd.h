#ifndef _SCSI_SCSI_CMND_H
#define _SCSI_SCSI_CMND_H

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/timer.h>

struct request;
struct scatterlist;
struct Scsi_Host;
struct scsi_device;


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
	struct scsi_device *device;
	struct list_head list;  /* scsi_cmnd participates in queue lists */
	struct list_head eh_entry; /* entry for the host eh_cmd_q */
	int eh_eflags;		/* Used by error handlr */
	void (*done) (struct scsi_cmnd *);	/* Mid-level done function */

	/*
	 * A SCSI Command is assigned a nonzero serial_number before passed
	 * to the driver's queue command function.  The serial_number is
	 * cleared when scsi_done is entered indicating that the command
	 * has been completed.  It currently doesn't have much use other
	 * than printk's.  Some lldd's use this number for other purposes.
	 * It's almost certain that such usages are either incorrect or
	 * meaningless.  Please kill all usages other than printk's.  Also,
	 * as this number is always identical to ->pid, please convert
	 * printk's to use ->pid, so that we can kill this field.
	 */
	unsigned long serial_number;
	/*
	 * This is set to jiffies as it was when the command was first
	 * allocated.  It is used to time how long the command has
	 * been outstanding
	 */
	unsigned long jiffies_at_alloc;

	int retries;
	int allowed;
	int timeout_per_command;

	unsigned char cmd_len;
	enum dma_data_direction sc_data_direction;

	/* These elements define the operation we are about to perform */
#define MAX_COMMAND_SIZE	16
	unsigned char cmnd[MAX_COMMAND_SIZE];
	unsigned request_bufflen;	/* Actual request size */

	struct timer_list eh_timeout;	/* Used to time out the command. */
	void *request_buffer;		/* Actual requested buffer */

	/* These elements define the operation we ultimately want to perform */
	unsigned short use_sg;	/* Number of pieces of scatter-gather */
	unsigned short sglist_len;	/* size of malloc'd scatter-gather list */

	unsigned underflow;	/* Return error if less than
				   this amount is transferred */

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
	unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];
				/* obtained by REQUEST SENSE when
				 * CHECK CONDITION is received on original
				 * command (auto-sense) */

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
					 * and hang it here.  The host adapter
					 * is also expected to call scsi_free
					 * to release this memory.  (The memory
					 * obtained by scsi_malloc is guaranteed
					 * to be at an address < 16Mb). */

	int result;		/* Status code from lower level driver */

	unsigned char tag;	/* SCSI-II queued command tag */
	unsigned long pid;	/* Process ID, starts at 0. Unique per host. */
};

extern struct scsi_cmnd *scsi_get_command(struct scsi_device *, gfp_t);
extern struct scsi_cmnd *__scsi_get_command(struct Scsi_Host *, gfp_t);
extern void scsi_put_command(struct scsi_cmnd *);
extern void __scsi_put_command(struct Scsi_Host *, struct scsi_cmnd *,
			       struct device *);
extern void scsi_io_completion(struct scsi_cmnd *, unsigned int);
extern void scsi_finish_command(struct scsi_cmnd *cmd);
extern void scsi_req_abort_cmd(struct scsi_cmnd *cmd);

extern void *scsi_kmap_atomic_sg(struct scatterlist *sg, int sg_count,
				 size_t *offset, size_t *len);
extern void scsi_kunmap_atomic_sg(void *virt);

extern struct scatterlist *scsi_alloc_sgtable(struct scsi_cmnd *, gfp_t);
extern void scsi_free_sgtable(struct scatterlist *, int);

#endif /* _SCSI_SCSI_CMND_H */
