/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__LOCAL_CAPTURE_H__
#define	__LOCAL_CAPTURE_H__

struct ImageLinkList;

class LocalCapture {
public:
	LocalCapture();
	~LocalCapture();
	void write(char* string);
	void copyImages(ImageLinkList* ptr);
	void createAPCDirectory(char* target_path);
private:
	char* createUniqueDirectory(const char* path, const char* ending);
	int removeDirAndAllContents(char* path);
};

#endif 	//__LOCAL_CAPTURE_H__
