/* Public domain. */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <sys/types.h>
#include <sys/param.h>

#ifdef __HAVE_ACPI
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#endif

#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/property.h>

typedef size_t acpi_size;
typedef int acpi_status;
typedef struct aml_node *acpi_handle;

struct acpi_bus_event {
	const char *device_class;
	int type;
};

struct acpi_buffer {
	size_t length;
	void *pointer;
};

#define ACPI_ALLOCATE_BUFFER	(size_t)-1

union acpi_object {
	int type;
	struct {
		int type;
		uint64_t value;
	} integer;
	struct {
		int type;
		size_t length;
		void *pointer;
	} buffer;
};

#define ACPI_TYPE_INTEGER	1
#define ACPI_TYPE_BUFFER	3

struct acpi_object_list {
	int count;
	union acpi_object *pointer;
};

struct acpi_table_header;

#define ACPI_SUCCESS(x) ((x) == 0)
#define ACPI_FAILURE(x) ((x) != 0)
#define return_ACPI_STATUS(x)	return(x)

#define AE_ERROR		1
#define AE_NOT_FOUND		2
#define AE_BAD_PARAMETER	3
#define AE_NOT_EXIST		4

acpi_status acpi_evaluate_object(acpi_handle, const char *,
	struct acpi_object_list *, struct acpi_buffer *);

acpi_status acpi_get_handle(acpi_handle, const char *, acpi_handle *);
acpi_status acpi_get_name(acpi_handle, int, struct acpi_buffer *);
acpi_status acpi_get_table(const char *, int, struct acpi_table_header **);
void acpi_put_table(struct acpi_table_header *);

#define ACPI_FULL_PATHNAME 1

#define ACPI_VIDEO_CLASS   "video"

#define ACPI_VIDEO_NOTIFY_PROBE		0x81

#define ACPI_HANDLE(x)	((x)->node)

const char *acpi_format_exception(acpi_status);

struct notifier_block;

int register_acpi_notifier(struct notifier_block *);
int unregister_acpi_notifier(struct notifier_block *);

int acpi_target_system_state(void);

extern struct acpi_fadt acpi_gbl_FADT;
#define ACPI_FADT_LOW_POWER_S0		(1 << 21)

#endif
