# Adds version control information to the variable VERS. For
# determining the Version Control System used (if any) it inspects the
# existence of certain subdirectories under SOURCE_DIR (if provided as an
# extra argument, otherwise uses CMAKE_CURRENT_SOURCE_DIR).

function(get_source_info path revision repository)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --git-dir
      WORKING_DIRECTORY ${path}
      RESULT_VARIABLE git_result
      OUTPUT_VARIABLE git_output
      ERROR_QUIET)
    if(git_result EQUAL 0)
      string(STRIP "${git_output}" git_output)
      get_filename_component(git_dir ${git_output} ABSOLUTE BASE_DIR ${path})
      execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${path}
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_output)
      if(git_result EQUAL 0)
        string(STRIP "${git_output}" git_output)
        set(${revision} ${git_output} PARENT_SCOPE)
      endif()
      execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref --symbolic-full-name @{upstream}
        WORKING_DIRECTORY ${path}
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_output
        ERROR_QUIET)
      if(git_result EQUAL 0)
        string(REPLACE "/" ";" branch ${git_output})
        list(GET branch 0 remote)
      else()
        set(remote "origin")
      endif()
      execute_process(COMMAND ${GIT_EXECUTABLE} remote get-url ${remote}
        WORKING_DIRECTORY ${path}
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_output
        ERROR_QUIET)
      if(git_result EQUAL 0)
        string(STRIP "${git_output}" git_output)
        set(${repository} ${git_output} PARENT_SCOPE)
      else()
        set(${repository} ${path} PARENT_SCOPE)
      endif()
    endif()
  else()
    message(WARNING "Git not found. Version cannot be determined.")
  endif()
endfunction()
