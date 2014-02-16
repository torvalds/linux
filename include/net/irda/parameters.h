/*********************************************************************
 *                
 * Filename:      parameters.h
 * Version:       1.0
 * Description:   A more general way to handle (pi,pl,pv) parameters
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Jun  7 08:47:28 1999
 * Modified at:   Sun Jan 30 14:05:14 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *     Michel Dänzer <daenzer@debian.org>, 10/2001
 *     - simplify irda_pv_t to avoid endianness issues
 *     
 ********************************************************************/

#ifndef IRDA_PARAMS_H
#define IRDA_PARAMS_H

/*
 *  The currently supported types. Beware not to change the sequence since
 *  it a good reason why the sized integers has a value equal to their size
 */
typedef enum {
	PV_INTEGER,      /* Integer of any (pl) length */
	PV_INT_8_BITS,   /* Integer of 8 bits in length */
	PV_INT_16_BITS,  /* Integer of 16 bits in length */
	PV_STRING,       /* \0 terminated string */
	PV_INT_32_BITS,  /* Integer of 32 bits in length */
	PV_OCT_SEQ,      /* Octet sequence */
	PV_NO_VALUE      /* Does not contain any value (pl=0) */
} PV_TYPE;

/* Bit 7 of type field */
#define PV_BIG_ENDIAN    0x80 
#define PV_LITTLE_ENDIAN 0x00
#define PV_MASK          0x7f   /* To mask away endian bit */

#define PV_PUT 0
#define PV_GET 1

typedef union {
	char   *c;
	__u32   i;
	__u32 *ip;
} irda_pv_t;

typedef struct {
	__u8 pi;
	__u8 pl;
	irda_pv_t pv;
} irda_param_t;

typedef int (*PI_HANDLER)(void *self, irda_param_t *param, int get);
typedef int (*PV_HANDLER)(void *self, __u8 *buf, int len, __u8 pi,
			  PV_TYPE type, PI_HANDLER func);

typedef struct {
	PI_HANDLER func;  /* Handler for this parameter identifier */
	PV_TYPE    type;  /* Data type for this parameter */
} pi_minor_info_t;

typedef struct {
	pi_minor_info_t *pi_minor_call_table;
	int len;
} pi_major_info_t;

typedef struct {
	pi_major_info_t *tables;
	int              len;
	__u8             pi_mask;
	int              pi_major_offset;
} pi_param_info_t;

int irda_param_pack(__u8 *buf, char *fmt, ...);

int irda_param_insert(void *self, __u8 pi, __u8 *buf, int len, 
		      pi_param_info_t *info);
int irda_param_extract_all(void *self, __u8 *buf, int len, 
			   pi_param_info_t *info);

#define irda_param_insert_byte(buf,pi,pv) irda_param_pack(buf,"bbb",pi,1,pv)

#endif /* IRDA_PARAMS_H */

