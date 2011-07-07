/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_TRANSLATION_TABLE_H_
#define _NET_BATMAN_ADV_TRANSLATION_TABLE_H_

int tt_len(int changes_num);
int tt_changes_fill_buffer(struct bat_priv *bat_priv,
			   unsigned char *buff, int buff_len);
int tt_init(struct bat_priv *bat_priv);
void tt_local_add(struct net_device *soft_iface, const uint8_t *addr,
		  int ifindex);
void tt_local_remove(struct bat_priv *bat_priv,
		     const uint8_t *addr, const char *message, bool roaming);
int tt_local_seq_print_text(struct seq_file *seq, void *offset);
void tt_global_add_orig(struct bat_priv *bat_priv, struct orig_node *orig_node,
			const unsigned char *tt_buff, int tt_buff_len);
int tt_global_add(struct bat_priv *bat_priv, struct orig_node *orig_node,
		  const unsigned char *addr, uint8_t ttvn, bool roaming,
		  bool wifi);
int tt_global_seq_print_text(struct seq_file *seq, void *offset);
void tt_global_del_orig(struct bat_priv *bat_priv,
			struct orig_node *orig_node, const char *message);
void tt_global_del(struct bat_priv *bat_priv,
		   struct orig_node *orig_node, const unsigned char *addr,
		   const char *message, bool roaming);
struct orig_node *transtable_search(struct bat_priv *bat_priv,
				    const uint8_t *addr);
void tt_save_orig_buffer(struct bat_priv *bat_priv, struct orig_node *orig_node,
			 const unsigned char *tt_buff, uint8_t tt_num_changes);
uint16_t tt_local_crc(struct bat_priv *bat_priv);
uint16_t tt_global_crc(struct bat_priv *bat_priv, struct orig_node *orig_node);
void tt_free(struct bat_priv *bat_priv);
int send_tt_request(struct bat_priv *bat_priv,
		    struct orig_node *dst_orig_node, uint8_t ttvn,
		    uint16_t tt_crc, bool full_table);
bool send_tt_response(struct bat_priv *bat_priv,
		      struct tt_query_packet *tt_request);
void tt_update_changes(struct bat_priv *bat_priv, struct orig_node *orig_node,
		       uint16_t tt_num_changes, uint8_t ttvn,
		       struct tt_change *tt_change);
bool is_my_client(struct bat_priv *bat_priv, const uint8_t *addr);
void handle_tt_response(struct bat_priv *bat_priv,
			struct tt_query_packet *tt_response);
void send_roam_adv(struct bat_priv *bat_priv, uint8_t *client,
		   struct orig_node *orig_node);
void tt_commit_changes(struct bat_priv *bat_priv);

#endif /* _NET_BATMAN_ADV_TRANSLATION_TABLE_H_ */
