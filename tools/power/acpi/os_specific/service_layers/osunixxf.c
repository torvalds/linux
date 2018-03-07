/******************************************************************************
 *
 * Module Name: osunixxf - UNIX OSL interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * These interfaces are required in order to compile the ASL compiler and the
 * various ACPICA tools under Linux or other Unix-like system.
 */
#include <acpi/acpi.h>
#include "accommon.h"
#include "amlcode.h"
#include "acparser.h"
#include "acdebug.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME("osunixxf")

/* Upcalls to acpi_exec */
void
ae_table_override(struct acpi_table_header *existing_table,
		  struct acpi_table_header **new_table);

typedef void *(*PTHREAD_CALLBACK) (void *);

/* Buffer used by acpi_os_vprintf */

#define ACPI_VPRINTF_BUFFER_SIZE    512
#define _ASCII_NEWLINE              '\n'

/* Terminal support for acpi_exec only */

#ifdef ACPI_EXEC_APP
#include <termios.h>

struct termios original_term_attributes;
int term_attributes_were_set = 0;

acpi_status acpi_ut_read_line(char *buffer, u32 buffer_length, u32 *bytes_read);

static void os_enter_line_edit_mode(void);

static void os_exit_line_edit_mode(void);

/******************************************************************************
 *
 * FUNCTION:    os_enter_line_edit_mode, os_exit_line_edit_mode
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enter/Exit the raw character input mode for the terminal.
 *
 * Interactive line-editing support for the AML debugger. Used with the
 * common/acgetline module.
 *
 * readline() is not used because of non-portability. It is not available
 * on all systems, and if it is, often the package must be manually installed.
 *
 * Therefore, we use the POSIX tcgetattr/tcsetattr and do the minimal line
 * editing that we need in acpi_os_get_line.
 *
 * If the POSIX tcgetattr/tcsetattr interfaces are unavailable, these
 * calls will also work:
 *     For os_enter_line_edit_mode: system ("stty cbreak -echo")
 *     For os_exit_line_edit_mode: system ("stty cooked echo")
 *
 *****************************************************************************/

static void os_enter_line_edit_mode(void)
{
	struct termios local_term_attributes;

	term_attributes_were_set = 0;

	/* STDIN must be a terminal */

	if (!isatty(STDIN_FILENO)) {
		return;
	}

	/* Get and keep the original attributes */

	if (tcgetattr(STDIN_FILENO, &original_term_attributes)) {
		fprintf(stderr, "Could not get terminal attributes!\n");
		return;
	}

	/* Set the new attributes to enable raw character input */

	memcpy(&local_term_attributes, &original_term_attributes,
	       sizeof(struct termios));

	local_term_attributes.c_lflag &= ~(ICANON | ECHO);
	local_term_attributes.c_cc[VMIN] = 1;
	local_term_attributes.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &local_term_attributes)) {
		fprintf(stderr, "Could not set terminal attributes!\n");
		return;
	}

	term_attributes_were_set = 1;
}

static void os_exit_line_edit_mode(void)
{

	if (!term_attributes_were_set) {
		return;
	}

	/* Set terminal attributes back to the original values */

	if (tcsetattr(STDIN_FILENO, TCSANOW, &original_term_attributes)) {
		fprintf(stderr, "Could not restore terminal attributes!\n");
	}
}

#else

/* These functions are not needed for other ACPICA utilities */

#define os_enter_line_edit_mode()
#define os_exit_line_edit_mode()
#endif

/******************************************************************************
 *
 * FUNCTION:    acpi_os_initialize, acpi_os_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and terminate this module.
 *
 *****************************************************************************/

acpi_status acpi_os_initialize(void)
{
	acpi_status status;

	acpi_gbl_output_file = stdout;

	os_enter_line_edit_mode();

	status = acpi_os_create_lock(&acpi_gbl_print_lock);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	return (AE_OK);
}

acpi_status acpi_os_terminate(void)
{

	os_exit_line_edit_mode();
	return (AE_OK);
}

#ifndef ACPI_USE_NATIVE_RSDP_POINTER
/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_root_pointer
 *
 * PARAMETERS:  None
 *
 * RETURN:      RSDP physical address
 *
 * DESCRIPTION: Gets the ACPI root pointer (RSDP)
 *
 *****************************************************************************/

acpi_physical_address acpi_os_get_root_pointer(void)
{

	return (0);
}
#endif

/******************************************************************************
 *
 * FUNCTION:    acpi_os_predefined_override
 *
 * PARAMETERS:  init_val            - Initial value of the predefined object
 *              new_val             - The new value for the object
 *
 * RETURN:      Status, pointer to value. Null pointer returned if not
 *              overriding.
 *
 * DESCRIPTION: Allow the OS to override predefined names
 *
 *****************************************************************************/

acpi_status
acpi_os_predefined_override(const struct acpi_predefined_names *init_val,
			    acpi_string *new_val)
{

	if (!init_val || !new_val) {
		return (AE_BAD_PARAMETER);
	}

	*new_val = NULL;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_table_override
 *
 * PARAMETERS:  existing_table      - Header of current table (probably
 *                                    firmware)
 *              new_table           - Where an entire new table is returned.
 *
 * RETURN:      Status, pointer to new table. Null pointer returned if no
 *              table is available to override
 *
 * DESCRIPTION: Return a different version of a table if one is available
 *
 *****************************************************************************/

acpi_status
acpi_os_table_override(struct acpi_table_header *existing_table,
		       struct acpi_table_header **new_table)
{

	if (!existing_table || !new_table) {
		return (AE_BAD_PARAMETER);
	}

	*new_table = NULL;

#ifdef ACPI_EXEC_APP

	ae_table_override(existing_table, new_table);
	return (AE_OK);
#else

	return (AE_NO_ACPI_TABLES);
#endif
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_physical_table_override
 *
 * PARAMETERS:  existing_table      - Header of current table (probably firmware)
 *              new_address         - Where new table address is returned
 *                                    (Physical address)
 *              new_table_length    - Where new table length is returned
 *
 * RETURN:      Status, address/length of new table. Null pointer returned
 *              if no table is available to override.
 *
 * DESCRIPTION: Returns AE_SUPPORT, function not used in user space.
 *
 *****************************************************************************/

acpi_status
acpi_os_physical_table_override(struct acpi_table_header *existing_table,
				acpi_physical_address *new_address,
				u32 *new_table_length)
{

	return (AE_SUPPORT);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_enter_sleep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *              rega_value          - Register A value
 *              regb_value          - Register B value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: A hook before writing sleep registers to enter the sleep
 *              state. Return AE_CTRL_TERMINATE to skip further sleep register
 *              writes.
 *
 *****************************************************************************/

acpi_status acpi_os_enter_sleep(u8 sleep_state, u32 rega_value, u32 regb_value)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_redirect_output
 *
 * PARAMETERS:  destination         - An open file handle/pointer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Causes redirect of acpi_os_printf and acpi_os_vprintf
 *
 *****************************************************************************/

void acpi_os_redirect_output(void *destination)
{

	acpi_gbl_output_file = destination;
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_printf
 *
 * PARAMETERS:  fmt, ...            - Standard printf format
 *
 * RETURN:      None
 *
 * DESCRIPTION: Formatted output. Note: very similar to acpi_os_vprintf
 *              (performance), changes should be tracked in both functions.
 *
 *****************************************************************************/

void ACPI_INTERNAL_VAR_XFACE acpi_os_printf(const char *fmt, ...)
{
	va_list args;
	u8 flags;

	flags = acpi_gbl_db_output_flags;
	if (flags & ACPI_DB_REDIRECTABLE_OUTPUT) {

		/* Output is directable to either a file (if open) or the console */

		if (acpi_gbl_debug_file) {

			/* Output file is open, send the output there */

			va_start(args, fmt);
			vfprintf(acpi_gbl_debug_file, fmt, args);
			va_end(args);
		} else {
			/* No redirection, send output to console (once only!) */

			flags |= ACPI_DB_CONSOLE_OUTPUT;
		}
	}

	if (flags & ACPI_DB_CONSOLE_OUTPUT) {
		va_start(args, fmt);
		vfprintf(acpi_gbl_output_file, fmt, args);
		va_end(args);
	}
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_vprintf
 *
 * PARAMETERS:  fmt                 - Standard printf format
 *              args                - Argument list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Formatted output with argument list pointer. Note: very
 *              similar to acpi_os_printf, changes should be tracked in both
 *              functions.
 *
 *****************************************************************************/

void acpi_os_vprintf(const char *fmt, va_list args)
{
	u8 flags;
	char buffer[ACPI_VPRINTF_BUFFER_SIZE];

	/*
	 * We build the output string in a local buffer because we may be
	 * outputting the buffer twice. Using vfprintf is problematic because
	 * some implementations modify the args pointer/structure during
	 * execution. Thus, we use the local buffer for portability.
	 *
	 * Note: Since this module is intended for use by the various ACPICA
	 * utilities/applications, we can safely declare the buffer on the stack.
	 * Also, This function is used for relatively small error messages only.
	 */
	vsnprintf(buffer, ACPI_VPRINTF_BUFFER_SIZE, fmt, args);

	flags = acpi_gbl_db_output_flags;
	if (flags & ACPI_DB_REDIRECTABLE_OUTPUT) {

		/* Output is directable to either a file (if open) or the console */

		if (acpi_gbl_debug_file) {

			/* Output file is open, send the output there */

			fputs(buffer, acpi_gbl_debug_file);
		} else {
			/* No redirection, send output to console (once only!) */

			flags |= ACPI_DB_CONSOLE_OUTPUT;
		}
	}

	if (flags & ACPI_DB_CONSOLE_OUTPUT) {
		fputs(buffer, acpi_gbl_output_file);
	}
}

#ifndef ACPI_EXEC_APP
/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_line
 *
 * PARAMETERS:  buffer              - Where to return the command line
 *              buffer_length       - Maximum length of Buffer
 *              bytes_read          - Where the actual byte count is returned
 *
 * RETURN:      Status and actual bytes read
 *
 * DESCRIPTION: Get the next input line from the terminal. NOTE: For the
 *              acpi_exec utility, we use the acgetline module instead to
 *              provide line-editing and history support.
 *
 *****************************************************************************/

acpi_status acpi_os_get_line(char *buffer, u32 buffer_length, u32 *bytes_read)
{
	int input_char;
	u32 end_of_line;

	/* Standard acpi_os_get_line for all utilities except acpi_exec */

	for (end_of_line = 0;; end_of_line++) {
		if (end_of_line >= buffer_length) {
			return (AE_BUFFER_OVERFLOW);
		}

		if ((input_char = getchar()) == EOF) {
			return (AE_ERROR);
		}

		if (!input_char || input_char == _ASCII_NEWLINE) {
			break;
		}

		buffer[end_of_line] = (char)input_char;
	}

	/* Null terminate the buffer */

	buffer[end_of_line] = 0;

	/* Return the number of bytes in the string */

	if (bytes_read) {
		*bytes_read = end_of_line;
	}

	return (AE_OK);
}
#endif

#ifndef ACPI_USE_NATIVE_MEMORY_MAPPING
/******************************************************************************
 *
 * FUNCTION:    acpi_os_map_memory
 *
 * PARAMETERS:  where               - Physical address of memory to be mapped
 *              length              - How much memory to map
 *
 * RETURN:      Pointer to mapped memory. Null on error.
 *
 * DESCRIPTION: Map physical memory into caller's address space
 *
 *****************************************************************************/

void *acpi_os_map_memory(acpi_physical_address where, acpi_size length)
{

	return (ACPI_TO_POINTER((acpi_size)where));
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_unmap_memory
 *
 * PARAMETERS:  where               - Logical address of memory to be unmapped
 *              length              - How much memory to unmap
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete a previously created mapping. Where and Length must
 *              correspond to a previous mapping exactly.
 *
 *****************************************************************************/

void acpi_os_unmap_memory(void *where, acpi_size length)
{

	return;
}
#endif

/******************************************************************************
 *
 * FUNCTION:    acpi_os_allocate
 *
 * PARAMETERS:  size                - Amount to allocate, in bytes
 *
 * RETURN:      Pointer to the new allocation. Null on error.
 *
 * DESCRIPTION: Allocate memory. Algorithm is dependent on the OS.
 *
 *****************************************************************************/

void *acpi_os_allocate(acpi_size size)
{
	void *mem;

	mem = (void *)malloc((size_t) size);
	return (mem);
}

#ifdef USE_NATIVE_ALLOCATE_ZEROED
/******************************************************************************
 *
 * FUNCTION:    acpi_os_allocate_zeroed
 *
 * PARAMETERS:  size                - Amount to allocate, in bytes
 *
 * RETURN:      Pointer to the new allocation. Null on error.
 *
 * DESCRIPTION: Allocate and zero memory. Algorithm is dependent on the OS.
 *
 *****************************************************************************/

void *acpi_os_allocate_zeroed(acpi_size size)
{
	void *mem;

	mem = (void *)calloc(1, (size_t) size);
	return (mem);
}
#endif

/******************************************************************************
 *
 * FUNCTION:    acpi_os_free
 *
 * PARAMETERS:  mem                 - Pointer to previously allocated memory
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free memory allocated via acpi_os_allocate
 *
 *****************************************************************************/

void acpi_os_free(void *mem)
{

	free(mem);
}

#ifdef ACPI_SINGLE_THREADED
/******************************************************************************
 *
 * FUNCTION:    Semaphore stub functions
 *
 * DESCRIPTION: Stub functions used for single-thread applications that do
 *              not require semaphore synchronization. Full implementations
 *              of these functions appear after the stubs.
 *
 *****************************************************************************/

acpi_status
acpi_os_create_semaphore(u32 max_units,
			 u32 initial_units, acpi_handle *out_handle)
{
	*out_handle = (acpi_handle)1;
	return (AE_OK);
}

acpi_status acpi_os_delete_semaphore(acpi_handle handle)
{
	return (AE_OK);
}

acpi_status acpi_os_wait_semaphore(acpi_handle handle, u32 units, u16 timeout)
{
	return (AE_OK);
}

acpi_status acpi_os_signal_semaphore(acpi_handle handle, u32 units)
{
	return (AE_OK);
}

#else
/******************************************************************************
 *
 * FUNCTION:    acpi_os_create_semaphore
 *
 * PARAMETERS:  initial_units       - Units to be assigned to the new semaphore
 *              out_handle          - Where a handle will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an OS semaphore
 *
 *****************************************************************************/

acpi_status
acpi_os_create_semaphore(u32 max_units,
			 u32 initial_units, acpi_handle *out_handle)
{
	sem_t *sem;

	if (!out_handle) {
		return (AE_BAD_PARAMETER);
	}
#ifdef __APPLE__
	{
		static int semaphore_count = 0;
		char semaphore_name[32];

		snprintf(semaphore_name, sizeof(semaphore_name), "acpi_sem_%d",
			 semaphore_count++);
		printf("%s\n", semaphore_name);
		sem =
		    sem_open(semaphore_name, O_EXCL | O_CREAT, 0755,
			     initial_units);
		if (!sem) {
			return (AE_NO_MEMORY);
		}
		sem_unlink(semaphore_name);	/* This just deletes the name */
	}

#else
	sem = acpi_os_allocate(sizeof(sem_t));
	if (!sem) {
		return (AE_NO_MEMORY);
	}

	if (sem_init(sem, 0, initial_units) == -1) {
		acpi_os_free(sem);
		return (AE_BAD_PARAMETER);
	}
#endif

	*out_handle = (acpi_handle)sem;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_delete_semaphore
 *
 * PARAMETERS:  handle              - Handle returned by acpi_os_create_semaphore
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete an OS semaphore
 *
 *****************************************************************************/

acpi_status acpi_os_delete_semaphore(acpi_handle handle)
{
	sem_t *sem = (sem_t *) handle;

	if (!sem) {
		return (AE_BAD_PARAMETER);
	}
#ifdef __APPLE__
	if (sem_close(sem) == -1) {
		return (AE_BAD_PARAMETER);
	}
#else
	if (sem_destroy(sem) == -1) {
		return (AE_BAD_PARAMETER);
	}
#endif

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_wait_semaphore
 *
 * PARAMETERS:  handle              - Handle returned by acpi_os_create_semaphore
 *              units               - How many units to wait for
 *              msec_timeout        - How long to wait (milliseconds)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Wait for units
 *
 *****************************************************************************/

acpi_status
acpi_os_wait_semaphore(acpi_handle handle, u32 units, u16 msec_timeout)
{
	acpi_status status = AE_OK;
	sem_t *sem = (sem_t *) handle;
	int ret_val;
#ifndef ACPI_USE_ALTERNATE_TIMEOUT
	struct timespec time;
#endif

	if (!sem) {
		return (AE_BAD_PARAMETER);
	}

	switch (msec_timeout) {
		/*
		 * No Wait:
		 * --------
		 * A zero timeout value indicates that we shouldn't wait - just
		 * acquire the semaphore if available otherwise return AE_TIME
		 * (a.k.a. 'would block').
		 */
	case 0:

		if (sem_trywait(sem) == -1) {
			status = (AE_TIME);
		}
		break;

		/* Wait Indefinitely */

	case ACPI_WAIT_FOREVER:

		while (((ret_val = sem_wait(sem)) == -1) && (errno == EINTR)) {
			continue;	/* Restart if interrupted */
		}
		if (ret_val != 0) {
			status = (AE_TIME);
		}
		break;

		/* Wait with msec_timeout */

	default:

#ifdef ACPI_USE_ALTERNATE_TIMEOUT
		/*
		 * Alternate timeout mechanism for environments where
		 * sem_timedwait is not available or does not work properly.
		 */
		while (msec_timeout) {
			if (sem_trywait(sem) == 0) {

				/* Got the semaphore */
				return (AE_OK);
			}

			if (msec_timeout >= 10) {
				msec_timeout -= 10;
				usleep(10 * ACPI_USEC_PER_MSEC);	/* ten milliseconds */
			} else {
				msec_timeout--;
				usleep(ACPI_USEC_PER_MSEC);	/* one millisecond */
			}
		}
		status = (AE_TIME);
#else
		/*
		 * The interface to sem_timedwait is an absolute time, so we need to
		 * get the current time, then add in the millisecond Timeout value.
		 */
		if (clock_gettime(CLOCK_REALTIME, &time) == -1) {
			perror("clock_gettime");
			return (AE_TIME);
		}

		time.tv_sec += (msec_timeout / ACPI_MSEC_PER_SEC);
		time.tv_nsec +=
		    ((msec_timeout % ACPI_MSEC_PER_SEC) * ACPI_NSEC_PER_MSEC);

		/* Handle nanosecond overflow (field must be less than one second) */

		if (time.tv_nsec >= ACPI_NSEC_PER_SEC) {
			time.tv_sec += (time.tv_nsec / ACPI_NSEC_PER_SEC);
			time.tv_nsec = (time.tv_nsec % ACPI_NSEC_PER_SEC);
		}

		while (((ret_val = sem_timedwait(sem, &time)) == -1)
		       && (errno == EINTR)) {
			continue;	/* Restart if interrupted */

		}

		if (ret_val != 0) {
			if (errno != ETIMEDOUT) {
				perror("sem_timedwait");
			}
			status = (AE_TIME);
		}
#endif
		break;
	}

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_signal_semaphore
 *
 * PARAMETERS:  handle              - Handle returned by acpi_os_create_semaphore
 *              units               - Number of units to send
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Send units
 *
 *****************************************************************************/

acpi_status acpi_os_signal_semaphore(acpi_handle handle, u32 units)
{
	sem_t *sem = (sem_t *) handle;

	if (!sem) {
		return (AE_BAD_PARAMETER);
	}

	if (sem_post(sem) == -1) {
		return (AE_LIMIT);
	}

	return (AE_OK);
}

#endif				/* ACPI_SINGLE_THREADED */

/******************************************************************************
 *
 * FUNCTION:    Spinlock interfaces
 *
 * DESCRIPTION: Map these interfaces to semaphore interfaces
 *
 *****************************************************************************/

acpi_status acpi_os_create_lock(acpi_spinlock * out_handle)
{

	return (acpi_os_create_semaphore(1, 1, out_handle));
}

void acpi_os_delete_lock(acpi_spinlock handle)
{
	acpi_os_delete_semaphore(handle);
}

acpi_cpu_flags acpi_os_acquire_lock(acpi_handle handle)
{
	acpi_os_wait_semaphore(handle, 1, 0xFFFF);
	return (0);
}

void acpi_os_release_lock(acpi_spinlock handle, acpi_cpu_flags flags)
{
	acpi_os_signal_semaphore(handle, 1);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_install_interrupt_handler
 *
 * PARAMETERS:  interrupt_number    - Level handler should respond to.
 *              isr                 - Address of the ACPI interrupt handler
 *              except_ptr          - Where status is returned
 *
 * RETURN:      Handle to the newly installed handler.
 *
 * DESCRIPTION: Install an interrupt handler. Used to install the ACPI
 *              OS-independent handler.
 *
 *****************************************************************************/

u32
acpi_os_install_interrupt_handler(u32 interrupt_number,
				  acpi_osd_handler service_routine,
				  void *context)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_remove_interrupt_handler
 *
 * PARAMETERS:  handle              - Returned when handler was installed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Uninstalls an interrupt handler.
 *
 *****************************************************************************/

acpi_status
acpi_os_remove_interrupt_handler(u32 interrupt_number,
				 acpi_osd_handler service_routine)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_stall
 *
 * PARAMETERS:  microseconds        - Time to sleep
 *
 * RETURN:      Blocks until sleep is completed.
 *
 * DESCRIPTION: Sleep at microsecond granularity
 *
 *****************************************************************************/

void acpi_os_stall(u32 microseconds)
{

	if (microseconds) {
		usleep(microseconds);
	}
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_sleep
 *
 * PARAMETERS:  milliseconds        - Time to sleep
 *
 * RETURN:      Blocks until sleep is completed.
 *
 * DESCRIPTION: Sleep at millisecond granularity
 *
 *****************************************************************************/

void acpi_os_sleep(u64 milliseconds)
{

	/* Sleep for whole seconds */

	sleep(milliseconds / ACPI_MSEC_PER_SEC);

	/*
	 * Sleep for remaining microseconds.
	 * Arg to usleep() is in usecs and must be less than 1,000,000 (1 second).
	 */
	usleep((milliseconds % ACPI_MSEC_PER_SEC) * ACPI_USEC_PER_MSEC);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_timer
 *
 * PARAMETERS:  None
 *
 * RETURN:      Current time in 100 nanosecond units
 *
 * DESCRIPTION: Get the current system time
 *
 *****************************************************************************/

u64 acpi_os_get_timer(void)
{
	struct timeval time;

	/* This timer has sufficient resolution for user-space application code */

	gettimeofday(&time, NULL);

	/* (Seconds * 10^7 = 100ns(10^-7)) + (Microseconds(10^-6) * 10^1 = 100ns) */

	return (((u64)time.tv_sec * ACPI_100NSEC_PER_SEC) +
		((u64)time.tv_usec * ACPI_100NSEC_PER_USEC));
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_read_pci_configuration
 *
 * PARAMETERS:  pci_id              - Seg/Bus/Dev
 *              pci_register        - Device Register
 *              value               - Buffer where value is placed
 *              width               - Number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read data from PCI configuration space
 *
 *****************************************************************************/

acpi_status
acpi_os_read_pci_configuration(struct acpi_pci_id *pci_id,
			       u32 pci_register, u64 *value, u32 width)
{

	*value = 0;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_write_pci_configuration
 *
 * PARAMETERS:  pci_id              - Seg/Bus/Dev
 *              pci_register        - Device Register
 *              value               - Value to be written
 *              width               - Number of bits
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Write data to PCI configuration space
 *
 *****************************************************************************/

acpi_status
acpi_os_write_pci_configuration(struct acpi_pci_id *pci_id,
				u32 pci_register, u64 value, u32 width)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_read_port
 *
 * PARAMETERS:  address             - Address of I/O port/register to read
 *              value               - Where value is placed
 *              width               - Number of bits
 *
 * RETURN:      Value read from port
 *
 * DESCRIPTION: Read data from an I/O port or register
 *
 *****************************************************************************/

acpi_status acpi_os_read_port(acpi_io_address address, u32 *value, u32 width)
{

	switch (width) {
	case 8:

		*value = 0xFF;
		break;

	case 16:

		*value = 0xFFFF;
		break;

	case 32:

		*value = 0xFFFFFFFF;
		break;

	default:

		return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_write_port
 *
 * PARAMETERS:  address             - Address of I/O port/register to write
 *              value               - Value to write
 *              width               - Number of bits
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write data to an I/O port or register
 *
 *****************************************************************************/

acpi_status acpi_os_write_port(acpi_io_address address, u32 value, u32 width)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_read_memory
 *
 * PARAMETERS:  address             - Physical Memory Address to read
 *              value               - Where value is placed
 *              width               - Number of bits (8,16,32, or 64)
 *
 * RETURN:      Value read from physical memory address. Always returned
 *              as a 64-bit integer, regardless of the read width.
 *
 * DESCRIPTION: Read data from a physical memory address
 *
 *****************************************************************************/

acpi_status
acpi_os_read_memory(acpi_physical_address address, u64 *value, u32 width)
{

	switch (width) {
	case 8:
	case 16:
	case 32:
	case 64:

		*value = 0;
		break;

	default:

		return (AE_BAD_PARAMETER);
	}
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_write_memory
 *
 * PARAMETERS:  address             - Physical Memory Address to write
 *              value               - Value to write
 *              width               - Number of bits (8,16,32, or 64)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write data to a physical memory address
 *
 *****************************************************************************/

acpi_status
acpi_os_write_memory(acpi_physical_address address, u64 value, u32 width)
{

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_readable
 *
 * PARAMETERS:  pointer             - Area to be verified
 *              length              - Size of area
 *
 * RETURN:      TRUE if readable for entire length
 *
 * DESCRIPTION: Verify that a pointer is valid for reading
 *
 *****************************************************************************/

u8 acpi_os_readable(void *pointer, acpi_size length)
{

	return (TRUE);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_writable
 *
 * PARAMETERS:  pointer             - Area to be verified
 *              length              - Size of area
 *
 * RETURN:      TRUE if writable for entire length
 *
 * DESCRIPTION: Verify that a pointer is valid for writing
 *
 *****************************************************************************/

u8 acpi_os_writable(void *pointer, acpi_size length)
{

	return (TRUE);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_signal
 *
 * PARAMETERS:  function            - ACPI A signal function code
 *              info                - Pointer to function-dependent structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Miscellaneous functions. Example implementation only.
 *
 *****************************************************************************/

acpi_status acpi_os_signal(u32 function, void *info)
{

	switch (function) {
	case ACPI_SIGNAL_FATAL:

		break;

	case ACPI_SIGNAL_BREAKPOINT:

		break;

	default:

		break;
	}

	return (AE_OK);
}

/* Optional multi-thread support */

#ifndef ACPI_SINGLE_THREADED
/******************************************************************************
 *
 * FUNCTION:    acpi_os_get_thread_id
 *
 * PARAMETERS:  None
 *
 * RETURN:      Id of the running thread
 *
 * DESCRIPTION: Get the ID of the current (running) thread
 *
 *****************************************************************************/

acpi_thread_id acpi_os_get_thread_id(void)
{
	pthread_t thread;

	thread = pthread_self();
	return (ACPI_CAST_PTHREAD_T(thread));
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_execute
 *
 * PARAMETERS:  type                - Type of execution
 *              function            - Address of the function to execute
 *              context             - Passed as a parameter to the function
 *
 * RETURN:      Status.
 *
 * DESCRIPTION: Execute a new thread
 *
 *****************************************************************************/

acpi_status
acpi_os_execute(acpi_execute_type type,
		acpi_osd_exec_callback function, void *context)
{
	pthread_t thread;
	int ret;

	ret =
	    pthread_create(&thread, NULL, (PTHREAD_CALLBACK) function, context);
	if (ret) {
		acpi_os_printf("Create thread failed");
	}
	return (0);
}

#else				/* ACPI_SINGLE_THREADED */
acpi_thread_id acpi_os_get_thread_id(void)
{
	return (1);
}

acpi_status
acpi_os_execute(acpi_execute_type type,
		acpi_osd_exec_callback function, void *context)
{

	function(context);

	return (AE_OK);
}

#endif				/* ACPI_SINGLE_THREADED */

/******************************************************************************
 *
 * FUNCTION:    acpi_os_wait_events_complete
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Wait for all asynchronous events to complete. This
 *              implementation does nothing.
 *
 *****************************************************************************/

void acpi_os_wait_events_complete(void)
{
	return;
}
