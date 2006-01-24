/*******************************************************************************
*
*      "cs4281pm.c" --  Cirrus Logic-Crystal CS4281 linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (audio@crystal.cirrus.com).
*
*      This program is free software; you can redistribute it and/or modify
*      it under the terms of the GNU General Public License as published by
*      the Free Software Foundation; either version 2 of the License, or
*      (at your option) any later version.
*
*      This program is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*      GNU General Public License for more details.
*
*      You should have received a copy of the GNU General Public License
*      along with this program; if not, write to the Free Software
*      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* 12/22/00 trw - new file. 
*
*******************************************************************************/

#ifndef NOT_CS4281_PM
#include <linux/pm.h>

static int cs4281_suspend(struct cs4281_state *s);
static int cs4281_resume(struct cs4281_state *s);
/* 
* for now (12/22/00) only enable the pm_register PM support.
* allow these table entries to be null.
#define CS4281_SUSPEND_TBL cs4281_suspend_tbl
#define CS4281_RESUME_TBL cs4281_resume_tbl
*/
#define CS4281_SUSPEND_TBL cs4281_suspend_null
#define CS4281_RESUME_TBL cs4281_resume_null

#else /* CS4281_PM */
#define CS4281_SUSPEND_TBL cs4281_suspend_null
#define CS4281_RESUME_TBL cs4281_resume_null
#endif /* CS4281_PM */

