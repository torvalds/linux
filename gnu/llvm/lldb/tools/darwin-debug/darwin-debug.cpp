//===-- darwin-debug.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Darwin launch helper
//
// This program was written to allow programs to be launched in a new
// Terminal.app window and have the application be stopped for debugging
// at the program entry point.
//
// Although it uses posix_spawn(), it uses Darwin specific posix spawn
// attribute flags to accomplish its task. It uses an "exec only" flag
// which avoids forking this process, and it uses a "stop at entry"
// flag to stop the program at the entry point.
//
// Since it uses darwin specific flags this code should not be compiled
// on other systems.
#if defined(__APPLE__)

#include <climits>
#include <crt_externs.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <mach/machine.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <string>

#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

#define streq(a, b) strcmp(a, b) == 0

static struct option g_long_options[] = {
    {"arch", required_argument, NULL, 'a'},
    {"disable-aslr", no_argument, NULL, 'd'},
    {"no-env", no_argument, NULL, 'e'},
    {"help", no_argument, NULL, 'h'},
    {"setsid", no_argument, NULL, 's'},
    {"unix-socket", required_argument, NULL, 'u'},
    {"working-dir", required_argument, NULL, 'w'},
    {"env", required_argument, NULL, 'E'},
    {NULL, 0, NULL, 0}};

static void usage() {
  puts("NAME\n"
       "    darwin-debug -- posix spawn a process that is stopped at the entry "
       "point\n"
       "                    for debugging.\n"
       "\n"
       "SYNOPSIS\n"
       "    darwin-debug --unix-socket=<SOCKET> [--arch=<ARCH>] "
       "[--working-dir=<PATH>] [--disable-aslr] [--no-env] [--setsid] [--help] "
       "-- <PROGRAM> [<PROGRAM-ARG> <PROGRAM-ARG> ....]\n"
       "\n"
       "DESCRIPTION\n"
       "    darwin-debug will exec itself into a child process <PROGRAM> that "
       "is\n"
       "    halted for debugging. It does this by using posix_spawn() along "
       "with\n"
       "    darwin specific posix_spawn flags that allows exec only (no fork), "
       "and\n"
       "    stop at the program entry point. Any program arguments "
       "<PROGRAM-ARG> are\n"
       "    passed on to the exec as the arguments for the new process. The "
       "current\n"
       "    environment will be passed to the new process unless the "
       "\"--no-env\"\n"
       "    option is used. A unix socket must be supplied using the\n"
       "    --unix-socket=<SOCKET> option so the calling program can handshake "
       "with\n"
       "    this process and get its process id.\n"
       "\n"
       "EXAMPLE\n"
       "   darwin-debug --arch=i386 -- /bin/ls -al /tmp\n");
  exit(1);
}

static void exit_with_errno(int err, const char *prefix) {
  if (err) {
    fprintf(stderr, "%s%s", prefix ? prefix : "", strerror(err));
    exit(err);
  }
}

pid_t posix_spawn_for_debug(char *const *argv, char *const *envp,
                            const char *working_dir, cpu_type_t cpu_type,
                            int disable_aslr) {
  pid_t pid = 0;

  const char *path = argv[0];

  posix_spawnattr_t attr;

  exit_with_errno(::posix_spawnattr_init(&attr),
                  "::posix_spawnattr_init (&attr) error: ");

  // Here we are using a darwin specific feature that allows us to exec only
  // since we want this program to turn into the program we want to debug,
  // and also have the new program start suspended (right at __dyld_start)
  // so we can debug it
  short flags = POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_SETEXEC |
                POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;

  // Disable ASLR if we were asked to
  if (disable_aslr)
    flags |= _POSIX_SPAWN_DISABLE_ASLR;

  sigset_t no_signals;
  sigset_t all_signals;
  sigemptyset(&no_signals);
  sigfillset(&all_signals);
  ::posix_spawnattr_setsigmask(&attr, &no_signals);
  ::posix_spawnattr_setsigdefault(&attr, &all_signals);

  // Set the flags we just made into our posix spawn attributes
  exit_with_errno(::posix_spawnattr_setflags(&attr, flags),
                  "::posix_spawnattr_setflags (&attr, flags) error: ");

  // Another darwin specific thing here where we can select the architecture
  // of the binary we want to re-exec as.
  if (cpu_type != 0) {
    size_t ocount = 0;
    exit_with_errno(
        ::posix_spawnattr_setbinpref_np(&attr, 1, &cpu_type, &ocount),
        "posix_spawnattr_setbinpref_np () error: ");
  }

  // I wish there was a posix_spawn flag to change the working directory of
  // the inferior process we will spawn, but there currently isn't. If there
  // ever is a better way to do this, we should use it. I would rather not
  // manually fork, chdir in the child process, and then posix_spawn with exec
  // as the whole reason for doing posix_spawn is to not hose anything up
  // after the fork and prior to the exec...
  if (working_dir)
    ::chdir(working_dir);

  exit_with_errno(::posix_spawnp(&pid, path, NULL, &attr, (char *const *)argv,
                                 (char *const *)envp),
                  "posix_spawn() error: ");

  // This code will only be reached if the posix_spawn exec failed...
  ::posix_spawnattr_destroy(&attr);

  return pid;
}

int main(int argc, char *const *argv, char *const *envp, const char **apple) {
#if defined(DEBUG_LLDB_LAUNCHER)
  const char *program_name = strrchr(apple[0], '/');

  if (program_name)
    program_name++; // Skip the last slash..
  else
    program_name = apple[0];

  printf("%s called with:\n", program_name);
  for (int i = 0; i < argc; ++i)
    printf("argv[%u] = '%s'\n", i, argv[i]);
#endif

  cpu_type_t cpu_type = 0;
  bool show_usage = false;
  int ch;
  int disable_aslr = 0; // By default we disable ASLR
  bool pass_env = true;
  std::string unix_socket_name;
  std::string working_dir;

#if __GLIBC__
  optind = 0;
#else
  optreset = 1;
  optind = 1;
#endif

  while ((ch = getopt_long_only(argc, argv, "a:deE:hsu:?", g_long_options,
                                NULL)) != -1) {
    switch (ch) {
    case 0:
      break;

    case 'a': // "-a i386" or "--arch=i386"
      if (optarg) {
        if (streq(optarg, "i386"))
          cpu_type = CPU_TYPE_I386;
        else if (streq(optarg, "x86_64"))
          cpu_type = CPU_TYPE_X86_64;
        else if (streq(optarg, "x86_64h"))
          cpu_type = 0; // Don't set CPU type when we have x86_64h
        else if (strstr(optarg, "arm") == optarg)
          cpu_type = CPU_TYPE_ARM;
        else {
          ::fprintf(stderr, "error: unsupported cpu type '%s'\n", optarg);
          ::exit(1);
        }
      }
      break;

    case 'd':
      disable_aslr = 1;
      break;

    case 'e':
      pass_env = false;
      break;

    case 'E': {
      // Since we will exec this program into our new program, we can just set
      // environment
      // variables in this process and they will make it into the child process.
      std::string name;
      std::string value;
      const char *equal_pos = strchr(optarg, '=');
      if (equal_pos) {
        name.assign(optarg, equal_pos - optarg);
        value.assign(equal_pos + 1);
      } else {
        name = optarg;
      }
      ::setenv(name.c_str(), value.c_str(), 1);
    } break;

    case 's':
      // Create a new session to avoid having control-C presses kill our current
      // terminal session when this program is launched from a .command file
      ::setsid();
      break;

    case 'u':
      unix_socket_name.assign(optarg);
      break;

    case 'w': {
      struct stat working_dir_stat;
      if (stat(optarg, &working_dir_stat) == 0)
        working_dir.assign(optarg);
      else
        ::fprintf(stderr, "warning: working directory doesn't exist: '%s'\n",
                  optarg);
    } break;

    case 'h':
    case '?':
    default:
      show_usage = true;
      break;
    }
  }
  argc -= optind;
  argv += optind;

  if (show_usage || argc <= 0 || unix_socket_name.empty())
    usage();

#if defined(DEBUG_LLDB_LAUNCHER)
  printf("\n%s post options:\n", program_name);
  for (int i = 0; i < argc; ++i)
    printf("argv[%u] = '%s'\n", i, argv[i]);
#endif

  // Open the socket that was passed in as an option
  struct sockaddr_un saddr_un;
  int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) {
    perror("error: socket (AF_UNIX, SOCK_STREAM, 0)");
    exit(1);
  }

  saddr_un.sun_family = AF_UNIX;
  ::strncpy(saddr_un.sun_path, unix_socket_name.c_str(),
            sizeof(saddr_un.sun_path) - 1);
  saddr_un.sun_path[sizeof(saddr_un.sun_path) - 1] = '\0';
  saddr_un.sun_len = SUN_LEN(&saddr_un);

  if (::connect(s, (struct sockaddr *)&saddr_un, SUN_LEN(&saddr_un)) < 0) {
    perror("error: connect (socket, &saddr_un, saddr_un_len)");
    exit(1);
  }

  // We were able to connect to the socket, now write our PID so whomever
  // launched us will know this process's ID
  char pid_str[64];
  const int pid_str_len =
      ::snprintf(pid_str, sizeof(pid_str), "%i", ::getpid());
  const int bytes_sent = ::send(s, pid_str, pid_str_len, 0);

  if (pid_str_len != bytes_sent) {
    perror("error: send (s, pid_str, pid_str_len, 0)");
    exit(1);
  }

  // We are done with the socket
  close(s);

  system("clear");
  printf("Launching: '%s'\n", argv[0]);
  if (working_dir.empty()) {
    char cwd[PATH_MAX];
    const char *cwd_ptr = getcwd(cwd, sizeof(cwd));
    printf("Working directory: '%s'\n", cwd_ptr);
  } else {
    printf("Working directory: '%s'\n", working_dir.c_str());
  }
  printf("%i arguments:\n", argc);

  for (int i = 0; i < argc; ++i)
    printf("argv[%u] = '%s'\n", i, argv[i]);

  // Now we posix spawn to exec this process into the inferior that we want
  // to debug.
  posix_spawn_for_debug(
      argv,
      pass_env ? *_NSGetEnviron() : NULL, // Pass current environment as we may
                                          // have modified it if "--env" options
                                          // was used, do NOT pass "envp" here
      working_dir.empty() ? NULL : working_dir.c_str(), cpu_type, disable_aslr);

  return 0;
}

#endif // #if defined (__APPLE__)
