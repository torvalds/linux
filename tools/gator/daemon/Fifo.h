/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__FIFO_H__
#define	__FIFO_H__

#ifdef WIN32
#include <windows.h>
#define sem_t HANDLE
#define sem_init(sem, pshared, value) ((*(sem) = CreateSemaphore(NULL, value, LONG_MAX, NULL)) == NULL)
#define sem_wait(sem) WaitForSingleObject(*(sem), INFINITE)
#define sem_post(sem) ReleaseSemaphore(*(sem), 1, NULL)
#define sem_destroy(sem) CloseHandle(*(sem))
#else
#include <semaphore.h>
#endif

class Fifo {
public:
  Fifo(int singleBufferSize, int totalBufferSize, sem_t* readerSem);
  ~Fifo();
  int numBytesFilled() const;
  bool isEmpty() const;
  bool isFull() const;
  bool willFill(int additional) const;
  char* start() const;
  char* write(int length);
  void release();
  char* read(int *const length);

private:
  int mSingleBufferSize, mWrite, mRead, mReadCommit, mRaggedEnd, mWrapThreshold;
  sem_t	mWaitForSpaceSem;
  sem_t* mReaderSem;
  char*	mBuffer;
  bool	mEnd;

  // Intentionally unimplemented
  Fifo(const Fifo &);
  Fifo &operator=(const Fifo &);
};

#endif 	//__FIFO_H__
