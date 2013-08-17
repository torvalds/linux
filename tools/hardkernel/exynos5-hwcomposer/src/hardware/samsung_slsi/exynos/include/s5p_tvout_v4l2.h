/*
 * Copyright@ Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __S5P_TVOUT_H__
#define __S5P_TVOUT_H__

#include <linux/fb.h>

#include "videodev2.h"
#include "videodev2_exynos_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************
 * Define
 *******************************************/
/* TVOUT control */
#define PFX_NODE_FB               "/dev/graphics/fb"

#define PFX_NODE_MEDIADEV         "/dev/media"
#define PFX_NODE_SUBDEV           "/dev/v4l-subdev"
#define PFX_NODE_VIDEODEV         "/dev/video"
#define PFX_ENTITY_SUBDEV_MIXER   "s5p-mixer%d"
#define PFX_ENTITY_VIDEODEV_MIXER_GRP "mxr%d_graph%d"
#define PFX_ENTITY_SUBDEV_GSC_OUT     "exynos-gsc-sd.%d"
#define PFX_ENTITY_VIDEODEV_GSC_OUT   "exynos-gsc.%d.output"

#define PFX_ENTITY_SUBDEV_FIMD        "s5p-fimd%d"
#define PFX_ENTITY_SUBDEV_GSC_CAP     "gsc-cap-subdev.%d"
#define PFX_ENTITY_VIDEODEV_GSC_CAP   "exynos-gsc.%d.capture"

/* Sub-Mixer 0 */
#define TVOUT0_DEV_G0      "/dev/video16"
#define TVOUT0_DEV_G1      "/dev/video17"
/* Sub-Mixer 1 */
#define TVOUT1_DEV_G0      "/dev/video18"
#define TVOUT1_DEV_G1      "/dev/video19"

#define MIXER_V_SUBDEV_PAD_SINK     (0)
#define MIXER_V_SUBDEV_PAD_SOURCE   (3)
#define MIXER_G0_SUBDEV_PAD_SINK    (1)
#define MIXER_G0_SUBDEV_PAD_SOURCE  (4)
#define MIXER_G1_SUBDEV_PAD_SINK    (2)
#define MIXER_G1_SUBDEV_PAD_SOURCE  (5)

#define GSCALER_SUBDEV_PAD_SINK     (0)
#define GSCALER_SUBDEV_PAD_SOURCE   (1)
#define FIMD_SUBDEV_PAD_SOURCE      (0)

#define HPD_DEV         "/dev/HPD"

/* ------------- Output -----------------*/
/* type */
#define V4L2_OUTPUT_TYPE_MSDMA          4
#define V4L2_OUTPUT_TYPE_COMPOSITE      5
#define V4L2_OUTPUT_TYPE_SVIDEO         6
#define V4L2_OUTPUT_TYPE_YPBPR_INERLACED    7
#define V4L2_OUTPUT_TYPE_YPBPR_PROGRESSIVE  8
#define V4L2_OUTPUT_TYPE_RGB_PROGRESSIVE    9
#define V4L2_OUTPUT_TYPE_DIGITAL        10
#define V4L2_OUTPUT_TYPE_HDMI           V4L2_OUTPUT_TYPE_DIGITAL
#define V4L2_OUTPUT_TYPE_HDMI_RGB       11
#define V4L2_OUTPUT_TYPE_DVI            12

/* ------------- STD -------------------*/
#define V4L2_STD_PAL_BDGHI\
 (V4L2_STD_PAL_B|V4L2_STD_PAL_D|V4L2_STD_PAL_G|V4L2_STD_PAL_H|V4L2_STD_PAL_I)

#define V4L2_STD_480P_60_16_9           ((v4l2_std_id)0x04000000)
#define V4L2_STD_480P_60_4_3            ((v4l2_std_id)0x05000000)
#define V4L2_STD_576P_50_16_9           ((v4l2_std_id)0x06000000)
#define V4L2_STD_576P_50_4_3            ((v4l2_std_id)0x07000000)
#define V4L2_STD_720P_60                ((v4l2_std_id)0x08000000)
#define V4L2_STD_720P_50                ((v4l2_std_id)0x09000000)
#define V4L2_STD_1080P_60               ((v4l2_std_id)0x0a000000)
#define V4L2_STD_1080P_50               ((v4l2_std_id)0x0b000000)
#define V4L2_STD_1080I_60               ((v4l2_std_id)0x0c000000)
#define V4L2_STD_1080I_50               ((v4l2_std_id)0x0d000000)
#define V4L2_STD_480P_59                ((v4l2_std_id)0x0e000000)
#define V4L2_STD_720P_59                ((v4l2_std_id)0x0f000000)
#define V4L2_STD_1080I_59               ((v4l2_std_id)0x10000000)
#define V4L2_STD_1080P_59               ((v4l2_std_id)0x11000000)
#define V4L2_STD_1080P_30               ((v4l2_std_id)0x12000000)
#define V4L2_STD_TVOUT_720P_60_SBS_HALF ((v4l2_std_id)0x13000000)
#define V4L2_STD_TVOUT_720P_59_SBS_HALF ((v4l2_std_id)0x14000000)
#define V4L2_STD_TVOUT_720P_50_TB       ((v4l2_std_id)0x15000000)
#define V4L2_STD_TVOUT_1080P_24_TB      ((v4l2_std_id)0x16000000)
#define V4L2_STD_TVOUT_1080P_23_TB      ((v4l2_std_id)0x17000000)
#define V4L2_STD_TVOUT_1080P_60_SBS_HALF ((v4l2_std_id)0x18000000)

/* ------------- Input ------------------*/
/* type */
#define V4L2_INPUT_TYPE_MSDMA           3
#define V4L2_INPUT_TYPE_FIFO            4

/*******************************************
 * structures
 *******************************************/

/* TVOUT */
struct v4l2_vid_overlay_src {
    void            *base_y;
    void            *base_c;
    struct v4l2_pix_format  pix_fmt;
};

struct v4l2_window_s5p_tvout {
    __u32       capability;
    __u32       flags;
    __u32       priority;
    struct v4l2_window  win;
};

struct v4l2_pix_format_s5p_tvout {
    void *base_y;
    void *base_c;
    __u32 src_img_endian;
    struct v4l2_pix_format  pix_fmt;
};

struct vid_overlay_param {
    struct v4l2_vid_overlay_src     src;
    struct v4l2_rect                src_crop;
    struct v4l2_framebuffer         dst;
    struct v4l2_window              dst_win;
};

struct tvout_param {
    struct v4l2_pix_format_s5p_tvout    tvout_src;
    struct v4l2_window_s5p_tvout        tvout_rect;
    struct v4l2_rect                    tvout_dst;
};

struct overlay_param {
    struct v4l2_framebuffer         overlay_frame;
    struct v4l2_window_s5p_tvout    overlay_rect;
    struct v4l2_rect                overlay_dst;
};

/* FB */
struct s5ptvfb_user_window {
    int x;
    int y;
};

struct s5ptvfb_user_plane_alpha {
    int channel;
    unsigned char alpha;
};

struct s5ptvfb_user_chroma {
    int enabled;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

enum s5ptvfb_ver_scaling_t {
    VERTICAL_X1,
    VERTICAL_X2,
};

enum s5ptvfb_hor_scaling_t {
    HORIZONTAL_X1,
    HORIZONTAL_X2,
};

struct s5ptvfb_user_scaling {
    enum s5ptvfb_ver_scaling_t ver;
    enum s5ptvfb_hor_scaling_t hor;
};

/*******************************************
 * custom ioctls
 *******************************************/

#define VIDIOC_S_BASEADDR        _IOR('V', 83, int)

#define VIDIOC_HDCP_ENABLE _IOWR('V', 100, unsigned int)
#define VIDIOC_HDCP_STATUS _IOR('V', 101, unsigned int)
#define VIDIOC_HDCP_PROT_STATUS _IOR('V', 102, unsigned int)

#define VIDIOC_INIT_AUDIO _IOR('V', 103, unsigned int)
#define VIDIOC_AV_MUTE _IOR('V', 104, unsigned int)
#define VIDIOC_G_AVMUTE _IOR('V', 105, unsigned int)
#define HPD_GET_STATE _IOR('H', 100, unsigned int)

#define S5PTVFB_WIN_POSITION _IOW('F', 213, struct s5ptvfb_user_window)
#define S5PTVFB_WIN_SET_PLANE_ALPHA _IOW('F', 214, struct s5ptvfb_user_plane_alpha)
#define S5PTVFB_WIN_SET_CHROMA _IOW('F', 215, struct s5ptvfb_user_chroma)

#define S5PTVFB_SET_VSYNC_INT _IOW('F', 216, unsigned int)
#define S5PTVFB_WAITFORVSYNC _IO('F', 32)
#define S5PTVFB_WIN_SET_ADDR _IOW('F', 219, unsigned int)
#define S5PTVFB_SET_WIN_ON _IOW('F', 220, unsigned int)
#define S5PTVFB_SET_WIN_OFF _IOW('F', 221, unsigned int)
#define S5PTVFB_SCALING _IOW('F', 222, struct s5ptvfb_user_scaling)

#ifdef __cplusplus
}
#endif

#endif /* __S5P_TVOUT_H__ */
