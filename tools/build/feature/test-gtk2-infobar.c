#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic error "-Wstrict-prototypes"

int main(int argc, char *argv[])
{
	gtk_init(&argc, &argv);
	gtk_info_bar_new();

	return 0;
}
