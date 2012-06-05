/* Copyright (C) 2008-2012 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
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
 */

#ifndef _NET_BATMAN_ADV_VIS_H_
#define _NET_BATMAN_ADV_VIS_H_

/* timeout of vis packets in miliseconds */
#define BATADV_VIS_TIMEOUT		200000

int batadv_vis_seq_print_text(struct seq_file *seq, void *offset);
void batadv_receive_server_sync_packet(struct batadv_priv *bat_priv,
				       struct batadv_vis_packet *vis_packet,
				       int vis_info_len);
void batadv_receive_client_update_packet(struct batadv_priv *bat_priv,
					 struct batadv_vis_packet *vis_packet,
					 int vis_info_len);
int batadv_vis_init(struct batadv_priv *bat_priv);
void batadv_vis_quit(struct batadv_priv *bat_priv);

#endif /* _NET_BATMAN_ADV_VIS_H_ */
