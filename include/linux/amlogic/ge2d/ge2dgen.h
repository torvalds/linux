#ifndef GE2DGEN_H
#define GE2DGEN_H



void ge2dgen_src(ge2d_context_t *wq,
                 unsigned canvas_addr,
                 unsigned format);

void ge2dgen_post_release_src1buf(ge2d_context_t *wq, unsigned buffer);

void ge2dgen_post_release_src1canvas(ge2d_context_t *wq);

void ge2dgen_post_release_src2buf(ge2d_context_t *wq, unsigned buffer);

void ge2dgen_post_release_src2canvas(ge2d_context_t *wq);

void ge2dgen_src2(ge2d_context_t *wq,
                  unsigned canvas_addr,
                  unsigned format);

void ge2dgen_src2_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h);
void ge2dgen_antiflicker(ge2d_context_t *wq,unsigned long enable) ;
void ge2dgen_rendering_dir(ge2d_context_t *wq,
                           int src1_xrev,
                           int src1_yrev,
                           int dst_xrev,
                           int dst_yrev,
                           int dst_xy_swap);

void ge2dgen_dst(ge2d_context_t *wq,
                 unsigned canvas_addr,
                 unsigned format);

void ge2dgen_src_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h);

void ge2dgen_src_key(ge2d_context_t *wq,
                     int en, int key, int keymask,int keymode);

void ge2dgent_src_gbalpha(ge2d_context_t *wq,
                          unsigned char alpha);

void ge2dgen_src_color(ge2d_context_t *wq,
                       unsigned color);

void ge2dgent_rendering_dir(ge2d_context_t *wq,
                            int src_x_dir, int src_y_dir,
                            int dst_x_dir, int dst_y_dir);

void ge2dgen_src2(ge2d_context_t *wq, unsigned canvas_addr, unsigned format);

void ge2dgen_dst_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h, int mode);

void ge2dgent_src2_clip(ge2d_context_t *wq,
                        int x, int y, int w, int h);

void ge2dgen_cb(ge2d_context_t *wq, int (*cmd_cb)(unsigned), unsigned param);

void ge2dgen_const_color(ge2d_context_t *wq, unsigned color);

#endif /* GE2DGEN_H */

