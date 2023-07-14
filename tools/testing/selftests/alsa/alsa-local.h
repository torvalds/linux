// SPDX-License-Identifier: GPL-2.0
//
// kselftest configuration helpers for the hw specific configuration
//
// Original author: Jaroslav Kysela <perex@perex.cz>
// Copyright (c) 2022 Red Hat Inc.

#ifndef __ALSA_LOCAL_H
#define __ALSA_LOCAL_H

#include <alsa/asoundlib.h>

snd_config_t *get_alsalib_config(void);

snd_config_t *conf_load_from_file(const char *filename);
void conf_load(void);
void conf_free(void);
snd_config_t *conf_by_card(int card);
snd_config_t *conf_get_subtree(snd_config_t *root, const char *key1, const char *key2);
int conf_get_count(snd_config_t *root, const char *key1, const char *key2);
const char *conf_get_string(snd_config_t *root, const char *key1, const char *key2, const char *def);
long conf_get_long(snd_config_t *root, const char *key1, const char *key2, long def);
int conf_get_bool(snd_config_t *root, const char *key1, const char *key2, int def);
void conf_get_string_array(snd_config_t *root, const char *key1, const char *key2,
			   const char **array, int array_size, const char *def);

#endif /* __ALSA_LOCAL_H */
