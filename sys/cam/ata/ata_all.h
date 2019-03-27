/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	CAM_ATA_ALL_H
#define CAM_ATA_ALL_H 1

#include <sys/ata.h>

struct ccb_ataio;
struct cam_periph;
union  ccb;

#define	SID_DMA48	0x01 /* Abuse inq_flags bit to track enabled DMA48. */
#define	SID_AEN		0x04 /* Abuse inq_flags bit to track enabled AEN. */
#define	SID_DMA		0x10 /* Abuse inq_flags bit to track enabled DMA. */

struct ata_cmd {
	u_int8_t	flags;		/* ATA command flags */
#define		CAM_ATAIO_48BIT		0x01	/* Command has 48-bit format */
#define		CAM_ATAIO_FPDMA		0x02	/* FPDMA command */
#define		CAM_ATAIO_CONTROL	0x04	/* Control, not a command */
#define		CAM_ATAIO_NEEDRESULT	0x08	/* Request requires result. */
#define		CAM_ATAIO_DMA		0x10	/* DMA command */

	u_int8_t	command;
	u_int8_t	features;

	u_int8_t	lba_low;
	u_int8_t	lba_mid;
	u_int8_t	lba_high;
	u_int8_t	device;

	u_int8_t	lba_low_exp;
	u_int8_t	lba_mid_exp;
	u_int8_t	lba_high_exp;
	u_int8_t	features_exp;

	u_int8_t	sector_count;
	u_int8_t	sector_count_exp;
	u_int8_t	control;
};

struct ata_res {
	u_int8_t	flags;		/* ATA command flags */
#define		CAM_ATAIO_48BIT		0x01	/* Command has 48-bit format */

	u_int8_t	status;
	u_int8_t	error;

	u_int8_t	lba_low;
	u_int8_t	lba_mid;
	u_int8_t	lba_high;
	u_int8_t	device;

	u_int8_t	lba_low_exp;
	u_int8_t	lba_mid_exp;
	u_int8_t	lba_high_exp;

	u_int8_t	sector_count;
	u_int8_t	sector_count_exp;
};

struct sep_identify_data {
	uint8_t		length;		/* Enclosure descriptor length */
	uint8_t		subenc_id;	/* Sub-enclosure identifier */
	uint8_t		logical_id[8];	/* Enclosure logical identifier (WWN) */
	uint8_t		vendor_id[8];	/* Vendor identification string */
	uint8_t		product_id[16];	/* Product identification string */
	uint8_t		product_rev[4];	/* Product revision string */
	uint8_t		channel_id;	/* Channel identifier */
	uint8_t		firmware_rev[4];/* Firmware revision */
	uint8_t		interface_id[6];/* Interface spec ("S-E-S "/"SAF-TE")*/
	uint8_t		interface_rev[4];/* Interface spec revision */
	uint8_t		vend_spec[11];	/* Vendor specific information */
};

int	ata_version(int ver);

char *	ata_op_string(struct ata_cmd *cmd);
char *	ata_cmd_string(struct ata_cmd *cmd, char *cmd_string, size_t len);
void	ata_cmd_sbuf(struct ata_cmd *cmd, struct sbuf *sb);
char *	ata_res_string(struct ata_res *res, char *res_string, size_t len);
int	ata_command_sbuf(struct ccb_ataio *ataio, struct sbuf *sb);
int	ata_status_sbuf(struct ccb_ataio *ataio, struct sbuf *sb);
int	ata_res_sbuf(struct ata_res *res, struct sbuf *sb);

void	ata_print_ident(struct ata_params *ident_data);
void	ata_print_ident_sbuf(struct ata_params *ident_data, struct sbuf *sb);
void	ata_print_ident_short(struct ata_params *ident_data);
void	ata_print_ident_short_sbuf(struct ata_params *ident_data, struct sbuf *sb);

uint32_t	ata_logical_sector_size(struct ata_params *ident_data);
uint64_t	ata_physical_sector_size(struct ata_params *ident_data);
uint64_t	ata_logical_sector_offset(struct ata_params *ident_data);

void	ata_28bit_cmd(struct ccb_ataio *ataio, uint8_t cmd, uint8_t features,
    uint32_t lba, uint8_t sector_count);
void	ata_48bit_cmd(struct ccb_ataio *ataio, uint8_t cmd, uint16_t features,
    uint64_t lba, uint16_t sector_count);
void	ata_ncq_cmd(struct ccb_ataio *ataio, uint8_t cmd,
    uint64_t lba, uint16_t sector_count);
void	ata_reset_cmd(struct ccb_ataio *ataio);
void	ata_pm_read_cmd(struct ccb_ataio *ataio, int reg, int port);
void	ata_pm_write_cmd(struct ccb_ataio *ataio, int reg, int port, uint32_t val);
void	ata_read_log(struct ccb_ataio *ataio, uint32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     uint32_t log_address, uint32_t page_number,
		     uint16_t block_count, uint32_t protocol,
		     uint8_t *data_ptr, uint32_t dxfer_len, uint32_t timeout);

void	ata_bswap(int8_t *buf, int len);
void	ata_btrim(int8_t *buf, int len);
void	ata_bpack(int8_t *src, int8_t *dst, int len);

int	ata_max_pmode(struct ata_params *ap);
int	ata_max_wmode(struct ata_params *ap);
int	ata_max_umode(struct ata_params *ap);
int	ata_max_mode(struct ata_params *ap, int maxmode);

char *	ata_mode2string(int mode);
int	ata_string2mode(char *str);
u_int	ata_mode2speed(int mode);
u_int	ata_revision2speed(int revision);
int	ata_speed2revision(u_int speed);

int	ata_identify_match(caddr_t identbuffer, caddr_t table_entry);
int	ata_static_identify_match(caddr_t identbuffer, caddr_t table_entry);

void	semb_print_ident(struct sep_identify_data *ident_data);
void	semb_print_ident_sbuf(struct sep_identify_data *ident_data, struct sbuf *sb);
void	semb_print_ident_short(struct sep_identify_data *ident_data);
void	semb_print_ident_short_sbuf(struct sep_identify_data *ident_data, struct sbuf *sb);

void semb_receive_diagnostic_results(struct ccb_ataio *ataio,
	u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb*),
	uint8_t tag_action, int pcv, uint8_t page_code,
	uint8_t *data_ptr, uint16_t allocation_length, uint32_t timeout);

void semb_send_diagnostic(struct ccb_ataio *ataio,
	u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb *),
	uint8_t tag_action, uint8_t *data_ptr, uint16_t param_list_length,
	uint32_t timeout);

void semb_read_buffer(struct ccb_ataio *ataio,
	u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb*),
	uint8_t tag_action, uint8_t page_code,
	uint8_t *data_ptr, uint16_t allocation_length, uint32_t timeout);

void semb_write_buffer(struct ccb_ataio *ataio,
	u_int32_t retries, void (*cbfcnp)(struct cam_periph *, union ccb *),
	uint8_t tag_action, uint8_t *data_ptr, uint16_t param_list_length,
	uint32_t timeout);

void ata_zac_mgmt_out(struct ccb_ataio *ataio, uint32_t retries, 
	void (*cbfcnp)(struct cam_periph *, union ccb *),
	int use_ncq __unused, uint8_t zm_action, uint64_t zone_id,
	uint8_t zone_flags, uint16_t sector_count, uint8_t *data_ptr,
	uint32_t dxfer_len, uint32_t timeout);

void ata_zac_mgmt_in(struct ccb_ataio *ataio, uint32_t retries, 
	void (*cbfcnp)(struct cam_periph *, union ccb *),
	int use_ncq __unused, uint8_t zm_action, uint64_t zone_id,
	uint8_t zone_flags, uint8_t *data_ptr, uint32_t dxfer_len,
	uint32_t timeout);

#endif
