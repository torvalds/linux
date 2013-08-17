/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <hardware_legacy/uevent.h>

#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/queue.h>
#include <linux/netlink.h>


LIST_HEAD(uevent_handler_head, uevent_handler) uevent_handler_list;
pthread_mutex_t uevent_handler_list_lock = PTHREAD_MUTEX_INITIALIZER;

struct uevent_handler {
    void (*handler)(void *data, const char *msg, int msg_len);
    void *handler_data;
    LIST_ENTRY(uevent_handler) list;
};

static int fd = -1;

/* Returns 0 on failure, 1 on success */
int uevent_init()
{
    struct sockaddr_nl addr;
    int sz = 64*1024;
    int s;

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 0xffffffff;

    s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if(s < 0)
        return 0;

    setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));

    if(bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(s);
        return 0;
    }

    fd = s;
    return (fd > 0);
}

int uevent_get_fd()
{
    return fd;
}

int uevent_next_event(char* buffer, int buffer_length)
{
    while (1) {
        struct pollfd fds;
        int nr;
    
        fds.fd = fd;
        fds.events = POLLIN;
        fds.revents = 0;
        nr = poll(&fds, 1, -1);
     
        if(nr > 0 && (fds.revents & POLLIN)) {
            int count = recv(fd, buffer, buffer_length, 0);
            if (count > 0) {
                struct uevent_handler *h;
                pthread_mutex_lock(&uevent_handler_list_lock);
                LIST_FOREACH(h, &uevent_handler_list, list)
                    h->handler(h->handler_data, buffer, buffer_length);
                pthread_mutex_unlock(&uevent_handler_list_lock);

                return count;
            } 
        }
    }
    
    // won't get here
    return 0;
}

int uevent_add_native_handler(void (*handler)(void *data, const char *msg, int msg_len),
                             void *handler_data)
{
    struct uevent_handler *h;

    h = malloc(sizeof(struct uevent_handler));
    if (h == NULL)
        return -1;
    h->handler = handler;
    h->handler_data = handler_data;

    pthread_mutex_lock(&uevent_handler_list_lock);
    LIST_INSERT_HEAD(&uevent_handler_list, h, list);
    pthread_mutex_unlock(&uevent_handler_list_lock);

    return 0;
}

int uevent_remove_native_handler(void (*handler)(void *data, const char *msg, int msg_len))
{
    struct uevent_handler *h;
    int err = -1;

    pthread_mutex_lock(&uevent_handler_list_lock);
    LIST_FOREACH(h, &uevent_handler_list, list) {
        if (h->handler == handler) {
            LIST_REMOVE(h, list);
            err = 0;
            break;
       }
    }
    pthread_mutex_unlock(&uevent_handler_list_lock);

    return err;
}
