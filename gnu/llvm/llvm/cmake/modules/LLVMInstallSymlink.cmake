# We need to execute this script at installation time because the
# DESTDIR environment variable may be unset at configuration time.
# See PR8397.

# Set to an arbitrary directory to silence GNUInstallDirs warnings
# regarding being unable to determine libdir.
set(CMAKE_INSTALL_LIBDIR "lib")
include(GNUInstallDirs)

function(install_symlink name target outdir link_or_copy)
  # link_or_copy is the "command" to pass to cmake -E.
  # It should be either "create_symlink" or "copy".

  set(DESTDIR $ENV{DESTDIR})
  if(NOT IS_ABSOLUTE "${outdir}")
    set(outdir "${CMAKE_INSTALL_PREFIX}/${outdir}")
  endif()
  set(outdir "${DESTDIR}${outdir}")

  message(STATUS "Creating ${name}")

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E ${link_or_copy} "${target}" "${name}"
    WORKING_DIRECTORY "${outdir}")

endfunction()
