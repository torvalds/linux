/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef DRM_GEM_NAMES_H
#define	DRM_GEM_NAMES_H

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>

struct drm_gem_name {
	uint32_t name;
	void *ptr;
	LIST_ENTRY(drm_gem_name) link;
};

struct drm_gem_names {
	struct mtx lock;
	LIST_HEAD(drm_gem_names_head, drm_gem_name) *names_hash;
	u_long hash_mask;
	struct unrhdr *unr;
};

void drm_gem_names_init(struct drm_gem_names *names);
void drm_gem_names_fini(struct drm_gem_names *names);
uint32_t drm_gem_find_name(struct drm_gem_names *names, void *ptr);
void *drm_gem_find_ptr(struct drm_gem_names *names, uint32_t name);
void *drm_gem_name_ref(struct drm_gem_names *names, uint32_t name,
    void (*ref)(void *));
int drm_gem_name_create(struct drm_gem_names *names, void *obj, uint32_t *name);
void drm_gem_names_foreach(struct drm_gem_names *names,
    int (*f)(uint32_t, void *, void *), void *arg);
void *drm_gem_names_remove(struct drm_gem_names *names, uint32_t name);

#endif
