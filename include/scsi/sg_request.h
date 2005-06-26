typedef struct scsi_request Scsi_Request;

static Scsi_Request *dummy_cmdp;	/* only used for sizeof */

typedef struct sg_scatter_hold { /* holding area for scsi scatter gather info */
	unsigned short k_use_sg; /* Count of kernel scatter-gather pieces */
	unsigned short sglist_len; /* size of malloc'd scatter-gather list ++ */
	unsigned bufflen;	/* Size of (aggregate) data buffer */
	unsigned b_malloc_len;	/* actual len malloc'ed in buffer */
	void *buffer;		/* Data buffer or scatter list (k_use_sg>0) */
	char dio_in_use;	/* 0->indirect IO (or mmap), 1->dio */
	unsigned char cmd_opcode; /* first byte of command */
} Sg_scatter_hold;

typedef struct sg_request {	/* SG_MAX_QUEUE requests outstanding per file */
	Scsi_Request *my_cmdp;	/* != 0  when request with lower levels */
	struct sg_request *nextrp;	/* NULL -> tail request (slist) */
	struct sg_fd *parentfp;	/* NULL -> not in use */
	Sg_scatter_hold data;	/* hold buffer, perhaps scatter list */
	sg_io_hdr_t header;	/* scsi command+info, see <scsi/sg.h> */
	unsigned char sense_b[sizeof (dummy_cmdp->sr_sense_buffer)];
	char res_used;		/* 1 -> using reserve buffer, 0 -> not ... */
	char orphan;		/* 1 -> drop on sight, 0 -> normal */
	char sg_io_owned;	/* 1 -> packet belongs to SG_IO */
	volatile char done;	/* 0->before bh, 1->before read, 2->read */
} Sg_request;
