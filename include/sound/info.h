#ifndef __SOUND_INFO_H
#define __SOUND_INFO_H

/*
 *  Header file for info interface
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/poll.h>

/* buffer for information */
struct snd_info_buffer {
	char *buffer;		/* pointer to begin of buffer */
	char *curr;		/* current position in buffer */
	unsigned long size;	/* current size */
	unsigned long len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

typedef struct snd_info_buffer snd_info_buffer_t;

#define SNDRV_INFO_CONTENT_TEXT		0
#define SNDRV_INFO_CONTENT_DATA		1

struct snd_info_entry;

struct snd_info_entry_text {
	unsigned long read_size;
	unsigned long write_size;
	void (*read) (snd_info_entry_t *entry, snd_info_buffer_t * buffer);
	void (*write) (snd_info_entry_t *entry, snd_info_buffer_t * buffer);
};

struct snd_info_entry_ops {
	int (*open) (snd_info_entry_t *entry,
		     unsigned short mode, void **file_private_data);
	int (*release) (snd_info_entry_t * entry,
			unsigned short mode, void *file_private_data);
	long (*read) (snd_info_entry_t *entry, void *file_private_data,
		      struct file * file, char __user *buf,
		      unsigned long count, unsigned long pos);
	long (*write) (snd_info_entry_t *entry, void *file_private_data,
		       struct file * file, const char __user *buf,
		       unsigned long count, unsigned long pos);
	long long (*llseek) (snd_info_entry_t *entry, void *file_private_data,
			    struct file * file, long long offset, int orig);
	unsigned int (*poll) (snd_info_entry_t *entry, void *file_private_data,
			      struct file * file, poll_table * wait);
	int (*ioctl) (snd_info_entry_t *entry, void *file_private_data,
		      struct file * file, unsigned int cmd, unsigned long arg);
	int (*mmap) (snd_info_entry_t *entry, void *file_private_data,
		     struct inode * inode, struct file * file,
		     struct vm_area_struct * vma);
};

struct snd_info_entry {
	const char *name;
	mode_t mode;
	long size;
	unsigned short content;
	unsigned short disconnected: 1;
	union {
		struct snd_info_entry_text text;
		struct snd_info_entry_ops *ops;
	} c;
	snd_info_entry_t *parent;
	snd_card_t *card;
	struct module *module;
	void *private_data;
	void (*private_free)(snd_info_entry_t *entry);
	struct proc_dir_entry *p;
	struct semaphore access;
};

extern int snd_info_check_reserved_words(const char *str);

#if defined(CONFIG_SND_OSSEMUL) && defined(CONFIG_PROC_FS)
extern int snd_info_minor_register(void);
extern int snd_info_minor_unregister(void);
#else
#define snd_info_minor_register() /* NOP */
#define snd_info_minor_unregister() /* NOP */
#endif


#ifdef CONFIG_PROC_FS

extern snd_info_entry_t *snd_seq_root;
#ifdef CONFIG_SND_OSSEMUL
extern snd_info_entry_t *snd_oss_root;
#else
#define snd_oss_root NULL
#endif

int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...) __attribute__ ((format (printf, 2, 3)));
int snd_info_init(void);
int snd_info_done(void);

int snd_info_get_line(snd_info_buffer_t * buffer, char *line, int len);
char *snd_info_get_str(char *dest, char *src, int len);
snd_info_entry_t *snd_info_create_module_entry(struct module * module,
					       const char *name,
					       snd_info_entry_t * parent);
snd_info_entry_t *snd_info_create_card_entry(snd_card_t * card,
					     const char *name,
					     snd_info_entry_t * parent);
void snd_info_free_entry(snd_info_entry_t * entry);
int snd_info_store_text(snd_info_entry_t * entry);
int snd_info_restore_text(snd_info_entry_t * entry);

int snd_info_card_create(snd_card_t * card);
int snd_info_card_register(snd_card_t * card);
int snd_info_card_free(snd_card_t * card);
int snd_info_register(snd_info_entry_t * entry);
int snd_info_unregister(snd_info_entry_t * entry);

/* for card drivers */
int snd_card_proc_new(snd_card_t *card, const char *name, snd_info_entry_t **entryp);

static inline void snd_info_set_text_ops(snd_info_entry_t *entry, 
					 void *private_data,
					 long read_size,
					 void (*read)(snd_info_entry_t *, snd_info_buffer_t *))
{
	entry->private_data = private_data;
	entry->c.text.read_size = read_size;
	entry->c.text.read = read;
}


#else

#define snd_seq_root NULL
#define snd_oss_root NULL

static inline int snd_iprintf(snd_info_buffer_t * buffer, char *fmt,...) { return 0; }
static inline int snd_info_init(void) { return 0; }
static inline int snd_info_done(void) { return 0; }

static inline int snd_info_get_line(snd_info_buffer_t * buffer, char *line, int len) { return 0; }
static inline char *snd_info_get_str(char *dest, char *src, int len) { return NULL; }
static inline snd_info_entry_t *snd_info_create_module_entry(struct module * module, const char *name, snd_info_entry_t * parent) { return NULL; }
static inline snd_info_entry_t *snd_info_create_card_entry(snd_card_t * card, const char *name, snd_info_entry_t * parent) { return NULL; }
static inline void snd_info_free_entry(snd_info_entry_t * entry) { ; }

static inline int snd_info_card_create(snd_card_t * card) { return 0; }
static inline int snd_info_card_register(snd_card_t * card) { return 0; }
static inline int snd_info_card_free(snd_card_t * card) { return 0; }
static inline int snd_info_register(snd_info_entry_t * entry) { return 0; }
static inline int snd_info_unregister(snd_info_entry_t * entry) { return 0; }

#define snd_card_proc_new(card,name,entryp)  0 /* always success */
#define snd_info_set_text_ops(entry,private_data,read_size,read) /*NOP*/

#endif

/*
 * OSS info part
 */

#if defined(CONFIG_SND_OSSEMUL) && defined(CONFIG_PROC_FS)

#define SNDRV_OSS_INFO_DEV_AUDIO	0
#define SNDRV_OSS_INFO_DEV_SYNTH	1
#define SNDRV_OSS_INFO_DEV_MIDI		2
#define SNDRV_OSS_INFO_DEV_TIMERS	4
#define SNDRV_OSS_INFO_DEV_MIXERS	5

#define SNDRV_OSS_INFO_DEV_COUNT	6

extern int snd_oss_info_register(int dev, int num, char *string);
#define snd_oss_info_unregister(dev, num) snd_oss_info_register(dev, num, NULL)

#endif /* CONFIG_SND_OSSEMUL && CONFIG_PROC_FS */

#endif /* __SOUND_INFO_H */
