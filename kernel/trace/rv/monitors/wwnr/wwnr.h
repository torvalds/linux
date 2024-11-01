/*
 * Automatically generated C representation of wwnr automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_wwnr {
	not_running_wwnr = 0,
	running_wwnr,
	state_max_wwnr
};

#define INVALID_STATE state_max_wwnr

enum events_wwnr {
	switch_in_wwnr = 0,
	switch_out_wwnr,
	wakeup_wwnr,
	event_max_wwnr
};

struct automaton_wwnr {
	char *state_names[state_max_wwnr];
	char *event_names[event_max_wwnr];
	unsigned char function[state_max_wwnr][event_max_wwnr];
	unsigned char initial_state;
	bool final_states[state_max_wwnr];
};

static struct automaton_wwnr automaton_wwnr = {
	.state_names = {
		"not_running",
		"running"
	},
	.event_names = {
		"switch_in",
		"switch_out",
		"wakeup"
	},
	.function = {
		{       running_wwnr,      INVALID_STATE,   not_running_wwnr },
		{      INVALID_STATE,   not_running_wwnr,      INVALID_STATE },
	},
	.initial_state = not_running_wwnr,
	.final_states = { 1, 0 },
};
