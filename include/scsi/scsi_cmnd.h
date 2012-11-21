#ifndef _SCSI_SCSI_CMND_H
#define _SCSI_SCSI_CMND_H

#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/scatterlist.h>

struct Scsi_Host;
struct scsi_device;
struct scsi_driver;

/*
 * MAX_COMMAND_SIZE is:
 * The longest fixed-length SCSI CDB as per the SCSI standard.
 * fixed-length means: commands that their size can be determined
 * by their opcode and the CDB does not carry a length specifier, (unlike
 * the VARIABLE_LENGTH_CMD(0x7f) command). This is actually not exactly
 * true and the SCSI standard also defines extended commands and
 * vendor specific commands that can be bigger than 16 bytes. The kernel
 * will support these using the same infrastructure used for VARLEN CDB's.
 * So in effect MAX_COMMAND_SIZE means the maximum size command scsi-ml
 * supports without specifying a cmd_len by ULD's
 */
#define MAX_COMMAND_SIZE 16
#if (MAX_COMMAND_SIZE > BLK_MAX_CDB)
# error MAX_COMMAND_SIZE can not be bigger than BLK_MAX_CDB
#endif

struct scsi_data_buffer {
	struct sg_table table;
	unsigned length;
	int resid;
};

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

	/*
	 * A SCSI Command is assigned a nonzero serial_number before passed
	 * to the driver's queue command function.  The serial_number is
	 * cleared when scsi_done is entered indicating that the command
	 * has been completed.  It is a bug for LLDDs to use this number
	 * for purposes other than printk (and even that is only useful
	 * for debugging).
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

	unsigned char prot_op;
	unsigned char prot_type;

	unsigned short cmd_len;
	enum dma_data_direction sc_data_direction;

	/* These elements define the operation we are about to perform */
	unsigned char *cmnd;


	/* These elements define the operation we ultimately want to perform */
	struct scsi_data_buffer sdb;
	struct scsi_data_buffer *prot_sdb;

	unsigned underflow;	/* Return error if less than
				   this amount is transferred */

	unsigned transfersize;	/* How much we are guaranteed to
				   transfer with each SCSI transfer
				   (ie, between disconnect / 
				   reconnects.   Probably == sector
				   size */

	struct request *request;	/* The command we are
				   	   working on */

#define SCSI_SENSE_BUFFERSIZE 	96
	unsigned char *sense_buffer;
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
};

/* make sure not to use it with REQ_TYPE_BLOCK_PC commands */
static inline struct scsi_driver *scsi_cmd_to_driver(struct scsi_cmnd *cmd)
{
	return *(struct scsi_driver **)cmd->request->rq_disk->private_data;
}

extern struct scsi_cmnd *scsi_get_command(struct scsi_device *, gfp_t);
extern struct scsi_cmnd *__scsi_get_command(struct Scsi_Host *, gfp_t);
extern void scsi_put_command(struct scsi_cmnd *);
extern void __scsi_put_command(struct Scsi_Host *, struct scsi_cmnd *,
			       struct device *);
extern void scsi_finish_command(struct scsi_cmnd *cmd);

extern void *scsi_kmap_atomic_sg(struct scatterlist *sg, int sg_count,
				 size_t *offset, size_t *len);
extern void scsi_kunmap_atomic_sg(void *virt);

extern int scsi_init_io(struct scsi_cmnd *cmd, gfp_t gfp_mask);
extern void scsi_release_buffers(struct scsi_cmnd *cmd);

extern int scsi_dma_map(struct scsi_cmnd *cmd);
extern void scsi_dma_unmap(struct scsi_cmnd *cmd);

struct scsi_cmnd *scsi_allocate_command(gfp_t gfp_mask);
void scsi_free_command(gfp_t gfp_mask, struct scsi_cmnd *cmd);

static inline unsigned scsi_sg_count(struct scsi_cmnd *cmd)
{
	return cmd->sdb.table.nents;
}

static inline struct scatterlist *scsi_sglist(struct scsi_cmnd *cmd)
{
	return cmd->sdb.table.sgl;
}

static inline unsigned scsi_bufflen(struct scsi_cmnd *cmd)
{
	return cmd->sdb.length;
}

static inline void scsi_set_resid(struct scsi_cmnd *cmd, int resid)
{
	cmd->sdb.resid = resid;
}

static inline int scsi_get_resid(struct scsi_cmnd *cmd)
{
	return cmd->sdb.resid;
}

#define scsi_for_each_sg(cmd, sg, nseg, __i)			\
	for_each_sg(scsi_sglist(cmd), sg, nseg, __i)

static inline int scsi_bidi_cmnd(struct scsi_cmnd *cmd)
{
	return blk_bidi_rq(cmd->request) &&
		(cmd->request->next_rq->special != NULL);
}

static inline struct scsi_data_buffer *scsi_in(struct scsi_cmnd *cmd)
{
	return scsi_bidi_cmnd(cmd) ?
		cmd->request->next_rq->special : &cmd->sdb;
}

static inline struct scsi_data_buffer *scsi_out(struct scsi_cmnd *cmd)
{
	return &cmd->sdb;
}

static inline int scsi_sg_copy_from_buffer(struct scsi_cmnd *cmd,
					   void *buf, int buflen)
{
	return sg_copy_from_buffer(scsi_sglist(cmd), scsi_sg_count(cmd),
				   buf, buflen);
}

static inline int scsi_sg_copy_to_buffer(struct scsi_cmnd *cmd,
					 void *buf, int buflen)
{
	return sg_copy_to_buffer(scsi_sglist(cmd), scsi_sg_count(cmd),
				 buf, buflen);
}

/*
 * The operations below are hints that tell the controller driver how
 * to handle I/Os with DIF or similar types of protection information.
 */
enum scsi_prot_operations {
	/* Normal I/O */
	SCSI_PROT_NORMAL = 0,

	/* OS-HBA: Protected, HBA-Target: Unprotected */
	SCSI_PROT_READ_INSERT,
	SCSI_PROT_WRITE_STRIP,

	/* OS-HBA: Unprotected, HBA-Target: Protected */
	SCSI_PROT_READ_STRIP,
	SCSI_PROT_WRITE_INSERT,

	/* OS-HBA: Protected, HBA-Target: Protected */
	SCSI_PROT_READ_PASS,
	SCSI_PROT_WRITE_PASS,
};

static inline void scsi_set_prot_op(struct scsi_cmnd *scmd, unsigned char op)
{
	scmd->prot_op = op;
}

static inline unsigned char scsi_get_prot_op(struct scsi_cmnd *scmd)
{
	return scmd->prot_op;
}

/*
 * The controller usually does not know anything about the target it
 * is communicating with.  However, when DIX is enabled the controller
 * must be know target type so it can verify the protection
 * information passed along with the I/O.
 */
enum scsi_prot_target_type {
	SCSI_PROT_DIF_TYPE0 = 0,
	SCSI_PROT_DIF_TYPE1,
	SCSI_PROT_DIF_TYPE2,
	SCSI_PROT_DIF_TYPE3,
};

static inline void scsi_set_prot_type(struct scsi_cmnd *scmd, unsigned char type)
{
	scmd->prot_type = type;
}

static inline unsigned char scsi_get_prot_type(struct scsi_cmnd *scmd)
{
	return scmd->prot_type;
}

static inline sector_t scsi_get_lba(struct scsi_cmnd *scmd)
{
	return blk_rq_pos(scmd->request);
}

static inline unsigned scsi_prot_sg_count(struct scsi_cmnd *cmd)
{
	return cmd->prot_sdb ? cmd->prot_sdb->table.nents : 0;
}

static inline struct scatterlist *scsi_prot_sglist(struct scsi_cmnd *cmd)
{
	return cmd->prot_sdb ? cmd->prot_sdb->table.sgl : NULL;
}

static inline struct scsi_data_buffer *scsi_prot(struct scsi_cmnd *cmd)
{
	return cmd->prot_sdb;
}

#define scsi_for_each_prot_sg(cmd, sg, nseg, __i)		\
	for_each_sg(scsi_prot_sglist(cmd), sg, nseg, __i)

static inline void set_msg_byte(struct scsi_cmnd *cmd, char status)
{
	cmd->result = (cmd->result & 0xffff00ff) | (status << 8);
}

static inline void set_host_byte(struct scsi_cmnd *cmd, char status)
{
	cmd->result = (cmd->result & 0xff00ffff) | (status << 16);
}

static inline void set_driver_byte(struct scsi_cmnd *cmd, char status)
{
	cmd->result = (cmd->result & 0x00ffffff) | (status << 24);
}

#endif /* _SCSI_SCSI_CMND_H */
