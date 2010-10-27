/*
   PAKET( name,
	  TYPE ( pn, pr, member )
	  ...
   )

   You may never reissue one of the pn arguments
*/

#if !defined(NL_PACKET) || !defined(NL_STRING) || !defined(NL_INTEGER) || !defined(NL_BIT) || !defined(NL_INT64)
#error "The macros NL_PACKET, NL_STRING, NL_INTEGER, NL_INT64 and NL_BIT needs to be defined"
#endif

NL_PACKET(primary, 1,
       NL_BIT(		1,	T_MAY_IGNORE,	primary_force)
)

NL_PACKET(secondary, 2, )

NL_PACKET(disk_conf, 3,
	NL_INT64(	2,	T_MAY_IGNORE,	disk_size)
	NL_STRING(	3,	T_MANDATORY,	backing_dev,	128)
	NL_STRING(	4,	T_MANDATORY,	meta_dev,	128)
	NL_INTEGER(	5,	T_MANDATORY,	meta_dev_idx)
	NL_INTEGER(	6,	T_MAY_IGNORE,	on_io_error)
	NL_INTEGER(	7,	T_MAY_IGNORE,	fencing)
	NL_BIT(		37,	T_MAY_IGNORE,	use_bmbv)
	NL_BIT(		53,	T_MAY_IGNORE,	no_disk_flush)
	NL_BIT(		54,	T_MAY_IGNORE,	no_md_flush)
	  /*  55 max_bio_size was available in 8.2.6rc2 */
	NL_INTEGER(	56,	T_MAY_IGNORE,	max_bio_bvecs)
	NL_BIT(		57,	T_MAY_IGNORE,	no_disk_barrier)
	NL_BIT(		58,	T_MAY_IGNORE,	no_disk_drain)
)

NL_PACKET(detach, 4, )

NL_PACKET(net_conf, 5,
	NL_STRING(	8,	T_MANDATORY,	my_addr,	128)
	NL_STRING(	9,	T_MANDATORY,	peer_addr,	128)
	NL_STRING(	10,	T_MAY_IGNORE,	shared_secret,	SHARED_SECRET_MAX)
	NL_STRING(	11,	T_MAY_IGNORE,	cram_hmac_alg,	SHARED_SECRET_MAX)
	NL_STRING(	44,	T_MAY_IGNORE,	integrity_alg,	SHARED_SECRET_MAX)
	NL_INTEGER(	14,	T_MAY_IGNORE,	timeout)
	NL_INTEGER(	15,	T_MANDATORY,	wire_protocol)
	NL_INTEGER(	16,	T_MAY_IGNORE,	try_connect_int)
	NL_INTEGER(	17,	T_MAY_IGNORE,	ping_int)
	NL_INTEGER(	18,	T_MAY_IGNORE,	max_epoch_size)
	NL_INTEGER(	19,	T_MAY_IGNORE,	max_buffers)
	NL_INTEGER(	20,	T_MAY_IGNORE,	unplug_watermark)
	NL_INTEGER(	21,	T_MAY_IGNORE,	sndbuf_size)
	NL_INTEGER(	22,	T_MAY_IGNORE,	ko_count)
	NL_INTEGER(	24,	T_MAY_IGNORE,	after_sb_0p)
	NL_INTEGER(	25,	T_MAY_IGNORE,	after_sb_1p)
	NL_INTEGER(	26,	T_MAY_IGNORE,	after_sb_2p)
	NL_INTEGER(	39,	T_MAY_IGNORE,	rr_conflict)
	NL_INTEGER(	40,	T_MAY_IGNORE,	ping_timeo)
	NL_INTEGER(	67,	T_MAY_IGNORE,	rcvbuf_size)
	  /* 59 addr_family was available in GIT, never released */
	NL_BIT(		60,	T_MANDATORY,	mind_af)
	NL_BIT(		27,	T_MAY_IGNORE,	want_lose)
	NL_BIT(		28,	T_MAY_IGNORE,	two_primaries)
	NL_BIT(		41,	T_MAY_IGNORE,	always_asbp)
	NL_BIT(		61,	T_MAY_IGNORE,	no_cork)
	NL_BIT(		62,	T_MANDATORY,	auto_sndbuf_size)
	NL_BIT(		70,	T_MANDATORY,	dry_run)
)

NL_PACKET(disconnect, 6, )

NL_PACKET(resize, 7,
	NL_INT64(		29,	T_MAY_IGNORE,	resize_size)
	NL_BIT(			68,	T_MAY_IGNORE,	resize_force)
	NL_BIT(			69,	T_MANDATORY,	no_resync)
)

NL_PACKET(syncer_conf, 8,
	NL_INTEGER(	30,	T_MAY_IGNORE,	rate)
	NL_INTEGER(	31,	T_MAY_IGNORE,	after)
	NL_INTEGER(	32,	T_MAY_IGNORE,	al_extents)
/*	NL_INTEGER(     71,	T_MAY_IGNORE,	dp_volume)
 *	NL_INTEGER(     72,	T_MAY_IGNORE,	dp_interval)
 *	NL_INTEGER(     73,	T_MAY_IGNORE,	throttle_th)
 *	NL_INTEGER(     74,	T_MAY_IGNORE,	hold_off_th)
 * feature will be reimplemented differently with 8.3.9 */
	NL_STRING(      52,     T_MAY_IGNORE,   verify_alg,     SHARED_SECRET_MAX)
	NL_STRING(      51,     T_MAY_IGNORE,   cpu_mask,       32)
	NL_STRING(	64,	T_MAY_IGNORE,	csums_alg,	SHARED_SECRET_MAX)
	NL_BIT(         65,     T_MAY_IGNORE,   use_rle)
)

NL_PACKET(invalidate, 9, )
NL_PACKET(invalidate_peer, 10, )
NL_PACKET(pause_sync, 11, )
NL_PACKET(resume_sync, 12, )
NL_PACKET(suspend_io, 13, )
NL_PACKET(resume_io, 14, )
NL_PACKET(outdate, 15, )
NL_PACKET(get_config, 16, )
NL_PACKET(get_state, 17,
	NL_INTEGER(	33,	T_MAY_IGNORE,	state_i)
)

NL_PACKET(get_uuids, 18,
	NL_STRING(	34,	T_MAY_IGNORE,	uuids,	(UI_SIZE*sizeof(__u64)))
	NL_INTEGER(	35,	T_MAY_IGNORE,	uuids_flags)
)

NL_PACKET(get_timeout_flag, 19,
	NL_BIT(		36,	T_MAY_IGNORE,	use_degraded)
)

NL_PACKET(call_helper, 20,
	NL_STRING(	38,	T_MAY_IGNORE,	helper,		32)
)

/* Tag nr 42 already allocated in drbd-8.1 development. */

NL_PACKET(sync_progress, 23,
	NL_INTEGER(	43,	T_MAY_IGNORE,	sync_progress)
)

NL_PACKET(dump_ee, 24,
	NL_STRING(	45,	T_MAY_IGNORE,	dump_ee_reason, 32)
	NL_STRING(	46,	T_MAY_IGNORE,	seen_digest, SHARED_SECRET_MAX)
	NL_STRING(	47,	T_MAY_IGNORE,	calc_digest, SHARED_SECRET_MAX)
	NL_INT64(	48,	T_MAY_IGNORE,	ee_sector)
	NL_INT64(	49,	T_MAY_IGNORE,	ee_block_id)
	NL_STRING(	50,	T_MAY_IGNORE,	ee_data,	32 << 10)
)

NL_PACKET(start_ov, 25,
	NL_INT64(	66,	T_MAY_IGNORE,	start_sector)
)

NL_PACKET(new_c_uuid, 26,
       NL_BIT(		63,	T_MANDATORY,	clear_bm)
)

#undef NL_PACKET
#undef NL_INTEGER
#undef NL_INT64
#undef NL_BIT
#undef NL_STRING

