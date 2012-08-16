/*
 * net/tipc/bearer.c: TIPC bearer code
 *
 * Copyright (c) 1996-2006, Ericsson AB
 * Copyright (c) 2004-2006, 2010-2011, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "config.h"
#include "bearer.h"
#include "discover.h"

#define MAX_ADDR_STR 32

static struct tipc_media *media_list[MAX_MEDIA];
static u32 media_count;

struct tipc_bearer tipc_bearers[MAX_BEARERS];

static void bearer_disable(struct tipc_bearer *b_ptr);

/**
 * tipc_media_find - locates specified media object by name
 */
struct tipc_media *tipc_media_find(const char *name)
{
	u32 i;

	for (i = 0; i < media_count; i++) {
		if (!strcmp(media_list[i]->name, name))
			return media_list[i];
	}
	return NULL;
}

/**
 * media_find_id - locates specified media object by type identifier
 */
static struct tipc_media *media_find_id(u8 type)
{
	u32 i;

	for (i = 0; i < media_count; i++) {
		if (media_list[i]->type_id == type)
			return media_list[i];
	}
	return NULL;
}

/**
 * tipc_register_media - register a media type
 *
 * Bearers for this media type must be activated separately at a later stage.
 */
int tipc_register_media(struct tipc_media *m_ptr)
{
	int res = -EINVAL;

	write_lock_bh(&tipc_net_lock);

	if ((strlen(m_ptr->name) + 1) > TIPC_MAX_MEDIA_NAME)
		goto exit;
	if ((m_ptr->bcast_addr.media_id != m_ptr->type_id) ||
	    !m_ptr->bcast_addr.broadcast)
		goto exit;
	if (m_ptr->priority > TIPC_MAX_LINK_PRI)
		goto exit;
	if ((m_ptr->tolerance < TIPC_MIN_LINK_TOL) ||
	    (m_ptr->tolerance > TIPC_MAX_LINK_TOL))
		goto exit;
	if (media_count >= MAX_MEDIA)
		goto exit;
	if (tipc_media_find(m_ptr->name) || media_find_id(m_ptr->type_id))
		goto exit;

	media_list[media_count] = m_ptr;
	media_count++;
	res = 0;
exit:
	write_unlock_bh(&tipc_net_lock);
	if (res)
		pr_warn("Media <%s> registration error\n", m_ptr->name);
	return res;
}

/**
 * tipc_media_addr_printf - record media address in print buffer
 */
void tipc_media_addr_printf(char *buf, int len, struct tipc_media_addr *a)
{
	char addr_str[MAX_ADDR_STR];
	struct tipc_media *m_ptr;
	int ret;

	m_ptr = media_find_id(a->media_id);

	if (m_ptr && !m_ptr->addr2str(a, addr_str, sizeof(addr_str)))
		ret = tipc_snprintf(buf, len, "%s(%s)", m_ptr->name, addr_str);
	else {
		u32 i;

		ret = tipc_snprintf(buf, len, "UNKNOWN(%u)", a->media_id);
		for (i = 0; i < sizeof(a->value); i++)
			ret += tipc_snprintf(buf - ret, len + ret,
					    "-%02x", a->value[i]);
	}
}

/**
 * tipc_media_get_names - record names of registered media in buffer
 */
struct sk_buff *tipc_media_get_names(void)
{
	struct sk_buff *buf;
	int i;

	buf = tipc_cfg_reply_alloc(MAX_MEDIA * TLV_SPACE(TIPC_MAX_MEDIA_NAME));
	if (!buf)
		return NULL;

	read_lock_bh(&tipc_net_lock);
	for (i = 0; i < media_count; i++) {
		tipc_cfg_append_tlv(buf, TIPC_TLV_MEDIA_NAME,
				    media_list[i]->name,
				    strlen(media_list[i]->name) + 1);
	}
	read_unlock_bh(&tipc_net_lock);
	return buf;
}

/**
 * bearer_name_validate - validate & (optionally) deconstruct bearer name
 * @name: ptr to bearer name string
 * @name_parts: ptr to area for bearer name components (or NULL if not needed)
 *
 * Returns 1 if bearer name is valid, otherwise 0.
 */
static int bearer_name_validate(const char *name,
				struct tipc_bearer_names *name_parts)
{
	char name_copy[TIPC_MAX_BEARER_NAME];
	char *media_name;
	char *if_name;
	u32 media_len;
	u32 if_len;

	/* copy bearer name & ensure length is OK */
	name_copy[TIPC_MAX_BEARER_NAME - 1] = 0;
	/* need above in case non-Posix strncpy() doesn't pad with nulls */
	strncpy(name_copy, name, TIPC_MAX_BEARER_NAME);
	if (name_copy[TIPC_MAX_BEARER_NAME - 1] != 0)
		return 0;

	/* ensure all component parts of bearer name are present */
	media_name = name_copy;
	if_name = strchr(media_name, ':');
	if (if_name == NULL)
		return 0;
	*(if_name++) = 0;
	media_len = if_name - media_name;
	if_len = strlen(if_name) + 1;

	/* validate component parts of bearer name */
	if ((media_len <= 1) || (media_len > TIPC_MAX_MEDIA_NAME) ||
	    (if_len <= 1) || (if_len > TIPC_MAX_IF_NAME))
		return 0;

	/* return bearer name components, if necessary */
	if (name_parts) {
		strcpy(name_parts->media_name, media_name);
		strcpy(name_parts->if_name, if_name);
	}
	return 1;
}

/**
 * tipc_bearer_find - locates bearer object with matching bearer name
 */
struct tipc_bearer *tipc_bearer_find(const char *name)
{
	struct tipc_bearer *b_ptr;
	u32 i;

	for (i = 0, b_ptr = tipc_bearers; i < MAX_BEARERS; i++, b_ptr++) {
		if (b_ptr->active && (!strcmp(b_ptr->name, name)))
			return b_ptr;
	}
	return NULL;
}

/**
 * tipc_bearer_find_interface - locates bearer object with matching interface name
 */
struct tipc_bearer *tipc_bearer_find_interface(const char *if_name)
{
	struct tipc_bearer *b_ptr;
	char *b_if_name;
	u32 i;

	for (i = 0, b_ptr = tipc_bearers; i < MAX_BEARERS; i++, b_ptr++) {
		if (!b_ptr->active)
			continue;
		b_if_name = strchr(b_ptr->name, ':') + 1;
		if (!strcmp(b_if_name, if_name))
			return b_ptr;
	}
	return NULL;
}

/**
 * tipc_bearer_get_names - record names of bearers in buffer
 */
struct sk_buff *tipc_bearer_get_names(void)
{
	struct sk_buff *buf;
	struct tipc_bearer *b_ptr;
	int i, j;

	buf = tipc_cfg_reply_alloc(MAX_BEARERS * TLV_SPACE(TIPC_MAX_BEARER_NAME));
	if (!buf)
		return NULL;

	read_lock_bh(&tipc_net_lock);
	for (i = 0; i < media_count; i++) {
		for (j = 0; j < MAX_BEARERS; j++) {
			b_ptr = &tipc_bearers[j];
			if (b_ptr->active && (b_ptr->media == media_list[i])) {
				tipc_cfg_append_tlv(buf, TIPC_TLV_BEARER_NAME,
						    b_ptr->name,
						    strlen(b_ptr->name) + 1);
			}
		}
	}
	read_unlock_bh(&tipc_net_lock);
	return buf;
}

void tipc_bearer_add_dest(struct tipc_bearer *b_ptr, u32 dest)
{
	tipc_nmap_add(&b_ptr->nodes, dest);
	tipc_bcbearer_sort();
	tipc_disc_add_dest(b_ptr->link_req);
}

void tipc_bearer_remove_dest(struct tipc_bearer *b_ptr, u32 dest)
{
	tipc_nmap_remove(&b_ptr->nodes, dest);
	tipc_bcbearer_sort();
	tipc_disc_remove_dest(b_ptr->link_req);
}

/*
 * bearer_push(): Resolve bearer congestion. Force the waiting
 * links to push out their unsent packets, one packet per link
 * per iteration, until all packets are gone or congestion reoccurs.
 * 'tipc_net_lock' is read_locked when this function is called
 * bearer.lock must be taken before calling
 * Returns binary true(1) ore false(0)
 */
static int bearer_push(struct tipc_bearer *b_ptr)
{
	u32 res = 0;
	struct tipc_link *ln, *tln;

	if (b_ptr->blocked)
		return 0;

	while (!list_empty(&b_ptr->cong_links) && (res != PUSH_FAILED)) {
		list_for_each_entry_safe(ln, tln, &b_ptr->cong_links, link_list) {
			res = tipc_link_push_packet(ln);
			if (res == PUSH_FAILED)
				break;
			if (res == PUSH_FINISHED)
				list_move_tail(&ln->link_list, &b_ptr->links);
		}
	}
	return list_empty(&b_ptr->cong_links);
}

void tipc_bearer_lock_push(struct tipc_bearer *b_ptr)
{
	spin_lock_bh(&b_ptr->lock);
	bearer_push(b_ptr);
	spin_unlock_bh(&b_ptr->lock);
}


/*
 * Interrupt enabling new requests after bearer congestion or blocking:
 * See bearer_send().
 */
void tipc_continue(struct tipc_bearer *b_ptr)
{
	spin_lock_bh(&b_ptr->lock);
	if (!list_empty(&b_ptr->cong_links))
		tipc_k_signal((Handler)tipc_bearer_lock_push, (unsigned long)b_ptr);
	b_ptr->blocked = 0;
	spin_unlock_bh(&b_ptr->lock);
}

/*
 * Schedule link for sending of messages after the bearer
 * has been deblocked by 'continue()'. This method is called
 * when somebody tries to send a message via this link while
 * the bearer is congested. 'tipc_net_lock' is in read_lock here
 * bearer.lock is busy
 */
static void tipc_bearer_schedule_unlocked(struct tipc_bearer *b_ptr,
						struct tipc_link *l_ptr)
{
	list_move_tail(&l_ptr->link_list, &b_ptr->cong_links);
}

/*
 * Schedule link for sending of messages after the bearer
 * has been deblocked by 'continue()'. This method is called
 * when somebody tries to send a message via this link while
 * the bearer is congested. 'tipc_net_lock' is in read_lock here,
 * bearer.lock is free
 */
void tipc_bearer_schedule(struct tipc_bearer *b_ptr, struct tipc_link *l_ptr)
{
	spin_lock_bh(&b_ptr->lock);
	tipc_bearer_schedule_unlocked(b_ptr, l_ptr);
	spin_unlock_bh(&b_ptr->lock);
}


/*
 * tipc_bearer_resolve_congestion(): Check if there is bearer congestion,
 * and if there is, try to resolve it before returning.
 * 'tipc_net_lock' is read_locked when this function is called
 */
int tipc_bearer_resolve_congestion(struct tipc_bearer *b_ptr,
					struct tipc_link *l_ptr)
{
	int res = 1;

	if (list_empty(&b_ptr->cong_links))
		return 1;
	spin_lock_bh(&b_ptr->lock);
	if (!bearer_push(b_ptr)) {
		tipc_bearer_schedule_unlocked(b_ptr, l_ptr);
		res = 0;
	}
	spin_unlock_bh(&b_ptr->lock);
	return res;
}

/**
 * tipc_bearer_congested - determines if bearer is currently congested
 */
int tipc_bearer_congested(struct tipc_bearer *b_ptr, struct tipc_link *l_ptr)
{
	if (unlikely(b_ptr->blocked))
		return 1;
	if (likely(list_empty(&b_ptr->cong_links)))
		return 0;
	return !tipc_bearer_resolve_congestion(b_ptr, l_ptr);
}

/**
 * tipc_enable_bearer - enable bearer with the given name
 */
int tipc_enable_bearer(const char *name, u32 disc_domain, u32 priority)
{
	struct tipc_bearer *b_ptr;
	struct tipc_media *m_ptr;
	struct tipc_bearer_names b_names;
	char addr_string[16];
	u32 bearer_id;
	u32 with_this_prio;
	u32 i;
	int res = -EINVAL;

	if (!tipc_own_addr) {
		pr_warn("Bearer <%s> rejected, not supported in standalone mode\n",
			name);
		return -ENOPROTOOPT;
	}
	if (!bearer_name_validate(name, &b_names)) {
		pr_warn("Bearer <%s> rejected, illegal name\n", name);
		return -EINVAL;
	}
	if (tipc_addr_domain_valid(disc_domain) &&
	    (disc_domain != tipc_own_addr)) {
		if (tipc_in_scope(disc_domain, tipc_own_addr)) {
			disc_domain = tipc_own_addr & TIPC_CLUSTER_MASK;
			res = 0;   /* accept any node in own cluster */
		} else if (in_own_cluster_exact(disc_domain))
			res = 0;   /* accept specified node in own cluster */
	}
	if (res) {
		pr_warn("Bearer <%s> rejected, illegal discovery domain\n",
			name);
		return -EINVAL;
	}
	if ((priority > TIPC_MAX_LINK_PRI) &&
	    (priority != TIPC_MEDIA_LINK_PRI)) {
		pr_warn("Bearer <%s> rejected, illegal priority\n", name);
		return -EINVAL;
	}

	write_lock_bh(&tipc_net_lock);

	m_ptr = tipc_media_find(b_names.media_name);
	if (!m_ptr) {
		pr_warn("Bearer <%s> rejected, media <%s> not registered\n",
			name, b_names.media_name);
		goto exit;
	}

	if (priority == TIPC_MEDIA_LINK_PRI)
		priority = m_ptr->priority;

restart:
	bearer_id = MAX_BEARERS;
	with_this_prio = 1;
	for (i = MAX_BEARERS; i-- != 0; ) {
		if (!tipc_bearers[i].active) {
			bearer_id = i;
			continue;
		}
		if (!strcmp(name, tipc_bearers[i].name)) {
			pr_warn("Bearer <%s> rejected, already enabled\n",
				name);
			goto exit;
		}
		if ((tipc_bearers[i].priority == priority) &&
		    (++with_this_prio > 2)) {
			if (priority-- == 0) {
				pr_warn("Bearer <%s> rejected, duplicate priority\n",
					name);
				goto exit;
			}
			pr_warn("Bearer <%s> priority adjustment required %u->%u\n",
				name, priority + 1, priority);
			goto restart;
		}
	}
	if (bearer_id >= MAX_BEARERS) {
		pr_warn("Bearer <%s> rejected, bearer limit reached (%u)\n",
			name, MAX_BEARERS);
		goto exit;
	}

	b_ptr = &tipc_bearers[bearer_id];
	strcpy(b_ptr->name, name);
	res = m_ptr->enable_bearer(b_ptr);
	if (res) {
		pr_warn("Bearer <%s> rejected, enable failure (%d)\n",
			name, -res);
		goto exit;
	}

	b_ptr->identity = bearer_id;
	b_ptr->media = m_ptr;
	b_ptr->tolerance = m_ptr->tolerance;
	b_ptr->window = m_ptr->window;
	b_ptr->net_plane = bearer_id + 'A';
	b_ptr->active = 1;
	b_ptr->priority = priority;
	INIT_LIST_HEAD(&b_ptr->cong_links);
	INIT_LIST_HEAD(&b_ptr->links);
	spin_lock_init(&b_ptr->lock);

	res = tipc_disc_create(b_ptr, &m_ptr->bcast_addr, disc_domain);
	if (res) {
		bearer_disable(b_ptr);
		pr_warn("Bearer <%s> rejected, discovery object creation failed\n",
			name);
		goto exit;
	}
	pr_info("Enabled bearer <%s>, discovery domain %s, priority %u\n",
		name,
		tipc_addr_string_fill(addr_string, disc_domain), priority);
exit:
	write_unlock_bh(&tipc_net_lock);
	return res;
}

/**
 * tipc_block_bearer - Block the bearer with the given name, and reset all its links
 */
int tipc_block_bearer(const char *name)
{
	struct tipc_bearer *b_ptr = NULL;
	struct tipc_link *l_ptr;
	struct tipc_link *temp_l_ptr;

	read_lock_bh(&tipc_net_lock);
	b_ptr = tipc_bearer_find(name);
	if (!b_ptr) {
		pr_warn("Attempt to block unknown bearer <%s>\n", name);
		read_unlock_bh(&tipc_net_lock);
		return -EINVAL;
	}

	pr_info("Blocking bearer <%s>\n", name);
	spin_lock_bh(&b_ptr->lock);
	b_ptr->blocked = 1;
	list_splice_init(&b_ptr->cong_links, &b_ptr->links);
	list_for_each_entry_safe(l_ptr, temp_l_ptr, &b_ptr->links, link_list) {
		struct tipc_node *n_ptr = l_ptr->owner;

		spin_lock_bh(&n_ptr->lock);
		tipc_link_reset(l_ptr);
		spin_unlock_bh(&n_ptr->lock);
	}
	spin_unlock_bh(&b_ptr->lock);
	read_unlock_bh(&tipc_net_lock);
	return 0;
}

/**
 * bearer_disable
 *
 * Note: This routine assumes caller holds tipc_net_lock.
 */
static void bearer_disable(struct tipc_bearer *b_ptr)
{
	struct tipc_link *l_ptr;
	struct tipc_link *temp_l_ptr;

	pr_info("Disabling bearer <%s>\n", b_ptr->name);
	spin_lock_bh(&b_ptr->lock);
	b_ptr->blocked = 1;
	b_ptr->media->disable_bearer(b_ptr);
	list_splice_init(&b_ptr->cong_links, &b_ptr->links);
	list_for_each_entry_safe(l_ptr, temp_l_ptr, &b_ptr->links, link_list) {
		tipc_link_delete(l_ptr);
	}
	if (b_ptr->link_req)
		tipc_disc_delete(b_ptr->link_req);
	spin_unlock_bh(&b_ptr->lock);
	memset(b_ptr, 0, sizeof(struct tipc_bearer));
}

int tipc_disable_bearer(const char *name)
{
	struct tipc_bearer *b_ptr;
	int res;

	write_lock_bh(&tipc_net_lock);
	b_ptr = tipc_bearer_find(name);
	if (b_ptr == NULL) {
		pr_warn("Attempt to disable unknown bearer <%s>\n", name);
		res = -EINVAL;
	} else {
		bearer_disable(b_ptr);
		res = 0;
	}
	write_unlock_bh(&tipc_net_lock);
	return res;
}



void tipc_bearer_stop(void)
{
	u32 i;

	for (i = 0; i < MAX_BEARERS; i++) {
		if (tipc_bearers[i].active)
			bearer_disable(&tipc_bearers[i]);
	}
	media_count = 0;
}
