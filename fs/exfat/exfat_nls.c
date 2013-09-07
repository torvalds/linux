/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/************************************************************************/
/*                                                                      */
/*  PROJECT : exFAT & FAT12/16/32 File System                           */
/*  FILE    : exfat_nls.c                                               */
/*  PURPOSE : exFAT NLS Manager                                         */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  NOTES                                                               */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  REVISION HISTORY (Ver 0.9)                                          */
/*                                                                      */
/*  - 2010.11.15 [Joosun Hahn] : first writing                          */
/*                                                                      */
/************************************************************************/

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"

#include "exfat_nls.h"
#include "exfat_api.h"
#include "exfat_super.h"
#include "exfat.h"

#include <linux/nls.h>

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Local Variable Definitions                                          */
/*----------------------------------------------------------------------*/

static UINT16 bad_dos_chars[] = {
  /* + , ; = [ ] */
	0x002B, 0x002C, 0x003B, 0x003D, 0x005B, 0x005D,
	0xFF0B, 0xFF0C, 0xFF1B, 0xFF1D, 0xFF3B, 0xFF3D,
	0
};

static UINT16 bad_uni_chars[] = {
	/* " * / : < > ? \ | */
	0x0022,         0x002A, 0x002F, 0x003A,
	0x003C, 0x003E, 0x003F, 0x005C, 0x007C,
	0
};

/*----------------------------------------------------------------------*/
/*  Local Function Declarations                                         */
/*----------------------------------------------------------------------*/

static INT32  convert_uni_to_ch(struct nls_table *nls, UINT8 *ch, UINT16 uni, INT32 *lossy);
static INT32  convert_ch_to_uni(struct nls_table *nls, UINT16 *uni, UINT8 *ch, INT32 *lossy);

/*======================================================================*/
/*  Global Function Definitions                                         */
/*======================================================================*/

UINT16 nls_upper(struct super_block *sb, UINT16 a)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (EXFAT_SB(sb)->options.casesensitive)
		return(a);
	if (p_fs->vol_utbl != NULL && (p_fs->vol_utbl)[get_col_index(a)] != NULL)
		return (p_fs->vol_utbl)[get_col_index(a)][get_row_index(a)];
	else
		return a;
}

INT32 nls_dosname_cmp(struct super_block *sb, UINT8 *a, UINT8 *b)
{
	return(STRNCMP((void *) a, (void *) b, DOS_NAME_LENGTH));
} /* end of nls_dosname_cmp */

INT32 nls_uniname_cmp(struct super_block *sb, UINT16 *a, UINT16 *b)
{
	INT32 i;

	for (i = 0; i < MAX_NAME_LENGTH; i++, a++, b++) {
		if (nls_upper(sb, *a) != nls_upper(sb, *b)) return(1);
		if (*a == 0x0) return(0);
	}
	return(0);
} /* end of nls_uniname_cmp */

void nls_uniname_to_dosname(struct super_block *sb, DOS_NAME_T *p_dosname, UNI_NAME_T *p_uniname, INT32 *p_lossy)
{
	INT32 i, j, len, lossy = FALSE;
	UINT8 buf[MAX_CHARSET_SIZE];
	UINT8 lower = 0, upper = 0;
	UINT8 *dosname = p_dosname->name;
	UINT16 *uniname = p_uniname->name;
	UINT16 *p, *last_period;
	struct nls_table *nls = EXFAT_SB(sb)->nls_disk;

	for (i = 0; i < DOS_NAME_LENGTH; i++) {
		*(dosname+i) = ' ';
	}

	if (!nls_uniname_cmp(sb, uniname, (UINT16 *) UNI_CUR_DIR_NAME)) {
		*(dosname) = '.';
		p_dosname->name_case = 0x0;
		if (p_lossy != NULL) *p_lossy = FALSE;
		return;
	}

	if (!nls_uniname_cmp(sb, uniname, (UINT16 *) UNI_PAR_DIR_NAME)) {
		*(dosname) = '.';
		*(dosname+1) = '.';
		p_dosname->name_case = 0x0;
		if (p_lossy != NULL) *p_lossy = FALSE;
		return;
	}

	/* search for the last embedded period */
	last_period = NULL;
	for (p = uniname; *p; p++) {
		if (*p == (UINT16) '.') last_period = p;
	}

	i = 0;
	while (i < DOS_NAME_LENGTH) {
		if (i == 8) {
			if (last_period == NULL) break;

			if (uniname <= last_period) {
				if (uniname < last_period) lossy = TRUE;
				uniname = last_period + 1;
			}
		}

		if (*uniname == (UINT16) '\0') {
			break;
		} else if (*uniname == (UINT16) ' ') {
			lossy = TRUE;
		} else if (*uniname == (UINT16) '.') {
			if (uniname < last_period) lossy = TRUE;
			else i = 8;
		} else if (WSTRCHR(bad_dos_chars, *uniname)) {
			lossy = TRUE;
			*(dosname+i) = '_';
			i++;
		} else {
			len = convert_uni_to_ch(nls, buf, *uniname, &lossy);

			if (len > 1) {
				if ((i >= 8) && ((i+len) > DOS_NAME_LENGTH)) {
					break;
				}
				if ((i <  8) && ((i+len) > 8)) {
					i = 8;
					continue;
				}

				lower = 0xFF;

				for (j = 0; j < len; j++, i++) {
					*(dosname+i) = *(buf+j);
				}
			} else { /* len == 1 */
				if ((*buf >= 'a') && (*buf <= 'z')) {
					*(dosname+i) = *buf - ('a' - 'A');

					if (i < 8) lower |= 0x08;
					else lower |= 0x10;
				} else if ((*buf >= 'A') && (*buf <= 'Z')) {
					*(dosname+i) = *buf;

					if (i < 8) upper |= 0x08;
					else upper |= 0x10;
				} else {
					*(dosname+i) = *buf;
				}
				i++;
			}
		}

		uniname++;
	}

	if (*dosname == 0xE5) *dosname = 0x05;
	if (*uniname != 0x0) lossy = TRUE;

	if (upper & lower) p_dosname->name_case = 0xFF;
	else p_dosname->name_case = lower;

	if (p_lossy != NULL) *p_lossy = lossy;
} /* end of nls_uniname_to_dosname */

void nls_dosname_to_uniname(struct super_block *sb, UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname)
{
	INT32 i = 0, j, n = 0;
	UINT8 buf[DOS_NAME_LENGTH+2];
	UINT8 *dosname = p_dosname->name;
	UINT16 *uniname = p_uniname->name;
	struct nls_table *nls = EXFAT_SB(sb)->nls_disk;

	if (*dosname == 0x05) {
		*buf = 0xE5;
		i++;
		n++;
	}

	for ( ; i < 8; i++, n++) {
		if (*(dosname+i) == ' ') break;

		if ((*(dosname+i) >= 'A') && (*(dosname+i) <= 'Z') && (p_dosname->name_case & 0x08))
			*(buf+n) = *(dosname+i) + ('a' - 'A');
		else
			*(buf+n) = *(dosname+i);
	}
	if (*(dosname+8) != ' ') {
		*(buf+n) = '.';
		n++;
	}

	for (i = 8; i < DOS_NAME_LENGTH; i++, n++) {
		if (*(dosname+i) == ' ') break;

		if ((*(dosname+i) >= 'A') && (*(dosname+i) <= 'Z') && (p_dosname->name_case & 0x10))
			*(buf+n) = *(dosname+i) + ('a' - 'A');
		else
			*(buf+n) = *(dosname+i);
	}
	*(buf+n) = '\0';

	i = j = 0;
	while (j < (MAX_NAME_LENGTH-1)) {
		if (*(buf+i) == '\0') break;

		i += convert_ch_to_uni(nls, uniname, (buf+i), NULL);

		uniname++;
		j++;
	}

	*uniname = (UINT16) '\0';
} /* end of nls_dosname_to_uniname */

void nls_uniname_to_cstring(struct super_block *sb, UINT8 *p_cstring, UNI_NAME_T *p_uniname)
{
	INT32 i, j, len;
	UINT8 buf[MAX_CHARSET_SIZE];
	UINT16 *uniname = p_uniname->name;
	struct nls_table *nls = EXFAT_SB(sb)->nls_io;

	i = 0;
	while (i < (MAX_NAME_LENGTH-1)) {
		if (*uniname == (UINT16) '\0') break;

		len = convert_uni_to_ch(nls, buf, *uniname, NULL);

		if (len > 1) {
			for (j = 0; j < len; j++)
				*p_cstring++ = (INT8) *(buf+j);
		} else { /* len == 1 */
			*p_cstring++ = (INT8) *buf;
		}

		uniname++;
		i++;
	}

	*p_cstring = '\0';
} /* end of nls_uniname_to_cstring */

void nls_cstring_to_uniname(struct super_block *sb, UNI_NAME_T *p_uniname, UINT8 *p_cstring, INT32 *p_lossy)
{
	INT32 i, j, lossy = FALSE;
	UINT8 *end_of_name;
	UINT8 upname[MAX_NAME_LENGTH * 2];
	UINT16 *uniname = p_uniname->name;
	struct nls_table *nls = EXFAT_SB(sb)->nls_io;


	/* strip all trailing spaces */
	end_of_name = p_cstring + STRLEN((INT8 *) p_cstring);

	while (*(--end_of_name) == ' ') {
		if (end_of_name < p_cstring) break;
	}
	*(++end_of_name) = '\0';

	if (STRCMP((INT8 *) p_cstring, ".") && STRCMP((INT8 *) p_cstring, "..")) {

		/* strip all trailing periods */
		while (*(--end_of_name) == '.') {
			if (end_of_name < p_cstring) break;
		}
		*(++end_of_name) = '\0';
	}

	if (*p_cstring == '\0')
		lossy = TRUE;

	i = j = 0;
	while (j < (MAX_NAME_LENGTH-1)) {
		if (*(p_cstring+i) == '\0') break;

		i += convert_ch_to_uni(nls, uniname, (UINT8 *)(p_cstring+i), &lossy);

		if ((*uniname < 0x0020) || WSTRCHR(bad_uni_chars, *uniname))
			lossy = TRUE;

		SET16_A(upname + j * 2, nls_upper(sb, *uniname));

		uniname++;
		j++;
	}

	if (*(p_cstring+i) != '\0')
		lossy = TRUE;
	*uniname = (UINT16) '\0';

	p_uniname->name_len = j;
	p_uniname->name_hash = calc_checksum_2byte((void *) upname, j<<1, 0, CS_DEFAULT);

	if (p_lossy != NULL)
		*p_lossy = lossy;
} /* end of nls_cstring_to_uniname */

/*======================================================================*/
/*  Local Function Definitions                                          */
/*======================================================================*/

static INT32 convert_ch_to_uni(struct nls_table *nls, UINT16 *uni, UINT8 *ch, INT32 *lossy)
{
	int len;

	*uni = 0x0;

	if (ch[0] < 0x80) {
		*uni = (UINT16) ch[0];
		return(1);
	}

	if ((len = nls->char2uni(ch, NLS_MAX_CHARSET_SIZE, uni)) < 0) {
		/* conversion failed */
		printk("%s: fail to use nls \n", __func__);
		if (lossy != NULL)
			*lossy = TRUE;
		*uni = (UINT16) '_';
		if (!strcmp(nls->charset, "utf8")) return(1);
		else return(2);
	}

	return(len);
} /* end of convert_ch_to_uni */

static INT32 convert_uni_to_ch(struct nls_table *nls, UINT8 *ch, UINT16 uni, INT32 *lossy)
{
	int len;

	ch[0] = 0x0;

	if (uni < 0x0080) {
		ch[0] = (UINT8) uni;
		return(1);
	}

	if ((len = nls->uni2char(uni, ch, NLS_MAX_CHARSET_SIZE)) < 0) {
		/* conversion failed */
		printk("%s: fail to use nls \n", __func__);
		if (lossy != NULL) *lossy = TRUE;
		ch[0] = '_';
		return(1);
	}

	return(len);

} /* end of convert_uni_to_ch */

/* end of exfat_nls.c */
