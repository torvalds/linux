#include <linux/module.h>
#include <linux/list.h>
#include "../../../drivers/target/iscsi/iscsi_target_core.h"

struct iscsit_transport {
#define ISCSIT_TRANSPORT_NAME	16
	char name[ISCSIT_TRANSPORT_NAME];
	int transport_type;
	struct module *owner;
	struct list_head t_node;
	int (*iscsit_setup_np)(struct iscsi_np *, struct __kernel_sockaddr_storage *);
	int (*iscsit_accept_np)(struct iscsi_np *, struct iscsi_conn *);
	void (*iscsit_free_np)(struct iscsi_np *);
	void (*iscsit_free_conn)(struct iscsi_conn *);
	struct iscsi_cmd *(*iscsit_alloc_cmd)(struct iscsi_conn *, gfp_t);
	int (*iscsit_get_login_rx)(struct iscsi_conn *, struct iscsi_login *);
	int (*iscsit_put_login_tx)(struct iscsi_conn *, struct iscsi_login *, u32);
	int (*iscsit_immediate_queue)(struct iscsi_conn *, struct iscsi_cmd *, int);
	int (*iscsit_response_queue)(struct iscsi_conn *, struct iscsi_cmd *, int);
	int (*iscsit_get_dataout)(struct iscsi_conn *, struct iscsi_cmd *, bool);
	int (*iscsit_queue_data_in)(struct iscsi_conn *, struct iscsi_cmd *);
	int (*iscsit_queue_status)(struct iscsi_conn *, struct iscsi_cmd *);
};

/*
 * From iscsi_target_transport.c
 */

extern int iscsit_register_transport(struct iscsit_transport *);
extern void iscsit_unregister_transport(struct iscsit_transport *);
extern struct iscsit_transport *iscsit_get_transport(int);
extern void iscsit_put_transport(struct iscsit_transport *);

/*
 * From iscsi_target.c
 */
extern int iscsit_add_reject_from_cmd(u8, int, int, unsigned char *,
				struct iscsi_cmd *);
extern int iscsit_setup_scsi_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				unsigned char *);
extern void iscsit_set_unsoliticed_dataout(struct iscsi_cmd *);
extern int iscsit_process_scsi_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				struct iscsi_scsi_req *);
extern int iscsit_check_dataout_hdr(struct iscsi_conn *, unsigned char *,
				struct iscsi_cmd **);
extern int iscsit_check_dataout_payload(struct iscsi_cmd *, struct iscsi_data *,
				bool);
extern int iscsit_setup_nop_out(struct iscsi_conn *, struct iscsi_cmd *,
				struct iscsi_nopout *);
extern int iscsit_process_nop_out(struct iscsi_conn *, struct iscsi_cmd *,
				struct iscsi_nopout *);
extern int iscsit_handle_logout_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				unsigned char *);
extern int iscsit_handle_task_mgt_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				unsigned char *);
extern int iscsit_setup_text_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				 struct iscsi_text *);
extern int iscsit_process_text_cmd(struct iscsi_conn *, struct iscsi_cmd *,
				   struct iscsi_text *);
extern void iscsit_build_rsp_pdu(struct iscsi_cmd *, struct iscsi_conn *,
				bool, struct iscsi_scsi_rsp *);
extern void iscsit_build_nopin_rsp(struct iscsi_cmd *, struct iscsi_conn *,
				struct iscsi_nopin *, bool);
extern void iscsit_build_task_mgt_rsp(struct iscsi_cmd *, struct iscsi_conn *,
				struct iscsi_tm_rsp *);
extern void iscsit_build_reject(struct iscsi_cmd *, struct iscsi_conn *,
				struct iscsi_reject *);
extern int iscsit_build_logout_rsp(struct iscsi_cmd *, struct iscsi_conn *,
				struct iscsi_logout_rsp *);
extern int iscsit_logout_post_handler(struct iscsi_cmd *, struct iscsi_conn *);
/*
 * From iscsi_target_device.c
 */
extern void iscsit_increment_maxcmdsn(struct iscsi_cmd *, struct iscsi_session *);
/*
 * From iscsi_target_erl1.c
 */
extern void iscsit_stop_dataout_timer(struct iscsi_cmd *);

/*
 * From iscsi_target_tmr.c
 */
extern int iscsit_tmr_post_handler(struct iscsi_cmd *, struct iscsi_conn *);

/*
 * From iscsi_target_util.c
 */
extern struct iscsi_cmd *iscsit_allocate_cmd(struct iscsi_conn *, gfp_t);
extern int iscsit_sequence_cmd(struct iscsi_conn *, struct iscsi_cmd *, __be32);
