/*
 * General overview:
 * full generic netlink message:
 * |nlmsghdr|genlmsghdr|<payload>
 *
 * payload:
 * |optional fixed size family header|<sequence of netlink attributes>
 *
 * sequence of netlink attributes:
 * I chose to have all "top level" attributes NLA_NESTED,
 * corresponding to some real struct.
 * So we have a sequence of |tla, len|<nested nla sequence>
 *
 * nested nla sequence:
 * may be empty, or contain a sequence of netlink attributes
 * representing the struct fields.
 *
 * The tag number of any field (regardless of containing struct)
 * will be available as T_ ## field_name,
 * so you cannot have the same field name in two differnt structs.
 *
 * The tag numbers themselves are per struct, though,
 * so should always begin at 1 (not 0, that is the special "NLA_UNSPEC" type,
 * which we won't use here).
 * The tag numbers are used as index in the respective nla_policy array.
 *
 * GENL_struct(tag_name, tag_number, struct name, struct fields) - struct and policy
 *	genl_magic_struct.h
 *		generates the struct declaration,
 *		generates an entry in the tla enum,
 *	genl_magic_func.h
 *		generates an entry in the static tla policy
 *		with .type = NLA_NESTED
 *		generates the static <struct_name>_nl_policy definition,
 *		and static conversion functions
 *
 *	genl_magic_func.h
 *
 * GENL_mc_group(group)
 *	genl_magic_struct.h
 *		does nothing
 *	genl_magic_func.h
 *		defines and registers the mcast group,
 *		and provides a send helper
 *
 * GENL_notification(op_name, op_num, mcast_group, tla list)
 *	These are notifications to userspace.
 *
 *	genl_magic_struct.h
 *		generates an entry in the genl_ops enum,
 *	genl_magic_func.h
 *		does nothing
 *
 *	mcast group: the name of the mcast group this notification should be
 *	expected on
 *	tla list: the list of expected top level attributes,
 *	for documentation and sanity checking.
 *
 * GENL_op(op_name, op_num, flags and handler, tla list) - "genl operations"
 *	These are requests from userspace.
 *
 *	_op and _notification share the same "number space",
 *	op_nr will be assigned to "genlmsghdr->cmd"
 *
 *	genl_magic_struct.h
 *		generates an entry in the genl_ops enum,
 *	genl_magic_func.h
 *		generates an entry in the static genl_ops array,
 *		and static register/unregister functions to
 *		genl_register_family_with_ops().
 *
 *	flags and handler:
 *		GENL_op_init( .doit = x, .dumpit = y, .flags = something)
 *		GENL_doit(x) => .dumpit = NULL, .flags = GENL_ADMIN_PERM
 *	tla list: the list of expected top level attributes,
 *	for documentation and sanity checking.
 */

/*
 * STRUCTS
 */

/* this is sent kernel -> userland on various error conditions, and contains
 * informational textual info, which is supposedly human readable.
 * The computer relevant return code is in the drbd_genlmsghdr.
 */
GENL_struct(DRBD_NLA_CFG_REPLY, 1, drbd_cfg_reply,
		/* "arbitrary" size strings, nla_policy.len = 0 */
	__str_field(1, GENLA_F_MANDATORY,	info_text, 0)
)

/* Configuration requests typically need a context to operate on.
 * Possible keys are device minor (fits in the drbd_genlmsghdr),
 * the replication link (aka connection) name,
 * and/or the replication group (aka resource) name,
 * and the volume id within the resource. */
GENL_struct(DRBD_NLA_CFG_CONTEXT, 2, drbd_cfg_context,
		/* currently only 256 volumes per group,
		 * but maybe we still change that */
	__u32_field(1, GENLA_F_MANDATORY,	ctx_volume)
	__str_field(2, GENLA_F_MANDATORY,	ctx_conn_name, 128)
)

GENL_struct(DRBD_NLA_DISK_CONF, 3, disk_conf,
	__u64_field(1, GENLA_F_MANDATORY,	disk_size)
	__str_field(2, GENLA_F_REQUIRED,	backing_dev,	128)
	__str_field(3, GENLA_F_REQUIRED,	meta_dev,	128)
	__u32_field(4, GENLA_F_REQUIRED,	meta_dev_idx)
	__u32_field(5, GENLA_F_MANDATORY,	max_bio_bvecs)
	__u32_field(6, GENLA_F_MANDATORY,	on_io_error)
	__u32_field(7, GENLA_F_MANDATORY,	fencing)
	__flg_field(8, GENLA_F_MANDATORY,	no_disk_barrier)
	__flg_field(9, GENLA_F_MANDATORY,	no_disk_flush)
	__flg_field(10, GENLA_F_MANDATORY,	no_disk_drain)
	__flg_field(11, GENLA_F_MANDATORY,	no_md_flush)
	__flg_field(12, GENLA_F_MANDATORY,	use_bmbv)
)

GENL_struct(DRBD_NLA_SYNCER_CONF, 4, syncer_conf,
	__u32_field(1,	GENLA_F_MANDATORY,	rate)
	__u32_field(2,	GENLA_F_MANDATORY,	after)
	__u32_field(3,	GENLA_F_MANDATORY,	al_extents)
	__str_field(4,	GENLA_F_MANDATORY,	cpu_mask,       32)
	__str_field(5,	GENLA_F_MANDATORY,	verify_alg,     SHARED_SECRET_MAX)
	__str_field(6,	GENLA_F_MANDATORY,	csums_alg,	SHARED_SECRET_MAX)
	__flg_field(7,	GENLA_F_MANDATORY,	use_rle)
	__u32_field(8,	GENLA_F_MANDATORY,	on_no_data)
	__u32_field(9,	GENLA_F_MANDATORY,	c_plan_ahead)
	__u32_field(10,	GENLA_F_MANDATORY,	c_delay_target)
	__u32_field(11,	GENLA_F_MANDATORY,	c_fill_target)
	__u32_field(12,	GENLA_F_MANDATORY,	c_max_rate)
	__u32_field(13,	GENLA_F_MANDATORY,	c_min_rate)
)

GENL_struct(DRBD_NLA_NET_CONF, 5, net_conf,
	__str_field(1,	GENLA_F_MANDATORY | GENLA_F_SENSITIVE,
						shared_secret,	SHARED_SECRET_MAX)
	__str_field(2,	GENLA_F_MANDATORY,	cram_hmac_alg,	SHARED_SECRET_MAX)
	__str_field(3,	GENLA_F_MANDATORY,	integrity_alg,	SHARED_SECRET_MAX)
	__str_field(4,	GENLA_F_REQUIRED,	my_addr,	128)
	__str_field(5,	GENLA_F_REQUIRED,	peer_addr,	128)
	__u32_field(6,	GENLA_F_REQUIRED,	wire_protocol)
	__u32_field(7,	GENLA_F_MANDATORY,	try_connect_int)
	__u32_field(8,	GENLA_F_MANDATORY,	timeout)
	__u32_field(9,	GENLA_F_MANDATORY,	ping_int)
	__u32_field(10,	GENLA_F_MANDATORY,	ping_timeo)
	__u32_field(11,	GENLA_F_MANDATORY,	sndbuf_size)
	__u32_field(12,	GENLA_F_MANDATORY,	rcvbuf_size)
	__u32_field(13,	GENLA_F_MANDATORY,	ko_count)
	__u32_field(14,	GENLA_F_MANDATORY,	max_buffers)
	__u32_field(15,	GENLA_F_MANDATORY,	max_epoch_size)
	__u32_field(16,	GENLA_F_MANDATORY,	unplug_watermark)
	__u32_field(17,	GENLA_F_MANDATORY,	after_sb_0p)
	__u32_field(18,	GENLA_F_MANDATORY,	after_sb_1p)
	__u32_field(19,	GENLA_F_MANDATORY,	after_sb_2p)
	__u32_field(20,	GENLA_F_MANDATORY,	rr_conflict)
	__u32_field(21,	GENLA_F_MANDATORY,	on_congestion)
	__u32_field(22,	GENLA_F_MANDATORY,	cong_fill)
	__u32_field(23,	GENLA_F_MANDATORY,	cong_extents)
	__flg_field(24, GENLA_F_MANDATORY,	two_primaries)
	__flg_field(25, GENLA_F_MANDATORY,	want_lose)
	__flg_field(26, GENLA_F_MANDATORY,	no_cork)
	__flg_field(27, GENLA_F_MANDATORY,	always_asbp)
	__flg_field(28, GENLA_F_MANDATORY,	dry_run)
)

GENL_struct(DRBD_NLA_SET_ROLE_PARMS, 6, set_role_parms,
	__flg_field(1, GENLA_F_MANDATORY,	assume_uptodate)
)

GENL_struct(DRBD_NLA_RESIZE_PARMS, 7, resize_parms,
	__u64_field(1, GENLA_F_MANDATORY,	resize_size)
	__flg_field(2, GENLA_F_MANDATORY,	resize_force)
	__flg_field(3, GENLA_F_MANDATORY,	no_resync)
)

GENL_struct(DRBD_NLA_STATE_INFO, 8, state_info,
	/* the reason of the broadcast,
	 * if this is an event triggered broadcast. */
	__u32_field(1, GENLA_F_MANDATORY,	sib_reason)
	__u32_field(2, GENLA_F_REQUIRED,	current_state)
	__u64_field(3, GENLA_F_MANDATORY,	capacity)
	__u64_field(4, GENLA_F_MANDATORY,	ed_uuid)

	/* These are for broadcast from after state change work.
	 * prev_state and new_state are from the moment the state change took
	 * place, new_state is not neccessarily the same as current_state,
	 * there may have been more state changes since.  Which will be
	 * broadcasted soon, in their respective after state change work.  */
	__u32_field(5, GENLA_F_MANDATORY,	prev_state)
	__u32_field(6, GENLA_F_MANDATORY,	new_state)

	/* if we have a local disk: */
	__bin_field(7, GENLA_F_MANDATORY,	uuids, (UI_SIZE*sizeof(__u64)))
	__u32_field(8, GENLA_F_MANDATORY,	disk_flags)
	__u64_field(9, GENLA_F_MANDATORY,	bits_total)
	__u64_field(10, GENLA_F_MANDATORY,	bits_oos)
	/* and in case resync or online verify is active */
	__u64_field(11, GENLA_F_MANDATORY,	bits_rs_total)
	__u64_field(12, GENLA_F_MANDATORY,	bits_rs_failed)

	/* for pre and post notifications of helper execution */
	__str_field(13, GENLA_F_MANDATORY,	helper, 32)
	__u32_field(14, GENLA_F_MANDATORY,	helper_exit_code)
)

GENL_struct(DRBD_NLA_START_OV_PARMS, 9, start_ov_parms,
	__u64_field(1, GENLA_F_MANDATORY,	ov_start_sector)
)

GENL_struct(DRBD_NLA_NEW_C_UUID_PARMS, 10, new_c_uuid_parms,
	__flg_field(1, GENLA_F_MANDATORY, clear_bm)
)

GENL_struct(DRBD_NLA_TIMEOUT_PARMS, 11, timeout_parms,
	__u32_field(1,	GENLA_F_REQUIRED,	timeout_type)
)

GENL_struct(DRBD_NLA_DISCONNECT_PARMS, 12, disconnect_parms,
	__flg_field(1, GENLA_F_MANDATORY,	force_disconnect)
)

/*
 * Notifications and commands (genlmsghdr->cmd)
 */
GENL_mc_group(events)

	/* kernel -> userspace announcement of changes */
GENL_notification(
	DRBD_EVENT, 1, events,
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_STATE_INFO, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_NET_CONF, GENLA_F_MANDATORY)
	GENL_tla_expected(DRBD_NLA_DISK_CONF, GENLA_F_MANDATORY)
	GENL_tla_expected(DRBD_NLA_SYNCER_CONF, GENLA_F_MANDATORY)
)

	/* query kernel for specific or all info */
GENL_op(
	DRBD_ADM_GET_STATUS, 2,
	GENL_op_init(
		.doit = drbd_adm_get_status,
		.dumpit = drbd_adm_get_status_all,
		/* anyone may ask for the status,
		 * it is broadcasted anyways */
	),
	/* To select the object .doit.
	 * Or a subset of objects in .dumpit. */
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_MANDATORY)
)

#if 0
	/* TO BE DONE */
	/* create or destroy resources, aka replication groups */
GENL_op(DRBD_ADM_CREATE_RESOURCE, 3, GENL_doit(drbd_adm_create_resource),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_DELETE_RESOURCE, 4, GENL_doit(drbd_adm_delete_resource),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
#endif

	/* add DRBD minor devices as volumes to resources */
GENL_op(DRBD_ADM_ADD_MINOR, 5, GENL_doit(drbd_adm_add_minor),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_DEL_MINOR, 6, GENL_doit(drbd_adm_delete_minor),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))

	/* add or delete replication links to resources */
GENL_op(DRBD_ADM_ADD_LINK, 7, GENL_doit(drbd_adm_create_connection),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_DEL_LINK, 8, GENL_doit(drbd_adm_delete_connection),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))

	/* operates on replication links */
GENL_op(DRBD_ADM_SYNCER, 9,
	GENL_doit(drbd_adm_syncer),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_SYNCER_CONF, GENLA_F_MANDATORY)
)

GENL_op(
	DRBD_ADM_CONNECT, 10,
	GENL_doit(drbd_adm_connect),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_NET_CONF, GENLA_F_REQUIRED)
)

GENL_op(DRBD_ADM_DISCONNECT, 11, GENL_doit(drbd_adm_disconnect),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))

	/* operates on minors */
GENL_op(DRBD_ADM_ATTACH, 12,
	GENL_doit(drbd_adm_attach),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_DISK_CONF, GENLA_F_REQUIRED)
)

GENL_op(
	DRBD_ADM_RESIZE, 13,
	GENL_doit(drbd_adm_resize),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_RESIZE_PARMS, GENLA_F_MANDATORY)
)

	/* operates on all volumes within a resource */
GENL_op(
	DRBD_ADM_PRIMARY, 14,
	GENL_doit(drbd_adm_set_role),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_SET_ROLE_PARMS, GENLA_F_REQUIRED)
)

GENL_op(
	DRBD_ADM_SECONDARY, 15,
	GENL_doit(drbd_adm_set_role),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_SET_ROLE_PARMS, GENLA_F_REQUIRED)
)

GENL_op(
	DRBD_ADM_NEW_C_UUID, 16,
	GENL_doit(drbd_adm_new_c_uuid),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED)
	GENL_tla_expected(DRBD_NLA_NEW_C_UUID_PARMS, GENLA_F_MANDATORY)
)

GENL_op(
	DRBD_ADM_START_OV, 17,
	GENL_doit(drbd_adm_start_ov),
	GENL_tla_expected(DRBD_NLA_START_OV_PARMS, GENLA_F_MANDATORY)
)

GENL_op(DRBD_ADM_DETACH,	18, GENL_doit(drbd_adm_detach),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_INVALIDATE,	19, GENL_doit(drbd_adm_invalidate),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_INVAL_PEER,	20, GENL_doit(drbd_adm_invalidate_peer),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_PAUSE_SYNC,	21, GENL_doit(drbd_adm_pause_sync),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_RESUME_SYNC,	22, GENL_doit(drbd_adm_resume_sync),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_SUSPEND_IO,	23, GENL_doit(drbd_adm_suspend_io),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_RESUME_IO,	24, GENL_doit(drbd_adm_resume_io),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_OUTDATE,	25, GENL_doit(drbd_adm_outdate),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_GET_TIMEOUT_TYPE, 26, GENL_doit(drbd_adm_get_timeout_type),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
GENL_op(DRBD_ADM_DOWN,		27, GENL_doit(drbd_adm_down),
	GENL_tla_expected(DRBD_NLA_CFG_CONTEXT, GENLA_F_REQUIRED))
