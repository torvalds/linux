// SPDX-License-Identifier: GPL-2.0
//
// kselftest for the ALSA mixer API
//
// Original author: Mark Brown <broonie@kernel.org>
// Copyright (c) 2021-2 Arm Limited

// This test will iterate over all cards detected in the system, exercising
// every mixer control it can find.  This may conflict with other system
// software if there is audio activity so is best run on a system with a
// minimal active userspace.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <alsa/asoundlib.h>
#include <poll.h>
#include <stdint.h>

#include "kselftest.h"
#include "alsa-local.h"

#define TESTS_PER_CONTROL 8

/* Suffixes of the SNDRV_CTL_NAME_IEC958() names, not exported to userspace */
#define IEC958_DEFAULT		"Default"
#define IEC958_CON_MASK		"Con Mask"
#define IEC958_PRO_MASK		"Pro Mask"
#define IEC958_MASK		"Mask"

struct card_data {
	snd_ctl_t *handle;
	int card;
	snd_ctl_card_info_t *info;
	const char *card_name;
	struct pollfd pollfd;
	int num_ctls;
	snd_ctl_elem_list_t *ctls;
	struct card_data *next;
};

struct ctl_data {
	const char *name;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_info_t *info;
	snd_ctl_elem_value_t *def_val;
	snd_ctl_elem_value_t *snapshot;
	bool snapshot_valid;
	bool moved;
	int elem;
	int event_missing;
	int event_spurious;
	int side_effects;
	unsigned int ev_mask;
	struct card_data *card;
	struct ctl_data *next;
};

int num_cards;
int num_controls;
struct card_data *card_list;
struct ctl_data *ctl_list;

static void find_controls(void)
{
	char name[32];
	int card, ctl, err;
	struct card_data *card_data;
	struct ctl_data *ctl_data;
	snd_config_t *config;
	char *card_name, *card_longname;

	card = -1;
	if (snd_card_next(&card) < 0 || card < 0)
		return;

	config = get_alsalib_config();

	while (card >= 0) {
		sprintf(name, "hw:%d", card);

		card_data = malloc(sizeof(*card_data));
		if (!card_data)
			ksft_exit_fail_msg("Out of memory\n");

		err = snd_ctl_open_lconf(&card_data->handle, name, 0, config);
		if (err < 0) {
			ksft_print_msg("Failed to get hctl for card %d: %s\n",
				       card, snd_strerror(err));
			free(card_data);
			goto next_card;
		}

		err = snd_card_get_name(card, &card_name);
		if (err != 0)
			card_name = "Unknown";
		err = snd_card_get_longname(card, &card_longname);
		if (err != 0)
			card_longname = "Unknown";

		err = snd_ctl_card_info_malloc(&card_data->info);
		if (err != 0)
			ksft_exit_fail_msg("Failed to allocate card info: %d\n",
				err);

		err = snd_ctl_card_info(card_data->handle, card_data->info);
		if (err == 0) {
			card_data->card_name = snd_ctl_card_info_get_id(card_data->info);
			if (!card_data->card_name)
				ksft_print_msg("Failed to get card ID\n");
		} else {
			ksft_print_msg("Failed to get card info: %d\n", err);
		}

		if (!card_data->card_name)
			card_data->card_name = "Unknown";

		ksft_print_msg("Card %d/%s - %s (%s)\n", card,
			       card_data->card_name, card_name, card_longname);

		/* Count controls */
		snd_ctl_elem_list_malloc(&card_data->ctls);
		snd_ctl_elem_list(card_data->handle, card_data->ctls);
		card_data->num_ctls = snd_ctl_elem_list_get_count(card_data->ctls);

		/* Enumerate control information */
		snd_ctl_elem_list_alloc_space(card_data->ctls, card_data->num_ctls);
		snd_ctl_elem_list(card_data->handle, card_data->ctls);

		card_data->card = num_cards++;
		card_data->next = card_list;
		card_list = card_data;

		num_controls += card_data->num_ctls;

		for (ctl = 0; ctl < card_data->num_ctls; ctl++) {
			ctl_data = malloc(sizeof(*ctl_data));
			if (!ctl_data)
				ksft_exit_fail_msg("Out of memory\n");

			memset(ctl_data, 0, sizeof(*ctl_data));
			ctl_data->card = card_data;
			ctl_data->elem = ctl;
			ctl_data->name = snd_ctl_elem_list_get_name(card_data->ctls,
								    ctl);

			err = snd_ctl_elem_id_malloc(&ctl_data->id);
			if (err < 0)
				ksft_exit_fail_msg("Out of memory\n");

			err = snd_ctl_elem_info_malloc(&ctl_data->info);
			if (err < 0)
				ksft_exit_fail_msg("Out of memory\n");

			err = snd_ctl_elem_value_malloc(&ctl_data->def_val);
			if (err < 0)
				ksft_exit_fail_msg("Out of memory\n");

			err = snd_ctl_elem_value_malloc(&ctl_data->snapshot);
			if (err < 0)
				ksft_exit_fail_msg("Out of memory\n");

			snd_ctl_elem_list_get_id(card_data->ctls, ctl,
						 ctl_data->id);
			snd_ctl_elem_info_set_id(ctl_data->info, ctl_data->id);
			err = snd_ctl_elem_info(card_data->handle,
						ctl_data->info);
			if (err < 0) {
				ksft_print_msg("%s getting info for %s\n",
					       snd_strerror(err),
					       ctl_data->name);
			}

			snd_ctl_elem_value_set_id(ctl_data->def_val,
						  ctl_data->id);

			ctl_data->next = ctl_list;
			ctl_list = ctl_data;
		}

		/* Set up for events */
		err = snd_ctl_subscribe_events(card_data->handle, true);
		if (err < 0) {
			ksft_exit_fail_msg("snd_ctl_subscribe_events() failed for card %d: %d\n",
					   card, err);
		}

		err = snd_ctl_poll_descriptors_count(card_data->handle);
		if (err != 1) {
			ksft_exit_fail_msg("Unexpected descriptor count %d for card %d\n",
					   err, card);
		}

		err = snd_ctl_poll_descriptors(card_data->handle,
					       &card_data->pollfd, 1);
		if (err != 1) {
			ksft_exit_fail_msg("snd_ctl_poll_descriptors() failed for card %d: %d\n",
				       card, err);
		}

	next_card:
		if (snd_card_next(&card) < 0) {
			ksft_print_msg("snd_card_next");
			break;
		}
	}

	snd_config_delete(config);
}

/* The control on the same card that an event's numid refers to */
static struct ctl_data *find_ctl_by_numid(struct card_data *card,
					  unsigned int numid)
{
	struct ctl_data *ctl;

	for (ctl = ctl_list; ctl != NULL; ctl = ctl->next)
		if (ctl->card == card &&
		    snd_ctl_elem_info_get_numid(ctl->info) == numid)
			return ctl;

	return NULL;
}

/*
 * Block for up to timeout ms for an event, returns a negative value
 * on error, 0 for no event and 1 for an event.
 */
static int wait_for_event(struct ctl_data *ctl, int timeout)
{
	unsigned short revents;
	snd_ctl_event_t *event;
	int err;
	unsigned int mask = 0;
	unsigned int ev_id;

	snd_ctl_event_alloca(&event);

	do {
		err = poll(&(ctl->card->pollfd), 1, timeout);
		if (err < 0) {
			ksft_print_msg("poll() failed for %s: %s (%d)\n",
				       ctl->name, strerror(errno), errno);
			return -1;
		}
		/* Timeout */
		if (err == 0)
			return 0;

		err = snd_ctl_poll_descriptors_revents(ctl->card->handle,
						       &(ctl->card->pollfd),
						       1, &revents);
		if (err < 0) {
			ksft_print_msg("snd_ctl_poll_descriptors_revents() failed for %s: %d\n",
				       ctl->name, err);
			return err;
		}
		if (revents & POLLERR) {
			ksft_print_msg("snd_ctl_poll_descriptors_revents() reported POLLERR for %s\n",
				       ctl->name);
			return -1;
		}
		/* No read events */
		if (!(revents & POLLIN)) {
			ksft_print_msg("No POLLIN\n");
			continue;
		}

		err = snd_ctl_read(ctl->card->handle, event);
		if (err < 0) {
			ksft_print_msg("snd_ctl_read() failed for %s: %d\n",
			       ctl->name, err);
			return err;
		}

		if (snd_ctl_event_get_type(event) != SND_CTL_EVENT_ELEM)
			continue;

		/* The ID returned from the event is 1 less than numid */
		mask = snd_ctl_event_elem_get_mask(event);
		ev_id = snd_ctl_event_elem_get_numid(event);
		if (ev_id != snd_ctl_elem_info_get_numid(ctl->info)) {
			struct ctl_data *other = find_ctl_by_numid(ctl->card,
								   ev_id);

			/*
			 * Remember that the driver announced this one.
			 * test_ctl_write_side_effects() uses that to tell a
			 * deliberate link from a silent register collision.
			 */
			if (other)
				other->ev_mask |= mask;

			ksft_print_msg("Event for unexpected ctl %s\n",
				       snd_ctl_event_elem_get_name(event));
			/* The loop condition must not see the other control's mask */
			mask = 0;
			continue;
		}

		if ((mask & SND_CTL_EVENT_MASK_REMOVE) == SND_CTL_EVENT_MASK_REMOVE) {
			ksft_print_msg("Removal event for %s\n",
				       ctl->name);
			return -1;
		}
	} while ((mask & SND_CTL_EVENT_MASK_VALUE) != SND_CTL_EVENT_MASK_VALUE);

	return 1;
}

static bool ctl_value_index_valid(struct ctl_data *ctl,
				  snd_ctl_elem_value_t *val,
				  int index)
{
	long int_val;
	long long int64_val;

	switch (snd_ctl_elem_info_get_type(ctl->info)) {
	case SND_CTL_ELEM_TYPE_NONE:
		ksft_print_msg("%s.%d Invalid control type NONE\n",
			       ctl->name, index);
		return false;

	case SND_CTL_ELEM_TYPE_BOOLEAN:
		int_val = snd_ctl_elem_value_get_boolean(val, index);
		switch (int_val) {
		case 0:
		case 1:
			break;
		default:
			ksft_print_msg("%s.%d Invalid boolean value %ld\n",
				       ctl->name, index, int_val);
			return false;
		}
		break;

	case SND_CTL_ELEM_TYPE_INTEGER:
		int_val = snd_ctl_elem_value_get_integer(val, index);

		if (int_val < snd_ctl_elem_info_get_min(ctl->info)) {
			ksft_print_msg("%s.%d value %ld less than minimum %ld\n",
				       ctl->name, index, int_val,
				       snd_ctl_elem_info_get_min(ctl->info));
			return false;
		}

		if (int_val > snd_ctl_elem_info_get_max(ctl->info)) {
			ksft_print_msg("%s.%d value %ld more than maximum %ld\n",
				       ctl->name, index, int_val,
				       snd_ctl_elem_info_get_max(ctl->info));
			return false;
		}

		/* Only check step size if there is one and we're in bounds */
		if (snd_ctl_elem_info_get_step(ctl->info) &&
		    (int_val - snd_ctl_elem_info_get_min(ctl->info)) %
		    snd_ctl_elem_info_get_step(ctl->info)) {
			ksft_print_msg("%s.%d value %ld invalid for step %ld minimum %ld\n",
				       ctl->name, index, int_val,
				       snd_ctl_elem_info_get_step(ctl->info),
				       snd_ctl_elem_info_get_min(ctl->info));
			return false;
		}
		break;

	case SND_CTL_ELEM_TYPE_INTEGER64:
		int64_val = snd_ctl_elem_value_get_integer64(val, index);

		if (int64_val < snd_ctl_elem_info_get_min64(ctl->info)) {
			ksft_print_msg("%s.%d value %lld less than minimum %lld\n",
				       ctl->name, index, int64_val,
				       snd_ctl_elem_info_get_min64(ctl->info));
			return false;
		}

		if (int64_val > snd_ctl_elem_info_get_max64(ctl->info)) {
			ksft_print_msg("%s.%d value %lld more than maximum %lld\n",
				       ctl->name, index, int64_val,
				       snd_ctl_elem_info_get_max64(ctl->info));
			return false;
		}

		/* Only check step size if there is one and we're in bounds */
		if (snd_ctl_elem_info_get_step64(ctl->info) &&
		    (int64_val - snd_ctl_elem_info_get_min64(ctl->info)) %
		    snd_ctl_elem_info_get_step64(ctl->info)) {
			ksft_print_msg("%s.%d value %lld invalid for step %lld minimum %lld\n",
				       ctl->name, index, int64_val,
				       snd_ctl_elem_info_get_step64(ctl->info),
				       snd_ctl_elem_info_get_min64(ctl->info));
			return false;
		}
		break;

	case SND_CTL_ELEM_TYPE_ENUMERATED:
		int_val = snd_ctl_elem_value_get_enumerated(val, index);

		if (int_val < 0) {
			ksft_print_msg("%s.%d negative value %ld for enumeration\n",
				       ctl->name, index, int_val);
			return false;
		}

		if (int_val >= snd_ctl_elem_info_get_items(ctl->info)) {
			ksft_print_msg("%s.%d value %ld more than item count %u\n",
				       ctl->name, index, int_val,
				       snd_ctl_elem_info_get_items(ctl->info));
			return false;
		}
		break;

	default:
		/* No tests for other types */
		break;
	}

	return true;
}

/*
 * Check that the provided value meets the constraints for the
 * provided control.
 */
static bool ctl_value_valid(struct ctl_data *ctl, snd_ctl_elem_value_t *val)
{
	int i;
	bool valid = true;

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++)
		if (!ctl_value_index_valid(ctl, val, i))
			valid = false;

	return valid;
}

/*
 * Check that we can read the default value and it is valid. Write
 * tests use the read value to restore the default.
 */
static void test_ctl_get_value(struct ctl_data *ctl)
{
	int err;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("get_value.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	/* Can't test reading on an unreadable control */
	if (!snd_ctl_elem_info_is_readable(ctl->info)) {
		ksft_print_msg("%s is not readable\n", ctl->name);
		ksft_test_result_skip("get_value.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	err = snd_ctl_elem_read(ctl->card->handle, ctl->def_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_read() failed: %s\n",
			       snd_strerror(err));
		goto out;
	}

	if (!ctl_value_valid(ctl, ctl->def_val))
		err = -EINVAL;

out:
	ksft_test_result(err >= 0, "get_value.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

static bool strend(const char *haystack, const char *needle)
{
	size_t haystack_len = strlen(haystack);
	size_t needle_len = strlen(needle);

	if (needle_len > haystack_len)
		return false;
	return strcmp(haystack + haystack_len - needle_len, needle) == 0;
}

static void test_ctl_name(struct ctl_data *ctl)
{
	bool name_ok = true;

	ksft_print_msg("%s.%d %s\n", ctl->card->card_name, ctl->elem,
		       ctl->name);

	/* Only boolean controls should end in Switch */
	if (strend(ctl->name, " Switch")) {
		if (snd_ctl_elem_info_get_type(ctl->info) != SND_CTL_ELEM_TYPE_BOOLEAN) {
			ksft_print_msg("%d.%d %s ends in Switch but is not boolean\n",
				       ctl->card->card, ctl->elem, ctl->name);
			name_ok = false;
		}
	}

	/* Writeable boolean controls should end in Switch */
	if (snd_ctl_elem_info_get_type(ctl->info) == SND_CTL_ELEM_TYPE_BOOLEAN &&
	    snd_ctl_elem_info_is_writable(ctl->info)) {
		if (!strend(ctl->name, " Switch")) {
			ksft_print_msg("%d.%d %s is a writeable boolean but not a Switch\n",
				       ctl->card->card, ctl->elem, ctl->name);
			name_ok = false;
		}
	}

	ksft_test_result(name_ok, "name.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

static void show_values(struct ctl_data *ctl, snd_ctl_elem_value_t *orig_val,
			snd_ctl_elem_value_t *read_val)
{
	long long orig_int, read_int;
	int i;

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		switch (snd_ctl_elem_info_get_type(ctl->info)) {
		case SND_CTL_ELEM_TYPE_BOOLEAN:
			orig_int = snd_ctl_elem_value_get_boolean(orig_val, i);
			read_int = snd_ctl_elem_value_get_boolean(read_val, i);
			break;

		case SND_CTL_ELEM_TYPE_INTEGER:
			orig_int = snd_ctl_elem_value_get_integer(orig_val, i);
			read_int = snd_ctl_elem_value_get_integer(read_val, i);
			break;

		case SND_CTL_ELEM_TYPE_INTEGER64:
			orig_int = snd_ctl_elem_value_get_integer64(orig_val,
								    i);
			read_int = snd_ctl_elem_value_get_integer64(read_val,
								    i);
			break;

		case SND_CTL_ELEM_TYPE_ENUMERATED:
			orig_int = snd_ctl_elem_value_get_enumerated(orig_val,
								     i);
			read_int = snd_ctl_elem_value_get_enumerated(read_val,
								     i);
			break;

		default:
			return;
		}

		ksft_print_msg("%s.%d orig %lld read %lld, is_volatile %d\n",
			       ctl->name, i, orig_int, read_int,
			       snd_ctl_elem_info_is_volatile(ctl->info));
	}
}

static bool show_mismatch(struct ctl_data *ctl, int index,
			  snd_ctl_elem_value_t *read_val,
			  snd_ctl_elem_value_t *expected_val)
{
	long long expected_int, read_int;

	/*
	 * We factor out the code to compare values representable as
	 * integers, ensure that check doesn't log otherwise.
	 */
	expected_int = 0;
	read_int = 0;

	switch (snd_ctl_elem_info_get_type(ctl->info)) {
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		expected_int = snd_ctl_elem_value_get_boolean(expected_val,
							      index);
		read_int = snd_ctl_elem_value_get_boolean(read_val, index);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER:
		expected_int = snd_ctl_elem_value_get_integer(expected_val,
							      index);
		read_int = snd_ctl_elem_value_get_integer(read_val, index);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER64:
		expected_int = snd_ctl_elem_value_get_integer64(expected_val,
								index);
		read_int = snd_ctl_elem_value_get_integer64(read_val,
							    index);
		break;

	case SND_CTL_ELEM_TYPE_ENUMERATED:
		expected_int = snd_ctl_elem_value_get_enumerated(expected_val,
								 index);
		read_int = snd_ctl_elem_value_get_enumerated(read_val,
							     index);
		break;

	default:
		break;
	}

	if (expected_int != read_int) {
		/*
		 * NOTE: The volatile attribute means that the hardware
		 * can voluntarily change the state of control element
		 * independent of any operation by software.  
		 */
		bool is_volatile = snd_ctl_elem_info_is_volatile(ctl->info);
		ksft_print_msg("%s.%d expected %lld but read %lld, is_volatile %d\n",
			       ctl->name, index, expected_int, read_int, is_volatile);
		return !is_volatile;
	} else {
		return false;
	}
}

/*
 * Write a value then if possible verify that we get the expected
 * result.  An optional expected value can be provided if we expect
 * the write to fail, for verifying that invalid writes don't corrupt
 * anything.
 */
static int write_and_verify(struct ctl_data *ctl,
			    snd_ctl_elem_value_t *write_val,
			    snd_ctl_elem_value_t *expected_val)
{
	int err, i;
	bool error_expected, mismatch_shown;
	snd_ctl_elem_value_t *initial_val, *read_val, *w_val;
	snd_ctl_elem_value_alloca(&initial_val);
	snd_ctl_elem_value_alloca(&read_val);
	snd_ctl_elem_value_alloca(&w_val);

	/*
	 * We need to copy the write value since writing can modify
	 * the value which causes surprises, and allocate an expected
	 * value if we expect to read back what we wrote.
	 */
	snd_ctl_elem_value_copy(w_val, write_val);
	if (expected_val) {
		error_expected = true;
	} else {
		error_expected = false;
		snd_ctl_elem_value_alloca(&expected_val);
		snd_ctl_elem_value_copy(expected_val, write_val);
	}

	/* Store the value before we write */
	if (snd_ctl_elem_info_is_readable(ctl->info)) {
		snd_ctl_elem_value_set_id(initial_val, ctl->id);

		err = snd_ctl_elem_read(ctl->card->handle, initial_val);
		if (err < 0) {
			ksft_print_msg("snd_ctl_elem_read() failed: %s\n",
				       snd_strerror(err));
			return err;
		}
	}

	/*
	 * Do the write, if we have an expected value ignore the error
	 * and carry on to validate the expected value.
	 */
	err = snd_ctl_elem_write(ctl->card->handle, w_val);
	if (err < 0 && !error_expected) {
		ksft_print_msg("snd_ctl_elem_write() failed: %s\n",
			       snd_strerror(err));
		return err;
	}

	/* Can we do the verification part? */
	if (!snd_ctl_elem_info_is_readable(ctl->info))
		return err;

	snd_ctl_elem_value_set_id(read_val, ctl->id);

	err = snd_ctl_elem_read(ctl->card->handle, read_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_read() failed: %s\n",
			       snd_strerror(err));
		return err;
	}

	/*
	 * We can't verify any specific value for volatile controls
	 * but we should still check that whatever we read is a valid
	 * vale for the control.
	 */
	if (snd_ctl_elem_info_is_volatile(ctl->info)) {
		if (!ctl_value_valid(ctl, read_val)) {
			ksft_print_msg("Volatile control %s has invalid value\n",
				       ctl->name);
			return -EINVAL;
		}

		return 0;
	}

	/*
	 * Check for an event if the value changed, or confirm that
	 * there was none if it didn't.  We rely on the kernel
	 * generating the notification before it returns from the
	 * write, this is currently true, should that ever change this
	 * will most likely break and need updating.
	 */
	err = wait_for_event(ctl, 0);
	if (snd_ctl_elem_value_compare(initial_val, read_val)) {
		if (err < 1) {
			ksft_print_msg("No event generated for %s\n",
				       ctl->name);
			show_values(ctl, initial_val, read_val);
			ctl->event_missing++;
		}
	} else {
		if (err != 0) {
			ksft_print_msg("Spurious event generated for %s\n",
				       ctl->name);
			show_values(ctl, initial_val, read_val);
			ctl->event_spurious++;
		}
	}

	/*
	 * Use the libray to compare values, if there's a mismatch
	 * carry on and try to provide a more useful diagnostic than
	 * just "mismatch".
	 */
	if (!snd_ctl_elem_value_compare(expected_val, read_val))
		return 0;

	mismatch_shown = false;
	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++)
		if (show_mismatch(ctl, i, read_val, expected_val))
			mismatch_shown = true;

	if (!mismatch_shown)
		ksft_print_msg("%s read and written values differ\n",
			       ctl->name);

	return -1;
}

/*
 * Make sure we can write the default value back to the control, this
 * should validate that at least some write works.
 */
static void test_ctl_write_default(struct ctl_data *ctl)
{
	int err;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("write_default.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_default.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	/* No idea what the default was for unreadable controls */
	if (!snd_ctl_elem_info_is_readable(ctl->info)) {
		ksft_print_msg("%s couldn't read default\n", ctl->name);
		ksft_test_result_skip("write_default.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	err = write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(err >= 0, "write_default.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

static bool test_ctl_write_valid_boolean(struct ctl_data *ctl)
{
	int err, i, j;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_set_id(val, ctl->id);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		for (j = 0; j < 2; j++) {
			snd_ctl_elem_value_set_boolean(val, i, j);
			err = write_and_verify(ctl, val, NULL);
			if (err != 0)
				fail = true;
		}
	}

	return !fail;
}

static bool test_ctl_write_valid_integer(struct ctl_data *ctl)
{
	int err;
	int i;
	long j, step;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_set_id(val, ctl->id);

	step = snd_ctl_elem_info_get_step(ctl->info);
	if (!step)
		step = 1;

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		for (j = snd_ctl_elem_info_get_min(ctl->info);
		     j <= snd_ctl_elem_info_get_max(ctl->info); j += step) {

			snd_ctl_elem_value_set_integer(val, i, j);
			err = write_and_verify(ctl, val, NULL);
			if (err != 0)
				fail = true;
		}
	}


	return !fail;
}

static bool test_ctl_write_valid_integer64(struct ctl_data *ctl)
{
	int err, i;
	long long j, step;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_set_id(val, ctl->id);

	step = snd_ctl_elem_info_get_step64(ctl->info);
	if (!step)
		step = 1;

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		for (j = snd_ctl_elem_info_get_min64(ctl->info);
		     j <= snd_ctl_elem_info_get_max64(ctl->info); j += step) {

			snd_ctl_elem_value_set_integer64(val, i, j);
			err = write_and_verify(ctl, val, NULL);
			if (err != 0)
				fail = true;
		}
	}

	return !fail;
}

static bool test_ctl_write_valid_enumerated(struct ctl_data *ctl)
{
	int err, i, j;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_set_id(val, ctl->id);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		for (j = 0; j < snd_ctl_elem_info_get_items(ctl->info); j++) {
			snd_ctl_elem_value_set_enumerated(val, i, j);
			err = write_and_verify(ctl, val, NULL);
			if (err != 0)
				fail = true;
		}
	}

	return !fail;
}

/*
 * Find the read only mask control for an IEC958 value control.  The two
 * share device, subdevice and index but not always the interface.
 */
static struct ctl_data *find_iec958_mask_ctl(struct ctl_data *ctl,
					     const char *suffix)
{
	char name[64];
	int stem;
	struct ctl_data *mask;

	stem = strlen(ctl->name) - strlen(IEC958_DEFAULT);
	if (snprintf(name, sizeof(name), "%.*s%s", stem, ctl->name, suffix) >=
	    (int)sizeof(name))
		return NULL;

	for (mask = ctl_list; mask != NULL; mask = mask->next) {
		if (mask->card != ctl->card)
			continue;
		if (snd_ctl_elem_info_get_type(mask->info) !=
		    SND_CTL_ELEM_TYPE_IEC958)
			continue;
		if (snd_ctl_elem_info_is_inactive(mask->info))
			continue;
		if (!snd_ctl_elem_info_is_readable(mask->info))
			continue;
		if (snd_ctl_elem_id_get_device(mask->id) !=
		    snd_ctl_elem_id_get_device(ctl->id))
			continue;
		if (snd_ctl_elem_id_get_subdevice(mask->id) !=
		    snd_ctl_elem_id_get_subdevice(ctl->id))
			continue;
		if (snd_ctl_elem_id_get_index(mask->id) !=
		    snd_ctl_elem_id_get_index(ctl->id))
			continue;
		if (strcmp(mask->name, name) == 0)
			return mask;
	}

	return NULL;
}

/*
 * Read the bits the device says it implements.  Bit 0 of the first status
 * byte picks which mask applies, some devices publish only a plain Mask.
 */
static bool read_iec958_mask(struct ctl_data *ctl,
			     const snd_aes_iec958_t *cur,
			     snd_aes_iec958_t *mask)
{
	int err;
	struct ctl_data *mask_ctl;
	snd_ctl_elem_value_t *val;

	if (!strend(ctl->name, IEC958_DEFAULT))
		return false;

	if (cur->status[0] & IEC958_AES0_PROFESSIONAL)
		mask_ctl = find_iec958_mask_ctl(ctl, IEC958_PRO_MASK);
	else
		mask_ctl = find_iec958_mask_ctl(ctl, IEC958_CON_MASK);
	if (!mask_ctl)
		mask_ctl = find_iec958_mask_ctl(ctl, IEC958_MASK);
	if (!mask_ctl)
		return false;

	snd_ctl_elem_value_alloca(&val);
	snd_ctl_elem_value_set_id(val, mask_ctl->id);

	err = snd_ctl_elem_read(mask_ctl->card->handle, val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_read() failed for %s: %s\n",
			       mask_ctl->name, snd_strerror(err));
		return false;
	}

	snd_ctl_elem_value_get_iec958(val, mask);

	return true;
}

/*
 * Throw away the events from a write we are not checking, one left behind
 * would make a missing notification look like a notification we got.
 */
static int drop_events(struct ctl_data *ctl)
{
	int err;

	do {
		err = wait_for_event(ctl, 0);
	} while (err > 0);

	return err;
}

/*
 * Toggle every bit the device advertises, one at a time.  Each one starts
 * from the value we read since a driver can look at the rest of the block
 * when it stores a bit, and bit 0 of the first status byte selects the
 * layout the others are read in so leave that one alone.
 */
static bool test_ctl_write_valid_iec958_mask(struct ctl_data *ctl,
					     snd_ctl_elem_value_t *orig_val,
					     const snd_aes_iec958_t *mask)
{
	int err, j;
	unsigned int byte;
	unsigned char bit;
	bool fail = false, found = false;
	snd_aes_iec958_t iec958;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_copy(val, orig_val);

	for (byte = 0; byte < sizeof(mask->status); byte++) {
		for (j = 0; j < 8; j++) {
			bit = 1 << j;

			if (byte == 0 && bit == IEC958_AES0_PROFESSIONAL)
				continue;
			if (!(mask->status[byte] & bit))
				continue;
			found = true;

			snd_ctl_elem_value_get_iec958(orig_val, &iec958);
			iec958.status[byte] ^= bit;
			snd_ctl_elem_value_set_iec958(val, &iec958);

			err = write_and_verify(ctl, val, NULL);
			if (err != 0) {
				ksft_print_msg("%s failed to set advertised status[%u] 0x%02x\n",
					       ctl->name, byte, bit);
				fail = true;
			}
		}
	}

	if (!found)
		ksft_print_msg("%s implements no settable status bits\n",
			       ctl->name);

	return !fail;
}

/*
 * With nothing advertised all we can do is try non audio, and the device
 * need not implement even that.  Write it once and ignore the result, then
 * compare against what came back so the value cannot fail, which leaves the
 * notification counted by event_missing.
 */
static bool test_ctl_write_valid_iec958_guess(struct ctl_data *ctl,
					      snd_ctl_elem_value_t *orig_val)
{
	int err;
	snd_aes_iec958_t iec958;
	snd_ctl_elem_value_t *val, *read_val, *w_val;
	snd_ctl_elem_value_alloca(&val);
	snd_ctl_elem_value_alloca(&read_val);
	snd_ctl_elem_value_alloca(&w_val);

	snd_ctl_elem_value_get_iec958(orig_val, &iec958);
	iec958.status[0] ^= IEC958_AES0_NONAUDIO;
	snd_ctl_elem_value_copy(val, orig_val);
	snd_ctl_elem_value_set_iec958(val, &iec958);

	/* Writing can modify the value so keep a copy to write from */
	snd_ctl_elem_value_copy(w_val, val);
	err = snd_ctl_elem_write(ctl->card->handle, w_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_write() failed: %s\n",
			       snd_strerror(err));
		return false;
	}

	snd_ctl_elem_value_set_id(read_val, ctl->id);
	err = snd_ctl_elem_read(ctl->card->handle, read_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_read() failed: %s\n",
			       snd_strerror(err));
		return false;
	}

	/* Put it back where we found it, then drop the events from both */
	snd_ctl_elem_value_copy(w_val, orig_val);
	err = snd_ctl_elem_write(ctl->card->handle, w_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_write() failed: %s\n",
			       snd_strerror(err));
		return false;
	}

	err = drop_events(ctl);
	if (err < 0) {
		ksft_print_msg("drop_events() failed for %s: %d\n",
			       ctl->name, err);
		return false;
	}

	if (snd_ctl_elem_value_compare(val, read_val)) {
		ksft_print_msg("%s does not implement status[0] 0x%02x\n",
			       ctl->name, IEC958_AES0_NONAUDIO);

		/* Expect what came back, the notification is still checked */
		return write_and_verify(ctl, val, read_val) == 0;
	}

	return write_and_verify(ctl, val, NULL) == 0;
}

/*
 * Write the channel status bits and check that userspace is told about it.
 * Where a mask was found every bit in it has to stick, otherwise we fall
 * back to one guessed bit the device is free to ignore.
 */
static bool test_ctl_write_valid_iec958(struct ctl_data *ctl)
{
	int err;
	snd_aes_iec958_t iec958, mask;
	snd_ctl_elem_value_t *orig_val;
	snd_ctl_elem_value_alloca(&orig_val);

	/*
	 * The bytes past the ones a driver implements are compared too,
	 * so start from a read rather than building a value here.
	 */
	snd_ctl_elem_value_set_id(orig_val, ctl->id);
	err = snd_ctl_elem_read(ctl->card->handle, orig_val);
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_read() failed: %s\n",
			       snd_strerror(err));
		return false;
	}

	snd_ctl_elem_value_get_iec958(orig_val, &iec958);

	if (!read_iec958_mask(ctl, &iec958, &mask)) {
		ksft_print_msg("%s falling back to non audio\n", ctl->name);
		return test_ctl_write_valid_iec958_guess(ctl, orig_val);
	}

	return test_ctl_write_valid_iec958_mask(ctl, orig_val, &mask);
}

static void test_ctl_write_valid(struct ctl_data *ctl)
{
	bool pass;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("write_valid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_valid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	switch (snd_ctl_elem_info_get_type(ctl->info)) {
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		pass = test_ctl_write_valid_boolean(ctl);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER:
		pass = test_ctl_write_valid_integer(ctl);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER64:
		pass = test_ctl_write_valid_integer64(ctl);
		break;

	case SND_CTL_ELEM_TYPE_ENUMERATED:
		pass = test_ctl_write_valid_enumerated(ctl);
		break;

	case SND_CTL_ELEM_TYPE_IEC958:
		pass = test_ctl_write_valid_iec958(ctl);
		break;

	default:
		/* No tests for this yet */
		ksft_test_result_skip("write_valid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	/* Restore the default value to minimise disruption */
	write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(pass, "write_valid.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

/*
 * Build the smallest or the largest value the control offers.  The smallest
 * clears the control's register field and the largest sets its top bit, which
 * is the bit a mask one bit too wide puts in its neighbour.
 */
static bool set_limit_value(struct ctl_data *ctl, snd_ctl_elem_value_t *val,
			    bool max)
{
	int i, count = snd_ctl_elem_info_get_count(ctl->info);

	snd_ctl_elem_value_set_id(val, ctl->id);

	switch (snd_ctl_elem_info_get_type(ctl->info)) {
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		for (i = 0; i < count; i++)
			snd_ctl_elem_value_set_boolean(val, i, max);
		return true;

	case SND_CTL_ELEM_TYPE_INTEGER:
		for (i = 0; i < count; i++)
			snd_ctl_elem_value_set_integer(val, i, max ?
				snd_ctl_elem_info_get_max(ctl->info) :
				snd_ctl_elem_info_get_min(ctl->info));
		return true;

	case SND_CTL_ELEM_TYPE_INTEGER64:
		for (i = 0; i < count; i++)
			snd_ctl_elem_value_set_integer64(val, i, max ?
				snd_ctl_elem_info_get_max64(ctl->info) :
				snd_ctl_elem_info_get_min64(ctl->info));
		return true;

	case SND_CTL_ELEM_TYPE_ENUMERATED:
		for (i = 0; i < count; i++)
			snd_ctl_elem_value_set_enumerated(val, i, max ?
				snd_ctl_elem_info_get_items(ctl->info) - 1 : 0);
		return true;

	default:
		/* Nothing sensible to write for the rest */
		return false;
	}
}

/* Note every control on the card that no longer reads as it did */
static void find_moved_ctls(struct ctl_data *ctl, snd_ctl_elem_value_t *val)
{
	struct ctl_data *other;
	int err;

	for (other = ctl_list; other != NULL; other = other->next) {
		if (!other->snapshot_valid)
			continue;

		/*
		 * The buffer is shared and compare() looks at all of it, so
		 * clear what the last control left in the slots this one
		 * does not use.
		 */
		snd_ctl_elem_value_clear(val);
		snd_ctl_elem_value_set_id(val, other->id);
		err = snd_ctl_elem_read(ctl->card->handle, val);
		if (err < 0) {
			ksft_print_msg("snd_ctl_elem_read() failed for %s: %s\n",
				       other->name, snd_strerror(err));
			continue;
		}

		if (snd_ctl_elem_value_compare(other->snapshot, val))
			other->moved = true;
	}
}

/*
 * Write one control and look for others on the same card that moved with it.
 * A driver that links two controls on purpose tells userspace about both, so
 * only an unannounced change is counted.  That is what a control whose
 * register mask covers bits belonging to its neighbour looks like from here.
 */
static void test_ctl_write_side_effects(struct ctl_data *ctl)
{
	struct ctl_data *other;
	snd_ctl_elem_value_t *min_val, *max_val, *read_val;
	int err;

	snd_ctl_elem_value_alloca(&min_val);
	snd_ctl_elem_value_alloca(&max_val);
	snd_ctl_elem_value_alloca(&read_val);

	/* Without a readable default there is nothing to put back */
	if (snd_ctl_elem_info_is_inactive(ctl->info) ||
	    !snd_ctl_elem_info_is_writable(ctl->info) ||
	    !snd_ctl_elem_info_is_readable(ctl->info) ||
	    !set_limit_value(ctl, min_val, false) ||
	    !set_limit_value(ctl, max_val, true)) {
		ksft_test_result_skip("write_side_effects.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	/* Drain first, a stale event would look like the driver announced it */
	drop_events(ctl);

	/*
	 * Record what the rest of the card reads as.  A volatile control can
	 * move on its own so there is nothing to compare it against.
	 */
	for (other = ctl_list; other != NULL; other = other->next) {
		other->snapshot_valid = false;
		other->moved = false;
		other->ev_mask = 0;

		if (other == ctl || other->card != ctl->card)
			continue;
		if (!snd_ctl_elem_info_is_readable(other->info) ||
		    snd_ctl_elem_info_is_volatile(other->info))
			continue;

		snd_ctl_elem_value_clear(other->snapshot);
		snd_ctl_elem_value_set_id(other->snapshot, other->id);
		err = snd_ctl_elem_read(ctl->card->handle, other->snapshot);
		if (err < 0) {
			ksft_print_msg("snd_ctl_elem_read() failed for %s: %s\n",
				       other->name, snd_strerror(err));
			continue;
		}

		other->snapshot_valid = true;
	}

	/*
	 * Compare against the snapshot after each write, before anything is
	 * put back.  Restoring the control we wrote goes through the same
	 * mask, so doing it first would hide the change we are looking for.
	 */
	err = snd_ctl_elem_write(ctl->card->handle, min_val);
	if (err >= 0) {
		drop_events(ctl);
		find_moved_ctls(ctl, read_val);
		err = snd_ctl_elem_write(ctl->card->handle, max_val);
	}
	if (err < 0) {
		ksft_print_msg("snd_ctl_elem_write() failed for %s: %s\n",
			       ctl->name, snd_strerror(err));
	} else {
		drop_events(ctl);
		find_moved_ctls(ctl, read_val);
	}

	for (other = ctl_list; other != NULL; other = other->next) {
		if (!other->moved)
			continue;

		if (other->ev_mask & SND_CTL_EVENT_MASK_VALUE) {
			ksft_print_msg("Writing %s changed %s, the driver said so\n",
				       ctl->name, other->name);
		} else {
			ksft_print_msg("Writing %s silently changed %s\n",
				       ctl->name, other->name);
			ctl->side_effects++;
		}
	}

	/*
	 * The control we wrote goes back first so its mask stops moving the
	 * rest.  A plain write keeps this out of the event counters, they
	 * belong to the tests that check them.
	 */
	snd_ctl_elem_write(ctl->card->handle, ctl->def_val);

	for (other = ctl_list; other != NULL; other = other->next) {
		if (!other->snapshot_valid ||
		    !snd_ctl_elem_info_is_writable(other->info))
			continue;

		snd_ctl_elem_value_clear(read_val);
		snd_ctl_elem_value_set_id(read_val, other->id);
		if (snd_ctl_elem_read(ctl->card->handle, read_val) < 0)
			continue;

		if (snd_ctl_elem_value_compare(other->snapshot, read_val))
			snd_ctl_elem_write(ctl->card->handle, other->snapshot);
	}

	/* Our own restores queue events, the next test must not see them */
	drop_events(ctl);

	if (err < 0)
		ksft_test_result_skip("write_side_effects.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
	else
		ksft_test_result(!ctl->side_effects,
				 "write_side_effects.%s.%d\n",
				 ctl->card->card_name, ctl->elem);
}

static bool test_ctl_write_invalid_value(struct ctl_data *ctl,
					 snd_ctl_elem_value_t *val)
{
	int err;

	/* Ideally this will fail... */
	err = snd_ctl_elem_write(ctl->card->handle, val);
	if (err < 0)
		return false;

	/* ...but some devices will clamp to an in range value */
	err = snd_ctl_elem_read(ctl->card->handle, val);
	if (err < 0) {
		ksft_print_msg("%s failed to read: %s\n",
			       ctl->name, snd_strerror(err));
		return true;
	}

	return !ctl_value_valid(ctl, val);
}

static bool test_ctl_write_invalid_boolean(struct ctl_data *ctl)
{
	int i;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		snd_ctl_elem_value_copy(val, ctl->def_val);
		snd_ctl_elem_value_set_boolean(val, i, 2);

		if (test_ctl_write_invalid_value(ctl, val))
			fail = true;
	}

	return !fail;
}

static bool test_ctl_write_invalid_integer(struct ctl_data *ctl)
{
	int i;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		if (snd_ctl_elem_info_get_min(ctl->info) != LONG_MIN) {
			/* Just under range */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer(val, i,
			       snd_ctl_elem_info_get_min(ctl->info) - 1);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;

			/* Minimum representable value */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer(val, i, LONG_MIN);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;
		}

		if (snd_ctl_elem_info_get_max(ctl->info) != LONG_MAX) {
			/* Just over range */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer(val, i,
			       snd_ctl_elem_info_get_max(ctl->info) + 1);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;

			/* Maximum representable value */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer(val, i, LONG_MAX);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;
		}
	}

	return !fail;
}

static bool test_ctl_write_invalid_integer64(struct ctl_data *ctl)
{
	int i;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		if (snd_ctl_elem_info_get_min64(ctl->info) != LLONG_MIN) {
			/* Just under range */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer64(val, i,
				snd_ctl_elem_info_get_min64(ctl->info) - 1);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;

			/* Minimum representable value */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer64(val, i, LLONG_MIN);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;
		}

		if (snd_ctl_elem_info_get_max64(ctl->info) != LLONG_MAX) {
			/* Just over range */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer64(val, i,
				snd_ctl_elem_info_get_max64(ctl->info) + 1);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;

			/* Maximum representable value */
			snd_ctl_elem_value_copy(val, ctl->def_val);
			snd_ctl_elem_value_set_integer64(val, i, LLONG_MAX);

			if (test_ctl_write_invalid_value(ctl, val))
				fail = true;
		}
	}

	return !fail;
}

static bool test_ctl_write_invalid_enumerated(struct ctl_data *ctl)
{
	int i;
	bool fail = false;
	snd_ctl_elem_value_t *val;
	snd_ctl_elem_value_alloca(&val);

	snd_ctl_elem_value_set_id(val, ctl->id);

	for (i = 0; i < snd_ctl_elem_info_get_count(ctl->info); i++) {
		/* One beyond maximum */
		snd_ctl_elem_value_copy(val, ctl->def_val);
		snd_ctl_elem_value_set_enumerated(val, i,
				  snd_ctl_elem_info_get_items(ctl->info));

		if (test_ctl_write_invalid_value(ctl, val))
			fail = true;

		/* Maximum representable value */
		snd_ctl_elem_value_copy(val, ctl->def_val);
		snd_ctl_elem_value_set_enumerated(val, i, UINT_MAX);

		if (test_ctl_write_invalid_value(ctl, val))
			fail = true;

	}

	return !fail;
}


static void test_ctl_write_invalid(struct ctl_data *ctl)
{
	bool pass;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("write_invalid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_invalid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	switch (snd_ctl_elem_info_get_type(ctl->info)) {
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		pass = test_ctl_write_invalid_boolean(ctl);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER:
		pass = test_ctl_write_invalid_integer(ctl);
		break;

	case SND_CTL_ELEM_TYPE_INTEGER64:
		pass = test_ctl_write_invalid_integer64(ctl);
		break;

	case SND_CTL_ELEM_TYPE_ENUMERATED:
		pass = test_ctl_write_invalid_enumerated(ctl);
		break;

	default:
		/* No tests for this yet */
		ksft_test_result_skip("write_invalid.%s.%d\n",
				      ctl->card->card_name, ctl->elem);
		return;
	}

	/* Restore the default value to minimise disruption */
	write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(pass, "write_invalid.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

static void test_ctl_event_missing(struct ctl_data *ctl)
{
	ksft_test_result(!ctl->event_missing, "event_missing.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

static void test_ctl_event_spurious(struct ctl_data *ctl)
{
	ksft_test_result(!ctl->event_spurious, "event_spurious.%s.%d\n",
			 ctl->card->card_name, ctl->elem);
}

int main(void)
{
	struct ctl_data *ctl;

	ksft_print_header();

	find_controls();

	ksft_set_plan(num_controls * TESTS_PER_CONTROL);

	for (ctl = ctl_list; ctl != NULL; ctl = ctl->next) {
		/*
		 * Must test get_value() before we write anything, the
		 * test stores the default value for later cleanup.
		 */
		test_ctl_get_value(ctl);
		test_ctl_name(ctl);
		test_ctl_write_default(ctl);
		test_ctl_write_valid(ctl);
		test_ctl_write_side_effects(ctl);
		test_ctl_write_invalid(ctl);
		test_ctl_event_missing(ctl);
		test_ctl_event_spurious(ctl);
	}

	ksft_exit_pass();

	return 0;
}
