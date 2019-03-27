/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_GEOM_VINUM_H_
#define	_GEOM_VINUM_H_

/* geom_vinum_create.c */
void	gv_concat(struct g_geom *gp, struct gctl_req *);
void	gv_mirror(struct g_geom *gp, struct gctl_req *);
void	gv_stripe(struct g_geom *gp, struct gctl_req *);
void	gv_raid5(struct g_geom *gp, struct gctl_req *);
int	gv_create_drive(struct gv_softc *, struct gv_drive *);
int	gv_create_volume(struct gv_softc *, struct gv_volume *);
int	gv_create_plex(struct gv_softc *, struct gv_plex *);
int	gv_create_sd(struct gv_softc *, struct gv_sd *);

/* geom_vinum_drive.c */
void	gv_save_config(struct gv_softc *);
int	gv_read_header(struct g_consumer *, struct gv_hdr *);
int	gv_write_header(struct g_consumer *, struct gv_hdr *);

/* geom_vinum_init.c */
void	gv_start_obj(struct g_geom *, struct gctl_req *);
int	gv_start_plex(struct gv_plex *);
int	gv_start_vol(struct gv_volume *);

/* geom_vinum_list.c */
void	gv_ld(struct g_geom *, struct gctl_req *, struct sbuf *);
void	gv_lp(struct g_geom *, struct gctl_req *, struct sbuf *);
void	gv_ls(struct g_geom *, struct gctl_req *, struct sbuf *);
void	gv_lv(struct g_geom *, struct gctl_req *, struct sbuf *);
void	gv_list(struct g_geom *, struct gctl_req *);

/* geom_vinum_move.c */
void	gv_move(struct g_geom *, struct gctl_req *);
int	gv_move_sd(struct gv_softc *, struct gv_sd *, struct gv_drive *, int);

/* geom_vinum_rename.c */
void	gv_rename(struct g_geom *, struct gctl_req *);
int	gv_rename_drive(struct gv_softc *, struct gv_drive *, char *, int);
int	gv_rename_plex(struct gv_softc *, struct gv_plex *, char *, int);
int	gv_rename_sd(struct gv_softc *, struct gv_sd *, char *, int);
int	gv_rename_vol(struct gv_softc *, struct gv_volume *, char *, int);

/* geom_vinum_rm.c */
void	gv_remove(struct g_geom *, struct gctl_req *);
int	gv_resetconfig(struct gv_softc *);
void	gv_rm_sd(struct gv_softc *sc, struct gv_sd *s);
void	gv_rm_drive(struct gv_softc *, struct gv_drive *, int);
void	gv_rm_plex(struct gv_softc *, struct gv_plex *);
void	gv_rm_vol(struct gv_softc *, struct gv_volume *);


/* geom_vinum_state.c */
int	gv_sdstatemap(struct gv_plex *);
void	gv_setstate(struct g_geom *, struct gctl_req *);
int	gv_set_drive_state(struct gv_drive *, int, int);
int	gv_set_sd_state(struct gv_sd *, int, int);
int	gv_set_vol_state(struct gv_volume *, int, int);
int	gv_set_plex_state(struct gv_plex *, int, int);
void	gv_update_sd_state(struct gv_sd *);
void	gv_update_plex_state(struct gv_plex *);
void	gv_update_vol_state(struct gv_volume *);

/* geom_vinum_subr.c */
void		 	 gv_adjust_freespace(struct gv_sd *, off_t);
void		 	 gv_free_sd(struct gv_sd *);
struct gv_drive		*gv_find_drive(struct gv_softc *, char *);
struct gv_drive		*gv_find_drive_device(struct gv_softc *, char *);
struct gv_plex		*gv_find_plex(struct gv_softc *, char *);
struct gv_sd		*gv_find_sd(struct gv_softc *, char *);
struct gv_volume	*gv_find_vol(struct gv_softc *, char *);
void			 gv_format_config(struct gv_softc *, struct sbuf *, int,
			     char *);
int			 gv_is_striped(struct gv_plex *);
int			 gv_consumer_is_open(struct g_consumer *);
int			 gv_provider_is_open(struct g_provider *);
int			 gv_object_type(struct gv_softc *, char *);
void			 gv_parse_config(struct gv_softc *, char *,
			     struct gv_drive *);
int			 gv_sd_to_drive(struct gv_sd *, struct gv_drive *);
int			 gv_sd_to_plex(struct gv_sd *, struct gv_plex *);
int			 gv_sdcount(struct gv_plex *, int);
void			 gv_update_plex_config(struct gv_plex *);
void			 gv_update_vol_size(struct gv_volume *, off_t);
off_t			 gv_vol_size(struct gv_volume *);
off_t			 gv_plex_size(struct gv_plex *);
int			 gv_plexdown(struct gv_volume *);
int			 gv_attach_plex(struct gv_plex *, struct gv_volume *,
			     int);
int			 gv_attach_sd(struct gv_sd *, struct gv_plex *, off_t,
			     int);
int			 gv_detach_plex(struct gv_plex *, int);
int			 gv_detach_sd(struct gv_sd *, int);

/* geom_vinum.c */
void	gv_worker(void *);
void	gv_post_event(struct gv_softc *, int, void *, void *, intmax_t,
	    intmax_t);
void	gv_worker_exit(struct gv_softc *);
struct gv_event *gv_get_event(struct gv_softc *);
void	gv_remove_event(struct gv_softc *, struct gv_event *);
void	gv_drive_tasted(struct gv_softc *, struct g_provider *);
void	gv_drive_lost(struct gv_softc *, struct gv_drive *);
void	gv_setup_objects(struct gv_softc *);
void	gv_start(struct bio *);
int	gv_access(struct g_provider *, int, int, int);
void	gv_cleanup(struct gv_softc *);

/* geom_vinum_volume.c */
void	gv_done(struct bio *);
void	gv_volume_start(struct gv_softc *, struct bio *);
void	gv_volume_flush(struct gv_volume *);
void	gv_bio_done(struct gv_softc *, struct bio *);

/* geom_vinum_plex.c */
void	gv_plex_start(struct gv_plex *, struct bio *);
void	gv_plex_raid5_done(struct gv_plex *, struct bio *);
void	gv_plex_normal_done(struct gv_plex *, struct bio *);
int	gv_grow_request(struct gv_plex *, off_t, off_t, int, caddr_t);
void	gv_grow_complete(struct gv_plex *, struct bio *);
void	gv_init_request(struct gv_sd *, off_t, caddr_t, off_t);
void	gv_init_complete(struct gv_plex *, struct bio *);
void	gv_parity_request(struct gv_plex *, int, off_t);
void	gv_parity_complete(struct gv_plex *, struct bio *);
void	gv_rebuild_complete(struct gv_plex *, struct bio *);
int	gv_sync_request(struct gv_plex *, struct gv_plex *, off_t, off_t, int,
	    caddr_t);
int	gv_sync_complete(struct gv_plex *, struct bio *);

extern	u_int	g_vinum_debug;

#define	G_VINUM_DEBUG(lvl, ...)	do {					\
	if (g_vinum_debug >= (lvl)) {					\
		printf("GEOM_VINUM");					\
		if (g_vinum_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

#define	G_VINUM_LOGREQ(lvl, bp, ...)	do {				\
	if (g_vinum_debug >= (lvl)) {					\
		printf("GEOM_VINUM");					\
		if (g_vinum_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

#endif /* !_GEOM_VINUM_H_ */
