include(CheckIncludeFiles)

set(LLVM_LINK_COMPONENTS support)

set(DIRECTORY_WATCHER_SOURCES DirectoryScanner.cpp)
set(DIRECTORY_WATCHER_LINK_LIBS "")

if(APPLE)
  check_include_files("CoreServices/CoreServices.h" HAVE_CORESERVICES)
  if(HAVE_CORESERVICES)
    list(APPEND DIRECTORY_WATCHER_SOURCES mac/DirectoryWatcher-mac.cpp)
    set(DIRECTORY_WATCHER_LINK_LIBS "-framework CoreServices")
  endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
  check_include_files("sys/inotify.h" HAVE_INOTIFY)
  if(HAVE_INOTIFY)
    list(APPEND DIRECTORY_WATCHER_SOURCES linux/DirectoryWatcher-linux.cpp)
    find_package(Threads REQUIRED)
  endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  list(APPEND DIRECTORY_WATCHER_SOURCES windows/DirectoryWatcher-windows.cpp)
else()
  list(APPEND DIRECTORY_WATCHER_SOURCES default/DirectoryWatcher-not-implemented.cpp)
endif()

add_clang_library(clangDirectoryWatcher
  ${DIRECTORY_WATCHER_SOURCES}
  )

target_link_libraries(clangDirectoryWatcher PUBLIC ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(clangDirectoryWatcher PRIVATE ${DIRECTORY_WATCHER_LINK_LIBS})
