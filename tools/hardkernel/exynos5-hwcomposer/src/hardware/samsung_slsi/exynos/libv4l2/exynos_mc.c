/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*!
 * \file      exynos_mc.c
 * \brief     source file for libexynosv4l2
 * \author    Jinsung Yang (jsgood.yang@samsung.com)
 * \author    Sangwoo Park (sw5771.park@samsung.com)
 * \date      2012/01/17
 *
 * <b>Revision History: </b>
 * - 2012/01/17: Jinsung Yang (jsgood.yang@samsung.com) \n
 *   Initial version
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <media.h>
#include <linux/kdev_t.h>
#include <linux/types.h>

#include "exynos_v4l2.h"

//#define LOG_NDEBUG 0
#define LOG_TAG "libexynosv4l2-mc"
#if defined(ANDROID)
#include <utils/Log.h>
#else
#include <log.h>
#endif

static inline unsigned int __media_entity_type(struct media_entity *entity)
{
    return entity->info.type & MEDIA_ENT_TYPE_MASK;
}

static void __media_debug_default(void *ptr, ...)
{
    va_list argptr;
    va_start(argptr, ptr);
    vprintf((const char*)ptr, argptr);
    va_end(argptr);
}

static void __media_debug_set_handler(
                struct media_device *media,
                void (*debug_handler)(void *, ...),
                void *debug_priv)
{
    if (debug_handler) {
        media->debug_handler = debug_handler;
        media->debug_priv = debug_priv;
    } else {
        media->debug_handler = __media_debug_default;
        media->debug_priv = NULL;
    }
}

static struct media_link *__media_entity_add_link(struct media_entity *entity)
{
    if (entity->num_links >= entity->max_links) {
        struct media_link *links = entity->links;
        unsigned int max_links = entity->max_links * 2;
        unsigned int i;

        links = (struct media_link*)realloc(links, max_links * sizeof *links);
        if (links == NULL)
            return NULL;

        for (i = 0; i < entity->num_links; ++i)
            links[i].twin->twin = &links[i];

        entity->max_links = max_links;
        entity->links = links;
    }

    return &entity->links[entity->num_links++];
}


static int __media_enum_links(struct media_device *media)
{
    ALOGD("%s: start", __func__);
    __u32 id;
    int ret = 0;

    for (id = 1; id <= media->entities_count; id++) {
        struct media_entity *entity = &media->entities[id - 1];
        struct media_links_enum links;
        unsigned int i;

        links.entity = entity->info.id;
        links.pads = (struct media_pad_desc*)malloc(entity->info.pads * sizeof(struct media_pad_desc));
        links.links = (struct media_link_desc*)malloc(entity->info.links * sizeof(struct media_link_desc));

        if (ioctl(media->fd, MEDIA_IOC_ENUM_LINKS, &links) < 0) {
            ALOGE("Unable to enumerate pads and links (%s)", strerror(errno));
            free(links.pads);
            free(links.links);
            return -errno;
        }

        for (i = 0; i < entity->info.pads; ++i) {
            entity->pads[i].entity = entity;
            entity->pads[i].index = links.pads[i].index;
            entity->pads[i].flags = links.pads[i].flags;
        }

        for (i = 0; i < entity->info.links; ++i) {
            struct media_link_desc *link = &links.links[i];
            struct media_link *fwdlink;
            struct media_link *backlink;
            struct media_entity *source;
            struct media_entity *sink;

            source = exynos_media_get_entity_by_id(media, link->source.entity);
            sink = exynos_media_get_entity_by_id(media, link->sink.entity);
            if (source == NULL || sink == NULL) {
                ALOGE("WARNING entity %u link %u from %u/%u to %u/%u is invalid!",
                      id, i, link->source.entity,
                      link->source.index,
                      link->sink.entity,
                      link->sink.index);
                ret = -EINVAL;
            } else {
                fwdlink = __media_entity_add_link(source);
                fwdlink->source = &source->pads[link->source.index];
                fwdlink->sink = &sink->pads[link->sink.index];
                fwdlink->flags = link->flags;

                backlink = __media_entity_add_link(sink);
                backlink->source = &source->pads[link->source.index];
                backlink->sink = &sink->pads[link->sink.index];
                backlink->flags = link->flags;

                fwdlink->twin = backlink;
                backlink->twin = fwdlink;
            }
        }

        free(links.pads);
        free(links.links);
    }
    return ret;
}

static int __media_get_devname_sysfs(struct media_entity *entity)
{
    //struct stat devstat;
    char devname[32];
    char sysname[32];
    char target[1024];
    char *p;
    int ret;

    snprintf(sysname, sizeof(sysname), "/sys/dev/char/%u:%u", entity->info.v4l.major,
        entity->info.v4l.minor);

    ret = readlink(sysname, target, sizeof(target));
    if (ret < 0 || ret >= sizeof(target))
        return -errno;

    target[ret] = '\0';
    p = strrchr(target, '/');
    if (p == NULL)
        return -EINVAL;

    snprintf(devname, sizeof(devname), "/tmp/%s", p + 1);

    ret = mknod(devname, 0666 | S_IFCHR, MKDEV(81, entity->info.v4l.minor));
    strncpy(entity->devname, devname, sizeof(devname) - 1);

    return 0;
}

static int __media_get_media_fd(const char *filename, struct media_device *media)
{
    ssize_t num;
    int media_node;
    char *ptr;
    char media_buf[6];

    ALOGD("%s: %s", __func__, filename);

    media->fd = open(filename, O_RDWR, 0);
    if (media->fd < 0) {
        ALOGE("Open sysfs media device failed, media->fd: %d", media->fd);
        return -1;
    }

    ALOGD("%s: media->fd: %d", __func__, media->fd);

    return media->fd;

}

static int __media_enum_entities(struct media_device *media)
{
    struct media_entity *entity, *temp_entity;
    unsigned int size;
    __u32 id;
    int ret;

    temp_entity = entity = (struct media_entity*)calloc(1,  sizeof(struct media_entity));
    for (id = 0, ret = 0; ; id = entity->info.id) {
        size = (media->entities_count + 1) * sizeof(*media->entities);
        media->entities = (struct media_entity*)realloc(media->entities, size);

        entity = &media->entities[media->entities_count];
        memset(entity, 0, sizeof(*entity));
        entity->fd = -1;
        entity->info.id = id | MEDIA_ENT_ID_FLAG_NEXT;
        entity->media = media;

        ret = ioctl(media->fd, MEDIA_IOC_ENUM_ENTITIES, &entity->info);

        if (ret < 0) {
            ret = errno != EINVAL ? -errno : 0;
            break;
        }

        /* Number of links (for outbound links) plus number of pads (for
         * inbound links) is a good safe initial estimate of the total
         * number of links.
         */
        entity->max_links = entity->info.pads + entity->info.links;

        entity->pads = (struct media_pad*)malloc(entity->info.pads * sizeof(*entity->pads));
        entity->links = (struct media_link*)malloc(entity->max_links * sizeof(*entity->links));
        if (entity->pads == NULL || entity->links == NULL) {
            ret = -ENOMEM;
            break;
        }

        media->entities_count++;

        /* Find the corresponding device name. */
        if (__media_entity_type(entity) != MEDIA_ENT_T_DEVNODE &&
            __media_entity_type(entity) != MEDIA_ENT_T_V4L2_SUBDEV)
            continue;

        /* Fall back to get the device name via sysfs */
        __media_get_devname_sysfs(entity);
        if (ret < 0)
            ALOGE("media_get_devname failed");
    }
    free(temp_entity);

    return ret;
}

static struct media_device *__media_open_debug(
        const char *filename,
        void (*debug_handler)(void *, ...),
        void *debug_priv)
{
    struct media_device *media;
    int ret;

    media = (struct media_device *)calloc(1, sizeof(struct media_device));
    if (media == NULL) {
        ALOGE("media: %p", media);
        return NULL;
    }

    __media_debug_set_handler(media, debug_handler, debug_priv);

    ALOGD("%s: Opening media device %s", __func__, filename);
    ALOGD("%s: media: %p", __func__, media);

    media->fd = __media_get_media_fd(filename, media);
    if (media->fd < 0) {
        exynos_media_close(media);
        ALOGE("failed __media_get_media_fd %s", filename);
        return NULL;
    }

    ALOGD("%s: media->fd: %d", __func__, media->fd);
    ret = __media_enum_entities(media);

    if (ret < 0) {
        ALOGE("Unable to enumerate entities for device %s (%s)", filename, strerror(-ret));
        exynos_media_close(media);
        return NULL;
    }

    ALOGD("%s: Found %u entities", __func__, media->entities_count);
    ALOGD("%s: Enumerating pads and links", __func__);

    ret = __media_enum_links(media);
    if (ret < 0) {
        ALOGE("Unable to enumerate pads and links for device %s", filename);
        exynos_media_close(media);
        return NULL;
    }

    return media;
}

/**
 * @brief Open a media device.
 * @param filename - name (including path) of the device node.
 *
 * Open the media device referenced by @a filename and enumerate entities, pads and
 * links.
 *
 * @return A pointer to a newly allocated media_device structure instance on
 * success and NULL on failure. The returned pointer must be freed with
 * exynos_media_close when the device isn't needed anymore.
 */
struct media_device *exynos_media_open(const char *filename)
{
    return __media_open_debug(filename, (void (*)(void *, ...))fprintf, stdout);
}

/**
 * @brief Close a media device.
 * @param media - device instance.
 *
 * Close the @a media device instance and free allocated resources. Access to the
 * device instance is forbidden after this function returns.
 */
void exynos_media_close(struct media_device *media)
{
    unsigned int i;

    if (media->fd != -1)
        close(media->fd);

    for (i = 0; i < media->entities_count; ++i) {
        struct media_entity *entity = &media->entities[i];

        free(entity->pads);
        free(entity->links);
        if (entity->fd != -1)
            close(entity->fd);
    }

    free(media->entities);
    free(media);
}

/**
 * @brief Locate the pad at the other end of a link.
 * @param pad - sink pad at one end of the link.
 *
 * Locate the source pad connected to @a pad through an enabled link. As only one
 * link connected to a sink pad can be enabled at a time, the connected source
 * pad is guaranteed to be unique.
 *
 * @return A pointer to the connected source pad, or NULL if all links connected
 * to @a pad are disabled. Return NULL also if @a pad is not a sink pad.
 */
struct media_pad *exynos_media_entity_remote_source(struct media_pad *pad)
{
    unsigned int i;

    if (!(pad->flags & MEDIA_PAD_FL_SINK))
        return NULL;

    for (i = 0; i < pad->entity->num_links; ++i) {
        struct media_link *link = &pad->entity->links[i];

        if (!(link->flags & MEDIA_LNK_FL_ENABLED))
            continue;

        if (link->sink == pad)
            return link->source;
    }

    return NULL;
}

/**
 * @brief Find an entity by its name.
 * @param media - media device.
 * @param name - entity name.
 * @param length - size of @a name.
 *
 * Search for an entity with a name equal to @a name.
 *
 * @return A pointer to the entity if found, or NULL otherwise.
 */
struct media_entity *exynos_media_get_entity_by_name(struct media_device *media,
                          const char *name, size_t length)
{
    unsigned int i;
    struct media_entity *entity;

    for (i = 0; i < media->entities_count; ++i) {
        entity = &media->entities[i];

        if (strncmp(entity->info.name, name, length) == 0)
            return entity;
    }

    return NULL;
}

/**
 * @brief Find an entity by its ID.
 * @param media - media device.
 * @param id - entity ID.
 *
 * Search for an entity with an ID equal to @a id.
 *
 * @return A pointer to the entity if found, or NULL otherwise.
 */
struct media_entity *exynos_media_get_entity_by_id(struct media_device *media,
                        __u32 id)
{
    unsigned int i;

    for (i = 0; i < media->entities_count; ++i) {
        struct media_entity *entity = &media->entities[i];

        if (entity->info.id == id)
            return entity;
    }

    return NULL;
}

/**
 * @brief Configure a link.
 * @param media - media device.
 * @param source - source pad at the link origin.
 * @param sink - sink pad at the link target.
 * @param flags - configuration flags.
 *
 * Locate the link between @a source and @a sink, and configure it by applying
 * the new @a flags.
 *
 * Only the MEDIA_LINK_FLAG_ENABLED flag is writable.
 *
 * @return 0 on success, -1 on failure:
 *       -ENOENT: link not found
 *       - other error codes returned by MEDIA_IOC_SETUP_LINK
 */
int exynos_media_setup_link(struct media_device *media,
             struct media_pad *source,
             struct media_pad *sink,
             __u32 flags)
{
    struct media_link *link;
    struct media_link_desc ulink;
    unsigned int i;
    int ret;

    for (i = 0; i < source->entity->num_links; i++) {
        link = &source->entity->links[i];

        if (link->source->entity == source->entity &&
            link->source->index == source->index &&
            link->sink->entity == sink->entity &&
            link->sink->index == sink->index)
            break;
    }

    if (i == source->entity->num_links) {
        ALOGE("Link not found");
        return -ENOENT;
    }

    /* source pad */
    ulink.source.entity = source->entity->info.id;
    ulink.source.index = source->index;
    ulink.source.flags = MEDIA_PAD_FL_SOURCE;

    /* sink pad */
    ulink.sink.entity = sink->entity->info.id;
    ulink.sink.index = sink->index;
    ulink.sink.flags = MEDIA_PAD_FL_SINK;

    ulink.flags = flags | (link->flags & MEDIA_LNK_FL_IMMUTABLE);

    ret = ioctl(media->fd, MEDIA_IOC_SETUP_LINK, &ulink);
    if (ret == -1) {
        ALOGE("Unable to setup link (%s)", strerror(errno));
        return -errno;
    }

    link->flags = ulink.flags;
    link->twin->flags = ulink.flags;
    return 0;
}

/**
 * @brief Reset all links to the disabled state.
 * @param media - media device.
 *
 * Disable all links in the media device. This function is usually used after
 * opening a media device to reset all links to a known state.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_media_reset_links(struct media_device *media)
{
    unsigned int i, j;
    int ret;

    for (i = 0; i < media->entities_count; ++i) {
        struct media_entity *entity = &media->entities[i];

        for (j = 0; j < entity->num_links; j++) {
            struct media_link *link = &entity->links[j];

            if (link->flags & MEDIA_LNK_FL_IMMUTABLE ||
                link->source->entity != entity)
                continue;

            ret = exynos_media_setup_link(media, link->source, link->sink,
                           link->flags & ~MEDIA_LNK_FL_ENABLED);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

#ifdef HAVE_LIBUDEV

#include <libudev.h>

static inline int __media_udev_open(struct udev **udev)
{
    *udev = udev_new();
    if (*udev == NULL)
        return -ENOMEM;
    return 0;
}

static inline void __media_udev_close(struct udev *udev)
{
    if (udev != NULL)
        udev_unref(udev);
}

static int __media_get_devname_udev(struct udev *udev,
        struct media_entity *entity)
{
    struct udev_device *device;
    dev_t devnum;
    const char *p;
    int ret = -ENODEV;

    if (udev == NULL)
        return -EINVAL;

    devnum = makedev(entity->info.v4l.major, entity->info.v4l.minor);
    ALOGE("looking up device: %u:%u",
          major(devnum), minor(devnum));
    device = udev_device_new_from_devnum(udev, 'c', devnum);
    if (device) {
        p = udev_device_get_devnode(device);
        if (p) {
            strncpy(entity->devname, p, sizeof(entity->devname));
            entity->devname[sizeof(entity->devname) - 1] = '\0';
        }
        ret = 0;
    }

    udev_device_unref(device);

    return ret;
}

#else    /* HAVE_LIBUDEV */

struct udev;

static inline int __media_udev_open(struct udev **udev) { return 0; }

static inline void __media_udev_close(struct udev *udev) { }

static inline int __media_get_devname_udev(struct udev *udev,
        struct media_entity *entity)
{
    return -ENOTSUP;
}

#endif    /* HAVE_LIBUDEV */

/**
 * @brief Parse string to a pad on the media device.
 * @param media - media device.
 * @param p - input string
 * @param endp - pointer to string where parsing ended
 *
 * Parse NULL terminated string describing a pad and return its struct
 * media_pad instance.
 *
 * @return Pointer to struct media_pad on success, NULL on failure.
 */
struct media_pad *exynos_media_parse_pad(struct media_device *media,
                  const char *p, char **endp)
{
    unsigned int entity_id, pad;
    struct media_entity *entity;
    char *end;

    for (; isspace(*p); ++p);

    if (*p == '"') {
        for (end = (char *)p + 1; *end && *end != '"'; ++end);
        if (*end != '"')
            return NULL;

        entity = exynos_media_get_entity_by_name(media, p + 1, end - p - 1);
        if (entity == NULL)
            return NULL;

        ++end;
    } else {
        entity_id = strtoul(p, &end, 10);
        entity = exynos_media_get_entity_by_id(media, entity_id);
        if (entity == NULL)
            return NULL;
    }
    for (; isspace(*end); ++end);

    if (*end != ':')
        return NULL;
    for (p = end + 1; isspace(*p); ++p);

    pad = strtoul(p, &end, 10);
    for (p = end; isspace(*p); ++p);

    if (pad >= entity->info.pads)
        return NULL;

    for (p = end; isspace(*p); ++p);
    if (endp)
        *endp = (char *)p;

    return &entity->pads[pad];
}

/**
 * @brief Parse string to a link on the media device.
 * @param media - media device.
 * @param p - input string
 * @param endp - pointer to p where parsing ended
 *
 * Parse NULL terminated string p describing a link and return its struct
 * media_link instance.
 *
 * @return Pointer to struct media_link on success, NULL on failure.
 */
struct media_link *exynos_media_parse_link(
        struct media_device *media,
        const char *p,
        char **endp)
{
    struct media_link *link;
    struct media_pad *source;
    struct media_pad *sink;
    unsigned int i;
    char *end;

    source = exynos_media_parse_pad(media, p, &end);
    if (source == NULL)
        return NULL;

    if (end[0] != '-' || end[1] != '>')
        return NULL;
    p = end + 2;

    sink = exynos_media_parse_pad(media, p, &end);
    if (sink == NULL)
        return NULL;

    *endp = end;

    for (i = 0; i < source->entity->num_links; i++) {
        link = &source->entity->links[i];

        if (link->source == source && link->sink == sink)
            return link;
    }

    return NULL;
}

/**
 * @brief Parse string to a link on the media device and set it up.
 * @param media - media device.
 * @param p - input string
 *
 * Parse NULL terminated string p describing a link and its configuration
 * and configure the link.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_media_parse_setup_link(
        struct media_device *media,
        const char *p,
        char **endp)
{
    struct media_link *link;
    __u32 flags;
    char *end;

    link = exynos_media_parse_link(media, p, &end);
    if (link == NULL) {
        ALOGE("Unable to parse link");
        return -EINVAL;
    }

    p = end;
    if (*p++ != '[') {
        ALOGE("Unable to parse link flags");
        return -EINVAL;
    }

    flags = strtoul(p, &end, 10);
    for (p = end; isspace(*p); p++);
    if (*p++ != ']') {
        ALOGE("Unable to parse link flags");
        return -EINVAL;
    }

    for (; isspace(*p); p++);
    *endp = (char *)p;

    ALOGD("%s: Setting up link %u:%u -> %u:%u [%u]", __func__,
          link->source->entity->info.id, link->source->index,
          link->sink->entity->info.id, link->sink->index,
          flags);

    return exynos_media_setup_link(media, link->source, link->sink, flags);
}

/**
 * @brief Parse string to link(s) on the media device and set it up.
 * @param media - media device.
 * @param p - input string
 *
 * Parse NULL terminated string p describing link(s) separated by
 * commas (,) and configure the link(s).
 *
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_media_parse_setup_links(struct media_device *media, const char *p)
{
    char *end;
    int ret;

    do {
        ret = exynos_media_parse_setup_link(media, p, &end);
        if (ret < 0)
            return ret;

        p = end + 1;
    } while (*end == ',');

    return *end ? -EINVAL : 0;
}
