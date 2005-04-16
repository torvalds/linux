#ifndef __SOUND_SEQ_KERNEL_H
#define __SOUND_SEQ_KERNEL_H

/*
 *  Main kernel header file for the ALSA sequencer
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/time.h>
#include "asequencer.h"

typedef sndrv_seq_tick_time_t snd_seq_tick_time_t;
typedef sndrv_seq_position_t snd_seq_position_t;
typedef sndrv_seq_frequency_t snd_seq_frequency_t;
typedef sndrv_seq_instr_cluster_t snd_seq_instr_cluster_t;
typedef enum sndrv_seq_client_type snd_seq_client_type_t;
typedef enum sndrv_seq_stop_mode snd_seq_stop_mode_t;
typedef struct sndrv_seq_port_info snd_seq_port_info_t;
typedef struct sndrv_seq_port_subscribe snd_seq_port_subscribe_t;
typedef struct sndrv_seq_event snd_seq_event_t;
typedef struct sndrv_seq_addr snd_seq_addr_t;
typedef struct sndrv_seq_ev_volume snd_seq_ev_volume_t;
typedef struct sndrv_seq_ev_loop snd_seq_ev_loop_t;
typedef struct sndrv_seq_remove_events snd_seq_remove_events_t;
typedef struct sndrv_seq_query_subs snd_seq_query_subs_t;
typedef struct sndrv_seq_real_time snd_seq_real_time_t;
typedef struct sndrv_seq_system_info snd_seq_system_info_t;
typedef struct sndrv_seq_client_info snd_seq_client_info_t;
typedef struct sndrv_seq_queue_info snd_seq_queue_info_t;
typedef struct sndrv_seq_queue_status snd_seq_queue_status_t;
typedef struct sndrv_seq_queue_tempo snd_seq_queue_tempo_t;
typedef struct sndrv_seq_queue_owner snd_seq_queue_owner_t;
typedef struct sndrv_seq_queue_timer snd_seq_queue_timer_t;
typedef struct sndrv_seq_queue_client snd_seq_queue_client_t;
typedef struct sndrv_seq_client_pool snd_seq_client_pool_t;
typedef struct sndrv_seq_instr snd_seq_instr_t;
typedef struct sndrv_seq_instr_data snd_seq_instr_data_t;
typedef struct sndrv_seq_instr_header snd_seq_instr_header_t;
typedef union sndrv_seq_timestamp snd_seq_timestamp_t;

#define snd_seq_event_bounce_ext_data	sndrv_seq_event_bounce_ext_data 
#define snd_seq_ev_is_result_type	sndrv_seq_ev_is_result_type     
#define snd_seq_ev_is_channel_type	sndrv_seq_ev_is_channel_type    
#define snd_seq_ev_is_note_type		sndrv_seq_ev_is_note_type       
#define snd_seq_ev_is_control_type	sndrv_seq_ev_is_control_type    
#define snd_seq_ev_is_queue_type	sndrv_seq_ev_is_queue_type      
#define snd_seq_ev_is_message_type	sndrv_seq_ev_is_message_type    
#define snd_seq_ev_is_sample_type	sndrv_seq_ev_is_sample_type     
#define snd_seq_ev_is_user_type		sndrv_seq_ev_is_user_type       
#define snd_seq_ev_is_fixed_type	sndrv_seq_ev_is_fixed_type      
#define snd_seq_ev_is_instr_type	sndrv_seq_ev_is_instr_type      
#define snd_seq_ev_is_variable_type	sndrv_seq_ev_is_variable_type   
#define snd_seq_ev_is_reserved		sndrv_seq_ev_is_reserved        
#define snd_seq_ev_is_direct		sndrv_seq_ev_is_direct          
#define snd_seq_ev_is_prior		sndrv_seq_ev_is_prior           
#define snd_seq_ev_length_type		sndrv_seq_ev_length_type        
#define snd_seq_ev_is_fixed		sndrv_seq_ev_is_fixed           
#define snd_seq_ev_is_variable		sndrv_seq_ev_is_variable        
#define snd_seq_ev_is_varusr		sndrv_seq_ev_is_varusr          
#define snd_seq_ev_timestamp_type	sndrv_seq_ev_timestamp_type     
#define snd_seq_ev_is_tick		sndrv_seq_ev_is_tick            
#define snd_seq_ev_is_real		sndrv_seq_ev_is_real            
#define snd_seq_ev_timemode_type	sndrv_seq_ev_timemode_type      
#define snd_seq_ev_is_abstime		sndrv_seq_ev_is_abstime         
#define snd_seq_ev_is_reltime		sndrv_seq_ev_is_reltime         
#define snd_seq_queue_sync_port		sndrv_seq_queue_sync_port       
#define snd_seq_queue_owner		sndrv_seq_queue_owner           

/* maximum number of events dequeued per schedule interval */
#define SNDRV_SEQ_MAX_DEQUEUE		50

/* maximum number of queues */
#define SNDRV_SEQ_MAX_QUEUES		8

/* max number of concurrent clients */
#define SNDRV_SEQ_MAX_CLIENTS 		192

/* max number of concurrent ports */
#define SNDRV_SEQ_MAX_PORTS 		254

/* max number of events in memory pool */
#define SNDRV_SEQ_MAX_EVENTS		2000

/* default number of events in memory chunk */
#define SNDRV_SEQ_DEFAULT_CHUNK_EVENTS	64

/* default number of events in memory pool */
#define SNDRV_SEQ_DEFAULT_EVENTS	500

/* max number of events in memory pool for one client (outqueue) */
#define SNDRV_SEQ_MAX_CLIENT_EVENTS	2000

/* default number of events in memory pool for one client (outqueue) */
#define SNDRV_SEQ_DEFAULT_CLIENT_EVENTS	200

/* max delivery path length */
#define SNDRV_SEQ_MAX_HOPS		10

/* max size of event size */
#define SNDRV_SEQ_MAX_EVENT_LEN		0x3fffffff

/* typedefs */
struct _snd_seq_user_client;
struct _snd_seq_kernel_client;
struct _snd_seq_client;
struct _snd_seq_queue;

typedef struct _snd_seq_user_client user_client_t;
typedef struct _snd_seq_kernel_client kernel_client_t;
typedef struct _snd_seq_client client_t;
typedef struct _snd_seq_queue queue_t;

/* call-backs for kernel client */

typedef struct {
	void *private_data;
	unsigned allow_input: 1,
		 allow_output: 1;
	/*...*/
} snd_seq_client_callback_t;

/* call-backs for kernel port */
typedef int (snd_seq_kernel_port_open_t)(void *private_data, snd_seq_port_subscribe_t *info);
typedef int (snd_seq_kernel_port_close_t)(void *private_data, snd_seq_port_subscribe_t *info);
typedef int (snd_seq_kernel_port_input_t)(snd_seq_event_t *ev, int direct, void *private_data, int atomic, int hop);
typedef void (snd_seq_kernel_port_private_free_t)(void *private_data);

typedef struct {
	struct module *owner;
	void *private_data;
	snd_seq_kernel_port_open_t *subscribe;
	snd_seq_kernel_port_close_t *unsubscribe;
	snd_seq_kernel_port_open_t *use;
	snd_seq_kernel_port_close_t *unuse;
	snd_seq_kernel_port_input_t *event_input;
	snd_seq_kernel_port_private_free_t *private_free;
	unsigned int callback_all;	/* call subscribe callbacks at each connection/disconnection */
	/*...*/
} snd_seq_port_callback_t;

/* interface for kernel client */
extern int snd_seq_create_kernel_client(snd_card_t *card, int client_index, snd_seq_client_callback_t *callback);
extern int snd_seq_delete_kernel_client(int client);
extern int snd_seq_kernel_client_enqueue(int client, snd_seq_event_t *ev, int atomic, int hop);
extern int snd_seq_kernel_client_dispatch(int client, snd_seq_event_t *ev, int atomic, int hop);
extern int snd_seq_kernel_client_ctl(int client, unsigned int cmd, void *arg);

#define SNDRV_SEQ_EXT_MASK	0xc0000000
#define SNDRV_SEQ_EXT_USRPTR	0x80000000
#define SNDRV_SEQ_EXT_CHAINED	0x40000000

typedef int (*snd_seq_dump_func_t)(void *ptr, void *buf, int count);
int snd_seq_expand_var_event(const snd_seq_event_t *event, int count, char *buf, int in_kernel, int size_aligned);
int snd_seq_dump_var_event(const snd_seq_event_t *event, snd_seq_dump_func_t func, void *private_data);

/* interface for OSS emulation */
int snd_seq_set_queue_tempo(int client, snd_seq_queue_tempo_t *tempo);

/* port callback routines */
void snd_port_init_callback(snd_seq_port_callback_t *p);
snd_seq_port_callback_t *snd_port_alloc_callback(void);

/* port attach/detach */
int snd_seq_event_port_attach(int client, snd_seq_port_callback_t *pcbp,
			      int cap, int type, int midi_channels, int midi_voices, char *portname);
int snd_seq_event_port_detach(int client, int port);

#ifdef CONFIG_KMOD
void snd_seq_autoload_lock(void);
void snd_seq_autoload_unlock(void);
#else
#define snd_seq_autoload_lock()
#define snd_seq_autoload_unlock()
#endif

#endif /* __SOUND_SEQ_KERNEL_H */
