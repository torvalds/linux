/* $OpenBSD: term.h,v 1.17 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2023 Thomas E. Dickey                                *
 * Copyright 1998-2013,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************/
/* Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995                */
/*    and: Eric S. Raymond <esr@snark.thyrsus.com>                          */
/*    and: Thomas E. Dickey                        1995-on                  */
/****************************************************************************/

/* $Id: term.h,v 1.17 2023/10/17 09:52:08 nicm Exp $ */

/*
**	term.h -- Definition of struct term
*/

#ifndef NCURSES_TERM_H_incl
#define NCURSES_TERM_H_incl 1

#undef  NCURSES_VERSION
#define NCURSES_VERSION "6.4"

#include <ncurses_dll.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Make this file self-contained by providing defaults for the HAVE_TERMIO[S]_H
 * definition (based on the system for which this was configured).
 */

#ifndef __NCURSES_H

typedef struct screen  SCREEN;

#if 1
#undef  NCURSES_SP_FUNCS
#define NCURSES_SP_FUNCS 20230826
#undef  NCURSES_SP_NAME
#define NCURSES_SP_NAME(name) name##_sp

/* Define the sp-funcs helper function */
#undef  NCURSES_SP_OUTC
#define NCURSES_SP_OUTC NCURSES_SP_NAME(NCURSES_OUTC)
typedef int (*NCURSES_SP_OUTC)(SCREEN*, int);
#endif

#endif /* __NCURSES_H */

#undef  NCURSES_CONST
#define NCURSES_CONST const

#undef  NCURSES_SBOOL
#define NCURSES_SBOOL signed char

#undef  NCURSES_USE_DATABASE
#define NCURSES_USE_DATABASE 1

#undef  NCURSES_USE_TERMCAP
#define NCURSES_USE_TERMCAP 1

#undef  NCURSES_XNAMES
#define NCURSES_XNAMES 1

/* We will use these symbols to hide differences between
 * termios/termio/sgttyb interfaces.
 */
#undef  TTY
#undef  SET_TTY
#undef  GET_TTY

/* Assume POSIX termio if we have the header and function */
/* #if HAVE_TERMIOS_H && HAVE_TCGETATTR */
#if 1 && 1

#undef  TERMIOS
#define TERMIOS 1

#include <termios.h>
#define TTY struct termios

#else /* !HAVE_TERMIOS_H */

/* #if HAVE_TERMIO_H */
#if 0

#undef  TERMIOS
#define TERMIOS 1

#include <termio.h>
#define TTY struct termio

#else /* !HAVE_TERMIO_H */

#if (defined(_WIN32) || defined(_WIN64))
#if 0
#include <win32_curses.h>
#define TTY struct winconmode
#else
#include <ncurses_mingw.h>
#define TTY struct termios
#endif
#else
#undef TERMIOS
#include <sgtty.h>
#include <sys/ioctl.h>
#define TTY struct sgttyb
#endif /* MINGW32 */
#endif /* HAVE_TERMIO_H */

#endif /* HAVE_TERMIOS_H */

#ifdef TERMIOS
#define GET_TTY(fd, buf) tcgetattr(fd, buf)
#define SET_TTY(fd, buf) tcsetattr(fd, TCSADRAIN, buf)
#elif 0 && (defined(_WIN32) || defined(_WIN64))
#define GET_TTY(fd, buf) _nc_console_getmode(_nc_console_fd2handle(fd),buf)
#define SET_TTY(fd, buf) _nc_console_setmode(_nc_console_fd2handle(fd),buf)
#else
#define GET_TTY(fd, buf) gtty(fd, buf)
#define SET_TTY(fd, buf) stty(fd, buf)
#endif

#ifndef	GCC_NORETURN
#define	GCC_NORETURN /* nothing */
#endif

#define NAMESIZE 256

/* The cast works because TERMTYPE is the first data in TERMINAL */
#define CUR ((TERMTYPE *)(cur_term))->

#define auto_left_margin               CUR Booleans[0]
#define auto_right_margin              CUR Booleans[1]
#define no_esc_ctlc                    CUR Booleans[2]
#define ceol_standout_glitch           CUR Booleans[3]
#define eat_newline_glitch             CUR Booleans[4]
#define erase_overstrike               CUR Booleans[5]
#define generic_type                   CUR Booleans[6]
#define hard_copy                      CUR Booleans[7]
#define has_meta_key                   CUR Booleans[8]
#define has_status_line                CUR Booleans[9]
#define insert_null_glitch             CUR Booleans[10]
#define memory_above                   CUR Booleans[11]
#define memory_below                   CUR Booleans[12]
#define move_insert_mode               CUR Booleans[13]
#define move_standout_mode             CUR Booleans[14]
#define over_strike                    CUR Booleans[15]
#define status_line_esc_ok             CUR Booleans[16]
#define dest_tabs_magic_smso           CUR Booleans[17]
#define tilde_glitch                   CUR Booleans[18]
#define transparent_underline          CUR Booleans[19]
#define xon_xoff                       CUR Booleans[20]
#define needs_xon_xoff                 CUR Booleans[21]
#define prtr_silent                    CUR Booleans[22]
#define hard_cursor                    CUR Booleans[23]
#define non_rev_rmcup                  CUR Booleans[24]
#define no_pad_char                    CUR Booleans[25]
#define non_dest_scroll_region         CUR Booleans[26]
#define can_change                     CUR Booleans[27]
#define back_color_erase               CUR Booleans[28]
#define hue_lightness_saturation       CUR Booleans[29]
#define col_addr_glitch                CUR Booleans[30]
#define cr_cancels_micro_mode          CUR Booleans[31]
#define has_print_wheel                CUR Booleans[32]
#define row_addr_glitch                CUR Booleans[33]
#define semi_auto_right_margin         CUR Booleans[34]
#define cpi_changes_res                CUR Booleans[35]
#define lpi_changes_res                CUR Booleans[36]
#define columns                        CUR Numbers[0]
#define init_tabs                      CUR Numbers[1]
#define lines                          CUR Numbers[2]
#define lines_of_memory                CUR Numbers[3]
#define magic_cookie_glitch            CUR Numbers[4]
#define padding_baud_rate              CUR Numbers[5]
#define virtual_terminal               CUR Numbers[6]
#define width_status_line              CUR Numbers[7]
#define num_labels                     CUR Numbers[8]
#define label_height                   CUR Numbers[9]
#define label_width                    CUR Numbers[10]
#define max_attributes                 CUR Numbers[11]
#define maximum_windows                CUR Numbers[12]
#define max_colors                     CUR Numbers[13]
#define max_pairs                      CUR Numbers[14]
#define no_color_video                 CUR Numbers[15]
#define buffer_capacity                CUR Numbers[16]
#define dot_vert_spacing               CUR Numbers[17]
#define dot_horz_spacing               CUR Numbers[18]
#define max_micro_address              CUR Numbers[19]
#define max_micro_jump                 CUR Numbers[20]
#define micro_col_size                 CUR Numbers[21]
#define micro_line_size                CUR Numbers[22]
#define number_of_pins                 CUR Numbers[23]
#define output_res_char                CUR Numbers[24]
#define output_res_line                CUR Numbers[25]
#define output_res_horz_inch           CUR Numbers[26]
#define output_res_vert_inch           CUR Numbers[27]
#define print_rate                     CUR Numbers[28]
#define wide_char_size                 CUR Numbers[29]
#define buttons                        CUR Numbers[30]
#define bit_image_entwining            CUR Numbers[31]
#define bit_image_type                 CUR Numbers[32]
#define back_tab                       CUR Strings[0]
#define bell                           CUR Strings[1]
#define carriage_return                CUR Strings[2]
#define change_scroll_region           CUR Strings[3]
#define clear_all_tabs                 CUR Strings[4]
#define clear_screen                   CUR Strings[5]
#define clr_eol                        CUR Strings[6]
#define clr_eos                        CUR Strings[7]
#define column_address                 CUR Strings[8]
#define command_character              CUR Strings[9]
#define cursor_address                 CUR Strings[10]
#define cursor_down                    CUR Strings[11]
#define cursor_home                    CUR Strings[12]
#define cursor_invisible               CUR Strings[13]
#define cursor_left                    CUR Strings[14]
#define cursor_mem_address             CUR Strings[15]
#define cursor_normal                  CUR Strings[16]
#define cursor_right                   CUR Strings[17]
#define cursor_to_ll                   CUR Strings[18]
#define cursor_up                      CUR Strings[19]
#define cursor_visible                 CUR Strings[20]
#define delete_character               CUR Strings[21]
#define delete_line                    CUR Strings[22]
#define dis_status_line                CUR Strings[23]
#define down_half_line                 CUR Strings[24]
#define enter_alt_charset_mode         CUR Strings[25]
#define enter_blink_mode               CUR Strings[26]
#define enter_bold_mode                CUR Strings[27]
#define enter_ca_mode                  CUR Strings[28]
#define enter_delete_mode              CUR Strings[29]
#define enter_dim_mode                 CUR Strings[30]
#define enter_insert_mode              CUR Strings[31]
#define enter_secure_mode              CUR Strings[32]
#define enter_protected_mode           CUR Strings[33]
#define enter_reverse_mode             CUR Strings[34]
#define enter_standout_mode            CUR Strings[35]
#define enter_underline_mode           CUR Strings[36]
#define erase_chars                    CUR Strings[37]
#define exit_alt_charset_mode          CUR Strings[38]
#define exit_attribute_mode            CUR Strings[39]
#define exit_ca_mode                   CUR Strings[40]
#define exit_delete_mode               CUR Strings[41]
#define exit_insert_mode               CUR Strings[42]
#define exit_standout_mode             CUR Strings[43]
#define exit_underline_mode            CUR Strings[44]
#define flash_screen                   CUR Strings[45]
#define form_feed                      CUR Strings[46]
#define from_status_line               CUR Strings[47]
#define init_1string                   CUR Strings[48]
#define init_2string                   CUR Strings[49]
#define init_3string                   CUR Strings[50]
#define init_file                      CUR Strings[51]
#define insert_character               CUR Strings[52]
#define insert_line                    CUR Strings[53]
#define insert_padding                 CUR Strings[54]
#define key_backspace                  CUR Strings[55]
#define key_catab                      CUR Strings[56]
#define key_clear                      CUR Strings[57]
#define key_ctab                       CUR Strings[58]
#define key_dc                         CUR Strings[59]
#define key_dl                         CUR Strings[60]
#define key_down                       CUR Strings[61]
#define key_eic                        CUR Strings[62]
#define key_eol                        CUR Strings[63]
#define key_eos                        CUR Strings[64]
#define key_f0                         CUR Strings[65]
#define key_f1                         CUR Strings[66]
#define key_f10                        CUR Strings[67]
#define key_f2                         CUR Strings[68]
#define key_f3                         CUR Strings[69]
#define key_f4                         CUR Strings[70]
#define key_f5                         CUR Strings[71]
#define key_f6                         CUR Strings[72]
#define key_f7                         CUR Strings[73]
#define key_f8                         CUR Strings[74]
#define key_f9                         CUR Strings[75]
#define key_home                       CUR Strings[76]
#define key_ic                         CUR Strings[77]
#define key_il                         CUR Strings[78]
#define key_left                       CUR Strings[79]
#define key_ll                         CUR Strings[80]
#define key_npage                      CUR Strings[81]
#define key_ppage                      CUR Strings[82]
#define key_right                      CUR Strings[83]
#define key_sf                         CUR Strings[84]
#define key_sr                         CUR Strings[85]
#define key_stab                       CUR Strings[86]
#define key_up                         CUR Strings[87]
#define keypad_local                   CUR Strings[88]
#define keypad_xmit                    CUR Strings[89]
#define lab_f0                         CUR Strings[90]
#define lab_f1                         CUR Strings[91]
#define lab_f10                        CUR Strings[92]
#define lab_f2                         CUR Strings[93]
#define lab_f3                         CUR Strings[94]
#define lab_f4                         CUR Strings[95]
#define lab_f5                         CUR Strings[96]
#define lab_f6                         CUR Strings[97]
#define lab_f7                         CUR Strings[98]
#define lab_f8                         CUR Strings[99]
#define lab_f9                         CUR Strings[100]
#define meta_off                       CUR Strings[101]
#define meta_on                        CUR Strings[102]
#define newline                        CUR Strings[103]
#define pad_char                       CUR Strings[104]
#define parm_dch                       CUR Strings[105]
#define parm_delete_line               CUR Strings[106]
#define parm_down_cursor               CUR Strings[107]
#define parm_ich                       CUR Strings[108]
#define parm_index                     CUR Strings[109]
#define parm_insert_line               CUR Strings[110]
#define parm_left_cursor               CUR Strings[111]
#define parm_right_cursor              CUR Strings[112]
#define parm_rindex                    CUR Strings[113]
#define parm_up_cursor                 CUR Strings[114]
#define pkey_key                       CUR Strings[115]
#define pkey_local                     CUR Strings[116]
#define pkey_xmit                      CUR Strings[117]
#define print_screen                   CUR Strings[118]
#define prtr_off                       CUR Strings[119]
#define prtr_on                        CUR Strings[120]
#define repeat_char                    CUR Strings[121]
#define reset_1string                  CUR Strings[122]
#define reset_2string                  CUR Strings[123]
#define reset_3string                  CUR Strings[124]
#define reset_file                     CUR Strings[125]
#define restore_cursor                 CUR Strings[126]
#define row_address                    CUR Strings[127]
#define save_cursor                    CUR Strings[128]
#define scroll_forward                 CUR Strings[129]
#define scroll_reverse                 CUR Strings[130]
#define set_attributes                 CUR Strings[131]
#define set_tab                        CUR Strings[132]
#define set_window                     CUR Strings[133]
#define tab                            CUR Strings[134]
#define to_status_line                 CUR Strings[135]
#define underline_char                 CUR Strings[136]
#define up_half_line                   CUR Strings[137]
#define init_prog                      CUR Strings[138]
#define key_a1                         CUR Strings[139]
#define key_a3                         CUR Strings[140]
#define key_b2                         CUR Strings[141]
#define key_c1                         CUR Strings[142]
#define key_c3                         CUR Strings[143]
#define prtr_non                       CUR Strings[144]
#define char_padding                   CUR Strings[145]
#define acs_chars                      CUR Strings[146]
#define plab_norm                      CUR Strings[147]
#define key_btab                       CUR Strings[148]
#define enter_xon_mode                 CUR Strings[149]
#define exit_xon_mode                  CUR Strings[150]
#define enter_am_mode                  CUR Strings[151]
#define exit_am_mode                   CUR Strings[152]
#define xon_character                  CUR Strings[153]
#define xoff_character                 CUR Strings[154]
#define ena_acs                        CUR Strings[155]
#define label_on                       CUR Strings[156]
#define label_off                      CUR Strings[157]
#define key_beg                        CUR Strings[158]
#define key_cancel                     CUR Strings[159]
#define key_close                      CUR Strings[160]
#define key_command                    CUR Strings[161]
#define key_copy                       CUR Strings[162]
#define key_create                     CUR Strings[163]
#define key_end                        CUR Strings[164]
#define key_enter                      CUR Strings[165]
#define key_exit                       CUR Strings[166]
#define key_find                       CUR Strings[167]
#define key_help                       CUR Strings[168]
#define key_mark                       CUR Strings[169]
#define key_message                    CUR Strings[170]
#define key_move                       CUR Strings[171]
#define key_next                       CUR Strings[172]
#define key_open                       CUR Strings[173]
#define key_options                    CUR Strings[174]
#define key_previous                   CUR Strings[175]
#define key_print                      CUR Strings[176]
#define key_redo                       CUR Strings[177]
#define key_reference                  CUR Strings[178]
#define key_refresh                    CUR Strings[179]
#define key_replace                    CUR Strings[180]
#define key_restart                    CUR Strings[181]
#define key_resume                     CUR Strings[182]
#define key_save                       CUR Strings[183]
#define key_suspend                    CUR Strings[184]
#define key_undo                       CUR Strings[185]
#define key_sbeg                       CUR Strings[186]
#define key_scancel                    CUR Strings[187]
#define key_scommand                   CUR Strings[188]
#define key_scopy                      CUR Strings[189]
#define key_screate                    CUR Strings[190]
#define key_sdc                        CUR Strings[191]
#define key_sdl                        CUR Strings[192]
#define key_select                     CUR Strings[193]
#define key_send                       CUR Strings[194]
#define key_seol                       CUR Strings[195]
#define key_sexit                      CUR Strings[196]
#define key_sfind                      CUR Strings[197]
#define key_shelp                      CUR Strings[198]
#define key_shome                      CUR Strings[199]
#define key_sic                        CUR Strings[200]
#define key_sleft                      CUR Strings[201]
#define key_smessage                   CUR Strings[202]
#define key_smove                      CUR Strings[203]
#define key_snext                      CUR Strings[204]
#define key_soptions                   CUR Strings[205]
#define key_sprevious                  CUR Strings[206]
#define key_sprint                     CUR Strings[207]
#define key_sredo                      CUR Strings[208]
#define key_sreplace                   CUR Strings[209]
#define key_sright                     CUR Strings[210]
#define key_srsume                     CUR Strings[211]
#define key_ssave                      CUR Strings[212]
#define key_ssuspend                   CUR Strings[213]
#define key_sundo                      CUR Strings[214]
#define req_for_input                  CUR Strings[215]
#define key_f11                        CUR Strings[216]
#define key_f12                        CUR Strings[217]
#define key_f13                        CUR Strings[218]
#define key_f14                        CUR Strings[219]
#define key_f15                        CUR Strings[220]
#define key_f16                        CUR Strings[221]
#define key_f17                        CUR Strings[222]
#define key_f18                        CUR Strings[223]
#define key_f19                        CUR Strings[224]
#define key_f20                        CUR Strings[225]
#define key_f21                        CUR Strings[226]
#define key_f22                        CUR Strings[227]
#define key_f23                        CUR Strings[228]
#define key_f24                        CUR Strings[229]
#define key_f25                        CUR Strings[230]
#define key_f26                        CUR Strings[231]
#define key_f27                        CUR Strings[232]
#define key_f28                        CUR Strings[233]
#define key_f29                        CUR Strings[234]
#define key_f30                        CUR Strings[235]
#define key_f31                        CUR Strings[236]
#define key_f32                        CUR Strings[237]
#define key_f33                        CUR Strings[238]
#define key_f34                        CUR Strings[239]
#define key_f35                        CUR Strings[240]
#define key_f36                        CUR Strings[241]
#define key_f37                        CUR Strings[242]
#define key_f38                        CUR Strings[243]
#define key_f39                        CUR Strings[244]
#define key_f40                        CUR Strings[245]
#define key_f41                        CUR Strings[246]
#define key_f42                        CUR Strings[247]
#define key_f43                        CUR Strings[248]
#define key_f44                        CUR Strings[249]
#define key_f45                        CUR Strings[250]
#define key_f46                        CUR Strings[251]
#define key_f47                        CUR Strings[252]
#define key_f48                        CUR Strings[253]
#define key_f49                        CUR Strings[254]
#define key_f50                        CUR Strings[255]
#define key_f51                        CUR Strings[256]
#define key_f52                        CUR Strings[257]
#define key_f53                        CUR Strings[258]
#define key_f54                        CUR Strings[259]
#define key_f55                        CUR Strings[260]
#define key_f56                        CUR Strings[261]
#define key_f57                        CUR Strings[262]
#define key_f58                        CUR Strings[263]
#define key_f59                        CUR Strings[264]
#define key_f60                        CUR Strings[265]
#define key_f61                        CUR Strings[266]
#define key_f62                        CUR Strings[267]
#define key_f63                        CUR Strings[268]
#define clr_bol                        CUR Strings[269]
#define clear_margins                  CUR Strings[270]
#define set_left_margin                CUR Strings[271]
#define set_right_margin               CUR Strings[272]
#define label_format                   CUR Strings[273]
#define set_clock                      CUR Strings[274]
#define display_clock                  CUR Strings[275]
#define remove_clock                   CUR Strings[276]
#define create_window                  CUR Strings[277]
#define goto_window                    CUR Strings[278]
#define hangup                         CUR Strings[279]
#define dial_phone                     CUR Strings[280]
#define quick_dial                     CUR Strings[281]
#define tone                           CUR Strings[282]
#define pulse                          CUR Strings[283]
#define flash_hook                     CUR Strings[284]
#define fixed_pause                    CUR Strings[285]
#define wait_tone                      CUR Strings[286]
#define user0                          CUR Strings[287]
#define user1                          CUR Strings[288]
#define user2                          CUR Strings[289]
#define user3                          CUR Strings[290]
#define user4                          CUR Strings[291]
#define user5                          CUR Strings[292]
#define user6                          CUR Strings[293]
#define user7                          CUR Strings[294]
#define user8                          CUR Strings[295]
#define user9                          CUR Strings[296]
#define orig_pair                      CUR Strings[297]
#define orig_colors                    CUR Strings[298]
#define initialize_color               CUR Strings[299]
#define initialize_pair                CUR Strings[300]
#define set_color_pair                 CUR Strings[301]
#define set_foreground                 CUR Strings[302]
#define set_background                 CUR Strings[303]
#define change_char_pitch              CUR Strings[304]
#define change_line_pitch              CUR Strings[305]
#define change_res_horz                CUR Strings[306]
#define change_res_vert                CUR Strings[307]
#define define_char                    CUR Strings[308]
#define enter_doublewide_mode          CUR Strings[309]
#define enter_draft_quality            CUR Strings[310]
#define enter_italics_mode             CUR Strings[311]
#define enter_leftward_mode            CUR Strings[312]
#define enter_micro_mode               CUR Strings[313]
#define enter_near_letter_quality      CUR Strings[314]
#define enter_normal_quality           CUR Strings[315]
#define enter_shadow_mode              CUR Strings[316]
#define enter_subscript_mode           CUR Strings[317]
#define enter_superscript_mode         CUR Strings[318]
#define enter_upward_mode              CUR Strings[319]
#define exit_doublewide_mode           CUR Strings[320]
#define exit_italics_mode              CUR Strings[321]
#define exit_leftward_mode             CUR Strings[322]
#define exit_micro_mode                CUR Strings[323]
#define exit_shadow_mode               CUR Strings[324]
#define exit_subscript_mode            CUR Strings[325]
#define exit_superscript_mode          CUR Strings[326]
#define exit_upward_mode               CUR Strings[327]
#define micro_column_address           CUR Strings[328]
#define micro_down                     CUR Strings[329]
#define micro_left                     CUR Strings[330]
#define micro_right                    CUR Strings[331]
#define micro_row_address              CUR Strings[332]
#define micro_up                       CUR Strings[333]
#define order_of_pins                  CUR Strings[334]
#define parm_down_micro                CUR Strings[335]
#define parm_left_micro                CUR Strings[336]
#define parm_right_micro               CUR Strings[337]
#define parm_up_micro                  CUR Strings[338]
#define select_char_set                CUR Strings[339]
#define set_bottom_margin              CUR Strings[340]
#define set_bottom_margin_parm         CUR Strings[341]
#define set_left_margin_parm           CUR Strings[342]
#define set_right_margin_parm          CUR Strings[343]
#define set_top_margin                 CUR Strings[344]
#define set_top_margin_parm            CUR Strings[345]
#define start_bit_image                CUR Strings[346]
#define start_char_set_def             CUR Strings[347]
#define stop_bit_image                 CUR Strings[348]
#define stop_char_set_def              CUR Strings[349]
#define subscript_characters           CUR Strings[350]
#define superscript_characters         CUR Strings[351]
#define these_cause_cr                 CUR Strings[352]
#define zero_motion                    CUR Strings[353]
#define char_set_names                 CUR Strings[354]
#define key_mouse                      CUR Strings[355]
#define mouse_info                     CUR Strings[356]
#define req_mouse_pos                  CUR Strings[357]
#define get_mouse                      CUR Strings[358]
#define set_a_foreground               CUR Strings[359]
#define set_a_background               CUR Strings[360]
#define pkey_plab                      CUR Strings[361]
#define device_type                    CUR Strings[362]
#define code_set_init                  CUR Strings[363]
#define set0_des_seq                   CUR Strings[364]
#define set1_des_seq                   CUR Strings[365]
#define set2_des_seq                   CUR Strings[366]
#define set3_des_seq                   CUR Strings[367]
#define set_lr_margin                  CUR Strings[368]
#define set_tb_margin                  CUR Strings[369]
#define bit_image_repeat               CUR Strings[370]
#define bit_image_newline              CUR Strings[371]
#define bit_image_carriage_return      CUR Strings[372]
#define color_names                    CUR Strings[373]
#define define_bit_image_region        CUR Strings[374]
#define end_bit_image_region           CUR Strings[375]
#define set_color_band                 CUR Strings[376]
#define set_page_length                CUR Strings[377]
#define display_pc_char                CUR Strings[378]
#define enter_pc_charset_mode          CUR Strings[379]
#define exit_pc_charset_mode           CUR Strings[380]
#define enter_scancode_mode            CUR Strings[381]
#define exit_scancode_mode             CUR Strings[382]
#define pc_term_options                CUR Strings[383]
#define scancode_escape                CUR Strings[384]
#define alt_scancode_esc               CUR Strings[385]
#define enter_horizontal_hl_mode       CUR Strings[386]
#define enter_left_hl_mode             CUR Strings[387]
#define enter_low_hl_mode              CUR Strings[388]
#define enter_right_hl_mode            CUR Strings[389]
#define enter_top_hl_mode              CUR Strings[390]
#define enter_vertical_hl_mode         CUR Strings[391]
#define set_a_attributes               CUR Strings[392]
#define set_pglen_inch                 CUR Strings[393]

#define BOOLWRITE 37
#define NUMWRITE  33
#define STRWRITE  394

/* older synonyms for some capabilities */
#define beehive_glitch	no_esc_ctlc
#define teleray_glitch	dest_tabs_magic_smso

/* HPUX-11 uses this name rather than the standard one */
#ifndef micro_char_size
#define micro_char_size micro_col_size
#endif

#ifdef __INTERNAL_CAPS_VISIBLE
#define termcap_init2                  CUR Strings[394]
#define termcap_reset                  CUR Strings[395]
#define magic_cookie_glitch_ul         CUR Numbers[33]
#define backspaces_with_bs             CUR Booleans[37]
#define crt_no_scrolling               CUR Booleans[38]
#define no_correctly_working_cr        CUR Booleans[39]
#define carriage_return_delay          CUR Numbers[34]
#define new_line_delay                 CUR Numbers[35]
#define linefeed_if_not_lf             CUR Strings[396]
#define backspace_if_not_bs            CUR Strings[397]
#define gnu_has_meta_key               CUR Booleans[40]
#define linefeed_is_newline            CUR Booleans[41]
#define backspace_delay                CUR Numbers[36]
#define horizontal_tab_delay           CUR Numbers[37]
#define number_of_function_keys        CUR Numbers[38]
#define other_non_function_keys        CUR Strings[398]
#define arrow_key_map                  CUR Strings[399]
#define has_hardware_tabs              CUR Booleans[42]
#define return_does_clr_eol            CUR Booleans[43]
#define acs_ulcorner                   CUR Strings[400]
#define acs_llcorner                   CUR Strings[401]
#define acs_urcorner                   CUR Strings[402]
#define acs_lrcorner                   CUR Strings[403]
#define acs_ltee                       CUR Strings[404]
#define acs_rtee                       CUR Strings[405]
#define acs_btee                       CUR Strings[406]
#define acs_ttee                       CUR Strings[407]
#define acs_hline                      CUR Strings[408]
#define acs_vline                      CUR Strings[409]
#define acs_plus                       CUR Strings[410]
#define memory_lock                    CUR Strings[411]
#define memory_unlock                  CUR Strings[412]
#define box_chars_1                    CUR Strings[413]
#endif /* __INTERNAL_CAPS_VISIBLE */


/*
 * Predefined terminfo array sizes
 */
#define BOOLCOUNT 44
#define NUMCOUNT  39
#define STRCOUNT  414

/* used by code for comparing entries */
#define acs_chars_index	 146

typedef struct termtype {	/* in-core form of terminfo data */
    char  *term_names;		/* str_table offset of term names */
    char  *str_table;		/* pointer to string table */
    NCURSES_SBOOL  *Booleans;	/* array of boolean values */
    short *Numbers;		/* array of integer values */
    char  **Strings;		/* array of string offsets */

#if NCURSES_XNAMES
    char  *ext_str_table;	/* pointer to extended string table */
    char  **ext_Names;		/* corresponding names */

    unsigned short num_Booleans;/* count total Booleans */
    unsigned short num_Numbers;	/* count total Numbers */
    unsigned short num_Strings;	/* count total Strings */

    unsigned short ext_Booleans;/* count extensions to Booleans */
    unsigned short ext_Numbers;	/* count extensions to Numbers */
    unsigned short ext_Strings;	/* count extensions to Strings */
#endif /* NCURSES_XNAMES */

} TERMTYPE;

/*
 * The only reason these structures are visible is for read-only use.
 * Programs which modify the data are not, never were, portable across
 * curses implementations.
 *
 * The first field in TERMINAL is used in macros.
 * The remaining fields are private.
 */
#ifdef NCURSES_INTERNALS

#undef TERMINAL
#define TERMINAL struct term
TERMINAL;

typedef struct termtype2 {	/* in-core form of terminfo data */
    char  *term_names;		/* str_table offset of term names */
    char  *str_table;		/* pointer to string table */
    NCURSES_SBOOL  *Booleans;	/* array of boolean values */
    int   *Numbers;		/* array of integer values */
    char  **Strings;		/* array of string offsets */

#if NCURSES_XNAMES
    char  *ext_str_table;	/* pointer to extended string table */
    char  **ext_Names;		/* corresponding names */

    unsigned short num_Booleans;/* count total Booleans */
    unsigned short num_Numbers;	/* count total Numbers */
    unsigned short num_Strings;	/* count total Strings */

    unsigned short ext_Booleans;/* count extensions to Booleans */
    unsigned short ext_Numbers;	/* count extensions to Numbers */
    unsigned short ext_Strings;	/* count extensions to Strings */
#endif /* NCURSES_XNAMES */

} TERMTYPE2;
#else

typedef struct term {		/* describe an actual terminal */
    TERMTYPE	type;		/* terminal type description */
} TERMINAL;

#endif /* NCURSES_INTERNALS */


#if 0 && !0
extern NCURSES_EXPORT_VAR(TERMINAL *) cur_term;
#elif 0
NCURSES_WRAPPED_VAR(TERMINAL *, cur_term);
#define cur_term   NCURSES_PUBLIC_VAR(cur_term())
#else
extern NCURSES_EXPORT_VAR(TERMINAL *) cur_term;
#endif

#if 0 || 0
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, boolnames);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, boolcodes);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, boolfnames);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, numnames);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, numcodes);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, numfnames);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, strnames);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, strcodes);
NCURSES_WRAPPED_VAR(NCURSES_CONST char * const *, strfnames);

#define boolnames  NCURSES_PUBLIC_VAR(boolnames())
#define boolcodes  NCURSES_PUBLIC_VAR(boolcodes())
#define boolfnames NCURSES_PUBLIC_VAR(boolfnames())
#define numnames   NCURSES_PUBLIC_VAR(numnames())
#define numcodes   NCURSES_PUBLIC_VAR(numcodes())
#define numfnames  NCURSES_PUBLIC_VAR(numfnames())
#define strnames   NCURSES_PUBLIC_VAR(strnames())
#define strcodes   NCURSES_PUBLIC_VAR(strcodes())
#define strfnames  NCURSES_PUBLIC_VAR(strfnames())

#else

extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) boolnames[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) boolcodes[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) boolfnames[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) numnames[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) numcodes[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) numfnames[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) strnames[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) strcodes[];
extern NCURSES_EXPORT_VAR(NCURSES_CONST char * const ) strfnames[];

#endif

/*
 * These entrypoints are used only by the ncurses utilities such as tic.
 */
#ifdef NCURSES_INTERNALS

extern NCURSES_EXPORT(int) _nc_set_tty_mode (TTY *buf);
extern NCURSES_EXPORT(int) _nc_read_entry2 (const char * const, char * const, TERMTYPE2 *const);
extern NCURSES_EXPORT(int) _nc_read_file_entry (const char *const, TERMTYPE2 *);
extern NCURSES_EXPORT(int) _nc_read_termtype (TERMTYPE2 *, char *, int);
extern NCURSES_EXPORT(char *) _nc_first_name (const char *const);
extern NCURSES_EXPORT(int) _nc_name_match (const char *const, const char *const, const char *const);
extern NCURSES_EXPORT(char *) _nc_tiparm(int, const char *, ...);
extern NCURSES_EXPORT(const TERMTYPE *) _nc_fallback (const char *);
extern NCURSES_EXPORT(int) _nc_read_entry (const char * const, char * const, TERMTYPE *const);

#endif /* NCURSES_INTERNALS */

/*
 * Normal entry points
 */
extern NCURSES_EXPORT(TERMINAL *) set_curterm (TERMINAL *);
extern NCURSES_EXPORT(int) del_curterm (TERMINAL *);

/* miscellaneous entry points */
extern NCURSES_EXPORT(int) restartterm (NCURSES_CONST char *, int, int *);
extern NCURSES_EXPORT(int) setupterm (const char *,int,int *);

/* terminfo entry points, also declared in curses.h */
#if !defined(__NCURSES_H)
extern NCURSES_EXPORT(char *) tigetstr (const char *);
extern NCURSES_EXPORT_VAR(char) ttytype[];
extern NCURSES_EXPORT(int) putp (const char *);
extern NCURSES_EXPORT(int) tigetflag (const char *);
extern NCURSES_EXPORT(int) tigetnum (const char *);

#if 1 /* NCURSES_TPARM_VARARGS */
extern NCURSES_EXPORT(char *) tparm (const char *, ...);	/* special */
#else
extern NCURSES_EXPORT(char *) tparm (const char *, long,long,long,long,long,long,long,long,long);	/* special */
#endif

extern NCURSES_EXPORT(char *) tiparm (const char *, ...);		/* special */
extern NCURSES_EXPORT(char *) tiparm_s (int, int, const char *, ...);	/* special */
extern NCURSES_EXPORT(int) tiscan_s (int *, int *, const char *);	/* special */

#endif /* __NCURSES_H */

/* termcap database emulation (XPG4 uses const only for 2nd param of tgetent) */
#if !defined(NCURSES_TERMCAP_H_incl)
extern NCURSES_EXPORT(char *) tgetstr (const char *, char **);
extern NCURSES_EXPORT(char *) tgoto (const char *, int, int);
extern NCURSES_EXPORT(int) tgetent (char *, const char *);
extern NCURSES_EXPORT(int) tgetflag (const char *);
extern NCURSES_EXPORT(int) tgetnum (const char *);
extern NCURSES_EXPORT(int) tputs (const char *, int, int (*)(int));
#endif /* NCURSES_TERMCAP_H_incl */

/*
 * Include curses.h before term.h to enable these extensions.
 */
#if defined(NCURSES_SP_FUNCS) && (NCURSES_SP_FUNCS != 0)

extern NCURSES_EXPORT(char *)  NCURSES_SP_NAME(tigetstr) (SCREEN*, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(putp) (SCREEN*, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tigetflag) (SCREEN*, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tigetnum) (SCREEN*, const char *);

#if 1 /* NCURSES_TPARM_VARARGS */
extern NCURSES_EXPORT(char *)  NCURSES_SP_NAME(tparm) (SCREEN*, const char *, ...);	/* special */
#else
extern NCURSES_EXPORT(char *)  NCURSES_SP_NAME(tparm) (SCREEN*, const char *, long,long,long,long,long,long,long,long,long);	/* special */
#endif

/* termcap database emulation (XPG4 uses const only for 2nd param of tgetent) */
extern NCURSES_EXPORT(char *)  NCURSES_SP_NAME(tgetstr) (SCREEN*, const char *, char **);
extern NCURSES_EXPORT(char *)  NCURSES_SP_NAME(tgoto) (SCREEN*, const char *, int, int);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tgetent) (SCREEN*, char *, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tgetflag) (SCREEN*, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tgetnum) (SCREEN*, const char *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(tputs) (SCREEN*, const char *, int, NCURSES_SP_OUTC);

extern NCURSES_EXPORT(TERMINAL *) NCURSES_SP_NAME(set_curterm) (SCREEN*, TERMINAL *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(del_curterm) (SCREEN*, TERMINAL *);

extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(restartterm) (SCREEN*, NCURSES_CONST char *, int, int *);
#endif /* NCURSES_SP_FUNCS */

/*
 * Debugging features.
 */
extern GCC_NORETURN NCURSES_EXPORT(void)    exit_terminfo(int);

#ifdef __cplusplus
}
#endif

#endif /* NCURSES_TERM_H_incl */
