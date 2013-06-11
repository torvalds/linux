/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "Child.h"
#include "SessionData.h"
#include "OlySocket.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "KMod.h"

#define DEBUG false

extern Child* child;
static int shutdownFilesystem();
static pthread_mutex_t numSessions_mutex;
static int numSessions = 0;
static OlySocket* socket = NULL;
static bool driverRunningAtStart = false;
static bool driverMountedAtStart = false;

struct cmdline_t {
	int port;
	char* module;
};

void cleanUp() {
	if (shutdownFilesystem() == -1) {
		logg->logMessage("Error shutting down gator filesystem");
	}
	delete socket;
	delete util;
	delete logg;
}

// CTRL C Signal Handler
static void handler(int signum) {
	logg->logMessage("Received signal %d, gator daemon exiting", signum);

	// Case 1: both child and parent receive the signal
	if (numSessions > 0) {
		// Arbitrary sleep of 1 second to give time for the child to exit;
		// if something bad happens, continue the shutdown process regardless
		sleep(1);
	}

	// Case 2: only the parent received the signal
	if (numSessions > 0) {
		// Kill child threads - the first signal exits gracefully
		logg->logMessage("Killing process group as %d child was running when signal was received", numSessions);
		kill(0, SIGINT);

		// Give time for the child to exit
		sleep(1);

		if (numSessions > 0) {
			// The second signal force kills the child
			logg->logMessage("Force kill the child");
			kill(0, SIGINT);
			// Again, sleep for 1 second
			sleep(1);

			if (numSessions > 0) {
				// Something bad has really happened; the child is not exiting and therefore may hold the /dev/gator resource open
				printf("Unable to kill the gatord child process, thus gator.ko may still be loaded.\n");
			}
		}
	}

	cleanUp();
	exit(0);
}

// Child exit Signal Handler
static void child_exit(int signum) {
	int status;
	int pid = wait(&status);
	if (pid != -1) {
		pthread_mutex_lock(&numSessions_mutex);
		numSessions--;
		pthread_mutex_unlock(&numSessions_mutex);
		logg->logMessage("Child process %d exited with status %d", pid, status);
	}
}

// retval: -1 = failure; 0 = was already mounted; 1 = successfully mounted
static int mountGatorFS() {
	// If already mounted,
	if (access("/dev/gator/buffer", F_OK) == 0) {
		return 0;
	}

	// else, mount the filesystem
	mkdir("/dev/gator", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (mount("nodev", "/dev/gator", "gatorfs", 0, NULL) != 0) {
		return -1;
	} else {
		return 1;
	}
}

static bool init_module (const char * const location) {
	bool ret(false);
	const int fd = open(location, O_RDONLY);
	if (fd >= 0) {
		struct stat st;
		if (fstat(fd, &st) == 0) {
			void * const p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			if (p != MAP_FAILED) {
				if (syscall(__NR_init_module, p, st.st_size, "") == 0) {
					ret = true;
				}
				munmap(p, st.st_size);
			}
		}
		close(fd);
	}

	return ret;
}

static int setupFilesystem(char* module) {
	int retval;

	// Verify root permissions
	uid_t euid = geteuid();
	if (euid) {
		logg->logError(__FILE__, __LINE__, "gatord must be launched with root privileges");
		handleException();
	}

	if (module) {
		// unmount and rmmod if the module was specified on the commandline, i.e. ensure that the specified module is indeed running
		shutdownFilesystem();

		// if still mounted
		if (access("/dev/gator/buffer", F_OK) == 0) {
			logg->logError(__FILE__, __LINE__, "Unable to remove the running gator.ko. Manually remove the module or use the running module by not specifying one on the commandline");
			handleException();
		}
	}

	retval = mountGatorFS();
	if (retval == 1) {
		logg->logMessage("Driver already running at startup");
		driverRunningAtStart = true;
	} else if (retval == 0) {
		logg->logMessage("Driver already mounted at startup");
		driverRunningAtStart = driverMountedAtStart = true;
	} else {
		char command[256]; // arbitrarily large amount
		char location[256]; // arbitrarily large amount

		if (module) {
			strncpy(location, module, sizeof(location));
		} else {
			// Is the driver co-located in the same directory?
			if (util->getApplicationFullPath(location, sizeof(location)) != 0) { // allow some buffer space
				logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
			}
			strncat(location, "gator.ko", sizeof(location) - strlen(location) - 1);
		}

		if (access(location, F_OK) == -1) {
			logg->logError(__FILE__, __LINE__, "Unable to locate gator.ko driver:\n  >>> gator.ko should be co-located with gatord in the same directory\n  >>> OR insmod gator.ko prior to launching gatord\n  >>> OR specify the location of gator.ko on the command line");
			handleException();
		}

		// Load driver
		bool success = init_module(location);
		if (!success) {
			logg->logMessage("init_module failed, trying insmod");
			snprintf(command, sizeof(command), "insmod %s >/dev/null 2>&1", location);
			if (system(command) != 0) {
				logg->logMessage("Unable to load gator.ko driver with command: %s", command);
				logg->logError(__FILE__, __LINE__, "Unable to load (insmod) gator.ko driver:\n  >>> gator.ko must be built against the current kernel version & configuration\n  >>> See dmesg for more details");
				handleException();
			}
		}

		if (mountGatorFS() == -1) {
			logg->logError(__FILE__, __LINE__, "Unable to mount the gator filesystem needed for profiling.");
			handleException();
		}
	}

	return 0;
}

static int shutdownFilesystem() {
	if (driverMountedAtStart == false) {
		umount("/dev/gator");
	}
	if (driverRunningAtStart == false) {
		if (syscall(__NR_delete_module, "gator", O_NONBLOCK) != 0) {
			logg->logMessage("delete_module failed, trying rmmod");
			if (system("rmmod gator >/dev/null 2>&1") != 0) {
				return -1;
			}
		}
	}

	return 0; // success
}

static struct cmdline_t parseCommandLine(int argc, char** argv) {
	struct cmdline_t cmdline;
	cmdline.port = 8080;
	cmdline.module = NULL;
	char version_string[256]; // arbitrary length to hold the version information
	int c;

	// build the version string
	if (PROTOCOL_VERSION < PROTOCOL_DEV) {
		snprintf(version_string, sizeof(version_string), "Streamline gatord version %d (DS-5 v5.%d)", PROTOCOL_VERSION, PROTOCOL_VERSION + 1);
	} else {
		snprintf(version_string, sizeof(version_string), "Streamline gatord development version %d", PROTOCOL_VERSION);
	}

	while ((c = getopt(argc, argv, "hvp:s:c:e:m:o:")) != -1) {
		switch(c) {
			case 'c':
				gSessionData->mConfigurationXMLPath = optarg;
				break;
			case 'e':
				gSessionData->mEventsXMLPath = optarg;
				break;
			case 'm':
				cmdline.module = optarg;
				break;
			case 'p':
				cmdline.port = strtol(optarg, NULL, 10);
				break;
			case 's':
				gSessionData->mSessionXMLPath = optarg;
				break;
			case 'o':
				gSessionData->mTargetPath = optarg;
				break;
			case 'h':
			case '?':
				logg->logError(__FILE__, __LINE__,
					"%s. All parameters are optional:\n"
					"-c config_xml   path and filename of the configuration.xml to use\n"
					"-e events_xml   path and filename of the events.xml to use\n"
					"-h              this help page\n"
					"-m module       path and filename of gator.ko\n"
					"-p port_number  port upon which the server listens; default is 8080\n"
					"-s session_xml  path and filename of a session xml used for local capture\n"
					"-o apc_dir      path and name of the output for a local capture\n"
					"-v              version information\n"
					, version_string);
				handleException();
				break;
			case 'v':
				logg->logError(__FILE__, __LINE__, version_string);
				handleException();
				break;
		}
	}

	// Error checking
	if (cmdline.port != 8080 && gSessionData->mSessionXMLPath != NULL) {
		logg->logError(__FILE__, __LINE__, "Only a port or a session xml can be specified, not both");
		handleException();
	}

	if (gSessionData->mTargetPath != NULL && gSessionData->mSessionXMLPath == NULL) {
		logg->logError(__FILE__, __LINE__, "Missing -s command line option required for a local capture.");
		handleException();
	}

	if (optind < argc) {
		logg->logError(__FILE__, __LINE__, "Unknown argument: %s. Use '-h' for help.", argv[optind]);
		handleException();
	}

	return cmdline;
}

// Gator data flow: collector -> collector fifo -> sender
int main(int argc, char** argv, char* envp[]) {
	// Ensure proper signal handling by making gatord the process group leader
	//   e.g. it may not be the group leader when launched as 'sudo gatord'
	setsid();

	logg = new Logging(DEBUG);  // Set up global thread-safe logging
	gSessionData = new SessionData(); // Global data class
	util = new OlyUtility();	// Set up global utility class

	// Initialize drivers
	new KMod();

	prctl(PR_SET_NAME, (unsigned long)&"gatord-main", 0, 0, 0);
	pthread_mutex_init(&numSessions_mutex, NULL);

	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	signal(SIGABRT, handler);

	// Set to high priority
	if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), -19) == -1) {
		logg->logMessage("setpriority() failed");
	}

	// Parse the command line parameters
	struct cmdline_t cmdline = parseCommandLine(argc, argv);

	// Call before setting up the SIGCHLD handler, as system() spawns child processes
	setupFilesystem(cmdline.module);

	// Handle child exit codes
	signal(SIGCHLD, child_exit);

	// Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
	// Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
	signal(SIGPIPE, SIG_IGN);

	// If the command line argument is a session xml file, no need to open a socket
	if (gSessionData->mSessionXMLPath) {
		child = new Child();
		child->run();
		delete child;
	} else {
		socket = new OlySocket(cmdline.port, true);
		// Forever loop, can be exited via a signal or exception
		while (1) {
			logg->logMessage("Waiting on connection...");
			socket->acceptConnection();

			int pid = fork();
			if (pid < 0) {
				// Error
				logg->logError(__FILE__, __LINE__, "Fork process failed. Please power cycle the target device if this error persists.");
			} else if (pid == 0) {
				// Child
				socket->closeServerSocket();
				child = new Child(socket, numSessions + 1);
				child->run();
				delete child;
				exit(0);
			} else {
				// Parent
				socket->closeSocket();

				pthread_mutex_lock(&numSessions_mutex);
				numSessions++;
				pthread_mutex_unlock(&numSessions_mutex);

				// Maximum number of connections is 2
				int wait = 0;
				while (numSessions > 1) {
					// Throttle until one of the children exits before continuing to accept another socket connection
					logg->logMessage("%d sessions active!", numSessions);
					if (wait++ >= 10) { // Wait no more than 10 seconds
						// Kill last created child
						kill(pid, SIGALRM);
						break;
					}
					sleep(1);
				}
			}
		}
	}

	cleanUp();
	return 0;
}
