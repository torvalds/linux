/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/msg.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */


void
copy_msg_alerting(struct uni_alerting *src, struct uni_alerting *dst);


void
copy_msg_call_proc(struct uni_call_proc *src, struct uni_call_proc *dst);


void
copy_msg_connect(struct uni_connect *src, struct uni_connect *dst);


void
copy_msg_connect_ack(struct uni_connect_ack *src, struct uni_connect_ack *dst);


void
copy_msg_release(struct uni_release *src, struct uni_release *dst);


void
copy_msg_release_compl(struct uni_release_compl *src, struct uni_release_compl *dst);


void
copy_msg_setup(struct uni_setup *src, struct uni_setup *dst);


void
copy_msg_status(struct uni_status *src, struct uni_status *dst);


void
copy_msg_status_enq(struct uni_status_enq *src, struct uni_status_enq *dst);


void
copy_msg_notify(struct uni_notify *src, struct uni_notify *dst);


void
copy_msg_restart(struct uni_restart *src, struct uni_restart *dst);


void
copy_msg_restart_ack(struct uni_restart_ack *src, struct uni_restart_ack *dst);


void
copy_msg_add_party(struct uni_add_party *src, struct uni_add_party *dst);


void
copy_msg_add_party_ack(struct uni_add_party_ack *src, struct uni_add_party_ack *dst);


void
copy_msg_party_alerting(struct uni_party_alerting *src, struct uni_party_alerting *dst);


void
copy_msg_add_party_rej(struct uni_add_party_rej *src, struct uni_add_party_rej *dst);


void
copy_msg_drop_party(struct uni_drop_party *src, struct uni_drop_party *dst);


void
copy_msg_drop_party_ack(struct uni_drop_party_ack *src, struct uni_drop_party_ack *dst);


void
copy_msg_leaf_setup_req(struct uni_leaf_setup_req *src, struct uni_leaf_setup_req *dst);


void
copy_msg_leaf_setup_fail(struct uni_leaf_setup_fail *src, struct uni_leaf_setup_fail *dst);


void
copy_msg_cobisetup(struct uni_cobisetup *src, struct uni_cobisetup *dst);


void
copy_msg_facility(struct uni_facility *src, struct uni_facility *dst);


void
copy_msg_modify_req(struct uni_modify_req *src, struct uni_modify_req *dst);


void
copy_msg_modify_ack(struct uni_modify_ack *src, struct uni_modify_ack *dst);


void
copy_msg_modify_rej(struct uni_modify_rej *src, struct uni_modify_rej *dst);


void
copy_msg_conn_avail(struct uni_conn_avail *src, struct uni_conn_avail *dst);


void
copy_msg_unknown(struct uni_unknown *src, struct uni_unknown *dst);

