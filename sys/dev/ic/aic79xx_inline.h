/*	$OpenBSD: aic79xx_inline.h,v 1.5 2022/01/09 05:42:38 jsg Exp $	*/

/*
 * Copyright (c) 2004 Milos Urbanek, Kenneth R. Westerback & Marco Peereboom
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Inline routines shareable across OS platforms.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2003 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aic79xx_inline.h,v 1.14 2004/02/04 16:38:38 gibbs Exp $
 *
 */

#ifndef _AIC79XX_INLINE_H_
#define _AIC79XX_INLINE_H_

/******************************** Debugging ***********************************/
char *ahd_name(struct ahd_softc *ahd);

/************************ Sequencer Execution Control *************************/
void ahd_known_modes(struct ahd_softc *, ahd_mode, ahd_mode);
ahd_mode_state ahd_build_mode_state(struct ahd_softc *,
					    ahd_mode , ahd_mode );
void ahd_extract_mode_state(struct ahd_softc *, ahd_mode_state,
					    ahd_mode *, ahd_mode *);
void ahd_set_modes(struct ahd_softc *, ahd_mode, ahd_mode );
void ahd_update_modes(struct ahd_softc *);
void ahd_assert_modes(struct ahd_softc *, ahd_mode,
				      ahd_mode, const char *, int);
ahd_mode_state ahd_save_modes(struct ahd_softc *);
void ahd_restore_modes(struct ahd_softc *, ahd_mode_state);
int  ahd_is_paused(struct ahd_softc *);
void ahd_pause(struct ahd_softc *);

void ahd_unpause(struct ahd_softc *);

/*********************** Scatter Gather List Handling *************************/
void	*ahd_sg_setup(struct ahd_softc *ahd, struct scb *scb,
				      void *sgptr, bus_addr_t addr,
				      bus_size_t len, int last);
void	 ahd_setup_scb_common(struct ahd_softc *ahd,
					      struct scb *scb);
void	 ahd_setup_data_scb(struct ahd_softc *ahd,
					    struct scb *scb);
void	 ahd_setup_noxfer_scb(struct ahd_softc *ahd,
					      struct scb *scb);

/************************** Memory mapping routines ***************************/
size_t	ahd_sg_size(struct ahd_softc *);
void *
			ahd_sg_bus_to_virt(struct ahd_softc *, struct scb *,
					   uint32_t);
uint32_t
			ahd_sg_virt_to_bus(struct ahd_softc *, struct scb *,
					   void *);
void	ahd_sync_scb(struct ahd_softc *, struct scb *, int);
void	ahd_sync_sglist(struct ahd_softc *, struct scb *, int);
void	ahd_sync_sense(struct ahd_softc *, struct scb *, int);
uint32_t
			ahd_targetcmd_offset(struct ahd_softc *, u_int);

/*********************** Miscellaneous Support Functions **********************/
void	ahd_complete_scb(struct ahd_softc *, struct scb *);
void	ahd_update_residual(struct ahd_softc *, struct scb *);
struct ahd_initiator_tinfo *
			ahd_fetch_transinfo(struct ahd_softc *, char , u_int,
					    u_int, struct ahd_tmode_tstate **);
uint16_t
			ahd_inw(struct ahd_softc *, u_int);
void	ahd_outw(struct ahd_softc *, u_int, u_int);
uint32_t
			ahd_inl(struct ahd_softc *, u_int);
void	ahd_outl(struct ahd_softc *, u_int, uint32_t);
uint64_t		ahd_inq(struct ahd_softc *, u_int);
void			ahd_outq(struct ahd_softc *, u_int, uint64_t);
u_int	ahd_get_scbptr(struct ahd_softc *ahd);
void	ahd_set_scbptr(struct ahd_softc *ahd, u_int scbptr);
u_int	ahd_get_hnscb_qoff(struct ahd_softc *ahd);
void	ahd_set_hnscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_hescb_qoff(struct ahd_softc *ahd);
void	ahd_set_hescb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_snscb_qoff(struct ahd_softc *ahd);
void	ahd_set_snscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_sescb_qoff(struct ahd_softc *ahd);
void	ahd_set_sescb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_get_sdscb_qoff(struct ahd_softc *ahd);
void	ahd_set_sdscb_qoff(struct ahd_softc *ahd, u_int value);
u_int	ahd_inb_scbram(struct ahd_softc *ahd, u_int offset);
u_int	ahd_inw_scbram(struct ahd_softc *ahd, u_int offset);
uint32_t ahd_inl_scbram(struct ahd_softc *ahd, u_int offset);
uint64_t ahd_inq_scbram(struct ahd_softc *ahd, u_int offset);
struct	scb *ahd_lookup_scb(struct ahd_softc *, u_int);
void	ahd_swap_with_next_hscb(struct ahd_softc *ahd, struct scb *scb);
void	ahd_queue_scb(struct ahd_softc *ahd, struct scb *scb);
uint8_t *ahd_get_sense_buf(struct ahd_softc *ahd, struct scb *scb);
uint32_t ahd_get_sense_bufaddr(struct ahd_softc *ahd, struct scb *scb);

/************************** Interrupt Processing ******************************/
void	ahd_sync_qoutfifo(struct ahd_softc *ahd, int op);
void	ahd_sync_tqinfifo(struct ahd_softc *ahd, int op);
u_int			ahd_check_cmdcmpltqueues(struct ahd_softc *ahd);
int			ahd_intr(struct ahd_softc *ahd);

#define AHD_ASSERT_MODES(ahd, source, dest) \
	ahd_assert_modes(ahd, source, dest, __FILE__, __LINE__);

#endif  /* _AIC79XX_INLINE_H_ */
