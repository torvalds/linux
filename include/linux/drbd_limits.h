/*
  drbd_limits.h
  This file is part of DRBD by Philipp Reisner and Lars Ellenberg.
*/

/*
 * Our current limitations.
 * Some of them are hard limits,
 * some of them are arbitrary range limits, that make it easier to provide
 * feedback about nonsense settings for certain configurable values.
 */

#ifndef DRBD_LIMITS_H
#define DRBD_LIMITS_H 1

#define DEBUG_RANGE_CHECK 0

#define DRBD_MINOR_COUNT_MIN 1
#define DRBD_MINOR_COUNT_MAX 255
#define DRBD_MINOR_COUNT_DEF 32
#define DRBD_MINOR_COUNT_SCALE '1'

#define DRBD_VOLUME_MAX 65535

#define DRBD_DIALOG_REFRESH_MIN 0
#define DRBD_DIALOG_REFRESH_MAX 600
#define DRBD_DIALOG_REFRESH_SCALE '1'

/* valid port number */
#define DRBD_PORT_MIN 1
#define DRBD_PORT_MAX 0xffff
#define DRBD_PORT_SCALE '1'

/* startup { */
  /* if you want more than 3.4 days, disable */
#define DRBD_WFC_TIMEOUT_MIN 0
#define DRBD_WFC_TIMEOUT_MAX 300000
#define DRBD_WFC_TIMEOUT_DEF 0
#define DRBD_WFC_TIMEOUT_SCALE '1'

#define DRBD_DEGR_WFC_TIMEOUT_MIN 0
#define DRBD_DEGR_WFC_TIMEOUT_MAX 300000
#define DRBD_DEGR_WFC_TIMEOUT_DEF 0
#define DRBD_DEGR_WFC_TIMEOUT_SCALE '1'

#define DRBD_OUTDATED_WFC_TIMEOUT_MIN 0
#define DRBD_OUTDATED_WFC_TIMEOUT_MAX 300000
#define DRBD_OUTDATED_WFC_TIMEOUT_DEF 0
#define DRBD_OUTDATED_WFC_TIMEOUT_SCALE '1'
/* }*/

/* net { */
  /* timeout, unit centi seconds
   * more than one minute timeout is not useful */
#define DRBD_TIMEOUT_MIN 1
#define DRBD_TIMEOUT_MAX 600
#define DRBD_TIMEOUT_DEF 60       /* 6 seconds */
#define DRBD_TIMEOUT_SCALE '1'

 /* If backing disk takes longer than disk_timeout, mark the disk as failed */
#define DRBD_DISK_TIMEOUT_MIN 0    /* 0 = disabled */
#define DRBD_DISK_TIMEOUT_MAX 6000 /* 10 Minutes */
#define DRBD_DISK_TIMEOUT_DEF 0    /* disabled */
#define DRBD_DISK_TIMEOUT_SCALE '1'

  /* active connection retries when C_WF_CONNECTION */
#define DRBD_CONNECT_INT_MIN 1
#define DRBD_CONNECT_INT_MAX 120
#define DRBD_CONNECT_INT_DEF 10   /* seconds */
#define DRBD_CONNECT_INT_SCALE '1'

  /* keep-alive probes when idle */
#define DRBD_PING_INT_MIN 1
#define DRBD_PING_INT_MAX 120
#define DRBD_PING_INT_DEF 10
#define DRBD_PING_INT_SCALE '1'

 /* timeout for the ping packets.*/
#define DRBD_PING_TIMEO_MIN  1
#define DRBD_PING_TIMEO_MAX  300
#define DRBD_PING_TIMEO_DEF  5
#define DRBD_PING_TIMEO_SCALE '1'

  /* max number of write requests between write barriers */
#define DRBD_MAX_EPOCH_SIZE_MIN 1
#define DRBD_MAX_EPOCH_SIZE_MAX 20000
#define DRBD_MAX_EPOCH_SIZE_DEF 2048
#define DRBD_MAX_EPOCH_SIZE_SCALE '1'

  /* I don't think that a tcp send buffer of more than 10M is useful */
#define DRBD_SNDBUF_SIZE_MIN  0
#define DRBD_SNDBUF_SIZE_MAX  (10<<20)
#define DRBD_SNDBUF_SIZE_DEF  0
#define DRBD_SNDBUF_SIZE_SCALE '1'

#define DRBD_RCVBUF_SIZE_MIN  0
#define DRBD_RCVBUF_SIZE_MAX  (10<<20)
#define DRBD_RCVBUF_SIZE_DEF  0
#define DRBD_RCVBUF_SIZE_SCALE '1'

  /* @4k PageSize -> 128kB - 512MB */
#define DRBD_MAX_BUFFERS_MIN  32
#define DRBD_MAX_BUFFERS_MAX  131072
#define DRBD_MAX_BUFFERS_DEF  2048
#define DRBD_MAX_BUFFERS_SCALE '1'

  /* @4k PageSize -> 4kB - 512MB */
#define DRBD_UNPLUG_WATERMARK_MIN  1
#define DRBD_UNPLUG_WATERMARK_MAX  131072
#define DRBD_UNPLUG_WATERMARK_DEF (DRBD_MAX_BUFFERS_DEF/16)
#define DRBD_UNPLUG_WATERMARK_SCALE '1'

  /* 0 is disabled.
   * 200 should be more than enough even for very short timeouts */
#define DRBD_KO_COUNT_MIN  0
#define DRBD_KO_COUNT_MAX  200
#define DRBD_KO_COUNT_DEF  7
#define DRBD_KO_COUNT_SCALE '1'
/* } */

/* syncer { */
  /* FIXME allow rate to be zero? */
#define DRBD_RESYNC_RATE_MIN 1
/* channel bonding 10 GbE, or other hardware */
#define DRBD_RESYNC_RATE_MAX (4 << 20)
#define DRBD_RESYNC_RATE_DEF 250
#define DRBD_RESYNC_RATE_SCALE 'k'  /* kilobytes */

  /* less than 7 would hit performance unnecessarily. */
#define DRBD_AL_EXTENTS_MIN  7
  /* we use u16 as "slot number", (u16)~0 is "FREE".
   * If you use >= 292 kB on-disk ring buffer,
   * this is the maximum you can use: */
#define DRBD_AL_EXTENTS_MAX  0xfffe
#define DRBD_AL_EXTENTS_DEF  1237
#define DRBD_AL_EXTENTS_SCALE '1'

#define DRBD_MINOR_NUMBER_MIN  -1
#define DRBD_MINOR_NUMBER_MAX  ((1 << 20) - 1)
#define DRBD_MINOR_NUMBER_DEF  -1
#define DRBD_MINOR_NUMBER_SCALE '1'

/* } */

/* drbdsetup XY resize -d Z
 * you are free to reduce the device size to nothing, if you want to.
 * the upper limit with 64bit kernel, enough ram and flexible meta data
 * is 1 PiB, currently. */
/* DRBD_MAX_SECTORS */
#define DRBD_DISK_SIZE_MIN  0
#define DRBD_DISK_SIZE_MAX  (1 * (2LLU << 40))
#define DRBD_DISK_SIZE_DEF  0 /* = disabled = no user size... */
#define DRBD_DISK_SIZE_SCALE 's'  /* sectors */

#define DRBD_ON_IO_ERROR_DEF EP_DETACH
#define DRBD_FENCING_DEF FP_DONT_CARE
#define DRBD_AFTER_SB_0P_DEF ASB_DISCONNECT
#define DRBD_AFTER_SB_1P_DEF ASB_DISCONNECT
#define DRBD_AFTER_SB_2P_DEF ASB_DISCONNECT
#define DRBD_RR_CONFLICT_DEF ASB_DISCONNECT
#define DRBD_ON_NO_DATA_DEF OND_IO_ERROR
#define DRBD_ON_CONGESTION_DEF OC_BLOCK
#define DRBD_READ_BALANCING_DEF RB_PREFER_LOCAL

#define DRBD_MAX_BIO_BVECS_MIN 0
#define DRBD_MAX_BIO_BVECS_MAX 128
#define DRBD_MAX_BIO_BVECS_DEF 0
#define DRBD_MAX_BIO_BVECS_SCALE '1'

#define DRBD_C_PLAN_AHEAD_MIN  0
#define DRBD_C_PLAN_AHEAD_MAX  300
#define DRBD_C_PLAN_AHEAD_DEF  20
#define DRBD_C_PLAN_AHEAD_SCALE '1'

#define DRBD_C_DELAY_TARGET_MIN 1
#define DRBD_C_DELAY_TARGET_MAX 100
#define DRBD_C_DELAY_TARGET_DEF 10
#define DRBD_C_DELAY_TARGET_SCALE '1'

#define DRBD_C_FILL_TARGET_MIN 0
#define DRBD_C_FILL_TARGET_MAX (1<<20) /* 500MByte in sec */
#define DRBD_C_FILL_TARGET_DEF 100 /* Try to place 50KiB in socket send buffer during resync */
#define DRBD_C_FILL_TARGET_SCALE 's'  /* sectors */

#define DRBD_C_MAX_RATE_MIN     250
#define DRBD_C_MAX_RATE_MAX     (4 << 20)
#define DRBD_C_MAX_RATE_DEF     102400
#define DRBD_C_MAX_RATE_SCALE	'k'  /* kilobytes */

#define DRBD_C_MIN_RATE_MIN     0
#define DRBD_C_MIN_RATE_MAX     (4 << 20)
#define DRBD_C_MIN_RATE_DEF     250
#define DRBD_C_MIN_RATE_SCALE	'k'  /* kilobytes */

#define DRBD_CONG_FILL_MIN	0
#define DRBD_CONG_FILL_MAX	(10<<21) /* 10GByte in sectors */
#define DRBD_CONG_FILL_DEF	0
#define DRBD_CONG_FILL_SCALE	's'  /* sectors */

#define DRBD_CONG_EXTENTS_MIN	DRBD_AL_EXTENTS_MIN
#define DRBD_CONG_EXTENTS_MAX	DRBD_AL_EXTENTS_MAX
#define DRBD_CONG_EXTENTS_DEF	DRBD_AL_EXTENTS_DEF
#define DRBD_CONG_EXTENTS_SCALE DRBD_AL_EXTENTS_SCALE

#define DRBD_PROTOCOL_DEF DRBD_PROT_C

#define DRBD_DISK_BARRIER_DEF	0
#define DRBD_DISK_FLUSHES_DEF	1
#define DRBD_DISK_DRAIN_DEF	1
#define DRBD_MD_FLUSHES_DEF	1
#define DRBD_TCP_CORK_DEF	1
#define DRBD_AL_UPDATES_DEF     1

#define DRBD_ALLOW_TWO_PRIMARIES_DEF	0
#define DRBD_ALWAYS_ASBP_DEF	0
#define DRBD_USE_RLE_DEF	1
#define DRBD_CSUMS_AFTER_CRASH_ONLY_DEF 0

#define DRBD_AL_STRIPES_MIN     1
#define DRBD_AL_STRIPES_MAX     1024
#define DRBD_AL_STRIPES_DEF     1
#define DRBD_AL_STRIPES_SCALE   '1'

#define DRBD_AL_STRIPE_SIZE_MIN   4
#define DRBD_AL_STRIPE_SIZE_MAX   16777216
#define DRBD_AL_STRIPE_SIZE_DEF   32
#define DRBD_AL_STRIPE_SIZE_SCALE 'k' /* kilobytes */
#endif
