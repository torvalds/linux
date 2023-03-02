/* SPDX-License-Identifier: GPL-2.0 */
/*
 * console_struct.h
 *
 * Data structure describing single virtual console except for data
 * used by vt.c.
 *
 * Fields marked with [#] must be set by the low-level driver.
 * Fields marked with [!] can be changed by the low-level driver
 * to achieve effects such as fast scrolling by changing the origin.
 */

#ifndef _LINUX_CONSOLE_STRUCT_H
#define _LINUX_CONSOLE_STRUCT_H

#include <linux/wait.h>
#include <linux/vt.h>
#include <linux/workqueue.h>

struct uni_pagedict;

#define NPAR 16
#define VC_TABSTOPS_COUNT	256U

enum vc_intensity {
	VCI_HALF_BRIGHT,
	VCI_NORMAL,
	VCI_BOLD,
	VCI_MASK = 0x3,
};

/**
 * struct vc_state -- state of a VC
 * @x: cursor's x-position
 * @y: cursor's y-position
 * @color: foreground & background colors
 * @Gx_charset: what's G0/G1 slot set to (like GRAF_MAP, LAT1_MAP)
 * @charset: what character set to use (0=G0 or 1=G1)
 * @intensity: see enum vc_intensity for values
 * @reverse: reversed foreground/background colors
 *
 * These members are defined separately from struct vc_data as we save &
 * restore them at times.
 */
struct vc_state {
	unsigned int	x, y;

	unsigned char	color;

	unsigned char	Gx_charset[2];
	unsigned int	charset		: 1;

	/* attribute flags */
	enum vc_intensity intensity;
	bool		italic;
	bool		underline;
	bool		blink;
	bool		reverse;
};

/*
 * Example: vc_data of a console that was scrolled 3 lines down.
 *
 *                              Console buffer
 * vc_screenbuf ---------> +----------------------+-.
 *                         | initializing W       |  \
 *                         | initializing X       |   |
 *                         | initializing Y       |    > scroll-back area
 *                         | initializing Z       |   |
 *                         |                      |  /
 * vc_visible_origin ---> ^+----------------------+-:
 * (changes by scroll)    || Welcome to linux     |  \
 *                        ||                      |   |
 *           vc_rows --->< | login: root          |   |  visible on console
 *                        || password:            |    > (vc_screenbuf_size is
 * vc_origin -----------> ||                      |   |   vc_size_row * vc_rows)
 * (start when no scroll) || Last login: 12:28    |  /
 *                        v+----------------------+-:
 *                         | Have a lot of fun... |  \
 * vc_pos -----------------|--------v             |   > scroll-front area
 *                         | ~ # cat_             |  /
 * vc_scr_end -----------> +----------------------+-:
 * (vc_origin +            |                      |  \ EMPTY, to be filled by
 *  vc_screenbuf_size)     |                      |  / vc_video_erase_char
 *                         +----------------------+-'
 *                         <---- 2 * vc_cols ----->
 *                         <---- vc_size_row ----->
 *
 * Note that every character in the console buffer is accompanied with an
 * attribute in the buffer right after the character. This is not depicted
 * in the figure.
 */
struct vc_data {
	struct tty_port port;			/* Upper level data */

	struct vc_state state, saved_state;

	unsigned short	vc_num;			/* Console number */
	unsigned int	vc_cols;		/* [#] Console size */
	unsigned int	vc_rows;
	unsigned int	vc_size_row;		/* Bytes per row */
	unsigned int	vc_scan_lines;		/* # of scan lines */
	unsigned int	vc_cell_height;		/* CRTC character cell height */
	unsigned long	vc_origin;		/* [!] Start of real screen */
	unsigned long	vc_scr_end;		/* [!] End of real screen */
	unsigned long	vc_visible_origin;	/* [!] Top of visible window */
	unsigned int	vc_top, vc_bottom;	/* Scrolling region */
	const struct consw *vc_sw;
	unsigned short	*vc_screenbuf;		/* In-memory character/attribute buffer */
	unsigned int	vc_screenbuf_size;
	unsigned char	vc_mode;		/* KD_TEXT, ... */
	/* attributes for all characters on screen */
	unsigned char	vc_attr;		/* Current attributes */
	unsigned char	vc_def_color;		/* Default colors */
	unsigned char	vc_ulcolor;		/* Color for underline mode */
	unsigned char   vc_itcolor;
	unsigned char	vc_halfcolor;		/* Color for half intensity mode */
	/* cursor */
	unsigned int	vc_cursor_type;
	unsigned short	vc_complement_mask;	/* [#] Xor mask for mouse pointer */
	unsigned short	vc_s_complement_mask;	/* Saved mouse pointer mask */
	unsigned long	vc_pos;			/* Cursor address */
	/* fonts */	
	unsigned short	vc_hi_font_mask;	/* [#] Attribute set for upper 256 chars of font or 0 if not supported */
	struct console_font vc_font;		/* Current VC font set */
	unsigned short	vc_video_erase_char;	/* Background erase character */
	/* VT terminal data */
	unsigned int	vc_state;		/* Escape sequence parser state */
	unsigned int	vc_npar,vc_par[NPAR];	/* Parameters of current escape sequence */
	/* data for manual vt switching */
	struct vt_mode	vt_mode;
	struct pid 	*vt_pid;
	int		vt_newvt;
	wait_queue_head_t paste_wait;
	/* mode flags */
	unsigned int	vc_disp_ctrl	: 1;	/* Display chars < 32? */
	unsigned int	vc_toggle_meta	: 1;	/* Toggle high bit? */
	unsigned int	vc_decscnm	: 1;	/* Screen Mode */
	unsigned int	vc_decom	: 1;	/* Origin Mode */
	unsigned int	vc_decawm	: 1;	/* Autowrap Mode */
	unsigned int	vc_deccm	: 1;	/* Cursor Visible */
	unsigned int	vc_decim	: 1;	/* Insert Mode */
	/* misc */
	unsigned int	vc_priv		: 3;
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	unsigned int	vc_report_mouse : 2;
	unsigned char	vc_utf		: 1;	/* Unicode UTF-8 encoding */
	unsigned char	vc_utf_count;
		 int	vc_utf_char;
	DECLARE_BITMAP(vc_tab_stop, VC_TABSTOPS_COUNT);	/* Tab stops. 256 columns. */
	unsigned char   vc_palette[16*3];       /* Colour palette for VGA+ */
	unsigned short * vc_translate;
	unsigned int    vc_resize_user;         /* resize request from user */
	unsigned int	vc_bell_pitch;		/* Console bell pitch */
	unsigned int	vc_bell_duration;	/* Console bell duration */
	unsigned short	vc_cur_blink_ms;	/* Cursor blink duration */
	struct vc_data **vc_display_fg;		/* [!] Ptr to var holding fg console for this display */
	struct uni_pagedict *uni_pagedict;
	struct uni_pagedict **uni_pagedict_loc; /* [!] Location of uni_pagedict variable for this console */
	u32 **vc_uni_lines;			/* unicode screen content */
	/* additional information is in vt_kern.h */
};

struct vc {
	struct vc_data *d;
	struct work_struct SAK_work;

	/* might add  scrmem, kbd  at some time,
	   to have everything in one place */
};

extern struct vc vc_cons [MAX_NR_CONSOLES];
extern void vc_SAK(struct work_struct *work);

#define CUR_MAKE(size, change, set)	((size) | ((change) << 8) |	\
		((set) << 16))
#define CUR_SIZE(c)		 ((c) & 0x00000f)
# define CUR_DEF			       0
# define CUR_NONE			       1
# define CUR_UNDERLINE			       2
# define CUR_LOWER_THIRD		       3
# define CUR_LOWER_HALF			       4
# define CUR_TWO_THIRDS			       5
# define CUR_BLOCK			       6
#define CUR_SW				0x000010
#define CUR_ALWAYS_BG			0x000020
#define CUR_INVERT_FG_BG		0x000040
#define CUR_FG				0x000700
#define CUR_BG				0x007000
#define CUR_CHANGE(c)		 ((c) & 0x00ff00)
#define CUR_SET(c)		(((c) & 0xff0000) >> 8)

bool con_is_visible(const struct vc_data *vc);

#endif /* _LINUX_CONSOLE_STRUCT_H */
