#!/bin/sh
#
# Suspend the system using either ACPI or APM.
# For APM, "apm -z" will be issued.
# For ACPI, the configured suspend state will be looked up, checked to see
# if it is supported, and "acpiconf -s <state>" will be issued.
#
# Mark Santcroos <marks@ripe.net>
#
# $FreeBSD$

PATH=/sbin:/usr/sbin:/usr/bin:/bin

ACPI_SUSPEND_STATE=hw.acpi.suspend_state
ACPI_SUPPORTED_STATES=hw.acpi.supported_sleep_state
APM_SUSPEND_DELAY=machdep.apm_suspend_delay

# Check for ACPI support
if sysctl $ACPI_SUSPEND_STATE >/dev/null 2>&1; then
	# Get configured suspend state
	SUSPEND_STATE=`sysctl -n $ACPI_SUSPEND_STATE `

	# Get list of supported suspend states
	SUPPORTED_STATES=`sysctl -n $ACPI_SUPPORTED_STATES `

	# Check if the configured suspend state is supported by the system
	if echo $SUPPORTED_STATES | grep $SUSPEND_STATE >/dev/null; then
		# execute ACPI style suspend command
		exec acpiconf -s $SUSPEND_STATE
	else
		echo -n "Requested suspend state $SUSPEND_STATE "
		echo -n "is not supported.  "
		echo    "Supported states: $SUPPORTED_STATES"
	fi
# Check for APM support
elif sysctl $APM_SUSPEND_DELAY >/dev/null 2>&1; then
	# Execute APM style suspend command
	exec apm -z
else
	echo "Error: no ACPI or APM suspend support found."
fi

exit 1
