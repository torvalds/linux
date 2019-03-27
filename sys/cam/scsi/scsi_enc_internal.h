/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This file contains definitions only intended for use within
 * sys/cam/scsi/scsi_enc*.c, and not in other kernel components.
 */

#ifndef	__SCSI_ENC_INTERNAL_H__
#define	__SCSI_ENC_INTERNAL_H__

typedef struct enc_element {
	uint32_t
		 enctype	: 8,	/* enclosure type */
		 subenclosure : 8,	/* subenclosure id */
		 svalid	: 1,		/* enclosure information valid */
		 overall_status_elem: 1,/*
					 * This object represents generic
					 * status about all objects of this
					 * type.
					 */
		 priv	: 14;		/* private data, per object */
	uint8_t	 encstat[4];		/* state && stats */
	uint8_t *physical_path;		/* Device physical path data. */
	u_int    physical_path_len;	/* Length of device path data. */
	void    *elm_private;		/* per-type object data */
} enc_element_t;

typedef enum {
	ENC_NONE,
	ENC_SES_SCSI2,
	ENC_SES,
	ENC_SES_PASSTHROUGH,
	ENC_SEN,
	ENC_SAFT,
	ENC_SEMB_SES,
	ENC_SEMB_SAFT
} enctyp;

/* Platform Independent Driver Internal Definitions for enclosure devices. */
typedef struct enc_softc enc_softc_t;

struct enc_fsm_state;
typedef int fsm_fill_handler_t(enc_softc_t *ssc,
				struct enc_fsm_state *state,
				union ccb *ccb,
				uint8_t *buf);
typedef int fsm_error_handler_t(union ccb *ccb, uint32_t cflags,
				uint32_t sflags);
typedef int fsm_done_handler_t(enc_softc_t *ssc,
			       struct enc_fsm_state *state, union ccb *ccb,
			       uint8_t **bufp, int error, int xfer_len);

struct enc_fsm_state {
	const char	    *name;
	int		     page_code;
	size_t		     buf_size;
	uint32_t	     timeout;
	fsm_fill_handler_t  *fill;
	fsm_done_handler_t  *done;
	fsm_error_handler_t *error;
};

typedef int (enc_softc_init_t)(enc_softc_t *);
typedef void (enc_softc_invalidate_t)(enc_softc_t *);
typedef void (enc_softc_cleanup_t)(enc_softc_t *);
typedef int (enc_init_enc_t)(enc_softc_t *); 
typedef int (enc_get_enc_status_t)(enc_softc_t *, int);
typedef int (enc_set_enc_status_t)(enc_softc_t *, encioc_enc_status_t, int);
typedef int (enc_get_elm_status_t)(enc_softc_t *, encioc_elm_status_t *, int);
typedef int (enc_set_elm_status_t)(enc_softc_t *, encioc_elm_status_t *, int);
typedef int (enc_get_elm_desc_t)(enc_softc_t *, encioc_elm_desc_t *); 
typedef int (enc_get_elm_devnames_t)(enc_softc_t *, encioc_elm_devnames_t *); 
typedef int (enc_handle_string_t)(enc_softc_t *, encioc_string_t *, int);
typedef void (enc_device_found_t)(enc_softc_t *);
typedef void (enc_poll_status_t)(enc_softc_t *);

struct enc_vec {
	enc_softc_invalidate_t	*softc_invalidate;
	enc_softc_cleanup_t	*softc_cleanup;
	enc_init_enc_t		*init_enc;
	enc_get_enc_status_t	*get_enc_status;
	enc_set_enc_status_t	*set_enc_status;
	enc_get_elm_status_t	*get_elm_status;
	enc_set_elm_status_t	*set_elm_status;
	enc_get_elm_desc_t	*get_elm_desc;
	enc_get_elm_devnames_t	*get_elm_devnames;
	enc_handle_string_t	*handle_string;
	enc_device_found_t	*device_found;
	enc_poll_status_t	*poll_status;
};

typedef struct enc_cache {
	enc_element_t		*elm_map;	/* objects */
	int			 nelms;		/* number of objects */
	encioc_enc_status_t	 enc_status;	/* overall status */
	void			*private;	/* per-type private data */
} enc_cache_t;

/* Enclosure instance toplevel structure */
struct enc_softc {
	enctyp			 enc_type;	/* type of enclosure */
	struct enc_vec		 enc_vec;	/* vector to handlers */
	void			*enc_private;	/* per-type private data */

	/**
	 * "Published" configuration and state data available to
	 * external consumers.
	 */
	enc_cache_t		 enc_cache;

	/**
	 * Configuration and state data being actively updated
	 * by the enclosure daemon.
	 */
	enc_cache_t		 enc_daemon_cache;

	struct sx		 enc_cache_lock;
	uint8_t			 enc_flags;
#define	ENC_FLAG_INVALID	0x01
#define	ENC_FLAG_INITIALIZED	0x02
#define	ENC_FLAG_SHUTDOWN	0x04
	union ccb		 saved_ccb;
	struct cdev		*enc_dev;
	struct cam_periph	*periph;
	int			 open_count;

	/* Bitmap of pending operations. */
	uint32_t		 pending_actions;

	/* The action on which the state machine is currently working. */
	uint32_t		 current_action;
#define	ENC_UPDATE_NONE		0x00
#define	ENC_UPDATE_INVALID	0xff

	/* Callout for auto-updating enclosure status */
	struct callout		 status_updater;

	struct proc		*enc_daemon;

	struct enc_fsm_state 	*enc_fsm_states;

	struct intr_config_hook  enc_boot_hold_ch;

#define 	ENC_ANNOUNCE_SZ		400
	char			announce_buf[ENC_ANNOUNCE_SZ];
};

static inline enc_cache_t *
enc_other_cache(enc_softc_t *enc, enc_cache_t *primary)
{
	return (primary == &enc->enc_cache
	      ? &enc->enc_daemon_cache : &enc->enc_cache);
}

/* SES Management mode page - SES2r20 Table 59 */
struct ses_mgmt_mode_page {
	struct scsi_mode_header_6 header;
	struct scsi_mode_blk_desc blk_desc;
	uint8_t byte0;  /* ps : 1, spf : 1, page_code : 6 */
#define SES_MGMT_MODE_PAGE_CODE 0x14
	uint8_t length;
#define SES_MGMT_MODE_PAGE_LEN  6
	uint8_t reserved[3];
	uint8_t byte5;  /* reserved : 7, enbltc : 1 */
#define SES_MGMT_TIMED_COMP_EN  0x1
	uint8_t max_comp_time[2];
};

/* Enclosure core interface for sub-drivers */
int  enc_runcmd(struct enc_softc *, char *, int, char *, int *);
void enc_log(struct enc_softc *, const char *, ...);
int  enc_error(union ccb *, uint32_t, uint32_t);
void enc_update_request(enc_softc_t *, uint32_t);

/* SES Native interface */
enc_softc_init_t	ses_softc_init;

/* SAF-TE interface */
enc_softc_init_t	safte_softc_init;

/* Helper macros */
MALLOC_DECLARE(M_SCSIENC);
#define	ENC_CFLAGS		CAM_RETRY_SELTO
#define	ENC_FLAGS		SF_NO_PRINT | SF_RETRY_UA
#define	STRNCMP			strncmp
#define	PRINTF			printf
#define	ENC_LOG			enc_log
#if defined(DEBUG) || defined(ENC_DEBUG)
#define	ENC_DLOG		enc_log
#else
#define	ENC_DLOG		if (0) enc_log
#endif
#define	ENC_VLOG		if (bootverbose) enc_log
#define	ENC_MALLOC(amt)		malloc(amt, M_SCSIENC, M_NOWAIT)
#define	ENC_MALLOCZ(amt)	malloc(amt, M_SCSIENC, M_ZERO|M_NOWAIT)
/* Cast away const avoiding GCC warnings. */
#define	ENC_FREE(ptr)		free((void *)((uintptr_t)ptr), M_SCSIENC)
#define	ENC_FREE_AND_NULL(ptr)	do {	\
	if (ptr != NULL) {		\
		ENC_FREE(ptr);		\
		ptr = NULL;		\
	}				\
} while(0)
#define	MEMZERO			bzero
#define	MEMCPY(dest, src, amt)	bcopy(src, dest, amt)

#endif	/* __SCSI_ENC_INTERNAL_H__ */
