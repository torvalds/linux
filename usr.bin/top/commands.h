/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 *  Copyright (c) 2016, Randy Westlund
 *
 * $FreeBSD$
 */
#ifndef COMMANDS_H
#define COMMANDS_H

void	show_errors(void);
int	error_count(void);
void	show_help(void);

enum cmd_id {
	CMD_NONE,
	CMD_redraw,
	CMD_update,
	CMD_quit,
	CMD_help,
	CMD_errors,
	CMD_number,
	CMD_delay,
	CMD_displays,
	CMD_kill,
	CMD_renice,
	CMD_idletog,
	CMD_user,
	CMD_selftog,
	CMD_thrtog,
	CMD_viewtog,
	CMD_viewsys,
	CMD_wcputog,
	CMD_showargs,
	CMD_jidtog,
	CMD_kidletog,
	CMD_pcputog,
	CMD_jail,
	CMD_swaptog,
	CMD_order,
	CMD_pid	,
	CMD_toggletid,
};

struct command {
	char c;
	const char * const desc;
	bool available_to_dumb;
	enum cmd_id id;
};

extern const struct command all_commands[];

#endif /* COMMANDS_H */
