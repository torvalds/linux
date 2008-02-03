#ifndef __SOUND_INFO_H
#define __SOUND_INFO_H

/*
 *  Header file for info interface
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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
	unsigned int curr;	/* current position in buffer */
	unsigned int size;	/* current size */
	unsigned int len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

#define SNDRV_INFO_CONTENT_TEXT		0
#define SNDRV_INFO_CONTENT_DATA		1

struct snd_info_entry;

struct snd_info_entry_text {
	void (*read) (struct snd_info_entry *entry, struct snd_info_buffer *buffer);
	void (*write) (struct snd_info_entry *entry, struct snd_info_buffer *buffer);
};

struct snd_info_entry_ops {
	int (*open) (struct snd_info_entry *entry,
		     unsigned short mode, void **file_private_data);
	int (*release) (struct snd_info_entry * entry,
			unsigned short mode, void *file_private_data);
	long (*read) (struct snd_info_entry *entry, void *file_private_data,
		      struct file * file, char __user *buf,
		      unsigned long count, unsigned long pos);
	long (*write) (struct snd_info_entry *entry, void *file_private_data,
		       struct file * file, const char __user *buf,
		       unsigned long count, unsigned long pos);
	long long (*llseek) (struct snd_info_entry *entry, void *file_private_data,
			    struct file * file, long long offset, int orig);
	unsigned int (*poll) (struct snd_info_entry *entry, void *file_private_data,
			      struct file * file, poll_table * wait);
	int (*ioctl) (struct snd_info_entry *entry, void *file_private_data,
		      struct file * file, unsigned int cmd, unsigned long arg);
	int (*mmap) (struct snd_info_entry *entry, void *file_private_data,
		     struct inode * inode, struct file * file,
		     struct vm_area_struct * vma);
};

struct snd_info_entry {
	const char *name;
	mode_t mode;
	long size;
	unsigned short content;
	union {
		struct snd_info_entry_text text;
		struct snd_info_entry_ops *ops;
	} c;
	struct snd_info_entry *parent;
	struct snd_card *card;
	struct module *module;
	void *private_data;
	void (*private_free)(struct snd_info_entry *entry);
	struct proc_dir_entry *p;
	struct mutex access;
	struct list_head children;
	struct list_head list;
};

#if defined(CONFIG_SND_OSSEMUL) && defined(CONFIG_PROC_FS)
int snd_info_minor_register(void);
int snd_info_minor_unregister(void);
#else
#define snd_info_minor_register() /* NOP */
#define snd_info_minor_unregister() /* NOP */
#endif


#ifdef CONFIG_PROC_FS

extern struct snd_info_entry *snd_seq_root;
#ifdef CONFIG_SND_OSSEMUL
extern struct snd_info_entry *snd_oss_root;
void snd_card_info_read_oss(struct snd_info_buffer *buffer);
#else
#define snd_oss_root NULL
static inline void snd_card_info_read_oss(struct snd_info_buffer *buffer) {}
#endif

int snd_iprintf(struct snd_info_buffer * buffer, char *fmt,...) __attribute__ ((format (printf, 2, 3)));
int snd_info_init(void);
int snd_info_done(void);

int snd_info_get_line(struct snd_info_buffer * buffer, char *line, int len);
char *snd_info_get_str(char *dest, char *src, int len);
struct snd_info_entry *snd_info_create_module_entry(struct module * module,
					       const char *name,
					       struct snd_info_entry * parent);
struct snd_info_entry *snd_info_create_card_entry(struct snd_card * card,
					     const char *name,
					     struct snd_info_entry * parent);
void snd_info_free_entry(struct snd_info_entry * entry);
int snd_info_store_text(struct snd_info_entry * entry);
int snd_info_restore_text(struct snd_info_entry * entry);

int snd_info_card_create(struct snd_card * card);
int snd_info_card_register(struct snd_card * card);
int snd_info_card_free(struct snd_card * card);
void snd_info_card_disconnect(struct snd_card * card);
int snd_info_register(struct snd_info_entry * entry);

/* for card drivers */
int snd_card_proc_new(struct snd_card *card, const char *name, struct snd_info_entry **entryp);

static inline void snd_info_set_text_ops(struct snd_info_entry *entry, 
					 void *private_data,
					 void (*read)(struct snd_info_entry *, struct snd_info_buffer *))
{
	entry->private_data = private_data;
	entry->c.text.read = read;
}

int snd_info_check_reserved_words(const char *str);

#else

#define snd_seq_root NULL
#define snd_oss_root NULL

static inline int snd_iprintf(struct snd_info_buffer * buffer, char *fmt,...) { return 0; }
static inline int snd_info_init(void) { return 0; }
static inline int snd_info_done(void) { return 0; }

static inline int snd_info_get_line(struct snd_info_buffer * buffer, char *line, int len) { return 0; }
static inline char *snd_info_get_str(char *dest, char *src, int len) { return NULL; }
static inline struct snd_info_entry *snd_info_create_module_entry(struct module * module, const char *name, struct snd_info_entry * parent) { return NULL; }
static inline struct snd_info_entry *snd_info_create_card_entry(struct snd_card * card, const char *name, struct snd_info_entry * parent) { return NULL; }
static inline void snd_info_free_entry(struct snd_info_entry * entry) { ; }

static inline int snd_info_card_create(struct snd_card * card) { return 0; }
static inline int snd_info_card_register(struct snd_card * card) { return 0; }
static inline int snd_info_card_free(struct snd_card * card) { return 0; }
static inline void snd_info_card_disconnect(struct snd_card * card) { }
static inline int snd_info_register(struct snd_info_entry * entry) { return 0; }

static inline int snd_card_proc_new(struct snd_card *card, const char *name,
				    struct snd_info_entry **entryp) { return -EINVAL; }
static inline void snd_info_set_text_ops(struct snd_info_entry *entry __attribute__((unused)),
					 void *private_data,
					 void (*read)(struct snd_info_entry *, struct snd_info_buffer *)) {}

static inline int snd_info_check_reserved_words(const char *str) { return 1; }

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

int snd_oss_info_register(int dev, int num, char *string);
#define snd_oss_info_unregister(dev, num) snd_oss_info_register(dev, num, NULL)

#endif /* CONFIG_SND_OSSEMUL && CONFIG_PROC_FS */

#endif /* __SOUND_INFO_H */
