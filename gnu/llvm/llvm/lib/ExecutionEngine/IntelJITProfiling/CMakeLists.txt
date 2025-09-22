include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/.. )

if(NOT DEFINED ITTAPI_GIT_REPOSITORY)
    set(ITTAPI_GIT_REPOSITORY https://github.com/intel/ittapi.git)
endif()

if(NOT DEFINED ITTAPI_GIT_TAG)
    set(ITTAPI_GIT_TAG v3.18.12)
endif()

if(NOT DEFINED ITTAPI_SOURCE_DIR)
    set(ITTAPI_SOURCE_DIR ${PROJECT_BINARY_DIR})
endif()

if(NOT EXISTS ${ITTAPI_SOURCE_DIR}/ittapi)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone ${ITTAPI_GIT_REPOSITORY}
                    WORKING_DIRECTORY ${ITTAPI_SOURCE_DIR}
                    RESULT_VARIABLE GIT_CLONE_RESULT)
    if(NOT GIT_CLONE_RESULT EQUAL "0")
        message(FATAL_ERROR "git clone ${ITTAPI_GIT_REPOSITORY} failed with ${GIT_CLONE_RESULT}, please clone ${ITTAPI_GIT_REPOSITORY}")
    endif()
endif()

execute_process(COMMAND ${GIT_EXECUTABLE} checkout ${ITTAPI_GIT_TAG}
                WORKING_DIRECTORY ${ITTAPI_SOURCE_DIR}/ittapi
                RESULT_VARIABLE GIT_CHECKOUT_RESULT)
if(NOT GIT_CHECKOUT_RESULT EQUAL "0")
    message(FATAL_ERROR "git checkout ${ITTAPI_GIT_TAG} failed with ${GIT_CHECKOUT_RESULT}, please checkout ${ITTAPI_GIT_TAG} at ${ITTAPI_SOURCE_DIR}/ittapi")
endif()

include_directories( ${ITTAPI_SOURCE_DIR}/ittapi/include/ )

if( HAVE_LIBDL )
    set(LLVM_INTEL_JIT_LIBS ${CMAKE_DL_LIBS})
endif()

set(LLVM_INTEL_JIT_LIBS ${LLVM_PTHREAD_LIB} ${LLVM_INTEL_JIT_LIBS})


add_llvm_component_library(LLVMIntelJITProfiling
  jitprofiling.c
  ${ITTAPI_SOURCE_DIR}/ittapi/src/ittnotify/ittnotify_static.c

  LINK_LIBS ${LLVM_INTEL_JIT_LIBS}

)

