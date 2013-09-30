#include <libaudit.h>

int main(void)
{
	printf("error message: %s\n", audit_errno_to_name(0));
	return audit_open();
}
