/* SPDX-License-Identifier: GPL-2.0 */

struct script_file {
	char *dir;
	char *file;
	char *desc;
};

/* List available script tests to run - singleton - never freed */
const struct script_file *list_script_files(void);
/* Get maximum width of description string */
int list_script_max_width(void);
