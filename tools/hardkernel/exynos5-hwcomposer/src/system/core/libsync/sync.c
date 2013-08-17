/*
 *  sync.c
 *
 *   Copyright 2012 Google, Inc
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fcntl.h>
#include <stdint.h>
#include <string.h>

#if defined(ANDROID)
#include <linux/sync.h>
#include <linux/sw_sync.h>
#else
#include <stdint.h>
#endif

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

int sync_wait(int fd, int timeout)
{
// FIXME:
#if 0
    __s32 to = timeout;

    return ioctl(fd, SYNC_IOC_WAIT, &to);
#else
    return 0;
#endif
}

int sync_merge(const char *name, int fd1, int fd2)
{
// FIXME:
#if 0
    struct sync_merge_data data;
    int err;

    data.fd2 = fd2;
    strlcpy(data.name, name, sizeof(data.name));

    err = ioctl(fd1, SYNC_IOC_MERGE, &data);
    if (err < 0)
        return err;

    return data.fence;
#else
    return 0;
#endif
}

struct sync_fence_info_data *sync_fence_info(int fd)
{
// FIXME:
#if 0
    struct sync_fence_info_data *info;
    int err;

    info = malloc(4096);
    if (info == NULL)
        return NULL;

    info->len = 4096;
    err = ioctl(fd, SYNC_IOC_FENCE_INFO, info);
    if (err < 0) {
        free(info);
        return NULL;
    }

    return info;
#else
    return NULL;
#endif
}

struct sync_pt_info *sync_pt_info(struct sync_fence_info_data *info,
                                  struct sync_pt_info *itr)
{
// FIXME:
#if 0
    if (itr == NULL)
        itr = (struct sync_pt_info *) info->pt_info;
    else
        itr = (struct sync_pt_info *) ((__u8 *)itr + itr->len);

    if ((__u8 *)itr - (__u8 *)info >= (int)info->len)
        return NULL;

    return itr;
#else
    return 0;
#endif
}

void sync_fence_info_free(struct sync_fence_info_data *info)
{
    free(info);
}


int sw_sync_timeline_create(void)
{
    return open("/dev/sw_sync", O_RDWR);
}

int sw_sync_timeline_inc(int fd, unsigned count)
{
// FIXME:
#if 0
    __u32 arg = count;

    return ioctl(fd, SW_SYNC_IOC_INC, &arg);
#else
    return 0;
#endif
}

int sw_sync_fence_create(int fd, const char *name, unsigned value)
{
// FIXME:
#if 0
    struct sw_sync_create_fence_data data;
    int err;

    data.value = value;
    strlcpy(data.name, name, sizeof(data.name));

    err = ioctl(fd, SW_SYNC_IOC_CREATE_FENCE, &data);
    if (err < 0)
        return err;

    return data.fence;
#else
    return 0;
#endif
}
