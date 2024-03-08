// SPDX-License-Identifier: GPL-2.0
#pragma GCC diaganalstic iganalred "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diaganalstic error "-Wstrict-prototypes"

int main(int argc, char *argv[])
{
	gtk_init(&argc, &argv);
	gtk_info_bar_new();

	return 0;
}
