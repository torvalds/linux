#ifndef _SCSI_SCSI_TCQ_H
#define _SCSI_SCSI_TCQ_H

#include <linux/blkdev.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#define MSG_SIMPLE_TAG	0x20
#define MSG_HEAD_TAG	0x21
#define MSG_ORDERED_TAG	0x22
#define MSG_ACA_TAG	0x24	/* unsupported */

#define SCSI_NO_TAG	(-1)    /* identify no tag in use */


#ifdef CONFIG_BLOCK

/**
 * scsi_get_tag_type - get the type of tag the device supports
 * @sdev:	the scsi device
 *
 * Notes:
 *	If the drive only supports simple tags, returns MSG_SIMPLE_TAG
 *	if it supports all tag types, returns MSG_ORDERED_TAG.
 */
static inline int scsi_get_tag_type(struct scsi_device *sdev)
{
	if (!sdev->tagged_supported)
		return 0;
	if (sdev->ordered_tags)
		return MSG_ORDERED_TAG;
	if (sdev->simple_tags)
		return MSG_SIMPLE_TAG;
	return 0;
}

static inline void scsi_set_tag_type(struct scsi_device *sdev, int tag)
{
	switch (tag) {
	case MSG_ORDERED_TAG:
		sdev->ordered_tags = 1;
		/* fall through */
	case MSG_SIMPLE_TAG:
		sdev->simple_tags = 1;
		break;
	case 0:
		/* fall through */
	default:
		sdev->ordered_tags = 0;
		sdev->simple_tags = 0;
		break;
	}
}
/**
 * scsi_activate_tcq - turn on tag command queueing
 * @SDpnt:	device to turn on TCQ for
 * @depth:	queue depth
 *
 * Notes:
 *	Eventually, I hope depth would be the maximum depth
 *	the device could cope with and the real queue depth
 *	would be adjustable from 0 to depth.
 **/
static inline void scsi_activate_tcq(struct scsi_device *sdev, int depth)
{
	if (!sdev->tagged_supported)
		return;

	if (!shost_use_blk_mq(sdev->host) &&
	    !blk_queue_tagged(sdev->request_queue))
		blk_queue_init_tags(sdev->request_queue, depth,
				    sdev->host->bqt);

	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), depth);
}

/**
 * scsi_deactivate_tcq - turn off tag command queueing
 * @SDpnt:	device to turn off TCQ for
 **/
static inline void scsi_deactivate_tcq(struct scsi_device *sdev, int depth)
{
	if (!shost_use_blk_mq(sdev->host) &&
	    blk_queue_tagged(sdev->request_queue))
		blk_queue_free_tags(sdev->request_queue);
	scsi_adjust_queue_depth(sdev, 0, depth);
}

/**
 * scsi_populate_tag_msg - place a tag message in a buffer
 * @SCpnt:	pointer to the Scsi_Cmnd for the tag
 * @msg:	pointer to the area to place the tag
 *
 * Notes:
 *	designed to create the correct type of tag message for the 
 *	particular request.  Returns the size of the tag message.
 *	May return 0 if TCQ is disabled for this device.
 **/
static inline int scsi_populate_tag_msg(struct scsi_cmnd *cmd, char *msg)
{
        struct request *req = cmd->request;

        if (blk_rq_tagged(req)) {
		*msg++ = MSG_SIMPLE_TAG;
        	*msg++ = req->tag;
        	return 2;
	}

	return 0;
}

static inline struct scsi_cmnd *scsi_mq_find_tag(struct Scsi_Host *shost,
		unsigned int hw_ctx, int tag)
{
	struct request *req;

	req = blk_mq_tag_to_rq(shost->tag_set.tags[hw_ctx], tag);
	return req ? (struct scsi_cmnd *)req->special : NULL;
}

/**
 * scsi_find_tag - find a tagged command by device
 * @SDpnt:	pointer to the ScSI device
 * @tag:	the tag number
 *
 * Notes:
 *	Only works with tags allocated by the generic blk layer.
 **/
static inline struct scsi_cmnd *scsi_find_tag(struct scsi_device *sdev, int tag)
{
        struct request *req;

        if (tag != SCSI_NO_TAG) {
		if (shost_use_blk_mq(sdev->host))
			return scsi_mq_find_tag(sdev->host, 0, tag);

        	req = blk_queue_find_tag(sdev->request_queue, tag);
	        return req ? (struct scsi_cmnd *)req->special : NULL;
	}

	/* single command, look in space */
	return sdev->current_cmnd;
}


/**
 * scsi_init_shared_tag_map - create a shared tag map
 * @shost:	the host to share the tag map among all devices
 * @depth:	the total depth of the map
 */
static inline int scsi_init_shared_tag_map(struct Scsi_Host *shost, int depth)
{
	/*
	 * We always have a shared tag map around when using blk-mq.
	 */
	if (shost_use_blk_mq(shost))
		return 0;

	/*
	 * If the shared tag map isn't already initialized, do it now.
	 * This saves callers from having to check ->bqt when setting up
	 * devices on the shared host (for libata)
	 */
	if (!shost->bqt) {
		shost->bqt = blk_init_tags(depth);
		if (!shost->bqt)
			return -ENOMEM;
	}

	return 0;
}

/**
 * scsi_host_find_tag - find the tagged command by host
 * @shost:	pointer to scsi_host
 * @tag:	tag of the scsi_cmnd
 *
 * Notes:
 *	Only works with tags allocated by the generic blk layer.
 **/
static inline struct scsi_cmnd *scsi_host_find_tag(struct Scsi_Host *shost,
						int tag)
{
	struct request *req;

	if (tag != SCSI_NO_TAG) {
		if (shost_use_blk_mq(shost))
			return scsi_mq_find_tag(shost, 0, tag);
		req = blk_map_queue_find_tag(shost->bqt, tag);
		return req ? (struct scsi_cmnd *)req->special : NULL;
	}
	return NULL;
}

#endif /* CONFIG_BLOCK */
#endif /* _SCSI_SCSI_TCQ_H */
