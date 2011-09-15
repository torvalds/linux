/*
 * Pretty printing Support for iptables xt_qtaguid module.
 *
 * (C) 2011 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __XT_QTAGUID_PRINT_H__
#define __XT_QTAGUID_PRINT_H__

#include "xt_qtaguid_internal.h"

char *pp_tag_t(tag_t *tag);
char *pp_data_counters(struct data_counters *dc, bool showValues);
char *pp_tag_node(struct tag_node *tn);
char *pp_tag_ref(struct tag_ref *tr);
char *pp_tag_stat(struct tag_stat *ts);
char *pp_iface_stat(struct iface_stat *is);
char *pp_sock_tag(struct sock_tag *st);
char *pp_uid_tag_data(struct uid_tag_data *qtd);
char *pp_proc_qtu_data(struct proc_qtu_data *pqd);

/*------------------------------------------*/
void prdebug_sock_tag_list(int indent_level,
			   struct list_head *sock_tag_list);
void prdebug_sock_tag_tree(int indent_level,
			   struct rb_root *sock_tag_tree);
void prdebug_proc_qtu_data_tree(int indent_level,
				struct rb_root *proc_qtu_data_tree);
void prdebug_tag_ref_tree(int indent_level, struct rb_root *tag_ref_tree);
void prdebug_uid_tag_data_tree(int indent_level,
			       struct rb_root *uid_tag_data_tree);
void prdebug_tag_stat_tree(int indent_level,
			   struct rb_root *tag_stat_tree);
void prdebug_iface_stat_list(int indent_level,
			     struct list_head *iface_stat_list);

/*------------------------------------------*/
const char *netdev_evt_str(int netdev_event);
#endif  /* ifndef __XT_QTAGUID_PRINT_H__ */
