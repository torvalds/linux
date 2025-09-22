//===-- sanitizer_libc_test.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Tests for sanitizer_libc.h.
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <vector>
#include <stdio.h>

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_platform.h"
#include "gtest/gtest.h"

#if SANITIZER_WINDOWS
#define NOMINMAX
#include <windows.h>
#undef NOMINMAX
#endif
#if SANITIZER_POSIX
# include <sys/stat.h>
# include "sanitizer_common/sanitizer_posix.h"
#endif

using namespace __sanitizer;

// A regression test for internal_memmove() implementation.
TEST(SanitizerCommon, InternalMemmoveRegression) {
  char src[] = "Hello World";
  char *dest = src + 6;
  __sanitizer::internal_memmove(dest, src, 5);
  EXPECT_EQ(dest[0], src[0]);
  EXPECT_EQ(dest[4], src[4]);
}

TEST(SanitizerCommon, mem_is_zero) {
  size_t size = 128;
  char *x = new char[size];
  memset(x, 0, size);
  for (size_t pos = 0; pos < size; pos++) {
    x[pos] = 1;
    for (size_t beg = 0; beg < size; beg++) {
      for (size_t end = beg; end < size; end++) {
        // fprintf(stderr, "pos %zd beg %zd end %zd \n", pos, beg, end);
        if (beg <= pos && pos < end)
          EXPECT_FALSE(__sanitizer::mem_is_zero(x + beg, end - beg));
        else
          EXPECT_TRUE(__sanitizer::mem_is_zero(x + beg, end - beg));
      }
    }
    x[pos] = 0;
  }
  delete [] x;
}

struct stat_and_more {
  struct stat st;
  unsigned char z;
};

static void get_temp_dir(char *buf, size_t bufsize) {
#if SANITIZER_WINDOWS
  buf[0] = '\0';
  if (!::GetTempPathA(bufsize, buf))
    return;
#else
  const char *tmpdir = "/tmp";
#  if SANITIZER_ANDROID
  tmpdir = GetEnv("TMPDIR");
#  endif
  internal_snprintf(buf, bufsize, "%s", tmpdir);
#endif
}

static void temp_file_name(char *buf, size_t bufsize, const char *prefix) {
#if SANITIZER_WINDOWS
  buf[0] = '\0';
  char tmp_dir[MAX_PATH];
  if (!::GetTempPathA(MAX_PATH, tmp_dir))
    return;
  // GetTempFileNameA needs a MAX_PATH buffer.
  char tmp_path[MAX_PATH];
  if (!::GetTempFileNameA(tmp_dir, prefix, 0, tmp_path))
    return;
  internal_strncpy(buf, tmp_path, bufsize);
#else
  const char *tmpdir = "/tmp";
#if SANITIZER_ANDROID
  tmpdir = GetEnv("TMPDIR");
#endif
  internal_snprintf(buf, bufsize, "%s/%sXXXXXX", tmpdir, prefix);
  ASSERT_TRUE(mkstemp(buf));
#endif
}

static void Unlink(const char *path) {
#if SANITIZER_WINDOWS
  // No sanitizer needs to delete a file on Windows yet. If we ever do, we can
  // add a portable wrapper and test it from here.
  ::DeleteFileA(&path[0]);
#else
  internal_unlink(path);
#endif
}

TEST(SanitizerCommon, FileOps) {
  const char *str1 = "qwerty";
  uptr len1 = internal_strlen(str1);
  const char *str2 = "zxcv";
  uptr len2 = internal_strlen(str2);

  char tmpfile[128];
  temp_file_name(tmpfile, sizeof(tmpfile), "sanitizer_common.fileops.tmp.");
  fd_t fd = OpenFile(tmpfile, WrOnly);
  ASSERT_NE(fd, kInvalidFd);
  ASSERT_TRUE(WriteToFile(fd, "A", 1));
  CloseFile(fd);

  fd = OpenFile(tmpfile, WrOnly);
  ASSERT_NE(fd, kInvalidFd);
#if SANITIZER_POSIX && !SANITIZER_APPLE
  EXPECT_EQ(internal_lseek(fd, 0, SEEK_END), 0u);
#endif
  uptr bytes_written = 0;
  EXPECT_TRUE(WriteToFile(fd, str1, len1, &bytes_written));
  EXPECT_EQ(len1, bytes_written);
  EXPECT_TRUE(WriteToFile(fd, str2, len2, &bytes_written));
  EXPECT_EQ(len2, bytes_written);
  CloseFile(fd);

  EXPECT_TRUE(FileExists(tmpfile));

  fd = OpenFile(tmpfile, RdOnly);
  ASSERT_NE(fd, kInvalidFd);

#if SANITIZER_POSIX
  // The stat wrappers are posix-only.
  uptr fsize = internal_filesize(fd);
  EXPECT_EQ(len1 + len2, fsize);

  struct stat st1, st2, st3;
  EXPECT_EQ(0u, internal_stat(tmpfile, &st1));
  EXPECT_EQ(0u, internal_lstat(tmpfile, &st2));
  EXPECT_EQ(0u, internal_fstat(fd, &st3));
  EXPECT_EQ(fsize, (uptr)st3.st_size);

  // Verify that internal_fstat does not write beyond the end of the supplied
  // buffer.
  struct stat_and_more sam;
  memset(&sam, 0xAB, sizeof(sam));
  EXPECT_EQ(0u, internal_fstat(fd, &sam.st));
  EXPECT_EQ(0xAB, sam.z);
  EXPECT_NE(0xAB, sam.st.st_size);
  EXPECT_NE(0, sam.st.st_size);
#endif

  char buf[64] = {};
  uptr bytes_read = 0;
  EXPECT_TRUE(ReadFromFile(fd, buf, len1, &bytes_read));
  EXPECT_EQ(len1, bytes_read);
  EXPECT_EQ(0, internal_memcmp(buf, str1, len1));
  EXPECT_EQ((char)0, buf[len1 + 1]);
  internal_memset(buf, 0, len1);
  EXPECT_TRUE(ReadFromFile(fd, buf, len2, &bytes_read));
  EXPECT_EQ(len2, bytes_read);
  EXPECT_EQ(0, internal_memcmp(buf, str2, len2));
  CloseFile(fd);

  Unlink(tmpfile);
}

class SanitizerCommonFileTest : public ::testing::TestWithParam<uptr> {
  void SetUp() override {
    data_.resize(GetParam());
    std::generate(data_.begin(), data_.end(), [] { return rand() % 256; });

    temp_file_name(file_name_, sizeof(file_name_),
                   "sanitizer_common.ReadFile.tmp.");

    if (FILE *f = fopen(file_name_, "wb")) {
      if (!data_.empty())
        fwrite(data_.data(), data_.size(), 1, f);
      fclose(f);
    }
  }

  void TearDown() override { Unlink(file_name_); }

 protected:
  char file_name_[256];
  std::vector<char> data_;
};

TEST_P(SanitizerCommonFileTest, ReadFileToBuffer) {
  char *buff;
  uptr size;
  uptr len;
  EXPECT_TRUE(ReadFileToBuffer(file_name_, &buff, &len, &size));
  EXPECT_EQ(data_, std::vector<char>(buff, buff + size));
  UnmapOrDie(buff, len);
}

TEST_P(SanitizerCommonFileTest, ReadFileToBufferHalf) {
  char *buff;
  uptr size;
  uptr len;
  data_.resize(data_.size() / 2);
  EXPECT_TRUE(ReadFileToBuffer(file_name_, &buff, &len, &size, data_.size()));
  EXPECT_EQ(data_, std::vector<char>(buff, buff + size));
  UnmapOrDie(buff, len);
}

TEST_P(SanitizerCommonFileTest, ReadFileToVector) {
  InternalMmapVector<char> buff;
  EXPECT_TRUE(ReadFileToVector(file_name_, &buff));
  EXPECT_EQ(data_, std::vector<char>(buff.begin(), buff.end()));
}

TEST_P(SanitizerCommonFileTest, ReadFileToVectorHalf) {
  InternalMmapVector<char> buff;
  data_.resize(data_.size() / 2);
  EXPECT_TRUE(ReadFileToVector(file_name_, &buff, data_.size()));
  EXPECT_EQ(data_, std::vector<char>(buff.begin(), buff.end()));
}

INSTANTIATE_TEST_SUITE_P(FileSizes, SanitizerCommonFileTest,
                         ::testing::Values(0, 1, 7, 13, 32, 4096, 4097, 1048575,
                                           1048576, 1048577));

static const size_t kStrlcpyBufSize = 8;
void test_internal_strlcpy(char *dbuf, const char *sbuf) {
  uptr retval = 0;
  retval = internal_strlcpy(dbuf, sbuf, kStrlcpyBufSize);
  EXPECT_EQ(internal_strncmp(dbuf, sbuf, kStrlcpyBufSize - 1), 0);
  EXPECT_EQ(internal_strlen(dbuf),
            std::min(internal_strlen(sbuf), (uptr)(kStrlcpyBufSize - 1)));
  EXPECT_EQ(retval, internal_strlen(sbuf));

  // Test with shorter maxlen.
  uptr maxlen = 2;
  if (internal_strlen(sbuf) > maxlen) {
    retval = internal_strlcpy(dbuf, sbuf, maxlen);
    EXPECT_EQ(internal_strncmp(dbuf, sbuf, maxlen - 1), 0);
    EXPECT_EQ(internal_strlen(dbuf), maxlen - 1);
  }
}

TEST(SanitizerCommon, InternalStrFunctions) {
  const char *haystack = "haystack";
  EXPECT_EQ(haystack + 2, internal_strchr(haystack, 'y'));
  EXPECT_EQ(haystack + 2, internal_strchrnul(haystack, 'y'));
  EXPECT_EQ(0, internal_strchr(haystack, 'z'));
  EXPECT_EQ(haystack + 8, internal_strchrnul(haystack, 'z'));

  char dbuf[kStrlcpyBufSize] = {};
  const char *samesizestr = "1234567";
  const char *shortstr = "123";
  const char *longerstr = "123456789";

  // Test internal_strlcpy.
  internal_strlcpy(dbuf, shortstr, 0);
  EXPECT_EQ(dbuf[0], 0);
  EXPECT_EQ(dbuf[0], 0);
  test_internal_strlcpy(dbuf, samesizestr);
  test_internal_strlcpy(dbuf, shortstr);
  test_internal_strlcpy(dbuf, longerstr);

  // Test internal_strlcat.
  char dcatbuf[kStrlcpyBufSize] = {};
  uptr retval = 0;
  retval = internal_strlcat(dcatbuf, "aaa", 0);
  EXPECT_EQ(internal_strlen(dcatbuf), (uptr)0);
  EXPECT_EQ(retval, (uptr)3);

  retval = internal_strlcat(dcatbuf, "123", kStrlcpyBufSize);
  EXPECT_EQ(internal_strcmp(dcatbuf, "123"), 0);
  EXPECT_EQ(internal_strlen(dcatbuf), (uptr)3);
  EXPECT_EQ(retval, (uptr)3);

  retval = internal_strlcat(dcatbuf, "123", kStrlcpyBufSize);
  EXPECT_EQ(internal_strcmp(dcatbuf, "123123"), 0);
  EXPECT_EQ(internal_strlen(dcatbuf), (uptr)6);
  EXPECT_EQ(retval, (uptr)6);

  retval = internal_strlcat(dcatbuf, "123", kStrlcpyBufSize);
  EXPECT_EQ(internal_strcmp(dcatbuf, "1231231"), 0);
  EXPECT_EQ(internal_strlen(dcatbuf), (uptr)7);
  EXPECT_EQ(retval, (uptr)9);
}

TEST(SanitizerCommon, InternalWideStringFunctions) {
  const wchar_t *emptystr = L"";
  const wchar_t *samesizestr = L"1234567";
  const wchar_t *shortstr = L"123";
  const wchar_t *longerstr = L"123456789";

  ASSERT_EQ(internal_wcslen(emptystr), 0ul);
  ASSERT_EQ(internal_wcslen(samesizestr), 7ul);
  ASSERT_EQ(internal_wcslen(shortstr), 3ul);
  ASSERT_EQ(internal_wcslen(longerstr), 9ul);

  ASSERT_EQ(internal_wcsnlen(emptystr, 7), 0ul);
  ASSERT_EQ(internal_wcsnlen(samesizestr, 7), 7ul);
  ASSERT_EQ(internal_wcsnlen(shortstr, 7), 3ul);
  ASSERT_EQ(internal_wcsnlen(longerstr, 7), 7ul);
}

// FIXME: File manipulations are not yet supported on Windows
#if SANITIZER_POSIX && !SANITIZER_APPLE
TEST(SanitizerCommon, InternalMmapWithOffset) {
  char tmpfile[128];
  temp_file_name(tmpfile, sizeof(tmpfile),
                 "sanitizer_common.internalmmapwithoffset.tmp.");
  fd_t fd = OpenFile(tmpfile, RdWr);
  ASSERT_NE(fd, kInvalidFd);

  uptr page_size = GetPageSizeCached();
  uptr res = internal_ftruncate(fd, page_size * 2);
  ASSERT_FALSE(internal_iserror(res));

  res = internal_lseek(fd, page_size, SEEK_SET);
  ASSERT_FALSE(internal_iserror(res));

  res = internal_write(fd, "AB", 2);
  ASSERT_FALSE(internal_iserror(res));

  char *p = (char *)MapWritableFileToMemory(nullptr, page_size, fd, page_size);
  ASSERT_NE(nullptr, p);

  ASSERT_EQ('A', p[0]);
  ASSERT_EQ('B', p[1]);

  CloseFile(fd);
  UnmapOrDie(p, page_size);
  internal_unlink(tmpfile);
}
#endif

TEST(SanitizerCommon, ReportFile) {
  SpinMutex report_file_mu;
  ReportFile report_file = {&report_file_mu, kStderrFd, "", "", 0};
  char tmpfile[128];
  temp_file_name(tmpfile, sizeof(tmpfile),
                 "dir/sanitizer_common.reportfile.tmp.");
  report_file.SetReportPath(tmpfile);
  const char *path = report_file.GetReportPath();
  EXPECT_EQ(internal_strncmp(tmpfile, path, strlen(tmpfile)), 0);
  // This will close tmpfile.
  report_file.SetReportPath("stderr");
  Unlink(tmpfile);
}

TEST(SanitizerCommon, FileExists) {
  char tmpfile[128];
  temp_file_name(tmpfile, sizeof(tmpfile), "sanitizer_common.fileexists.tmp.");
  fd_t fd = OpenFile(tmpfile, WrOnly);
  ASSERT_NE(fd, kInvalidFd);
  EXPECT_TRUE(FileExists(tmpfile));
  CloseFile(fd);
  Unlink(tmpfile);
}

TEST(SanitizerCommon, DirExists) {
  char tmpdir[128];
  get_temp_dir(tmpdir, sizeof(tmpdir));
  EXPECT_TRUE(DirExists(tmpdir));
}
