// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * system_state.c - State of the system modified by livepatches
 *
 * Copyright (C) 2019 SUSE
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/livepatch.h>
#include "core.h"
#include "state.h"
#include "transition.h"

#define klp_for_each_state(patch, state)		\
	for (state = patch->states; state && state->id; state++)

/**
 * klp_get_state() - get information about system state modified by
 *	the given patch
 * @patch:	livepatch that modifies the given system state
 * @id:		custom identifier of the modified system state
 *
 * Checks whether the given patch modifies the given system state.
 *
 * The function can be called either from pre/post (un)patch
 * callbacks or from the kernel code added by the livepatch.
 *
 * Return: pointer to struct klp_state when found, otherwise NULL.
 */
struct klp_state *klp_get_state(struct klp_patch *patch, unsigned long id)
{
	struct klp_state *state;

	klp_for_each_state(patch, state) {
		if (state->id == id)
			return state;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(klp_get_state);

/**
 * klp_get_prev_state() - get information about system state modified by
 *	the already installed livepatches
 * @id:		custom identifier of the modified system state
 *
 * Checks whether already installed livepatches modify the given
 * system state.
 *
 * The same system state can be modified by more non-cumulative
 * livepatches. It is expected that the latest livepatch has
 * the most up-to-date information.
 *
 * The function can be called only during transition when a new
 * livepatch is being enabled or when such a transition is reverted.
 * It is typically called only from pre/post (un)patch
 * callbacks.
 *
 * Return: pointer to the latest struct klp_state from already
 *	installed livepatches, NULL when not found.
 */
struct klp_state *klp_get_prev_state(unsigned long id)
{
	struct klp_patch *patch;
	struct klp_state *state, *last_state = NULL;

	if (WARN_ON_ONCE(!klp_transition_patch))
		return NULL;

	klp_for_each_patch(patch) {
		if (patch == klp_transition_patch)
			goto out;

		state = klp_get_state(patch, id);
		if (state)
			last_state = state;
	}

out:
	return last_state;
}
EXPORT_SYMBOL_GPL(klp_get_prev_state);

/* Check if the patch is able to deal with the existing system state. */
static bool klp_is_state_compatible(struct klp_patch *patch,
				    struct klp_state *old_state)
{
	struct klp_state *state;

	state = klp_get_state(patch, old_state->id);

	/* A cumulative livepatch must handle all already modified states. */
	if (!state)
		return !patch->replace;

	return state->version >= old_state->version;
}

/*
 * Check that the new livepatch will not break the existing system states.
 * Cumulative patches must handle all already modified states.
 * Non-cumulative patches can touch already modified states.
 */
bool klp_is_patch_compatible(struct klp_patch *patch)
{
	struct klp_patch *old_patch;
	struct klp_state *old_state;

	klp_for_each_patch(old_patch) {
		klp_for_each_state(old_patch, old_state) {
			if (!klp_is_state_compatible(patch, old_state))
				return false;
		}
	}

	return true;
}
