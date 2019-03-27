/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 1999 Cameron Grant <cg@FreeBSD.org>
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

#ifndef _PCM_MIXER_H_
#define	_PCM_MIXER_H_

struct snd_mixer *mixer_create(device_t dev, kobj_class_t cls, void *devinfo,
    const char *desc);
int mixer_delete(struct snd_mixer *m);
int mixer_init(device_t dev, kobj_class_t cls, void *devinfo);
int mixer_uninit(device_t dev);
int mixer_reinit(device_t dev);
int mixer_ioctl_cmd(struct cdev *i_dev, u_long cmd, caddr_t arg, int mode, struct thread *td, int from);
int mixer_oss_mixerinfo(struct cdev *i_dev, oss_mixerinfo *mi);

int mixer_hwvol_init(device_t dev);
void mixer_hwvol_mute_locked(struct snd_mixer *m);
void mixer_hwvol_mute(device_t dev);
void mixer_hwvol_step_locked(struct snd_mixer *m, int l_step, int r_step);
void mixer_hwvol_step(device_t dev, int left_step, int right_step);

int mixer_busy(struct snd_mixer *m);

int mix_get_locked(struct snd_mixer *m, u_int dev, int *pleft, int *pright);
int mix_set_locked(struct snd_mixer *m, u_int dev, int left, int right);
int mix_set(struct snd_mixer *m, u_int dev, u_int left, u_int right);
int mix_get(struct snd_mixer *m, u_int dev);
int mix_setrecsrc(struct snd_mixer *m, u_int32_t src);
u_int32_t mix_getrecsrc(struct snd_mixer *m);
int mix_get_type(struct snd_mixer *m);

void mix_setdevs(struct snd_mixer *m, u_int32_t v);
void mix_setrecdevs(struct snd_mixer *m, u_int32_t v);
u_int32_t mix_getdevs(struct snd_mixer *m);
u_int32_t mix_getrecdevs(struct snd_mixer *m);
void mix_setparentchild(struct snd_mixer *m, u_int32_t parent, u_int32_t childs);
void mix_setrealdev(struct snd_mixer *m, u_int32_t dev, u_int32_t realdev);
u_int32_t mix_getparent(struct snd_mixer *m, u_int32_t dev);
u_int32_t mix_getchild(struct snd_mixer *m, u_int32_t dev);
void *mix_getdevinfo(struct snd_mixer *m);
struct mtx *mixer_get_lock(struct snd_mixer *m);

extern int mixer_count;

#define MIXER_CMD_DIRECT	0	/* send command within driver   */
#define MIXER_CMD_CDEV		1	/* send command from cdev/ioctl */

#define MIXER_TYPE_PRIMARY	0	/* mixer_init()   */
#define MIXER_TYPE_SECONDARY	1	/* mixer_create() */

/*
 * this is a kludge to allow hiding of the struct snd_mixer definition
 * 512 should be enough for all architectures
 */
#define MIXER_SIZE	(512 + sizeof(struct kobj) +		\
			    sizeof(oss_mixer_enuminfo))

#define MIXER_DECLARE(name) static DEFINE_CLASS(name, name ## _methods, MIXER_SIZE)

#endif				/* _PCM_MIXER_H_ */
