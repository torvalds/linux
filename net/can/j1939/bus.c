// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 EIA Electronics,
//                         Kurt Van Dijck <kurt.van.dijck@eia.be>
// Copyright (c) 2017-2019 Pengutronix,
//                         Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2017-2019 Pengutronix,
//                         Oleksij Rempel <kernel@pengutronix.de>

/* bus for j1939 remote devices
 * Since rtnetlink, no real bus is used.
 */

#include <net/sock.h>

#include "j1939-priv.h"

static void __j1939_ecu_release(struct kref *kref)
{
	struct j1939_ecu *ecu = container_of(kref, struct j1939_ecu, kref);
	struct j1939_priv *priv = ecu->priv;

	list_del(&ecu->list);
	kfree(ecu);
	j1939_priv_put(priv);
}

void j1939_ecu_put(struct j1939_ecu *ecu)
{
	kref_put(&ecu->kref, __j1939_ecu_release);
}

static void j1939_ecu_get(struct j1939_ecu *ecu)
{
	kref_get(&ecu->kref);
}

static bool j1939_ecu_is_mapped_locked(struct j1939_ecu *ecu)
{
	struct j1939_priv *priv = ecu->priv;

	lockdep_assert_held(&priv->lock);

	return j1939_ecu_find_by_addr_locked(priv, ecu->addr) == ecu;
}

/* ECU device interface */
/* map ECU to a bus address space */
static void j1939_ecu_map_locked(struct j1939_ecu *ecu)
{
	struct j1939_priv *priv = ecu->priv;
	struct j1939_addr_ent *ent;

	lockdep_assert_held(&priv->lock);

	if (!j1939_address_is_unicast(ecu->addr))
		return;

	ent = &priv->ents[ecu->addr];

	if (ent->ecu) {
		netdev_warn(priv->ndev, "Trying to map already mapped ECU, addr: 0x%02x, name: 0x%016llx. Skip it.\n",
			    ecu->addr, ecu->name);
		return;
	}

	j1939_ecu_get(ecu);
	ent->ecu = ecu;
	ent->nusers += ecu->nusers;
}

/* unmap ECU from a bus address space */
void j1939_ecu_unmap_locked(struct j1939_ecu *ecu)
{
	struct j1939_priv *priv = ecu->priv;
	struct j1939_addr_ent *ent;

	lockdep_assert_held(&priv->lock);

	if (!j1939_address_is_unicast(ecu->addr))
		return;

	if (!j1939_ecu_is_mapped_locked(ecu))
		return;

	ent = &priv->ents[ecu->addr];
	ent->ecu = NULL;
	ent->nusers -= ecu->nusers;
	j1939_ecu_put(ecu);
}

void j1939_ecu_unmap(struct j1939_ecu *ecu)
{
	write_lock_bh(&ecu->priv->lock);
	j1939_ecu_unmap_locked(ecu);
	write_unlock_bh(&ecu->priv->lock);
}

void j1939_ecu_unmap_all(struct j1939_priv *priv)
{
	int i;

	write_lock_bh(&priv->lock);
	for (i = 0; i < ARRAY_SIZE(priv->ents); i++)
		if (priv->ents[i].ecu)
			j1939_ecu_unmap_locked(priv->ents[i].ecu);
	write_unlock_bh(&priv->lock);
}

void j1939_ecu_timer_start(struct j1939_ecu *ecu)
{
	/* The ECU is held here and released in the
	 * j1939_ecu_timer_handler() or j1939_ecu_timer_cancel().
	 */
	j1939_ecu_get(ecu);

	/* Schedule timer in 250 msec to commit address change. */
	hrtimer_start(&ecu->ac_timer, ms_to_ktime(250),
		      HRTIMER_MODE_REL_SOFT);
}

void j1939_ecu_timer_cancel(struct j1939_ecu *ecu)
{
	if (hrtimer_cancel(&ecu->ac_timer))
		j1939_ecu_put(ecu);
}

static enum hrtimer_restart j1939_ecu_timer_handler(struct hrtimer *hrtimer)
{
	struct j1939_ecu *ecu =
		container_of(hrtimer, struct j1939_ecu, ac_timer);
	struct j1939_priv *priv = ecu->priv;

	write_lock_bh(&priv->lock);
	/* TODO: can we test if ecu->addr is unicast before starting
	 * the timer?
	 */
	j1939_ecu_map_locked(ecu);

	/* The corresponding j1939_ecu_get() is in
	 * j1939_ecu_timer_start().
	 */
	j1939_ecu_put(ecu);
	write_unlock_bh(&priv->lock);

	return HRTIMER_NORESTART;
}

struct j1939_ecu *j1939_ecu_create_locked(struct j1939_priv *priv, name_t name)
{
	struct j1939_ecu *ecu;

	lockdep_assert_held(&priv->lock);

	ecu = kzalloc(sizeof(*ecu), gfp_any());
	if (!ecu)
		return ERR_PTR(-ENOMEM);
	kref_init(&ecu->kref);
	ecu->addr = J1939_IDLE_ADDR;
	ecu->name = name;

	hrtimer_setup(&ecu->ac_timer, j1939_ecu_timer_handler, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_SOFT);
	INIT_LIST_HEAD(&ecu->list);

	j1939_priv_get(priv);
	ecu->priv = priv;
	list_add_tail(&ecu->list, &priv->ecus);

	return ecu;
}

struct j1939_ecu *j1939_ecu_find_by_addr_locked(struct j1939_priv *priv,
						u8 addr)
{
	lockdep_assert_held(&priv->lock);

	return priv->ents[addr].ecu;
}

struct j1939_ecu *j1939_ecu_get_by_addr_locked(struct j1939_priv *priv, u8 addr)
{
	struct j1939_ecu *ecu;

	lockdep_assert_held(&priv->lock);

	if (!j1939_address_is_unicast(addr))
		return NULL;

	ecu = j1939_ecu_find_by_addr_locked(priv, addr);
	if (ecu)
		j1939_ecu_get(ecu);

	return ecu;
}

struct j1939_ecu *j1939_ecu_get_by_addr(struct j1939_priv *priv, u8 addr)
{
	struct j1939_ecu *ecu;

	read_lock_bh(&priv->lock);
	ecu = j1939_ecu_get_by_addr_locked(priv, addr);
	read_unlock_bh(&priv->lock);

	return ecu;
}

/* get pointer to ecu without increasing ref counter */
static struct j1939_ecu *j1939_ecu_find_by_name_locked(struct j1939_priv *priv,
						       name_t name)
{
	struct j1939_ecu *ecu;

	lockdep_assert_held(&priv->lock);

	list_for_each_entry(ecu, &priv->ecus, list) {
		if (ecu->name == name)
			return ecu;
	}

	return NULL;
}

struct j1939_ecu *j1939_ecu_get_by_name_locked(struct j1939_priv *priv,
					       name_t name)
{
	struct j1939_ecu *ecu;

	lockdep_assert_held(&priv->lock);

	if (!name)
		return NULL;

	ecu = j1939_ecu_find_by_name_locked(priv, name);
	if (ecu)
		j1939_ecu_get(ecu);

	return ecu;
}

struct j1939_ecu *j1939_ecu_get_by_name(struct j1939_priv *priv, name_t name)
{
	struct j1939_ecu *ecu;

	read_lock_bh(&priv->lock);
	ecu = j1939_ecu_get_by_name_locked(priv, name);
	read_unlock_bh(&priv->lock);

	return ecu;
}

u8 j1939_name_to_addr(struct j1939_priv *priv, name_t name)
{
	struct j1939_ecu *ecu;
	int addr = J1939_IDLE_ADDR;

	if (!name)
		return J1939_NO_ADDR;

	read_lock_bh(&priv->lock);
	ecu = j1939_ecu_find_by_name_locked(priv, name);
	if (ecu && j1939_ecu_is_mapped_locked(ecu))
		/* ecu's SA is registered */
		addr = ecu->addr;

	read_unlock_bh(&priv->lock);

	return addr;
}

/* TX addr/name accounting
 * Transport protocol needs to know if a SA is local or not
 * These functions originate from userspace manipulating sockets,
 * so locking is straigforward
 */

int j1939_local_ecu_get(struct j1939_priv *priv, name_t name, u8 sa)
{
	struct j1939_ecu *ecu;
	int err = 0;

	write_lock_bh(&priv->lock);

	if (j1939_address_is_unicast(sa))
		priv->ents[sa].nusers++;

	if (!name)
		goto done;

	ecu = j1939_ecu_get_by_name_locked(priv, name);
	if (!ecu)
		ecu = j1939_ecu_create_locked(priv, name);
	err = PTR_ERR_OR_ZERO(ecu);
	if (err) {
		if (j1939_address_is_unicast(sa))
			priv->ents[sa].nusers--;
		goto done;
	}

	ecu->nusers++;
	/* TODO: do we care if ecu->addr != sa? */
	if (j1939_ecu_is_mapped_locked(ecu))
		/* ecu's sa is active already */
		priv->ents[ecu->addr].nusers++;

 done:
	write_unlock_bh(&priv->lock);

	return err;
}

void j1939_local_ecu_put(struct j1939_priv *priv, name_t name, u8 sa)
{
	struct j1939_ecu *ecu;

	write_lock_bh(&priv->lock);

	if (j1939_address_is_unicast(sa))
		priv->ents[sa].nusers--;

	if (!name)
		goto done;

	ecu = j1939_ecu_find_by_name_locked(priv, name);
	if (WARN_ON_ONCE(!ecu))
		goto done;

	ecu->nusers--;
	/* TODO: do we care if ecu->addr != sa? */
	if (j1939_ecu_is_mapped_locked(ecu))
		/* ecu's sa is active already */
		priv->ents[ecu->addr].nusers--;
	j1939_ecu_put(ecu);

 done:
	write_unlock_bh(&priv->lock);
}
