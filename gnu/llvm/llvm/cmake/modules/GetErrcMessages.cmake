# This function returns the messages of various POSIX error codes as they are returned by std::error_code.
# The purpose of this function is to supply those error messages to llvm-lit using the errc_messages config.
# Currently supplied and needed error codes: ENOENT, EISDIR, EINVAL and EACCES.
# Messages are semi colon separated.
# Keep amount, order and tested error codes in sync with llvm/utils/lit/lit/llvm/config.py.
function(get_errc_messages outvar)
    if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR AND NOT DEFINED errc_exit_code)
        set(${outvar} "" PARENT_SCOPE)
        message(STATUS "Can't get errc messages in cross-compilation mode")
        return()
    endif()

    set(errc_test_code ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/getErrc.cpp)

    file(WRITE ${errc_test_code} "
        #include <cerrno>
        #include <iostream>
        #include <string>
        #include <system_error>

        std::string getMessageFor(int err) {
            return std::make_error_code(static_cast<std::errc>(err)).message();
        }

        int main() {
            std::cout << getMessageFor(ENOENT) << ';' << getMessageFor(EISDIR);
            std::cout << ';' << getMessageFor(EINVAL) << ';' << getMessageFor(EACCES);
            return 0;
        }
    ")

    try_run(errc_exit_code
            errc_compiled
            ${CMAKE_BINARY_DIR}
            ${errc_test_code}
            RUN_OUTPUT_VARIABLE errc_result
            COMPILE_OUTPUT_VARIABLE errc_compile_errors)
    if (errc_compiled AND "${errc_exit_code}" STREQUAL "0")
        set(${outvar} ${errc_result} PARENT_SCOPE)
    else()
        set(${outvar} "" PARENT_SCOPE)
        message(NOTICE "${errc_compile_errors}")
        message(STATUS "Failed to get errc messages")
    endif ()
endfunction()
