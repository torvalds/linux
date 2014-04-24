/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "LocalCapture.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "SessionData.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "EventsXML.h"

LocalCapture::LocalCapture() {}

LocalCapture::~LocalCapture() {}

void LocalCapture::createAPCDirectory(char* target_path) {
	gSessionData->mAPCDir = createUniqueDirectory(target_path, ".apc");
	if ((removeDirAndAllContents(gSessionData->mAPCDir) != 0 || mkdir(gSessionData->mAPCDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)) {
		logg->logError(__FILE__, __LINE__, "Unable to create directory %s", gSessionData->mAPCDir);
		handleException();
	}
}

void LocalCapture::write(char* string) {
	char file[PATH_MAX];

	// Set full path
	snprintf(file, PATH_MAX, "%s/session.xml", gSessionData->mAPCDir);

	// Write the file
	if (util->writeToDisk(file, string) < 0) {
		logg->logError(__FILE__, __LINE__, "Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	// Write events XML
	EventsXML eventsXML;
	eventsXML.write(gSessionData->mAPCDir);
}

char* LocalCapture::createUniqueDirectory(const char* initialPath, const char* ending) {
	char* output;
	char path[PATH_MAX];

	// Ensure the path is an absolute path, i.e. starts with a slash
	if (initialPath == 0 || strlen(initialPath) == 0) {
		logg->logError(__FILE__, __LINE__, "Missing -o command line option required for a local capture.");
		handleException();
	} else if (initialPath[0] != '/') {
		if (getcwd(path, PATH_MAX) == 0) {
			logg->logMessage("Unable to retrieve the current working directory");
		}
		strncat(path, "/", PATH_MAX - strlen(path) - 1);
		strncat(path, initialPath, PATH_MAX - strlen(path) - 1);
	} else {
		strncpy(path, initialPath, PATH_MAX);
		path[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
	}

	// Add ending if it is not already there
	if (strcmp(&path[strlen(path) - strlen(ending)], ending) != 0) {
		strncat(path, ending, PATH_MAX - strlen(path) - 1);
	}

	output = strdup(path);

	return output;
}

int LocalCapture::removeDirAndAllContents(char* path) {
	int error = 0;
	struct stat mFileInfo;
	// Does the path exist?
	if (stat(path, &mFileInfo) == 0) {
		// Is it a directory?
		if (mFileInfo.st_mode & S_IFDIR) {
			DIR * dir = opendir(path);
			dirent* entry = readdir(dir);
			while (entry) {
				if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
					char* newpath = (char*)malloc(strlen(path) + strlen(entry->d_name) + 2);
					sprintf(newpath, "%s/%s", path, entry->d_name);
					error = removeDirAndAllContents(newpath);
					free(newpath);
					if (error) {
						break;
					}
				}
				entry = readdir(dir);
			}
			closedir(dir);
			if (error == 0) {
				error = rmdir(path);
			}
		} else {
			error = remove(path);
		}
	}
	return error;
}

void LocalCapture::copyImages(ImageLinkList* ptr) {
	char dstfilename[PATH_MAX];

	while (ptr) {
		strncpy(dstfilename, gSessionData->mAPCDir, PATH_MAX);
		dstfilename[PATH_MAX - 1] = 0; // strncpy does not guarantee a null-terminated string
		if (gSessionData->mAPCDir[strlen(gSessionData->mAPCDir) - 1] != '/') {
			strncat(dstfilename, "/", PATH_MAX - strlen(dstfilename) - 1);
		}
		strncat(dstfilename, util->getFilePart(ptr->path), PATH_MAX - strlen(dstfilename) - 1);
		if (util->copyFile(ptr->path, dstfilename)) {
			logg->logMessage("copied file %s to %s", ptr->path, dstfilename);
		} else {
			logg->logMessage("copy of file %s to %s failed", ptr->path, dstfilename);
		}

		ptr = ptr->next;
	}
}
