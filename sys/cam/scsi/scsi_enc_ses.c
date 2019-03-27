/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Matthew Jacob
 * Copyright (c) 2010 Spectra Logic Corporation
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
 */

/**
 * \file scsi_enc_ses.c
 *
 * Structures and routines specific && private to SES only
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/types.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>

#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_enc.h>
#include <cam/scsi/scsi_enc_internal.h>

/* SES Native Type Device Support */

/* SES Diagnostic Page Codes */
typedef enum {
	SesSupportedPages	= 0x0,
	SesConfigPage		= 0x1,
	SesControlPage		= 0x2,
	SesStatusPage		= SesControlPage,
	SesHelpTxt		= 0x3,
	SesStringOut		= 0x4,
	SesStringIn		= SesStringOut,
	SesThresholdOut		= 0x5,
	SesThresholdIn		= SesThresholdOut,
	SesArrayControl		= 0x6,	/* Obsolete in SES v2 */
	SesArrayStatus		= SesArrayControl,
	SesElementDescriptor	= 0x7,
	SesShortStatus		= 0x8,
	SesEnclosureBusy	= 0x9,
	SesAddlElementStatus	= 0xa
} SesDiagPageCodes;

typedef struct ses_type {
	const struct ses_elm_type_desc  *hdr;
	const char			*text;
} ses_type_t;

typedef struct ses_comstat {
	uint8_t	comstatus;
	uint8_t	comstat[3];
} ses_comstat_t;

typedef union ses_addl_data {
	struct ses_elm_sas_device_phy *sasdev_phys;
	struct ses_elm_sas_expander_phy *sasexp_phys;
	struct ses_elm_sas_port_phy *sasport_phys;
	struct ses_fcobj_port *fc_ports;
} ses_add_data_t;

typedef struct ses_addl_status {
	struct ses_elm_addlstatus_base_hdr *hdr;
	union {
		union ses_fcobj_hdr *fc;
		union ses_elm_sas_hdr *sas;
	} proto_hdr;
	union ses_addl_data proto_data;	/* array sizes stored in header */
} ses_add_status_t;

typedef struct ses_element {
	uint8_t eip;			/* eip bit is set */
	uint16_t descr_len;		/* length of the descriptor */
	char *descr;			/* descriptor for this object */
	struct ses_addl_status addl;	/* additional status info */
} ses_element_t;

typedef struct ses_control_request {
	int	      elm_idx;
	ses_comstat_t elm_stat;
	int	      result;
	TAILQ_ENTRY(ses_control_request) links;
} ses_control_request_t;
TAILQ_HEAD(ses_control_reqlist, ses_control_request);
typedef struct ses_control_reqlist ses_control_reqlist_t;
enum {
	SES_SETSTATUS_ENC_IDX = -1
};

static void
ses_terminate_control_requests(ses_control_reqlist_t *reqlist, int result)
{
	ses_control_request_t *req;

	while ((req = TAILQ_FIRST(reqlist)) != NULL) {
		TAILQ_REMOVE(reqlist, req, links);
		req->result = result;
		wakeup(req);
	}
}

enum ses_iter_index_values {
	/**
	 * \brief  Value of an initialized but invalid index
	 *         in a ses_iterator object.
	 *
	 * This value is used for the  individual_element_index of
	 * overal status elements and for all index types when
	 * an iterator is first initialized.
	 */
	ITERATOR_INDEX_INVALID = -1,

	/**
	 * \brief  Value of an index in a ses_iterator object
	 *	   when the iterator has traversed past the last
	 *	   valid element..
	 */
	ITERATOR_INDEX_END     = INT_MAX
};

/**
 * \brief Structure encapsulating all data necessary to traverse the
 *        elements of a SES configuration.
 *
 * The ses_iterator object simplifies the task of iterating through all
 * elements detected via the SES configuration page by tracking the numerous
 * element indexes that, instead of memoizing in the softc, we calculate
 * on the fly during the traversal of the element objects.  The various
 * indexes are necessary due to the varying needs of matching objects in
 * the different SES pages.  Some pages (e.g. Status/Control) contain all
 * elements, while others (e.g. Additional Element Status) only contain
 * individual elements (no overal status elements) of particular types.
 *
 * To use an iterator, initialize it with ses_iter_init(), and then
 * use ses_iter_next() to traverse the elements (including the first) in
 * the configuration.  Once an iterator is initiailized with ses_iter_init(),
 * you may also seek to any particular element by either it's global or
 * individual element index via the ses_iter_seek_to() function.  You may
 * also return an iterator to the position just before the first element
 * (i.e. the same state as after an ses_iter_init()), with ses_iter_reset().
 */
struct ses_iterator {
	/**
	 * \brief Backlink to the overal software configuration structure.
	 *
	 * This is included for convenience so the iteration functions
	 * need only take a single, struct ses_iterator *, argument.
	 */
	enc_softc_t *enc;

	enc_cache_t *cache;

	/**
	 * \brief Index of the type of the current element within the
	 *        ses_cache's ses_types array.
	 */
	int	          type_index;

	/**
	 * \brief The position (0 based) of this element relative to all other
	 *        elements of this type.
	 *
	 * This index resets to zero every time the iterator transitions
	 * to elements of a new type in the configuration.
	 */
	int	          type_element_index;

	/**
	 * \brief The position (0 based) of this element relative to all
	 *        other individual status elements in the configuration.
	 *
	 * This index ranges from 0 through the number of individual
	 * elements in the configuration.  When the iterator returns
	 * an overall status element, individual_element_index is
	 * set to ITERATOR_INDEX_INVALID, to indicate that it does
	 * not apply to the current element.
	 */
	int	          individual_element_index;

	/**
	 * \brief The position (0 based) of this element relative to
	 *        all elements in the configration.
	 *
	 * This index is appropriate for indexing into enc->ses_elm_map.
	 */
	int	          global_element_index;

	/**
	 * \brief The last valid individual element index of this
	 *        iterator.
	 *
	 * When an iterator traverses an overal status element, the
	 * individual element index is reset to ITERATOR_INDEX_INVALID
	 * to prevent unintential use of the individual_element_index
	 * field.  The saved_individual_element_index allows the iterator
	 * to restore it's position in the individual elements upon
	 * reaching the next individual element.
	 */
	int	          saved_individual_element_index;
};

typedef enum {
	SES_UPDATE_NONE,
	SES_UPDATE_PAGES,
	SES_UPDATE_GETCONFIG,
	SES_UPDATE_GETSTATUS,
	SES_UPDATE_GETELMDESCS,
	SES_UPDATE_GETELMADDLSTATUS,
	SES_PROCESS_CONTROL_REQS,
	SES_PUBLISH_PHYSPATHS,
	SES_PUBLISH_CACHE,
	SES_NUM_UPDATE_STATES
} ses_update_action;

static enc_softc_cleanup_t ses_softc_cleanup;

#define	SCSZ	0x8000

static fsm_fill_handler_t ses_fill_rcv_diag_io;
static fsm_fill_handler_t ses_fill_control_request;
static fsm_done_handler_t ses_process_pages;
static fsm_done_handler_t ses_process_config;
static fsm_done_handler_t ses_process_status;
static fsm_done_handler_t ses_process_elm_descs;
static fsm_done_handler_t ses_process_elm_addlstatus;
static fsm_done_handler_t ses_process_control_request;
static fsm_done_handler_t ses_publish_physpaths;
static fsm_done_handler_t ses_publish_cache;

static struct enc_fsm_state enc_fsm_states[SES_NUM_UPDATE_STATES] =
{
	{ "SES_UPDATE_NONE", 0, 0, 0, NULL, NULL, NULL },
	{
		"SES_UPDATE_PAGES",
		SesSupportedPages,
		SCSZ,
		60 * 1000,
		ses_fill_rcv_diag_io,
		ses_process_pages,
		enc_error
	},
	{
		"SES_UPDATE_GETCONFIG",
		SesConfigPage,
		SCSZ,
		60 * 1000,
		ses_fill_rcv_diag_io,
		ses_process_config,
		enc_error
	},
	{
		"SES_UPDATE_GETSTATUS",
		SesStatusPage,
		SCSZ,
		60 * 1000,
		ses_fill_rcv_diag_io,
		ses_process_status,
		enc_error
	},
	{
		"SES_UPDATE_GETELMDESCS",
		SesElementDescriptor,
		SCSZ,
		60 * 1000,
		ses_fill_rcv_diag_io,
		ses_process_elm_descs,
		enc_error
	},
	{
		"SES_UPDATE_GETELMADDLSTATUS",
		SesAddlElementStatus,
		SCSZ,
		60 * 1000,
		ses_fill_rcv_diag_io,
		ses_process_elm_addlstatus,
		enc_error
	},
	{
		"SES_PROCESS_CONTROL_REQS",
		SesControlPage,
		SCSZ,
		60 * 1000,
		ses_fill_control_request,
		ses_process_control_request,
		enc_error
	},
	{
		"SES_PUBLISH_PHYSPATHS",
		0,
		0,
		0,
		NULL,
		ses_publish_physpaths,
		NULL
	},
	{
		"SES_PUBLISH_CACHE",
		0,
		0,
		0,
		NULL,
		ses_publish_cache,
		NULL
	}
};

typedef struct ses_cache {
	/* Source for all the configuration data pointers */
	const struct ses_cfg_page		*cfg_page;

	/* References into the config page. */
	int					 ses_nsubencs;
	const struct ses_enc_desc * const	*subencs;
	int					 ses_ntypes;
	const ses_type_t			*ses_types;

	/* Source for all the status pointers */
	const struct ses_status_page		*status_page;

	/* Source for all the object descriptor pointers */
	const struct ses_elem_descr_page	*elm_descs_page;

	/* Source for all the additional object status pointers */
	const struct ses_addl_elem_status_page  *elm_addlstatus_page;

} ses_cache_t;

typedef struct ses_softc {
	uint32_t		ses_flags;
#define	SES_FLAG_TIMEDCOMP	0x01
#define	SES_FLAG_ADDLSTATUS	0x02
#define	SES_FLAG_DESC		0x04

	ses_control_reqlist_t	ses_requests;
	ses_control_reqlist_t	ses_pending_requests;
} ses_softc_t;

/**
 * \brief Reset a SES iterator to just before the first element
 *        in the configuration.
 *
 * \param iter  The iterator object to reset.
 *
 * The indexes within a reset iterator are invalid and will only
 * become valid upon completion of a ses_iter_seek_to() or a
 * ses_iter_next().
 */
static void
ses_iter_reset(struct ses_iterator *iter)
{
	/*
	 * Set our indexes to just before the first valid element
	 * of the first type (ITERATOR_INDEX_INVALID == -1).  This
	 * simplifies the implementation of ses_iter_next().
	 */
	iter->type_index                     = 0;
	iter->type_element_index             = ITERATOR_INDEX_INVALID;
	iter->global_element_index           = ITERATOR_INDEX_INVALID;
	iter->individual_element_index       = ITERATOR_INDEX_INVALID;
	iter->saved_individual_element_index = ITERATOR_INDEX_INVALID;
}

/**
 * \brief Initialize the storage of a SES iterator and reset it to
 *        the position just before the first element of the
 *        configuration.
 *
 * \param enc	The SES softc for the SES instance whose configuration
 *              will be enumerated by this iterator.
 * \param iter  The iterator object to initialize.
 */
static void
ses_iter_init(enc_softc_t *enc, enc_cache_t *cache, struct ses_iterator *iter)
{
	iter->enc = enc;
	iter->cache = cache;
	ses_iter_reset(iter);
}

/**
 * \brief Traverse the provided SES iterator to the next element
 *        within the configuraiton.
 *
 * \param iter  The iterator to move.
 *
 * \return  If a valid next element exists, a pointer to it's enc_element_t.
 *          Otherwise NULL.
 */
static enc_element_t *
ses_iter_next(struct ses_iterator *iter)
{
	ses_cache_t	 *ses_cache;
	const ses_type_t *element_type;

	ses_cache = iter->cache->private;

	/*
	 * Note: Treat nelms as signed, so we will hit this case
	 *       and immediately terminate the iteration if the
	 *	 configuration has 0 objects.
	 */
	if (iter->global_element_index >= (int)iter->cache->nelms - 1) {

		/* Elements exhausted. */
		iter->type_index	       = ITERATOR_INDEX_END;
		iter->type_element_index       = ITERATOR_INDEX_END;
		iter->global_element_index     = ITERATOR_INDEX_END;
		iter->individual_element_index = ITERATOR_INDEX_END;
		return (NULL);
	}

	KASSERT((iter->type_index < ses_cache->ses_ntypes),
		("Corrupted element iterator. %d not less than %d",
		 iter->type_index, ses_cache->ses_ntypes));

	element_type = &ses_cache->ses_types[iter->type_index];
	iter->global_element_index++;
	iter->type_element_index++;

	/*
	 * There is an object for overal type status in addition
	 * to one for each allowed element, but only if the element
	 * count is non-zero.
	 */
	if (iter->type_element_index > element_type->hdr->etype_maxelt) {

		/*
		 * We've exhausted the elements of this type.
		 * This next element belongs to the next type.
		 */
		iter->type_index++;
		iter->type_element_index = 0;
		iter->saved_individual_element_index
		    = iter->individual_element_index;
		iter->individual_element_index = ITERATOR_INDEX_INVALID;
	}

	if (iter->type_element_index > 0) {
		if (iter->type_element_index == 1) {
			iter->individual_element_index
			    = iter->saved_individual_element_index;
		}
		iter->individual_element_index++;
	}

	return (&iter->cache->elm_map[iter->global_element_index]);
}

/**
 * Element index types tracked by a SES iterator.
 */
typedef enum {
	/**
	 * Index relative to all elements (overall and individual)
	 * in the system.
	 */
	SES_ELEM_INDEX_GLOBAL,

	/**
	 * \brief Index relative to all individual elements in the system.
	 *
	 * This index counts only individual elements, skipping overall
	 * status elements.  This is the index space of the additional
	 * element status page (page 0xa).
	 */
	SES_ELEM_INDEX_INDIVIDUAL
} ses_elem_index_type_t;

/**
 * \brief Move the provided iterator forwards or backwards to the object 
 *        having the give index.
 *
 * \param iter           The iterator on which to perform the seek.
 * \param element_index  The index of the element to find.
 * \param index_type     The type (global or individual) of element_index.
 *
 * \return  If the element is found, a pointer to it's enc_element_t.
 *          Otherwise NULL.
 */
static enc_element_t *
ses_iter_seek_to(struct ses_iterator *iter, int element_index,
		 ses_elem_index_type_t index_type)
{
	enc_element_t	*element;
	int		*cur_index;

	if (index_type == SES_ELEM_INDEX_GLOBAL)
		cur_index = &iter->global_element_index;
	else
		cur_index = &iter->individual_element_index;

	if (*cur_index == element_index) {
		/* Already there. */
		return (&iter->cache->elm_map[iter->global_element_index]);
	}

	ses_iter_reset(iter);
	while ((element = ses_iter_next(iter)) != NULL
	    && *cur_index != element_index)
		;

	if (*cur_index != element_index)
		return (NULL);

	return (element);
}

#if 0
static int ses_encode(enc_softc_t *, uint8_t *, int, int,
    struct ses_comstat *);
#endif
static int ses_set_timed_completion(enc_softc_t *, uint8_t);
#if 0
static int ses_putstatus(enc_softc_t *, int, struct ses_comstat *);
#endif

static void ses_poll_status(enc_softc_t *);
static void ses_print_addl_data(enc_softc_t *, enc_element_t *);

/*=========================== SES cleanup routines ===========================*/

static void
ses_cache_free_elm_addlstatus(enc_softc_t *enc, enc_cache_t *cache)
{
	ses_cache_t   *ses_cache;
	ses_cache_t   *other_ses_cache;
	enc_element_t *cur_elm;
	enc_element_t *last_elm;

	ENC_DLOG(enc, "%s: enter\n", __func__);
	ses_cache = cache->private;
	if (ses_cache->elm_addlstatus_page == NULL)
		return;

	for (cur_elm = cache->elm_map,
	     last_elm = &cache->elm_map[cache->nelms];
	     cur_elm != last_elm; cur_elm++) {
		ses_element_t *elmpriv;

		elmpriv = cur_elm->elm_private;

		/* Clear references to the additional status page. */
		bzero(&elmpriv->addl, sizeof(elmpriv->addl));
	}

	other_ses_cache = enc_other_cache(enc, cache)->private;
	if (other_ses_cache->elm_addlstatus_page
	 != ses_cache->elm_addlstatus_page)
		ENC_FREE(ses_cache->elm_addlstatus_page);
	ses_cache->elm_addlstatus_page = NULL;
}

static void
ses_cache_free_elm_descs(enc_softc_t *enc, enc_cache_t *cache)
{
	ses_cache_t   *ses_cache;
	ses_cache_t   *other_ses_cache;
	enc_element_t *cur_elm;
	enc_element_t *last_elm;

	ENC_DLOG(enc, "%s: enter\n", __func__);
	ses_cache = cache->private;
	if (ses_cache->elm_descs_page == NULL)
		return;

	for (cur_elm = cache->elm_map,
	     last_elm = &cache->elm_map[cache->nelms];
	     cur_elm != last_elm; cur_elm++) {
		ses_element_t *elmpriv;

		elmpriv = cur_elm->elm_private;
		elmpriv->descr_len = 0;
		elmpriv->descr = NULL;
	}

	other_ses_cache = enc_other_cache(enc, cache)->private;
	if (other_ses_cache->elm_descs_page
	 != ses_cache->elm_descs_page)
		ENC_FREE(ses_cache->elm_descs_page);
	ses_cache->elm_descs_page = NULL;
}

static void
ses_cache_free_status(enc_softc_t *enc, enc_cache_t *cache)
{
	ses_cache_t *ses_cache;
	ses_cache_t *other_ses_cache;

	ENC_DLOG(enc, "%s: enter\n", __func__);
	ses_cache   = cache->private;
	if (ses_cache->status_page == NULL)
		return;
	
	other_ses_cache = enc_other_cache(enc, cache)->private;
	if (other_ses_cache->status_page != ses_cache->status_page)
		ENC_FREE(ses_cache->status_page);
	ses_cache->status_page = NULL;
}

static void
ses_cache_free_elm_map(enc_softc_t *enc, enc_cache_t *cache)
{
	enc_element_t *cur_elm;
	enc_element_t *last_elm;

	ENC_DLOG(enc, "%s: enter\n", __func__);
	if (cache->elm_map == NULL)
		return;

	ses_cache_free_elm_descs(enc, cache);
	ses_cache_free_elm_addlstatus(enc, cache);
	for (cur_elm = cache->elm_map,
	     last_elm = &cache->elm_map[cache->nelms];
	     cur_elm != last_elm; cur_elm++) {

		ENC_FREE_AND_NULL(cur_elm->elm_private);
	}
	ENC_FREE_AND_NULL(cache->elm_map);
	cache->nelms = 0;
	ENC_DLOG(enc, "%s: exit\n", __func__);
}

static void
ses_cache_free(enc_softc_t *enc, enc_cache_t *cache)
{
	ses_cache_t *other_ses_cache;
	ses_cache_t *ses_cache;

	ENC_DLOG(enc, "%s: enter\n", __func__);
	ses_cache_free_elm_addlstatus(enc, cache);
	ses_cache_free_status(enc, cache);
	ses_cache_free_elm_map(enc, cache);

	ses_cache = cache->private;
	ses_cache->ses_ntypes = 0;

	other_ses_cache = enc_other_cache(enc, cache)->private;
	if (other_ses_cache->subencs != ses_cache->subencs)
		ENC_FREE(ses_cache->subencs);
	ses_cache->subencs = NULL;

	if (other_ses_cache->ses_types != ses_cache->ses_types)
		ENC_FREE(ses_cache->ses_types);
	ses_cache->ses_types = NULL;

	if (other_ses_cache->cfg_page != ses_cache->cfg_page)
		ENC_FREE(ses_cache->cfg_page);
	ses_cache->cfg_page = NULL;

	ENC_DLOG(enc, "%s: exit\n", __func__);
}

static void
ses_cache_clone(enc_softc_t *enc, enc_cache_t *src, enc_cache_t *dst)
{
	ses_cache_t   *dst_ses_cache;
	ses_cache_t   *src_ses_cache;
	enc_element_t *src_elm;
	enc_element_t *dst_elm;
	enc_element_t *last_elm;

	ses_cache_free(enc, dst);
	src_ses_cache = src->private;
	dst_ses_cache = dst->private;

	/*
	 * The cloned enclosure cache and ses specific cache are
	 * mostly identical to the source.
	 */
	*dst = *src;
	*dst_ses_cache = *src_ses_cache;

	/*
	 * But the ses cache storage is still independent.  Restore
	 * the pointer that was clobbered by the structure copy above.
	 */
	dst->private = dst_ses_cache;

	/*
	 * The element map is independent even though it starts out
	 * pointing to the same constant page data.
	 */
	dst->elm_map = malloc(dst->nelms * sizeof(enc_element_t),
	    M_SCSIENC, M_WAITOK);
	memcpy(dst->elm_map, src->elm_map, dst->nelms * sizeof(enc_element_t));
	for (dst_elm = dst->elm_map, src_elm = src->elm_map,
	     last_elm = &src->elm_map[src->nelms];
	     src_elm != last_elm; src_elm++, dst_elm++) {

		dst_elm->elm_private = malloc(sizeof(ses_element_t),
		    M_SCSIENC, M_WAITOK);
		memcpy(dst_elm->elm_private, src_elm->elm_private,
		       sizeof(ses_element_t));
	}
}

/* Structure accessors.  These are strongly typed to avoid errors. */

int
ses_elm_sas_descr_type(union ses_elm_sas_hdr *obj)
{
	return ((obj)->base_hdr.byte1 >> 6);
}
int
ses_elm_addlstatus_proto(struct ses_elm_addlstatus_base_hdr *hdr)
{
	return ((hdr)->byte0 & 0xf);
}
int
ses_elm_addlstatus_eip(struct ses_elm_addlstatus_base_hdr *hdr)
{
	return ((hdr)->byte0 >> 4) & 0x1;
}
int
ses_elm_addlstatus_invalid(struct ses_elm_addlstatus_base_hdr *hdr)
{
	return ((hdr)->byte0 >> 7);
}
int
ses_elm_sas_type0_not_all_phys(union ses_elm_sas_hdr *hdr)
{
	return ((hdr)->type0_noneip.byte1 & 0x1);
}
int
ses_elm_sas_dev_phy_sata_dev(struct ses_elm_sas_device_phy *phy)
{
	return ((phy)->target_ports & 0x1);
}
int
ses_elm_sas_dev_phy_sata_port(struct ses_elm_sas_device_phy *phy)
{
	return ((phy)->target_ports >> 7);
}
int
ses_elm_sas_dev_phy_dev_type(struct ses_elm_sas_device_phy *phy)
{
	return (((phy)->byte0 >> 4) & 0x7);
}

/**
 * \brief Verify that the cached configuration data in our softc
 *        is valid for processing the page data corresponding to
 *        the provided page header.
 *
 * \param ses_cache The SES cache to validate.
 * \param gen_code  The 4 byte generation code from a SES diagnostic
 *		    page header.
 *
 * \return  non-zero if true, 0 if false.
 */
static int
ses_config_cache_valid(ses_cache_t *ses_cache, const uint8_t *gen_code)
{
	uint32_t cache_gc;
	uint32_t cur_gc;

	if (ses_cache->cfg_page == NULL)
		return (0);

	cache_gc = scsi_4btoul(ses_cache->cfg_page->hdr.gen_code);
	cur_gc   = scsi_4btoul(gen_code);
	return (cache_gc == cur_gc);
}

/**
 * Function signature for consumers of the ses_devids_iter() interface.
 */
typedef void ses_devid_callback_t(enc_softc_t *, enc_element_t *,
				  struct scsi_vpd_id_descriptor *, void *);

/**
 * \brief Iterate over and create vpd device id records from the
 *        additional element status data for elm, passing that data
 *        to the provided callback.
 *
 * \param enc	        SES instance containing elm
 * \param elm	        Element for which to extract device ID data.
 * \param callback      The callback function to invoke on each generated
 *                      device id descriptor for elm.
 * \param callback_arg  Argument passed through to callback on each invocation.
 */
static void
ses_devids_iter(enc_softc_t *enc, enc_element_t *elm,
		ses_devid_callback_t *callback, void *callback_arg)
{
	ses_element_t           *elmpriv;
	struct ses_addl_status *addl;
	u_int                   i;
	size_t			devid_record_size;

	elmpriv = elm->elm_private;
	addl = &(elmpriv->addl);

	/*
	 * Don't assume this object has additional status information, or
	 * that it is a SAS device, or that it is a device slot device.
	 */
	if (addl->hdr == NULL || addl->proto_hdr.sas == NULL
	 || addl->proto_data.sasdev_phys == NULL)
		return;

	devid_record_size = SVPD_DEVICE_ID_DESC_HDR_LEN
			  + sizeof(struct scsi_vpd_id_naa_ieee_reg);
	for (i = 0; i < addl->proto_hdr.sas->base_hdr.num_phys; i++) {
		uint8_t			       devid_buf[devid_record_size];
		struct scsi_vpd_id_descriptor *devid;
		uint8_t			      *phy_addr;

		devid = (struct scsi_vpd_id_descriptor *)devid_buf;
		phy_addr = addl->proto_data.sasdev_phys[i].phy_addr;
		devid->proto_codeset = (SCSI_PROTO_SAS << SVPD_ID_PROTO_SHIFT)
				     | SVPD_ID_CODESET_BINARY;
		devid->id_type       = SVPD_ID_PIV
				     | SVPD_ID_ASSOC_PORT
				     | SVPD_ID_TYPE_NAA;
		devid->reserved	     = 0;
		devid->length	     = sizeof(struct scsi_vpd_id_naa_ieee_reg);
		memcpy(devid->identifier, phy_addr, devid->length);

		callback(enc, elm, devid, callback_arg);
	}
}

/**
 * Function signature for consumers of the ses_paths_iter() interface.
 */
typedef void ses_path_callback_t(enc_softc_t *, enc_element_t *,
				 struct cam_path *, void *);

/**
 * Argument package passed through ses_devids_iter() by
 * ses_paths_iter() to ses_path_iter_devid_callback().
 */
typedef struct ses_path_iter_args {
	ses_path_callback_t *callback;
	void		    *callback_arg;
} ses_path_iter_args_t;

/**
 * ses_devids_iter() callback function used by ses_paths_iter()
 * to map device ids to peripheral driver instances.
 *
 * \param enc	  SES instance containing elm
 * \param elm	  Element on which device ID matching is active.
 * \param periph  A device ID corresponding to elm.
 * \param arg     Argument passed through to callback on each invocation.
 */
static void
ses_path_iter_devid_callback(enc_softc_t *enc, enc_element_t *elem,
			       struct scsi_vpd_id_descriptor *devid,
			       void *arg)
{
	struct ccb_dev_match         cdm;
	struct dev_match_pattern     match_pattern;
	struct dev_match_result      match_result;
	struct device_match_result  *device_match;
	struct device_match_pattern *device_pattern;
	ses_path_iter_args_t	    *args;

	args = (ses_path_iter_args_t *)arg;
	match_pattern.type = DEV_MATCH_DEVICE;
	device_pattern = &match_pattern.pattern.device_pattern;
	device_pattern->flags = DEV_MATCH_DEVID;
	device_pattern->data.devid_pat.id_len = 
	    offsetof(struct scsi_vpd_id_descriptor, identifier)
	  + devid->length;
	memcpy(device_pattern->data.devid_pat.id, devid,
	       device_pattern->data.devid_pat.id_len);

	memset(&cdm, 0, sizeof(cdm));
	if (xpt_create_path(&cdm.ccb_h.path, /*periph*/NULL,
			     CAM_XPT_PATH_ID,
			     CAM_TARGET_WILDCARD,
			     CAM_LUN_WILDCARD) != CAM_REQ_CMP)
		return;

	cdm.ccb_h.func_code = XPT_DEV_MATCH;
	cdm.num_patterns    = 1;
	cdm.patterns        = &match_pattern;
	cdm.pattern_buf_len = sizeof(match_pattern);
	cdm.match_buf_len   = sizeof(match_result);
	cdm.matches         = &match_result;

	xpt_action((union ccb *)&cdm);
	xpt_free_path(cdm.ccb_h.path);

	if ((cdm.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP
	 || (cdm.status != CAM_DEV_MATCH_LAST
	  && cdm.status != CAM_DEV_MATCH_MORE)
	 || cdm.num_matches == 0)
		return;

	device_match = &match_result.result.device_result;
	if (xpt_create_path(&cdm.ccb_h.path, /*periph*/NULL,
			     device_match->path_id,
			     device_match->target_id,
			     device_match->target_lun) != CAM_REQ_CMP)
		return;

	args->callback(enc, elem, cdm.ccb_h.path, args->callback_arg);

	xpt_free_path(cdm.ccb_h.path);
}

/**
 * \brief Iterate over and find the matching periph objects for the
 *        specified element.
 *
 * \param enc	        SES instance containing elm
 * \param elm	        Element for which to perform periph object matching.
 * \param callback      The callback function to invoke with each matching
 *                      periph object.
 * \param callback_arg  Argument passed through to callback on each invocation.
 */
static void
ses_paths_iter(enc_softc_t *enc, enc_element_t *elm,
	       ses_path_callback_t *callback, void *callback_arg)
{
	ses_path_iter_args_t args;

	args.callback     = callback;
	args.callback_arg = callback_arg;
	ses_devids_iter(enc, elm, ses_path_iter_devid_callback, &args);
}

/**
 * ses_paths_iter() callback function used by ses_get_elmdevname()
 * to record periph driver instance strings corresponding to a SES
 * element.
 *
 * \param enc	  SES instance containing elm
 * \param elm	  Element on which periph matching is active.
 * \param periph  A periph instance that matches elm.
 * \param arg     Argument passed through to callback on each invocation.
 */
static void
ses_elmdevname_callback(enc_softc_t *enc, enc_element_t *elem,
			struct cam_path *path, void *arg)
{
	struct sbuf *sb;

	sb = (struct sbuf *)arg;
	cam_periph_list(path, sb);
}

/**
 * Argument package passed through ses_paths_iter() to
 * ses_getcampath_callback.
 */
typedef struct ses_setphyspath_callback_args {
	struct sbuf *physpath;
	int          num_set;
} ses_setphyspath_callback_args_t;

/**
 * \brief ses_paths_iter() callback to set the physical path on the
 *        CAM EDT entries corresponding to a given SES element.
 *
 * \param enc	  SES instance containing elm
 * \param elm	  Element on which periph matching is active.
 * \param periph  A periph instance that matches elm.
 * \param arg     Argument passed through to callback on each invocation.
 */
static void
ses_setphyspath_callback(enc_softc_t *enc, enc_element_t *elm,
			 struct cam_path *path, void *arg)
{
	struct ccb_dev_advinfo cdai;
	ses_setphyspath_callback_args_t *args;
	char *old_physpath;

	args = (ses_setphyspath_callback_args_t *)arg;
	old_physpath = malloc(MAXPATHLEN, M_SCSIENC, M_WAITOK|M_ZERO);
	cam_periph_lock(enc->periph);
	xpt_setup_ccb(&cdai.ccb_h, path, CAM_PRIORITY_NORMAL);
	cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
	cdai.buftype = CDAI_TYPE_PHYS_PATH;
	cdai.flags = CDAI_FLAG_NONE;
	cdai.bufsiz = MAXPATHLEN;
	cdai.buf = old_physpath;
	xpt_action((union ccb *)&cdai);
	if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
		cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);

	if (strcmp(old_physpath, sbuf_data(args->physpath)) != 0) {

		xpt_setup_ccb(&cdai.ccb_h, path, CAM_PRIORITY_NORMAL);
		cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
		cdai.buftype = CDAI_TYPE_PHYS_PATH;
		cdai.flags = CDAI_FLAG_STORE;
		cdai.bufsiz = sbuf_len(args->physpath);
		cdai.buf = sbuf_data(args->physpath);
		xpt_action((union ccb *)&cdai);
		if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);
		if (cdai.ccb_h.status == CAM_REQ_CMP)
			args->num_set++;
	}
	cam_periph_unlock(enc->periph);
	free(old_physpath, M_SCSIENC);
}

/**
 * \brief Set a device's physical path string in CAM XPT.
 *
 * \param enc	SES instance containing elm
 * \param elm	Element to publish physical path string for
 * \param iter	Iterator whose state corresponds to elm
 *
 * \return	0 on success, errno otherwise.
 */
static int
ses_set_physpath(enc_softc_t *enc, enc_element_t *elm,
		 struct ses_iterator *iter)
{
	struct ccb_dev_advinfo cdai;
	ses_setphyspath_callback_args_t args;
	int i, ret;
	struct sbuf sb;
	struct scsi_vpd_id_descriptor *idd;
	uint8_t *devid;
	ses_element_t *elmpriv;
	const char *c;

	ret = EIO;
	devid = NULL;

	/*
	 * Assemble the components of the physical path starting with
	 * the device ID of the enclosure itself.
	 */
	xpt_setup_ccb(&cdai.ccb_h, enc->periph->path, CAM_PRIORITY_NORMAL);
	cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
	cdai.flags = CDAI_FLAG_NONE;
	cdai.buftype = CDAI_TYPE_SCSI_DEVID;
	cdai.bufsiz = CAM_SCSI_DEVID_MAXLEN;
	cdai.buf = devid = malloc(cdai.bufsiz, M_SCSIENC, M_WAITOK|M_ZERO);
	cam_periph_lock(enc->periph);
	xpt_action((union ccb *)&cdai);
	if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
		cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);
	cam_periph_unlock(enc->periph);
	if (cdai.ccb_h.status != CAM_REQ_CMP)
		goto out;

	idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
	    cdai.provsiz, scsi_devid_is_naa_ieee_reg);
	if (idd == NULL)
		goto out;

	if (sbuf_new(&sb, NULL, 128, SBUF_AUTOEXTEND) == NULL) {
		ret = ENOMEM;
		goto out;
	}
	/* Next, generate the physical path string */
	sbuf_printf(&sb, "id1,enc@n%jx/type@%x/slot@%x",
	    scsi_8btou64(idd->identifier), iter->type_index,
	    iter->type_element_index);
	/* Append the element descriptor if one exists */
	elmpriv = elm->elm_private;
	if (elmpriv->descr != NULL && elmpriv->descr_len > 0) {
		sbuf_cat(&sb, "/elmdesc@");
		for (i = 0, c = elmpriv->descr; i < elmpriv->descr_len;
		    i++, c++) {
			if (!isprint(*c) || isspace(*c) || *c == '/')
				sbuf_putc(&sb, '_');
			else
				sbuf_putc(&sb, *c);
		}
	}
	sbuf_finish(&sb);

	/*
	 * Set this physical path on any CAM devices with a device ID
	 * descriptor that matches one created from the SES additional
	 * status data for this element.
	 */
	args.physpath= &sb;
	args.num_set = 0;
	ses_paths_iter(enc, elm, ses_setphyspath_callback, &args);
	sbuf_delete(&sb);

	ret = args.num_set == 0 ? ENOENT : 0;

out:
	if (devid != NULL)
		ENC_FREE(devid);
	return (ret);
}

/**
 * \brief Helper to set the CDB fields appropriately.
 *
 * \param cdb		Buffer containing the cdb.
 * \param pagenum	SES diagnostic page to query for.
 * \param dir		Direction of query.
 */
static void
ses_page_cdb(char *cdb, int bufsiz, SesDiagPageCodes pagenum, int dir)
{

	/* Ref: SPC-4 r25 Section 6.20 Table 223 */
	if (dir == CAM_DIR_IN) {
		cdb[0] = RECEIVE_DIAGNOSTIC;
		cdb[1] = 1; /* Set page code valid bit */
		cdb[2] = pagenum;
	} else {
		cdb[0] = SEND_DIAGNOSTIC;
		cdb[1] = 0x10;
		cdb[2] = pagenum;
	}
	cdb[3] = bufsiz >> 8;	/* high bits */
	cdb[4] = bufsiz & 0xff;	/* low bits */
	cdb[5] = 0;
}

/**
 * \brief Discover whether this instance supports timed completion of a
 * 	  RECEIVE DIAGNOSTIC RESULTS command requesting the Enclosure Status
 * 	  page, and store the result in the softc, updating if necessary.
 *
 * \param enc	SES instance to query and update.
 * \param tc_en	Value of timed completion to set (see \return).
 *
 * \return	1 if timed completion enabled, 0 otherwise.
 */
static int
ses_set_timed_completion(enc_softc_t *enc, uint8_t tc_en)
{
	union ccb *ccb;
	struct cam_periph *periph;
	struct ses_mgmt_mode_page *mgmt;
	uint8_t *mode_buf;
	size_t mode_buf_len;
	ses_softc_t *ses;

	periph = enc->periph;
	ses = enc->enc_private;
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	mode_buf_len = sizeof(struct ses_mgmt_mode_page);
	mode_buf = ENC_MALLOCZ(mode_buf_len);
	if (mode_buf == NULL)
		goto out;

	scsi_mode_sense(&ccb->csio, /*retries*/4, NULL, MSG_SIMPLE_Q_TAG,
	    /*dbd*/FALSE, SMS_PAGE_CTRL_CURRENT, SES_MGMT_MODE_PAGE_CODE,
	    mode_buf, mode_buf_len, SSD_FULL_SIZE, /*timeout*/60 * 1000);

	/*
	 * Ignore illegal request errors, as they are quite common and we
	 * will print something out in that case anyway.
	 */
	cam_periph_runccb(ccb, enc_error, ENC_CFLAGS,
	    ENC_FLAGS|SF_QUIET_IR, NULL);
	if (ccb->ccb_h.status != CAM_REQ_CMP) {
		ENC_VLOG(enc, "Timed Completion Unsupported\n");
		goto release;
	}

	/* Skip the mode select if the desired value is already set */
	mgmt = (struct ses_mgmt_mode_page *)mode_buf;
	if ((mgmt->byte5 & SES_MGMT_TIMED_COMP_EN) == tc_en)
		goto done;

	/* Value is not what we wanted, set it */
	if (tc_en)
		mgmt->byte5 |= SES_MGMT_TIMED_COMP_EN;
	else
		mgmt->byte5 &= ~SES_MGMT_TIMED_COMP_EN;
	/* SES2r20: a completion time of zero means as long as possible */
	bzero(&mgmt->max_comp_time, sizeof(mgmt->max_comp_time));

	scsi_mode_select(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG,
	    /*page_fmt*/FALSE, /*save_pages*/TRUE, mode_buf, mode_buf_len,
	    SSD_FULL_SIZE, /*timeout*/60 * 1000);

	cam_periph_runccb(ccb, enc_error, ENC_CFLAGS, ENC_FLAGS, NULL);
	if (ccb->ccb_h.status != CAM_REQ_CMP) {
		ENC_VLOG(enc, "Timed Completion Set Failed\n");
		goto release;
	}

done:
	if ((mgmt->byte5 & SES_MGMT_TIMED_COMP_EN) != 0) {
		ENC_LOG(enc, "Timed Completion Enabled\n");
		ses->ses_flags |= SES_FLAG_TIMEDCOMP;
	} else {
		ENC_LOG(enc, "Timed Completion Disabled\n");
		ses->ses_flags &= ~SES_FLAG_TIMEDCOMP;
	}
release:
	ENC_FREE(mode_buf);
	xpt_release_ccb(ccb);
out:
	return (ses->ses_flags & SES_FLAG_TIMEDCOMP);
}

/**
 * \brief Process the list of supported pages and update flags.
 *
 * \param enc       SES device to query.
 * \param buf       Buffer containing the config page.
 * \param xfer_len  Length of the config page in the buffer.
 *
 * \return  0 on success, errno otherwise.
 */
static int
ses_process_pages(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	ses_softc_t *ses;
	struct scsi_diag_page *page;
	int err, i, length;

	CAM_DEBUG(enc->periph->path, CAM_DEBUG_SUBTRACE,
	    ("entering %s(%p, %d)\n", __func__, bufp, xfer_len));
	ses = enc->enc_private;
	err = -1;

	if (error != 0) {
		err = error;
		goto out;
	}
	if (xfer_len < sizeof(*page)) {
		ENC_VLOG(enc, "Unable to parse Diag Pages List Header\n");
		err = EIO;
		goto out;
	}
	page = (struct scsi_diag_page *)*bufp;
	length = scsi_2btoul(page->length);
	if (length + offsetof(struct scsi_diag_page, params) > xfer_len) {
		ENC_VLOG(enc, "Diag Pages List Too Long\n");
		goto out;
	}
	ENC_DLOG(enc, "%s: page length %d, xfer_len %d\n",
		 __func__, length, xfer_len);

	err = 0;
	for (i = 0; i < length; i++) {
		if (page->params[i] == SesElementDescriptor)
			ses->ses_flags |= SES_FLAG_DESC;
		else if (page->params[i] == SesAddlElementStatus)
			ses->ses_flags |= SES_FLAG_ADDLSTATUS;
	}

out:
	ENC_DLOG(enc, "%s: exiting with err %d\n", __func__, err);
	return (err);
}

/**
 * \brief Process the config page and update associated structures.
 *
 * \param enc       SES device to query.
 * \param buf       Buffer containing the config page.
 * \param xfer_len  Length of the config page in the buffer.
 *
 * \return  0 on success, errno otherwise.
 */
static int
ses_process_config(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct ses_iterator iter;
	ses_softc_t *ses;
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;
	uint8_t *buf;
	int length;
	int err;
	int nelm;
	int ntype;
	struct ses_cfg_page *cfg_page;
	struct ses_enc_desc *buf_subenc;
	const struct ses_enc_desc **subencs;
	const struct ses_enc_desc **cur_subenc;
	const struct ses_enc_desc **last_subenc;
	ses_type_t *ses_types;
	ses_type_t *sestype;
	const struct ses_elm_type_desc *cur_buf_type;
	const struct ses_elm_type_desc *last_buf_type;
	uint8_t *last_valid_byte;
	enc_element_t *element;
	const char *type_text;

	CAM_DEBUG(enc->periph->path, CAM_DEBUG_SUBTRACE,
	    ("entering %s(%p, %d)\n", __func__, bufp, xfer_len));
	ses = enc->enc_private;
	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	buf = *bufp;
	err = -1;

	if (error != 0) {
		err = error;
		goto out;
	}
	if (xfer_len < sizeof(cfg_page->hdr)) {
		ENC_VLOG(enc, "Unable to parse SES Config Header\n");
		err = EIO;
		goto out;
	}

	cfg_page = (struct ses_cfg_page *)buf;
	length = ses_page_length(&cfg_page->hdr);
	if (length > xfer_len) {
		ENC_VLOG(enc, "Enclosure Config Page Too Long\n");
		goto out;
	}
	last_valid_byte = &buf[length - 1];

	ENC_DLOG(enc, "%s: total page length %d, xfer_len %d\n",
		 __func__, length, xfer_len);

	err = 0;
	if (ses_config_cache_valid(ses_cache, cfg_page->hdr.gen_code)) {

		/* Our cache is still valid.  Proceed to fetching status. */
		goto out;
	}

	/* Cache is no longer valid.  Free old data to make way for new. */
	ses_cache_free(enc, enc_cache);
	ENC_VLOG(enc, "Generation Code 0x%x has %d SubEnclosures\n",
	    scsi_4btoul(cfg_page->hdr.gen_code),
	    ses_cfg_page_get_num_subenc(cfg_page));

	/* Take ownership of the buffer. */
	ses_cache->cfg_page = cfg_page;
	*bufp = NULL;

	/*
	 * Now waltz through all the subenclosures summing the number of
	 * types available in each.
	 */
	subencs = malloc(ses_cfg_page_get_num_subenc(cfg_page)
	    * sizeof(*subencs), M_SCSIENC, M_WAITOK|M_ZERO);
	/*
	 * Sub-enclosure data is const after construction (i.e. when
	 * accessed via our cache object.
	 *
	 * The cast here is not required in C++ but C99 is not so
	 * sophisticated (see C99 6.5.16.1(1)).
	 */
	ses_cache->ses_nsubencs = ses_cfg_page_get_num_subenc(cfg_page);
	ses_cache->subencs = subencs;

	buf_subenc = cfg_page->subencs;
	cur_subenc = subencs;
	last_subenc = &subencs[ses_cache->ses_nsubencs - 1];
	ntype = 0;
	while (cur_subenc <= last_subenc) {

		if (!ses_enc_desc_is_complete(buf_subenc, last_valid_byte)) {
			ENC_VLOG(enc, "Enclosure %d Beyond End of "
			    "Descriptors\n", cur_subenc - subencs);
			err = EIO;
			goto out;
		}

		ENC_VLOG(enc, " SubEnclosure ID %d, %d Types With this ID, "
		    "Descriptor Length %d, offset %d\n", buf_subenc->subenc_id,
		    buf_subenc->num_types, buf_subenc->length,
		    &buf_subenc->byte0 - buf);
		ENC_VLOG(enc, "WWN: %jx\n",
		    (uintmax_t)scsi_8btou64(buf_subenc->logical_id));

		ntype += buf_subenc->num_types;
		*cur_subenc = buf_subenc;
		cur_subenc++;
		buf_subenc = ses_enc_desc_next(buf_subenc);
	}

	/* Process the type headers. */
	ses_types = malloc(ntype * sizeof(*ses_types),
	    M_SCSIENC, M_WAITOK|M_ZERO);
	/*
	 * Type data is const after construction (i.e. when accessed via
	 * our cache object.
	 */
	ses_cache->ses_ntypes = ntype;
	ses_cache->ses_types = ses_types;

	cur_buf_type = (const struct ses_elm_type_desc *)
	    (&(*last_subenc)->length + (*last_subenc)->length + 1);
	last_buf_type = cur_buf_type + ntype - 1;
	type_text = (const uint8_t *)(last_buf_type + 1);
	nelm = 0;
	sestype = ses_types;
	while (cur_buf_type <= last_buf_type) {
		if (&cur_buf_type->etype_txt_len > last_valid_byte) {
			ENC_VLOG(enc, "Runt Enclosure Type Header %d\n",
			    sestype - ses_types);
			err = EIO;
			goto out;
		}
		sestype->hdr  = cur_buf_type;
		sestype->text = type_text;
		type_text += cur_buf_type->etype_txt_len;
		ENC_VLOG(enc, " Type Desc[%d]: Type 0x%x, MaxElt %d, In Subenc "
		    "%d, Text Length %d: %.*s\n", sestype - ses_types,
		    sestype->hdr->etype_elm_type, sestype->hdr->etype_maxelt,
		    sestype->hdr->etype_subenc, sestype->hdr->etype_txt_len,
		    sestype->hdr->etype_txt_len, sestype->text);

		nelm += sestype->hdr->etype_maxelt
		      + /*overall status element*/1;
		sestype++;
		cur_buf_type++;
	}

	/* Create the object map. */
	enc_cache->elm_map = malloc(nelm * sizeof(enc_element_t),
	    M_SCSIENC, M_WAITOK|M_ZERO);
	enc_cache->nelms = nelm;

	ses_iter_init(enc, enc_cache, &iter);
	while ((element = ses_iter_next(&iter)) != NULL) {
		const struct ses_elm_type_desc *thdr;

		ENC_DLOG(enc, "%s: checking obj %d(%d,%d)\n", __func__,
		    iter.global_element_index, iter.type_index, nelm,
		    iter.type_element_index);
		thdr = ses_cache->ses_types[iter.type_index].hdr;
		element->subenclosure = thdr->etype_subenc;
		element->enctype = thdr->etype_elm_type;
		element->overall_status_elem = iter.type_element_index == 0;
		element->elm_private = malloc(sizeof(ses_element_t),
		    M_SCSIENC, M_WAITOK|M_ZERO);
		ENC_DLOG(enc, "%s: creating elmpriv %d(%d,%d) subenc %d "
		    "type 0x%x\n", __func__, iter.global_element_index,
		    iter.type_index, iter.type_element_index,
		    thdr->etype_subenc, thdr->etype_elm_type);
	}

	err = 0;

out:
	if (err)
		ses_cache_free(enc, enc_cache);
	else {
		ses_poll_status(enc);
		enc_update_request(enc, SES_PUBLISH_CACHE);
	}
	ENC_DLOG(enc, "%s: exiting with err %d\n", __func__, err);
	return (err);
}

/**
 * \brief Update the status page and associated structures.
 * 
 * \param enc   SES softc to update for.
 * \param buf   Buffer containing the status page.
 * \param bufsz	Amount of data in the buffer.
 *
 * \return	0 on success, errno otherwise.
 */
static int
ses_process_status(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct ses_iterator iter;
	enc_element_t *element;
	ses_softc_t *ses;
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;
	uint8_t *buf;
	int err = -1;
	int length;
	struct ses_status_page *page;
	union ses_status_element *cur_stat;
	union ses_status_element *last_stat;

	ses = enc->enc_private;
	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	buf = *bufp;

	ENC_DLOG(enc, "%s: enter (%p, %p, %d)\n", __func__, enc, buf, xfer_len);
	page = (struct ses_status_page *)buf;
	length = ses_page_length(&page->hdr);

	if (error != 0) {
		err = error;
		goto out;
	}
	/*
	 * Make sure the length fits in the buffer.
	 *
	 * XXX all this means is that the page is larger than the space
	 * we allocated.  Since we use a statically sized buffer, this
	 * could happen... Need to use dynamic discovery of the size.
	 */
	if (length > xfer_len) {
		ENC_VLOG(enc, "Enclosure Status Page Too Long\n");
		goto out;
	}

	/* Check for simple enclosure reporting short enclosure status. */
	if (length >= 4 && page->hdr.page_code == SesShortStatus) {
		ENC_DLOG(enc, "Got Short Enclosure Status page\n");
		ses->ses_flags &= ~(SES_FLAG_ADDLSTATUS | SES_FLAG_DESC);
		ses_cache_free(enc, enc_cache);
		enc_cache->enc_status = page->hdr.page_specific_flags;
		enc_update_request(enc, SES_PUBLISH_CACHE);
		err = 0;
		goto out;
	}

	/* Make sure the length contains at least one header and status */
	if (length < (sizeof(*page) + sizeof(*page->elements))) {
		ENC_VLOG(enc, "Enclosure Status Page Too Short\n");
		goto out;
	}

	if (!ses_config_cache_valid(ses_cache, page->hdr.gen_code)) {
		ENC_DLOG(enc, "%s: Generation count change detected\n",
		    __func__);
		enc_update_request(enc, SES_UPDATE_GETCONFIG);
		goto out;
	}

	ses_cache_free_status(enc, enc_cache);
	ses_cache->status_page = page;
	*bufp = NULL;

	enc_cache->enc_status = page->hdr.page_specific_flags;

	/*
	 * Read in individual element status.  The element order
	 * matches the order reported in the config page (i.e. the
	 * order of an unfiltered iteration of the config objects)..
	 */
	ses_iter_init(enc, enc_cache, &iter);
	cur_stat  = page->elements;
	last_stat = (union ses_status_element *)
	    &buf[length - sizeof(*last_stat)];
	ENC_DLOG(enc, "%s: total page length %d, xfer_len %d\n",
		__func__, length, xfer_len);
	while (cur_stat <= last_stat
	    && (element = ses_iter_next(&iter)) != NULL) {

		ENC_DLOG(enc, "%s: obj %d(%d,%d) off=0x%tx status=%jx\n",
		    __func__, iter.global_element_index, iter.type_index,
		    iter.type_element_index, (uint8_t *)cur_stat - buf,
		    scsi_4btoul(cur_stat->bytes));

		memcpy(&element->encstat, cur_stat, sizeof(element->encstat));
		element->svalid = 1;
		cur_stat++;
	}

	if (ses_iter_next(&iter) != NULL) {
		ENC_VLOG(enc, "Status page, length insufficient for "
			"expected number of objects\n");
	} else {
		if (cur_stat <= last_stat)
			ENC_VLOG(enc, "Status page, exhausted objects before "
				"exhausing page\n");
		enc_update_request(enc, SES_PUBLISH_CACHE);
		err = 0;
	}
out:
	ENC_DLOG(enc, "%s: exiting with error %d\n", __func__, err);
	return (err);
}

typedef enum {
	/**
	 * The enclosure should not provide additional element
	 * status for this element type in page 0x0A.
	 *
	 * \note  This status is returned for any types not
	 *        listed SES3r02.  Further types added in a
	 *        future specification will be incorrectly
	 *        classified.
	 */
	TYPE_ADDLSTATUS_NONE,

	/**
	 * The element type provides additional element status
	 * in page 0x0A.
	 */
	TYPE_ADDLSTATUS_MANDATORY,

	/**
	 * The element type may provide additional element status
	 * in page 0x0A, but i
	 */
	TYPE_ADDLSTATUS_OPTIONAL
} ses_addlstatus_avail_t;

/**
 * \brief Check to see whether a given type (as obtained via type headers) is
 *	  supported by the additional status command.
 *
 * \param enc     SES softc to check.
 * \param typidx  Type index to check for.
 *
 * \return  An enumeration indicating if additional status is mandatory,
 *          optional, or not required for this type.
 */
static ses_addlstatus_avail_t
ses_typehasaddlstatus(enc_softc_t *enc, uint8_t typidx)
{
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;

	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	switch(ses_cache->ses_types[typidx].hdr->etype_elm_type) {
	case ELMTYP_DEVICE:
	case ELMTYP_ARRAY_DEV:
	case ELMTYP_SAS_EXP:
		return (TYPE_ADDLSTATUS_MANDATORY);
	case ELMTYP_SCSI_INI:
	case ELMTYP_SCSI_TGT:
	case ELMTYP_ESCC:
		return (TYPE_ADDLSTATUS_OPTIONAL);
	default:
		/* No additional status information available. */
		break;
	}
	return (TYPE_ADDLSTATUS_NONE);
}

static int ses_get_elm_addlstatus_fc(enc_softc_t *, enc_cache_t *,
				     uint8_t *, int);
static int ses_get_elm_addlstatus_sas(enc_softc_t *, enc_cache_t *, uint8_t *,
				      int, int, int, int);

/**
 * \brief Parse the additional status element data for each object.
 *
 * \param enc       The SES softc to update.
 * \param buf       The buffer containing the additional status
 *                  element response.
 * \param xfer_len  Size of the buffer.
 *
 * \return  0 on success, errno otherwise.
 */
static int
ses_process_elm_addlstatus(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct ses_iterator iter, titer;
	int eip;
	int err;
	int ignore_index = 0;
	int length;
	int offset;
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;
	uint8_t *buf;
	ses_element_t *elmpriv;
	const struct ses_page_hdr *hdr;
	enc_element_t *element, *telement;

	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	buf = *bufp;
	err = -1;

	if (error != 0) {
		err = error;
		goto out;
	}
	ses_cache_free_elm_addlstatus(enc, enc_cache);
	ses_cache->elm_addlstatus_page =
	    (struct ses_addl_elem_status_page *)buf;
	*bufp = NULL;

	/*
	 * The objects appear in the same order here as in Enclosure Status,
	 * which itself is ordered by the Type Descriptors from the Config
	 * page.  However, it is necessary to skip elements that are not
	 * supported by this page when counting them.
	 */
	hdr = &ses_cache->elm_addlstatus_page->hdr;
	length = ses_page_length(hdr);
	ENC_DLOG(enc, "Additional Element Status Page Length 0x%x\n", length);
	/* Make sure the length includes at least one header. */
	if (length < sizeof(*hdr)+sizeof(struct ses_elm_addlstatus_base_hdr)) {
		ENC_VLOG(enc, "Runt Additional Element Status Page\n");
		goto out;
	}
	if (length > xfer_len) {
		ENC_VLOG(enc, "Additional Element Status Page Too Long\n");
		goto out;
	}

	if (!ses_config_cache_valid(ses_cache, hdr->gen_code)) {
		ENC_DLOG(enc, "%s: Generation count change detected\n",
		    __func__);
		enc_update_request(enc, SES_UPDATE_GETCONFIG);
		goto out;
	}

	offset = sizeof(struct ses_page_hdr);
	ses_iter_init(enc, enc_cache, &iter);
	while (offset < length
	    && (element = ses_iter_next(&iter)) != NULL) {
		struct ses_elm_addlstatus_base_hdr *elm_hdr;
		int proto_info_len;
		ses_addlstatus_avail_t status_type;

		/*
		 * Additional element status is only provided for
		 * individual elements (i.e. overal status elements
		 * are excluded) and those of the types specified
		 * in the SES spec.
		 */
		status_type = ses_typehasaddlstatus(enc, iter.type_index);
		if (iter.individual_element_index == ITERATOR_INDEX_INVALID
		 || status_type == TYPE_ADDLSTATUS_NONE)
			continue;

		elm_hdr = (struct ses_elm_addlstatus_base_hdr *)&buf[offset];
		eip = ses_elm_addlstatus_eip(elm_hdr);
		if (eip && !ignore_index) {
			struct ses_elm_addlstatus_eip_hdr *eip_hdr;
			int expected_index, index;
			ses_elem_index_type_t index_type;

			eip_hdr = (struct ses_elm_addlstatus_eip_hdr *)elm_hdr;
			if (eip_hdr->byte2 & SES_ADDL_EIP_EIIOE) {
				index_type = SES_ELEM_INDEX_GLOBAL;
				expected_index = iter.global_element_index;
			} else {
				index_type = SES_ELEM_INDEX_INDIVIDUAL;
				expected_index = iter.individual_element_index;
			}
			titer = iter;
			telement = ses_iter_seek_to(&titer,
			    eip_hdr->element_index, index_type);
			if (telement != NULL &&
			    (ses_typehasaddlstatus(enc, titer.type_index) !=
			     TYPE_ADDLSTATUS_NONE ||
			     titer.type_index > ELMTYP_SAS_CONN)) {
				iter = titer;
				element = telement;
			} else
				ignore_index = 1;

			if (eip_hdr->byte2 & SES_ADDL_EIP_EIIOE)
				index = iter.global_element_index;
			else
				index = iter.individual_element_index;
			if (index > expected_index
			 && status_type == TYPE_ADDLSTATUS_MANDATORY) {
				ENC_VLOG(enc, "%s: provided %s element"
					"index %d skips mandatory status "
					" element at index %d\n",
					__func__, (eip_hdr->byte2 &
					SES_ADDL_EIP_EIIOE) ? "global " : "",
					index, expected_index);
			}
		}
		elmpriv = element->elm_private;
		elmpriv->addl.hdr = elm_hdr;
		ENC_DLOG(enc, "%s: global element index=%d, type index=%d "
		    "type element index=%d, offset=0x%x, "
		    "byte0=0x%x, length=0x%x\n", __func__,
		    iter.global_element_index, iter.type_index,
		    iter.type_element_index, offset, elmpriv->addl.hdr->byte0,
		    elmpriv->addl.hdr->length);

		/* Skip to after the length field */
		offset += sizeof(struct ses_elm_addlstatus_base_hdr);

		/* Make sure the descriptor is within bounds */
		if ((offset + elmpriv->addl.hdr->length) > length) {
			ENC_VLOG(enc, "Element %d Beyond End "
			    "of Additional Element Status Descriptors\n",
			    iter.global_element_index);
			break;
		}

		/* Advance to the protocol data, skipping eip bytes if needed */
		offset += (eip * SES_EIP_HDR_EXTRA_LEN);
		proto_info_len = elmpriv->addl.hdr->length
			       - (eip * SES_EIP_HDR_EXTRA_LEN);

		/* Errors in this block are ignored as they are non-fatal */
		switch(ses_elm_addlstatus_proto(elmpriv->addl.hdr)) {
		case SPSP_PROTO_FC:
			if (elmpriv->addl.hdr->length == 0)
				break;
			ses_get_elm_addlstatus_fc(enc, enc_cache,
						  &buf[offset], proto_info_len);
			break;
		case SPSP_PROTO_SAS:
			if (elmpriv->addl.hdr->length <= 2)
				break;
			ses_get_elm_addlstatus_sas(enc, enc_cache,
						   &buf[offset],
						   proto_info_len,
						   eip, iter.type_index,
						   iter.global_element_index);
			break;
		default:
			ENC_VLOG(enc, "Element %d: Unknown Additional Element "
			    "Protocol 0x%x\n", iter.global_element_index,
			    ses_elm_addlstatus_proto(elmpriv->addl.hdr));
			break;
		}

		offset += proto_info_len;
	}
	err = 0;
out:
	if (err)
		ses_cache_free_elm_addlstatus(enc, enc_cache);
	enc_update_request(enc, SES_PUBLISH_PHYSPATHS);
	enc_update_request(enc, SES_PUBLISH_CACHE);
	return (err);
}

static int
ses_process_control_request(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	ses_softc_t *ses;

	ses = enc->enc_private;
	/*
	 * Possible errors:
	 *  o Generation count wrong.
	 *  o Some SCSI status error.
	 */
	ses_terminate_control_requests(&ses->ses_pending_requests, error);
	ses_poll_status(enc);
	return (0);
}

static int
ses_publish_physpaths(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	struct ses_iterator iter;
	enc_cache_t *enc_cache;
	enc_element_t *element;

	enc_cache = &enc->enc_daemon_cache;

	ses_iter_init(enc, enc_cache, &iter);
	while ((element = ses_iter_next(&iter)) != NULL) {
		/*
		 * ses_set_physpath() returns success if we changed
		 * the physpath of any element.  This allows us to
		 * only announce devices once regardless of how
		 * many times we process additional element status.
		 */
		if (ses_set_physpath(enc, element, &iter) == 0)
			ses_print_addl_data(enc, element);
	}

	return (0);
}

static int
ses_publish_cache(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{

	sx_xlock(&enc->enc_cache_lock);
	ses_cache_clone(enc, /*src*/&enc->enc_daemon_cache,
			/*dst*/&enc->enc_cache);
	sx_xunlock(&enc->enc_cache_lock);

	return (0);
}

/**
 * \brief Parse the descriptors for each object.
 *
 * \param enc       The SES softc to update.
 * \param buf       The buffer containing the descriptor list response.
 * \param xfer_len  Size of the buffer.
 * 
 * \return	0 on success, errno otherwise.
 */
static int
ses_process_elm_descs(enc_softc_t *enc, struct enc_fsm_state *state,
    union ccb *ccb, uint8_t **bufp, int error, int xfer_len)
{
	ses_softc_t *ses;
	struct ses_iterator iter;
	enc_element_t *element;
	int err;
	int offset;
	u_long length, plength;
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;
	uint8_t *buf;
	ses_element_t *elmpriv;
	const struct ses_page_hdr *phdr;
	const struct ses_elm_desc_hdr *hdr;

	ses = enc->enc_private;
	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	buf = *bufp;
	err = -1;

	if (error != 0) {
		err = error;
		goto out;
	}
	ses_cache_free_elm_descs(enc, enc_cache);
	ses_cache->elm_descs_page = (struct ses_elem_descr_page *)buf;
	*bufp = NULL;

	phdr = &ses_cache->elm_descs_page->hdr;
	plength = ses_page_length(phdr);
	if (xfer_len < sizeof(struct ses_page_hdr)) {
		ENC_VLOG(enc, "Runt Element Descriptor Page\n");
		goto out;
	}
	if (plength > xfer_len) {
		ENC_VLOG(enc, "Element Descriptor Page Too Long\n");
		goto out;
	}

	if (!ses_config_cache_valid(ses_cache, phdr->gen_code)) {
		ENC_VLOG(enc, "%s: Generation count change detected\n",
		    __func__);
		enc_update_request(enc, SES_UPDATE_GETCONFIG);
		goto out;
	}

	offset = sizeof(struct ses_page_hdr);

	ses_iter_init(enc, enc_cache, &iter);
	while (offset < plength
	    && (element = ses_iter_next(&iter)) != NULL) {

		if ((offset + sizeof(struct ses_elm_desc_hdr)) > plength) {
			ENC_VLOG(enc, "Element %d Descriptor Header Past "
			    "End of Buffer\n", iter.global_element_index);
			goto out;
		}
		hdr = (struct ses_elm_desc_hdr *)&buf[offset];
		length = scsi_2btoul(hdr->length);
		ENC_DLOG(enc, "%s: obj %d(%d,%d) length=%d off=%d\n", __func__,
		    iter.global_element_index, iter.type_index,
		    iter.type_element_index, length, offset);
		if ((offset + sizeof(*hdr) + length) > plength) {
			ENC_VLOG(enc, "Element%d Descriptor Past "
			    "End of Buffer\n", iter.global_element_index);
			goto out;
		}
		offset += sizeof(*hdr);

		if (length > 0) {
			elmpriv = element->elm_private;
			elmpriv->descr_len = length;
			elmpriv->descr = &buf[offset];
		}

		/* skip over the descriptor itself */
		offset += length;
	}

	err = 0;
out:
	if (err == 0) {
		if (ses->ses_flags & SES_FLAG_ADDLSTATUS)
			enc_update_request(enc, SES_UPDATE_GETELMADDLSTATUS);
	}
	enc_update_request(enc, SES_PUBLISH_CACHE);
	return (err);
}

static int
ses_fill_rcv_diag_io(enc_softc_t *enc, struct enc_fsm_state *state,
		       union ccb *ccb, uint8_t *buf)
{

	if (enc->enc_type == ENC_SEMB_SES) {
		semb_receive_diagnostic_results(&ccb->ataio, /*retries*/5,
					NULL, MSG_SIMPLE_Q_TAG, /*pcv*/1,
					state->page_code, buf, state->buf_size,
					state->timeout);
	} else {
		scsi_receive_diagnostic_results(&ccb->csio, /*retries*/5,
					NULL, MSG_SIMPLE_Q_TAG, /*pcv*/1,
					state->page_code, buf, state->buf_size,
					SSD_FULL_SIZE, state->timeout);
	}
	return (0);
}

/**
 * \brief Encode the object status into the response buffer, which is
 *	  expected to contain the current enclosure status.  This function
 *	  turns off all the 'select' bits for the objects except for the
 *	  object specified, then sends it back to the enclosure.
 *
 * \param enc	SES enclosure the change is being applied to.
 * \param buf	Buffer containing the current enclosure status response.
 * \param amt	Length of the response in the buffer.
 * \param req	The control request to be applied to buf.
 *
 * \return	0 on success, errno otherwise.
 */
static int
ses_encode(enc_softc_t *enc, uint8_t *buf, int amt, ses_control_request_t *req)
{
	struct ses_iterator iter;
	enc_element_t *element;
	int offset;
	struct ses_control_page_hdr *hdr;

	ses_iter_init(enc, &enc->enc_cache, &iter);
	hdr = (struct ses_control_page_hdr *)buf;
	if (req->elm_idx == -1) {
		/* for enclosure status, at least 2 bytes are needed */
		if (amt < 2)
			return EIO;
		hdr->control_flags =
		    req->elm_stat.comstatus & SES_SET_STATUS_MASK;
		ENC_DLOG(enc, "Set EncStat %x\n", hdr->control_flags);
		return (0);
	}

	element = ses_iter_seek_to(&iter, req->elm_idx, SES_ELEM_INDEX_GLOBAL);
	if (element == NULL)
		return (ENXIO);

	/*
	 * Seek to the type set that corresponds to the requested object.
	 * The +1 is for the overall status element for the type.
	 */
	offset = sizeof(struct ses_control_page_hdr)
	       + (iter.global_element_index * sizeof(struct ses_comstat));

	/* Check for buffer overflow. */
	if (offset + sizeof(struct ses_comstat) > amt)
		return (EIO);

	/* Set the status. */
	memcpy(&buf[offset], &req->elm_stat, sizeof(struct ses_comstat));

	ENC_DLOG(enc, "Set Type 0x%x Obj 0x%x (offset %d) with %x %x %x %x\n",
	    iter.type_index, iter.global_element_index, offset,
	    req->elm_stat.comstatus, req->elm_stat.comstat[0],
	    req->elm_stat.comstat[1], req->elm_stat.comstat[2]);

	return (0);
}

static int
ses_fill_control_request(enc_softc_t *enc, struct enc_fsm_state *state,
			 union ccb *ccb, uint8_t *buf)
{
	ses_softc_t			*ses;
	enc_cache_t			*enc_cache;
	ses_cache_t			*ses_cache;
	struct ses_control_page_hdr	*hdr;
	ses_control_request_t		*req;
	size_t				 plength;
	size_t				 offset;

	ses = enc->enc_private;
	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;
	hdr = (struct ses_control_page_hdr *)buf;
	
	if (ses_cache->status_page == NULL) {
		ses_terminate_control_requests(&ses->ses_requests, EIO);
		return (EIO);
	}

	plength = ses_page_length(&ses_cache->status_page->hdr);
	memcpy(buf, ses_cache->status_page, plength);

	/* Disable the select bits in all status entries.  */
	offset = sizeof(struct ses_control_page_hdr);
	for (offset = sizeof(struct ses_control_page_hdr);
	     offset < plength; offset += sizeof(struct ses_comstat)) {
		buf[offset] &= ~SESCTL_CSEL;
	}

	/* And make sure the INVOP bit is clear.  */
	hdr->control_flags &= ~SES_ENCSTAT_INVOP;

	/* Apply incoming requests. */
	while ((req = TAILQ_FIRST(&ses->ses_requests)) != NULL) {

		TAILQ_REMOVE(&ses->ses_requests, req, links);
		req->result = ses_encode(enc, buf, plength, req);
		if (req->result != 0) {
			wakeup(req);
			continue;
		}
		TAILQ_INSERT_TAIL(&ses->ses_pending_requests, req, links);
	}

	if (TAILQ_EMPTY(&ses->ses_pending_requests) != 0)
		return (ENOENT);

	/* Fill out the ccb */
	if (enc->enc_type == ENC_SEMB_SES) {
		semb_send_diagnostic(&ccb->ataio, /*retries*/5, NULL,
			     MSG_SIMPLE_Q_TAG,
			     buf, ses_page_length(&ses_cache->status_page->hdr),
			     state->timeout);
	} else {
		scsi_send_diagnostic(&ccb->csio, /*retries*/5, NULL,
			     MSG_SIMPLE_Q_TAG, /*unit_offline*/0,
			     /*device_offline*/0, /*self_test*/0,
			     /*page_format*/1, /*self_test_code*/0,
			     buf, ses_page_length(&ses_cache->status_page->hdr),
			     SSD_FULL_SIZE, state->timeout);
	}
	return (0);
}

static int
ses_get_elm_addlstatus_fc(enc_softc_t *enc, enc_cache_t *enc_cache,
			  uint8_t *buf, int bufsiz)
{
	ENC_VLOG(enc, "FC Device Support Stubbed in Additional Status Page\n");
	return (ENODEV);
}

#define	SES_PRINT_PORTS(p, type) do {					\
	sbuf_printf(sbp, " %s(", type);					\
	if (((p) & SES_SASOBJ_DEV_PHY_PROTOMASK) == 0)			\
		sbuf_printf(sbp, " None");				\
	else {								\
		if ((p) & SES_SASOBJ_DEV_PHY_SMP)			\
			sbuf_printf(sbp, " SMP");			\
		if ((p) & SES_SASOBJ_DEV_PHY_STP)			\
			sbuf_printf(sbp, " STP");			\
		if ((p) & SES_SASOBJ_DEV_PHY_SSP)			\
			sbuf_printf(sbp, " SSP");			\
	}								\
	sbuf_printf(sbp, " )");						\
} while(0)

/**
 * \brief Print the additional element status data for this object, for SAS
 * 	  type 0 objects.  See SES2 r20 Section 6.1.13.3.2.
 *
 * \param sesname	SES device name associated with the object.
 * \param sbp		Sbuf to print to.
 * \param obj		The object to print the data for.
 * \param periph_name	Peripheral string associated with the object.
 */
static void
ses_print_addl_data_sas_type0(char *sesname, struct sbuf *sbp,
			      enc_element_t *obj, char *periph_name)
{
	int i;
	ses_element_t *elmpriv;
	struct ses_addl_status *addl;
	struct ses_elm_sas_device_phy *phy;

	elmpriv = obj->elm_private;
	addl = &(elmpriv->addl);
	if (addl->proto_hdr.sas == NULL)
		return;
	sbuf_printf(sbp, "%s: %s: SAS Device Slot Element:",
	    sesname, periph_name);
	sbuf_printf(sbp, " %d Phys", addl->proto_hdr.sas->base_hdr.num_phys);
	if (ses_elm_addlstatus_eip(addl->hdr))
		sbuf_printf(sbp, " at Slot %d",
		    addl->proto_hdr.sas->type0_eip.dev_slot_num);
	if (ses_elm_sas_type0_not_all_phys(addl->proto_hdr.sas))
		sbuf_printf(sbp, ", Not All Phys");
	sbuf_printf(sbp, "\n");
	if (addl->proto_data.sasdev_phys == NULL)
		return;
	for (i = 0;i < addl->proto_hdr.sas->base_hdr.num_phys;i++) {
		phy = &addl->proto_data.sasdev_phys[i];
		sbuf_printf(sbp, "%s:  phy %d:", sesname, i);
		if (ses_elm_sas_dev_phy_sata_dev(phy))
			/* Spec says all other fields are specific values */
			sbuf_printf(sbp, " SATA device\n");
		else {
			sbuf_printf(sbp, " SAS device type %d id %d\n",
			    ses_elm_sas_dev_phy_dev_type(phy), phy->phy_id);
			sbuf_printf(sbp, "%s:  phy %d: protocols:", sesname, i);
			SES_PRINT_PORTS(phy->initiator_ports, "Initiator");
			SES_PRINT_PORTS(phy->target_ports, "Target");
			sbuf_printf(sbp, "\n");
		}
		sbuf_printf(sbp, "%s:  phy %d: parent %jx addr %jx\n",
		    sesname, i,
		    (uintmax_t)scsi_8btou64(phy->parent_addr),
		    (uintmax_t)scsi_8btou64(phy->phy_addr));
	}
}
#undef SES_PRINT_PORTS

/**
 * \brief Report whether a given enclosure object is an expander.
 *
 * \param enc	SES softc associated with object.
 * \param obj	Enclosure object to report for.
 *
 * \return	1 if true, 0 otherwise.
 */
static int
ses_obj_is_expander(enc_softc_t *enc, enc_element_t *obj)
{
	return (obj->enctype == ELMTYP_SAS_EXP);
}

/**
 * \brief Print the additional element status data for this object, for SAS
 *	  type 1 objects.  See SES2 r20 Sections 6.1.13.3.3 and 6.1.13.3.4.
 *
 * \param enc		SES enclosure, needed for type identification.
 * \param sesname	SES device name associated with the object.
 * \param sbp		Sbuf to print to.
 * \param obj		The object to print the data for.
 * \param periph_name	Peripheral string associated with the object.
 */
static void
ses_print_addl_data_sas_type1(enc_softc_t *enc, char *sesname,
    struct sbuf *sbp, enc_element_t *obj, char *periph_name)
{
	int i, num_phys;
	ses_element_t *elmpriv;
	struct ses_addl_status *addl;
	struct ses_elm_sas_expander_phy *exp_phy;
	struct ses_elm_sas_port_phy *port_phy;

	elmpriv = obj->elm_private;
	addl = &(elmpriv->addl);
	if (addl->proto_hdr.sas == NULL)
		return;
	sbuf_printf(sbp, "%s: %s: SAS ", sesname, periph_name);
	if (ses_obj_is_expander(enc, obj)) {
		num_phys = addl->proto_hdr.sas->base_hdr.num_phys;
		sbuf_printf(sbp, "Expander: %d Phys", num_phys);
		if (addl->proto_data.sasexp_phys == NULL)
			return;
		for (i = 0;i < num_phys;i++) {
			exp_phy = &addl->proto_data.sasexp_phys[i];
			sbuf_printf(sbp, "%s:  phy %d: connector %d other %d\n",
			    sesname, i, exp_phy->connector_index,
			    exp_phy->other_index);
		}
	} else {
		num_phys = addl->proto_hdr.sas->base_hdr.num_phys;
		sbuf_printf(sbp, "Port: %d Phys", num_phys);
		if (addl->proto_data.sasport_phys == NULL)
			return;
		for (i = 0;i < num_phys;i++) {
			port_phy = &addl->proto_data.sasport_phys[i];
			sbuf_printf(sbp,
			    "%s:  phy %d: id %d connector %d other %d\n",
			    sesname, i, port_phy->phy_id,
			    port_phy->connector_index, port_phy->other_index);
			sbuf_printf(sbp, "%s:  phy %d: addr %jx\n", sesname, i,
			    (uintmax_t)scsi_8btou64(port_phy->phy_addr));
		}
	}
}

/**
 * \brief Print the additional element status data for this object.
 *
 * \param enc		SES softc associated with the object.
 * \param obj		The object to print the data for.
 */
static void
ses_print_addl_data(enc_softc_t *enc, enc_element_t *obj)
{
	ses_element_t *elmpriv;
	struct ses_addl_status *addl;
	struct sbuf sesname, name, out;

	elmpriv = obj->elm_private;
	if (elmpriv == NULL)
		return;

	addl = &(elmpriv->addl);
	if (addl->hdr == NULL)
		return;

	sbuf_new(&sesname, NULL, 16, SBUF_AUTOEXTEND);
	sbuf_new(&name, NULL, 16, SBUF_AUTOEXTEND);
	sbuf_new(&out, NULL, 512, SBUF_AUTOEXTEND);
	ses_paths_iter(enc, obj, ses_elmdevname_callback, &name);
	if (sbuf_len(&name) == 0)
		sbuf_printf(&name, "(none)");
	sbuf_finish(&name);
	sbuf_printf(&sesname, "%s%d", enc->periph->periph_name,
	    enc->periph->unit_number);
	sbuf_finish(&sesname);
	if (elmpriv->descr != NULL)
		sbuf_printf(&out, "%s: %s: Element descriptor: '%s'\n",
		    sbuf_data(&sesname), sbuf_data(&name), elmpriv->descr);
	switch(ses_elm_addlstatus_proto(addl->hdr)) {
	case SPSP_PROTO_SAS:
		switch(ses_elm_sas_descr_type(addl->proto_hdr.sas)) {
		case SES_SASOBJ_TYPE_SLOT:
			ses_print_addl_data_sas_type0(sbuf_data(&sesname),
			    &out, obj, sbuf_data(&name));
			break;
		case SES_SASOBJ_TYPE_OTHER:
			ses_print_addl_data_sas_type1(enc, sbuf_data(&sesname),
			    &out, obj, sbuf_data(&name));
			break;
		default:
			break;
		}
		break;
	case SPSP_PROTO_FC:	/* stubbed for now */
		break;
	default:
		break;
	}
	sbuf_finish(&out);
	printf("%s", sbuf_data(&out));
	sbuf_delete(&out);
	sbuf_delete(&name);
	sbuf_delete(&sesname);
}

/**
 * \brief Update the softc with the additional element status data for this
 * 	  object, for SAS type 0 objects.
 *
 * \param enc		SES softc to be updated.
 * \param buf		The additional element status response buffer.
 * \param bufsiz	Size of the response buffer.
 * \param eip		The EIP bit value.
 * \param nobj		Number of objects attached to the SES softc.
 * 
 * \return		0 on success, errno otherwise.
 */
static int
ses_get_elm_addlstatus_sas_type0(enc_softc_t *enc, enc_cache_t *enc_cache,
				 uint8_t *buf, int bufsiz, int eip, int nobj)
{
	int err, offset, physz;
	enc_element_t *obj;
	ses_element_t *elmpriv;
	struct ses_addl_status *addl;

	err = offset = 0;

	/* basic object setup */
	obj = &(enc_cache->elm_map[nobj]);
	elmpriv = obj->elm_private;
	addl = &(elmpriv->addl);

	addl->proto_hdr.sas = (union ses_elm_sas_hdr *)&buf[offset];

	/* Don't assume this object has any phys */
	bzero(&addl->proto_data, sizeof(addl->proto_data));
	if (addl->proto_hdr.sas->base_hdr.num_phys == 0)
		goto out;

	/* Skip forward to the phy list */
	if (eip)
		offset += sizeof(struct ses_elm_sas_type0_eip_hdr);
	else
		offset += sizeof(struct ses_elm_sas_type0_base_hdr);

	/* Make sure the phy list fits in the buffer */
	physz = addl->proto_hdr.sas->base_hdr.num_phys;
	physz *= sizeof(struct ses_elm_sas_device_phy);
	if (physz > (bufsiz - offset + 4)) {
		ENC_VLOG(enc, "Element %d Device Phy List Beyond End Of Buffer\n",
		    nobj);
		err = EIO;
		goto out;
	}

	/* Point to the phy list */
	addl->proto_data.sasdev_phys =
	    (struct ses_elm_sas_device_phy *)&buf[offset];

out:
	return (err);
}

/**
 * \brief Update the softc with the additional element status data for this
 * 	  object, for SAS type 1 objects.
 *
 * \param enc		SES softc to be updated.
 * \param buf		The additional element status response buffer.
 * \param bufsiz	Size of the response buffer.
 * \param eip		The EIP bit value.
 * \param nobj		Number of objects attached to the SES softc.
 * 
 * \return		0 on success, errno otherwise.
 */
static int
ses_get_elm_addlstatus_sas_type1(enc_softc_t *enc, enc_cache_t *enc_cache,
			         uint8_t *buf, int bufsiz, int eip, int nobj)
{
	int err, offset, physz;
	enc_element_t *obj;
	ses_element_t *elmpriv;
	struct ses_addl_status *addl;

	err = offset = 0;

	/* basic object setup */
	obj = &(enc_cache->elm_map[nobj]);
	elmpriv = obj->elm_private;
	addl = &(elmpriv->addl);

	addl->proto_hdr.sas = (union ses_elm_sas_hdr *)&buf[offset];

	/* Don't assume this object has any phys */
	bzero(&addl->proto_data, sizeof(addl->proto_data));
	if (addl->proto_hdr.sas->base_hdr.num_phys == 0)
		goto out;

	/* Process expanders differently from other type1 cases */
	if (ses_obj_is_expander(enc, obj)) {
		offset += sizeof(struct ses_elm_sas_type1_expander_hdr);
		physz = addl->proto_hdr.sas->base_hdr.num_phys *
		    sizeof(struct ses_elm_sas_expander_phy);
		if (physz > (bufsiz - offset)) {
			ENC_VLOG(enc, "Element %d: Expander Phy List Beyond "
			    "End Of Buffer\n", nobj);
			err = EIO;
			goto out;
		}
		addl->proto_data.sasexp_phys =
		    (struct ses_elm_sas_expander_phy *)&buf[offset];
	} else {
		offset += sizeof(struct ses_elm_sas_type1_nonexpander_hdr);
		physz = addl->proto_hdr.sas->base_hdr.num_phys *
		    sizeof(struct ses_elm_sas_port_phy);
		if (physz > (bufsiz - offset + 4)) {
			ENC_VLOG(enc, "Element %d: Port Phy List Beyond End "
			    "Of Buffer\n", nobj);
			err = EIO;
			goto out;
		}
		addl->proto_data.sasport_phys =
		    (struct ses_elm_sas_port_phy *)&buf[offset];
	}

out:
	return (err);
}

/**
 * \brief Update the softc with the additional element status data for this
 * 	  object, for SAS objects.
 *
 * \param enc		SES softc to be updated.
 * \param buf		The additional element status response buffer.
 * \param bufsiz	Size of the response buffer.
 * \param eip		The EIP bit value.
 * \param tidx		Type index for this object.
 * \param nobj		Number of objects attached to the SES softc.
 * 
 * \return		0 on success, errno otherwise.
 */
static int
ses_get_elm_addlstatus_sas(enc_softc_t *enc, enc_cache_t *enc_cache,
			   uint8_t *buf, int bufsiz, int eip, int tidx,
			   int nobj)
{
	int dtype, err;
	ses_cache_t *ses_cache;
	union ses_elm_sas_hdr *hdr;

	/* Need to be able to read the descriptor type! */
	if (bufsiz < sizeof(union ses_elm_sas_hdr)) {
		err = EIO;
		goto out;
	}

	ses_cache = enc_cache->private;

	hdr = (union ses_elm_sas_hdr *)buf;
	dtype = ses_elm_sas_descr_type(hdr);
	switch(dtype) {
	case SES_SASOBJ_TYPE_SLOT:
		switch(ses_cache->ses_types[tidx].hdr->etype_elm_type) {
		case ELMTYP_DEVICE:
		case ELMTYP_ARRAY_DEV:
			break;
		default:
			ENC_VLOG(enc, "Element %d has Additional Status type 0, "
			    "invalid for SES element type 0x%x\n", nobj,
			    ses_cache->ses_types[tidx].hdr->etype_elm_type);
			err = ENODEV;
			goto out;
		}
		err = ses_get_elm_addlstatus_sas_type0(enc, enc_cache,
						       buf, bufsiz, eip,
		    nobj);
		break;
	case SES_SASOBJ_TYPE_OTHER:
		switch(ses_cache->ses_types[tidx].hdr->etype_elm_type) {
		case ELMTYP_SAS_EXP:
		case ELMTYP_SCSI_INI:
		case ELMTYP_SCSI_TGT:
		case ELMTYP_ESCC:
			break;
		default:
			ENC_VLOG(enc, "Element %d has Additional Status type 1, "
			    "invalid for SES element type 0x%x\n", nobj,
			    ses_cache->ses_types[tidx].hdr->etype_elm_type);
			err = ENODEV;
			goto out;
		}
		err = ses_get_elm_addlstatus_sas_type1(enc, enc_cache, buf,
						       bufsiz, eip, nobj);
		break;
	default:
		ENC_VLOG(enc, "Element %d of type 0x%x has Additional Status "
		    "of unknown type 0x%x\n", nobj,
		    ses_cache->ses_types[tidx].hdr->etype_elm_type, dtype);
		err = ENODEV;
		break;
	}

out:
	return (err);
}

static void
ses_softc_invalidate(enc_softc_t *enc)
{
	ses_softc_t *ses;

	ses = enc->enc_private;
	ses_terminate_control_requests(&ses->ses_requests, ENXIO);
}

static void
ses_softc_cleanup(enc_softc_t *enc)
{

	ses_cache_free(enc, &enc->enc_cache);
	ses_cache_free(enc, &enc->enc_daemon_cache);
	ENC_FREE_AND_NULL(enc->enc_private);
	ENC_FREE_AND_NULL(enc->enc_cache.private);
	ENC_FREE_AND_NULL(enc->enc_daemon_cache.private);
}

static int
ses_init_enc(enc_softc_t *enc)
{
	return (0);
}

static int
ses_get_enc_status(enc_softc_t *enc, int slpflag)
{
	/* Automatically updated, caller checks enc_cache->encstat itself */
	return (0);
}

static int
ses_set_enc_status(enc_softc_t *enc, uint8_t encstat, int slpflag)
{
	ses_control_request_t req;
	ses_softc_t	     *ses;

	ses = enc->enc_private;
	req.elm_idx = SES_SETSTATUS_ENC_IDX;
	req.elm_stat.comstatus = encstat & 0xf;
	
	TAILQ_INSERT_TAIL(&ses->ses_requests, &req, links);
	enc_update_request(enc, SES_PROCESS_CONTROL_REQS);
	cam_periph_sleep(enc->periph, &req, PUSER, "encstat", 0);

	return (req.result);
}

static int
ses_get_elm_status(enc_softc_t *enc, encioc_elm_status_t *elms, int slpflag)
{
	unsigned int i = elms->elm_idx;

	memcpy(elms->cstat, &enc->enc_cache.elm_map[i].encstat, 4);
	return (0);
}

static int
ses_set_elm_status(enc_softc_t *enc, encioc_elm_status_t *elms, int slpflag)
{
	ses_control_request_t req;
	ses_softc_t	     *ses;

	/* If this is clear, we don't do diddly.  */
	if ((elms->cstat[0] & SESCTL_CSEL) == 0)
		return (0);

	ses = enc->enc_private;
	req.elm_idx = elms->elm_idx;
	memcpy(&req.elm_stat, elms->cstat, sizeof(req.elm_stat));

	TAILQ_INSERT_TAIL(&ses->ses_requests, &req, links);
	enc_update_request(enc, SES_PROCESS_CONTROL_REQS);
	cam_periph_sleep(enc->periph, &req, PUSER, "encstat", 0);

	return (req.result);
}

static int
ses_get_elm_desc(enc_softc_t *enc, encioc_elm_desc_t *elmd)
{
	int i = (int)elmd->elm_idx;
	ses_element_t *elmpriv;

	/* Assume caller has already checked obj_id validity */
	elmpriv = enc->enc_cache.elm_map[i].elm_private;
	/* object might not have a descriptor */
	if (elmpriv == NULL || elmpriv->descr == NULL) {
		elmd->elm_desc_len = 0;
		return (0);
	}
	if (elmd->elm_desc_len > elmpriv->descr_len)
		elmd->elm_desc_len = elmpriv->descr_len;
	copyout(elmpriv->descr, elmd->elm_desc_str, elmd->elm_desc_len);
	return (0);
}

/**
 * \brief Respond to ENCIOC_GETELMDEVNAME, providing a device name for the
 *	  given object id if one is available.
 *
 * \param enc	SES softc to examine.
 * \param objdn	ioctl structure to read/write device name info.
 *
 * \return	0 on success, errno otherwise.
 */
static int
ses_get_elm_devnames(enc_softc_t *enc, encioc_elm_devnames_t *elmdn)
{
	struct sbuf sb;
	int len;

	len = elmdn->elm_names_size;
	if (len < 0)
		return (EINVAL);

	cam_periph_unlock(enc->periph);
	sbuf_new(&sb, NULL, len, SBUF_FIXEDLEN);
	ses_paths_iter(enc, &enc->enc_cache.elm_map[elmdn->elm_idx],
	    ses_elmdevname_callback, &sb);
	sbuf_finish(&sb);
	elmdn->elm_names_len = sbuf_len(&sb);
	copyout(sbuf_data(&sb), elmdn->elm_devnames, elmdn->elm_names_len + 1);
	sbuf_delete(&sb);
	cam_periph_lock(enc->periph);
	return (elmdn->elm_names_len > 0 ? 0 : ENODEV);
}

/**
 * \brief Send a string to the primary subenclosure using the String Out
 * 	  SES diagnostic page.
 *
 * \param enc	SES enclosure to run the command on.
 * \param sstr	SES string structure to operate on
 * \param ioc	Ioctl being performed
 *
 * \return	0 on success, errno otherwise.
 */
static int
ses_handle_string(enc_softc_t *enc, encioc_string_t *sstr, int ioc)
{
	ses_softc_t *ses;
	enc_cache_t *enc_cache;
	ses_cache_t *ses_cache;
	const struct ses_enc_desc *enc_desc;
	int amt, payload, ret;
	char cdb[6];
	char str[32];
	char vendor[9];
	char product[17];
	char rev[5];
	uint8_t *buf;
	size_t size, rsize;

	ses = enc->enc_private;
	enc_cache = &enc->enc_daemon_cache;
	ses_cache = enc_cache->private;

	/* Implement SES2r20 6.1.6 */
	if (sstr->bufsiz > 0xffff)
		return (EINVAL); /* buffer size too large */

	switch (ioc) {
	case ENCIOC_SETSTRING:
		payload = sstr->bufsiz + 4; /* header for SEND DIAGNOSTIC */
		amt = 0 - payload;
		buf = ENC_MALLOC(payload);
		if (buf == NULL)
			return (ENOMEM);
		ses_page_cdb(cdb, payload, 0, CAM_DIR_OUT);
		/* Construct the page request */
		buf[0] = SesStringOut;
		buf[1] = 0;
		buf[2] = sstr->bufsiz >> 8;
		buf[3] = sstr->bufsiz & 0xff;
		memcpy(&buf[4], sstr->buf, sstr->bufsiz);
		break;
	case ENCIOC_GETSTRING:
		payload = sstr->bufsiz;
		amt = payload;
		ses_page_cdb(cdb, payload, SesStringIn, CAM_DIR_IN);
		buf = sstr->buf;
		break;
	case ENCIOC_GETENCNAME:
		if (ses_cache->ses_nsubencs < 1)
			return (ENODEV);
		enc_desc = ses_cache->subencs[0];
		cam_strvis(vendor, enc_desc->vendor_id,
		    sizeof(enc_desc->vendor_id), sizeof(vendor));
		cam_strvis(product, enc_desc->product_id,
		    sizeof(enc_desc->product_id), sizeof(product));
		cam_strvis(rev, enc_desc->product_rev,
		    sizeof(enc_desc->product_rev), sizeof(rev));
		rsize = snprintf(str, sizeof(str), "%s %s %s",
		    vendor, product, rev) + 1;
		if (rsize > sizeof(str))
			rsize = sizeof(str);
		copyout(&rsize, &sstr->bufsiz, sizeof(rsize));
		size = rsize;
		if (size > sstr->bufsiz)
			size = sstr->bufsiz;
		copyout(str, sstr->buf, size);
		return (size == rsize ? 0 : ENOMEM);
	case ENCIOC_GETENCID:
		if (ses_cache->ses_nsubencs < 1)
			return (ENODEV);
		enc_desc = ses_cache->subencs[0];
		rsize = snprintf(str, sizeof(str), "%16jx",
		    scsi_8btou64(enc_desc->logical_id)) + 1;
		if (rsize > sizeof(str))
			rsize = sizeof(str);
		copyout(&rsize, &sstr->bufsiz, sizeof(rsize));
		size = rsize;
		if (size > sstr->bufsiz)
			size = sstr->bufsiz;
		copyout(str, sstr->buf, size);
		return (size == rsize ? 0 : ENOMEM);
	default:
		return (EINVAL);
	}
	ret = enc_runcmd(enc, cdb, 6, buf, &amt);
	if (ioc == ENCIOC_SETSTRING)
		ENC_FREE(buf);
	return (ret);
}

/**
 * \invariant Called with cam_periph mutex held.
 */
static void
ses_poll_status(enc_softc_t *enc)
{
	ses_softc_t *ses;

	ses = enc->enc_private;
	enc_update_request(enc, SES_UPDATE_GETSTATUS);
	if (ses->ses_flags & SES_FLAG_DESC)
		enc_update_request(enc, SES_UPDATE_GETELMDESCS);
	if (ses->ses_flags & SES_FLAG_ADDLSTATUS)
		enc_update_request(enc, SES_UPDATE_GETELMADDLSTATUS);
}

/**
 * \brief Notification received when CAM detects a new device in the
 *        SCSI domain in which this SEP resides.
 *
 * \param enc	SES enclosure instance.
 */
static void
ses_device_found(enc_softc_t *enc)
{
	ses_poll_status(enc);
	enc_update_request(enc, SES_PUBLISH_PHYSPATHS);
}

static struct enc_vec ses_enc_vec =
{
	.softc_invalidate	= ses_softc_invalidate,
	.softc_cleanup		= ses_softc_cleanup,
	.init_enc		= ses_init_enc,
	.get_enc_status		= ses_get_enc_status,
	.set_enc_status		= ses_set_enc_status,
	.get_elm_status		= ses_get_elm_status,
	.set_elm_status		= ses_set_elm_status,
	.get_elm_desc		= ses_get_elm_desc,
	.get_elm_devnames	= ses_get_elm_devnames,
	.handle_string		= ses_handle_string,
	.device_found		= ses_device_found,
	.poll_status		= ses_poll_status
};

/**
 * \brief Initialize a new SES instance.
 *
 * \param enc		SES softc structure to set up the instance in.
 * \param doinit	Do the initialization (see main driver).
 *
 * \return		0 on success, errno otherwise.
 */
int
ses_softc_init(enc_softc_t *enc)
{
	ses_softc_t *ses_softc;

	CAM_DEBUG(enc->periph->path, CAM_DEBUG_SUBTRACE,
	    ("entering enc_softc_init(%p)\n", enc));

	enc->enc_vec = ses_enc_vec;
	enc->enc_fsm_states = enc_fsm_states;

	if (enc->enc_private == NULL)
		enc->enc_private = ENC_MALLOCZ(sizeof(ses_softc_t));
	if (enc->enc_cache.private == NULL)
		enc->enc_cache.private = ENC_MALLOCZ(sizeof(ses_cache_t));
	if (enc->enc_daemon_cache.private == NULL)
		enc->enc_daemon_cache.private =
		     ENC_MALLOCZ(sizeof(ses_cache_t));

	if (enc->enc_private == NULL
	 || enc->enc_cache.private == NULL
	 || enc->enc_daemon_cache.private == NULL) {
		ENC_FREE_AND_NULL(enc->enc_private);
		ENC_FREE_AND_NULL(enc->enc_cache.private);
		ENC_FREE_AND_NULL(enc->enc_daemon_cache.private);
		return (ENOMEM);
	}

	ses_softc = enc->enc_private;
	TAILQ_INIT(&ses_softc->ses_requests);
	TAILQ_INIT(&ses_softc->ses_pending_requests);

	enc_update_request(enc, SES_UPDATE_PAGES);

	// XXX: Move this to the FSM so it doesn't hang init
	if (0) (void) ses_set_timed_completion(enc, 1);

	return (0);
}

