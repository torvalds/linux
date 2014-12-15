/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef VFRAME_PROVIDER_H
#define VFRAME_PROVIDER_H

/* Standard Linux headers */
#include <linux/list.h>

/* Amlogic headers */
#include <linux/amlogic/amports/vframe.h>
typedef struct vframe_states {
	int vf_pool_size;
	int buf_free_num;
	int buf_recycle_num;
    int buf_avail_num;
} vframe_states_t;

#define VFRAME_EVENT_RECEIVER_GET               0x01
#define VFRAME_EVENT_RECEIVER_PUT               0x02
#define VFRAME_EVENT_RECEIVER_FRAME_WAIT        0x04
#define VFRAME_EVENT_RECEIVER_POS_CHANGED       0x08
#define VFRAME_EVENT_RECEIVER_PARAM_SET       	0x10
#define VFRAME_EVENT_RECEIVER_RESET				0x20
#define VFRAME_EVENT_RECEIVER_FORCE_UNREG			0x40

typedef struct vframe_operations_s {
    struct vframe_s * (*peek) (void* op_arg);
    struct vframe_s * (*get ) (void* op_arg);
    void (*put ) (struct vframe_s *, void* op_arg);
    int  (*event_cb)(int type, void* data, void* private_data);
  	int (*vf_states)(vframe_states_t *states, void* op_arg);
} vframe_operations_t;

typedef struct vframe_provider_s {
    const char *name;
	const struct vframe_operations_s *ops;
  void* op_arg;
	struct list_head list;
} vframe_provider_t;

extern struct vframe_provider_s *vf_provider_alloc(void);
extern void vf_provider_init(struct vframe_provider_s *prov,
    const char *name, const struct vframe_operations_s *ops, void* op_arg);
extern void vf_provider_free(struct vframe_provider_s *prov);


extern int vf_reg_provider(struct vframe_provider_s *prov);
extern void vf_unreg_provider(struct vframe_provider_s *prov);
extern int vf_notify_provider(const char* receiver_name, int event_type, void* data);

void vf_light_unreg_provider(struct vframe_provider_s *prov);
void vf_ext_light_unreg_provider(struct vframe_provider_s *prov);
struct vframe_provider_s * vf_get_provider(const char *name);

struct vframe_s *vf_peek(const char* receiver);
struct vframe_s *vf_get(const char* receiver);
void vf_put(struct vframe_s *vf, const char *receiver);

unsigned int get_post_canvas(void);
unsigned int vf_keep_current(void);
void get_video_keep_buffer(unsigned long *addr, unsigned long *phys_addr);
vframe_t* get_cur_dispbuf(void);
int query_video_status(int type , int* value);

#ifdef CONFIG_V4L_AMLOGIC_VIDEO 
void v4l_reg_provider(struct vframe_provider_s *prov);
void v4l_unreg_provider(void);
const vframe_provider_t * v4l_get_vfp(void);
#endif
#endif /* VFRAME_PROVIDER_H */

