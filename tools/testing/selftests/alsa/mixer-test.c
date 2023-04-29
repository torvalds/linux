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

#include "../kselftest.h"
#include "alsa-local.h"

#define TESTS_PER_CONTROL 7

struct card_data {
	snd_ctl_t *handle;
	int card;
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
	int elem;
	int event_missing;
	int event_spurious;
	struct card_data *card;
	struct ctl_data *next;
};

int num_cards = 0;
int num_controls = 0;
struct card_data *card_list = NULL;
struct ctl_data *ctl_list = NULL;

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
			goto next_card;
		}

		err = snd_card_get_name(card, &card_name);
		if (err != 0)
			card_name = "Unknown";
		err = snd_card_get_longname(card, &card_longname);
		if (err != 0)
			card_longname = "Unknown";
		ksft_print_msg("Card %d - %s (%s)\n", card,
			       card_name, card_longname);

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

			snd_ctl_elem_list_get_id(card_data->ctls, ctl,
						 ctl_data->id);
			snd_ctl_elem_info_set_id(ctl_data->info, ctl_data->id);
			err = snd_ctl_elem_info(card_data->handle,
						ctl_data->info);
			if (err < 0) {
				ksft_print_msg("%s getting info for %d\n",
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
			ksft_exit_fail_msg("snd_ctl_poll_descriptors() failed for %d\n",
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

/*
 * Block for up to timeout ms for an event, returns a negative value
 * on error, 0 for no event and 1 for an event.
 */
static int wait_for_event(struct ctl_data *ctl, int timeout)
{
	unsigned short revents;
	snd_ctl_event_t *event;
	int count, err;
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
			ksft_print_msg("Event for unexpected ctl %s\n",
				       snd_ctl_event_elem_get_name(event));
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
		    (int_val - snd_ctl_elem_info_get_min(ctl->info) %
		     snd_ctl_elem_info_get_step(ctl->info))) {
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
				       snd_ctl_elem_info_get_max(ctl->info));
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
			ksft_print_msg("%s.%d value %ld more than item count %ld\n",
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
		ksft_test_result_skip("get_value.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	/* Can't test reading on an unreadable control */
	if (!snd_ctl_elem_info_is_readable(ctl->info)) {
		ksft_print_msg("%s is not readable\n", ctl->name);
		ksft_test_result_skip("get_value.%d.%d\n",
				      ctl->card->card, ctl->elem);
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
	ksft_test_result(err >= 0, "get_value.%d.%d\n",
			 ctl->card->card, ctl->elem);
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
	bool check;

	ksft_print_msg("%d.%d %s\n", ctl->card->card, ctl->elem,
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

	ksft_test_result(name_ok, "name.%d.%d\n",
			 ctl->card->card, ctl->elem);
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
	 * Check for an event if the value changed, or confirm that
	 * there was none if it didn't.  We rely on the kernel
	 * generating the notification before it returns from the
	 * write, this is currently true, should that ever change this
	 * will most likely break and need updating.
	 */
	if (!snd_ctl_elem_info_is_volatile(ctl->info)) {
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
		ksft_test_result_skip("write_default.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_default.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	/* No idea what the default was for unreadable controls */
	if (!snd_ctl_elem_info_is_readable(ctl->info)) {
		ksft_print_msg("%s couldn't read default\n", ctl->name);
		ksft_test_result_skip("write_default.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	err = write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(err >= 0, "write_default.%d.%d\n",
			 ctl->card->card, ctl->elem);
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

static void test_ctl_write_valid(struct ctl_data *ctl)
{
	bool pass;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("write_valid.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_valid.%d.%d\n",
				      ctl->card->card, ctl->elem);
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

	default:
		/* No tests for this yet */
		ksft_test_result_skip("write_valid.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	/* Restore the default value to minimise disruption */
	write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(pass, "write_valid.%d.%d\n",
			 ctl->card->card, ctl->elem);
}

static bool test_ctl_write_invalid_value(struct ctl_data *ctl,
					 snd_ctl_elem_value_t *val)
{
	int err;
	long val_read;

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
	int err, i;
	long val_read;
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
	int err, i;
	unsigned int val_read;
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
	int err;

	/* If the control is turned off let's be polite */
	if (snd_ctl_elem_info_is_inactive(ctl->info)) {
		ksft_print_msg("%s is inactive\n", ctl->name);
		ksft_test_result_skip("write_invalid.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	if (!snd_ctl_elem_info_is_writable(ctl->info)) {
		ksft_print_msg("%s is not writeable\n", ctl->name);
		ksft_test_result_skip("write_invalid.%d.%d\n",
				      ctl->card->card, ctl->elem);
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
		ksft_test_result_skip("write_invalid.%d.%d\n",
				      ctl->card->card, ctl->elem);
		return;
	}

	/* Restore the default value to minimise disruption */
	write_and_verify(ctl, ctl->def_val, NULL);

	ksft_test_result(pass, "write_invalid.%d.%d\n",
			 ctl->card->card, ctl->elem);
}

static void test_ctl_event_missing(struct ctl_data *ctl)
{
	ksft_test_result(!ctl->event_missing, "event_missing.%d.%d\n",
			 ctl->card->card, ctl->elem);
}

static void test_ctl_event_spurious(struct ctl_data *ctl)
{
	ksft_test_result(!ctl->event_spurious, "event_spurious.%d.%d\n",
			 ctl->card->card, ctl->elem);
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
		test_ctl_write_invalid(ctl);
		test_ctl_event_missing(ctl);
		test_ctl_event_spurious(ctl);
	}

	ksft_exit_pass();

	return 0;
}
