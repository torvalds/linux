# Utility functions for packaging an LLVM distribution. See the
# BuildingADistribution documentation for more details.

# These functions assume a number of conventions that are common across all LLVM
# subprojects:
# - The generated CMake exports file for ${project} is called ${project}Targets
#   (except for LLVM where it's called ${project}Exports for legacy reasons).
# - The build target for the CMake exports is called ${project}-cmake-exports
#   (except LLVM where it's just cmake-exports).
# - The ${PROJECT}${distribution}_HAS_EXPORTS global property holds whether a
#   project has any exports for a particular ${distribution} (where ${PROJECT}
#   is the project name in uppercase).
# - The ${PROJECT}_CMAKE_DIR variable is computed by ${project}Config.cmake to
#   hold the path of the installed CMake modules directory.
# - The ${PROJECT}_INSTALL_PACKAGE_DIR variable contains the install destination
#   for the project's CMake modules.

include_guard(GLOBAL)

if(LLVM_DISTRIBUTION_COMPONENTS AND LLVM_DISTRIBUTIONS)
  message(FATAL_ERROR "LLVM_DISTRIBUTION_COMPONENTS and LLVM_DISTRIBUTIONS cannot be specified together")
endif()

if(LLVM_DISTRIBUTION_COMPONENTS OR LLVM_DISTRIBUTIONS)
  if(LLVM_ENABLE_IDE)
    message(FATAL_ERROR "LLVM_DISTRIBUTION_COMPONENTS cannot be specified with multi-configuration generators (i.e. Xcode or Visual Studio)")
  endif()
endif()

# Build the map of targets to distributions that's used to look up the
# distribution for a target later. The distribution for ${target} is stored in
# the global property LLVM_DISTRIBUTION_FOR_${target}.
function(llvm_distribution_build_target_map)
  foreach(target ${LLVM_DISTRIBUTION_COMPONENTS})
    # CMake doesn't easily distinguish between properties that are unset and
    # properties that are empty (you have to do a second get_property call with
    # the SET option, which is unergonomic), so just use a special marker to
    # denote the default (unnamed) distribution.
    set_property(GLOBAL PROPERTY LLVM_DISTRIBUTION_FOR_${target} "<DEFAULT>")
  endforeach()

  foreach(distribution ${LLVM_DISTRIBUTIONS})
    foreach(target ${LLVM_${distribution}_DISTRIBUTION_COMPONENTS})
      # By default, we allow a target to be in multiple distributions, and use
      # the last one to determine its export set. We disallow this in strict
      # mode, emitting a single error at the end for readability.
      if(LLVM_STRICT_DISTRIBUTIONS)
        get_property(current_distribution GLOBAL PROPERTY LLVM_DISTRIBUTION_FOR_${target})
        if(current_distribution AND NOT current_distribution STREQUAL distribution)
          set_property(GLOBAL APPEND_STRING PROPERTY LLVM_DISTRIBUTION_ERRORS
            "Target ${target} cannot be in multiple distributions \
             ${distribution} and ${current_distribution}\n"
            )
        endif()
      endif()
      set_property(GLOBAL PROPERTY LLVM_DISTRIBUTION_FOR_${target} ${distribution})
    endforeach()
  endforeach()
endfunction()

# The include guard ensures this will only be called once. The rest of this file
# only defines other functions (i.e. it doesn't execute any more code directly).
llvm_distribution_build_target_map()

# Look up the distribution a particular target belongs to.
# - target: The target to look up.
# - in_distribution_var: The variable with this name is set in the caller's
#   scope to indicate if the target is in any distribution. If no distributions
#   have been configured, this will always be set to true.
# - distribution_var: The variable with this name is set in the caller's scope
#   to indicate the distribution name for the target. If the target belongs to
#   the default (unnamed) distribution, or if no distributions have been
#   configured, it's set to the empty string.
# - UMBRELLA: The (optional) umbrella target that the target is a part of. For
#   example, all LLVM libraries have the umbrella target llvm-libraries.
function(get_llvm_distribution target in_distribution_var distribution_var)
  if(NOT LLVM_DISTRIBUTION_COMPONENTS AND NOT LLVM_DISTRIBUTIONS)
    set(${in_distribution_var} YES PARENT_SCOPE)
    set(${distribution_var} "" PARENT_SCOPE)
    return()
  endif()

  cmake_parse_arguments(ARG "" UMBRELLA "" ${ARGN})
  get_property(distribution GLOBAL PROPERTY LLVM_DISTRIBUTION_FOR_${target})
  if(ARG_UMBRELLA)
    get_property(umbrella_distribution GLOBAL PROPERTY LLVM_DISTRIBUTION_FOR_${ARG_UMBRELLA})
    if(LLVM_STRICT_DISTRIBUTIONS AND distribution AND umbrella_distribution AND
        NOT distribution STREQUAL umbrella_distribution)
      set_property(GLOBAL APPEND_STRING PROPERTY LLVM_DISTRIBUTION_ERRORS
        "Target ${target} has different distribution ${distribution} from its \
         umbrella target's (${ARG_UMBRELLA}) distribution ${umbrella_distribution}\n"
        )
    endif()
    if(NOT distribution)
      set(distribution ${umbrella_distribution})
    endif()
  endif()

  if(distribution)
    set(${in_distribution_var} YES PARENT_SCOPE)
    if(distribution STREQUAL "<DEFAULT>")
      set(distribution "")
    endif()
    set(${distribution_var} "${distribution}" PARENT_SCOPE)
  else()
    set(${in_distribution_var} NO PARENT_SCOPE)
  endif()
endfunction()

# Get the EXPORT argument to use for an install command for a target in a
# project. As explained at the top of the file, the project export set for a
# distribution is named ${project}{distribution}Targets (except for LLVM where
# it's named ${project}{distribution}Exports for legacy reasons). Also set the
# ${PROJECT}_${DISTRIBUTION}_HAS_EXPORTS global property to mark the project as
# having exports for the distribution.
# - target: The target to get the EXPORT argument for.
# - project: The project to produce the argument for. IMPORTANT: The casing of
#   this argument should match the casing used by the project's Config.cmake
#   file. The correct casing for the LLVM projects is Clang, Flang, LLD, LLVM,
#   and MLIR.
# - export_arg_var The variable with this name is set in the caller's scope to
#   the EXPORT argument for the target for the project.
# - UMBRELLA: The (optional) umbrella target that the target is a part of. For
#   example, all LLVM libraries have the umbrella target llvm-libraries.
function(get_target_export_arg target project export_arg_var)
  string(TOUPPER "${project}" project_upper)
  if(project STREQUAL "LLVM")
    set(suffix "Exports") # legacy
  else()
    set(suffix "Targets")
  endif()

  get_llvm_distribution(${target} in_distribution distribution ${ARGN})

  if(in_distribution)
    set(${export_arg_var} EXPORT ${project}${distribution}${suffix} PARENT_SCOPE)
    if(distribution)
      string(TOUPPER "${distribution}" distribution_upper)
      set_property(GLOBAL PROPERTY ${project_upper}_${distribution_upper}_HAS_EXPORTS True)
    else()
      set_property(GLOBAL PROPERTY ${project_upper}_HAS_EXPORTS True)
    endif()
  else()
    set(${export_arg_var} "" PARENT_SCOPE)
  endif()
endfunction()

# Produce a string of CMake include() commands to include the exported targets
# files for all distributions. See the comment at the top of this file for
# various assumptions made.
# - project: The project to produce the commands for. IMPORTANT: See the comment
#   for get_target_export_arg above for the correct casing of this argument.
# - includes_var: The variable with this name is set in the caller's scope to
#   the string of include commands.
function(get_config_exports_includes project includes_var)
  string(TOUPPER "${project}" project_upper)
  set(prefix "\${${project_upper}_CMAKE_DIR}/${project}")
  if(project STREQUAL "LLVM")
    set(suffix "Exports.cmake") # legacy
  else()
    set(suffix "Targets.cmake")
  endif()

  if(NOT LLVM_DISTRIBUTIONS)
    set(${includes_var} "include(\"${prefix}${suffix}\")" PARENT_SCOPE)
  else()
    set(includes)
    foreach(distribution ${LLVM_DISTRIBUTIONS})
      list(APPEND includes "include(\"${prefix}${distribution}${suffix}\" OPTIONAL)")
    endforeach()
    string(REPLACE ";" "\n" includes "${includes}")
    set(${includes_var} "${includes}" PARENT_SCOPE)
  endif()
endfunction()

# Create the install commands and targets for the distributions' CMake exports.
# The target to install ${distribution} for a project is called
# ${project}-${distribution}-cmake-exports, where ${project} is the project name
# in lowercase and ${distribution} is the distribution name in lowercase, except
# for LLVM, where the target is just called ${distribution}-cmake-exports. See
# the comment at the top of this file for various assumptions made.
# - project: The project. IMPORTANT: See the comment for get_target_export_arg
#   above for the correct casing of this argument.
function(install_distribution_exports project)
  string(TOUPPER "${project}" project_upper)
  string(TOLOWER "${project}" project_lower)
  if(project STREQUAL "LLVM")
    set(prefix "")
    set(suffix "Exports") # legacy
  else()
    set(prefix "${project_lower}-")
    set(suffix "Targets")
  endif()
  set(destination "${${project_upper}_INSTALL_PACKAGE_DIR}")

  if(NOT LLVM_DISTRIBUTIONS)
    get_property(has_exports GLOBAL PROPERTY ${project_upper}_HAS_EXPORTS)
    if(has_exports)
      install(EXPORT ${project}${suffix} DESTINATION "${destination}"
              COMPONENT ${prefix}cmake-exports)
    endif()
  else()
    foreach(distribution ${LLVM_DISTRIBUTIONS})
      string(TOUPPER "${distribution}" distribution_upper)
      get_property(has_exports GLOBAL PROPERTY ${project_upper}_${distribution_upper}_HAS_EXPORTS)
      if(has_exports)
        string(TOLOWER "${distribution}" distribution_lower)
        set(target ${prefix}${distribution_lower}-cmake-exports)
        install(EXPORT ${project}${distribution}${suffix} DESTINATION "${destination}"
                COMPONENT ${target})
        if(NOT LLVM_ENABLE_IDE)
          add_custom_target(${target})
          get_subproject_title(subproject_title)
          set_target_properties(${target} PROPERTIES FOLDER "${subproject_title}/Distribution")
          add_llvm_install_targets(install-${target} COMPONENT ${target})
        endif()
      endif()
    endforeach()
  endif()
endfunction()

# Create the targets for installing the configured distributions. The
# ${distribution} target builds the distribution, install-${distribution}
# installs it, and install-${distribution}-stripped installs a stripped version,
# where ${distribution} is the distribution name in lowercase, or "distribution"
# for the default distribution.
function(llvm_distribution_add_targets)
  # This function is called towards the end of LLVM's CMakeLists.txt, so all
  # errors will have been seen by now.
  if(LLVM_STRICT_DISTRIBUTIONS)
    get_property(errors GLOBAL PROPERTY LLVM_DISTRIBUTION_ERRORS)
    if(errors)
      string(PREPEND errors
        "Strict distribution errors (turn off LLVM_STRICT_DISTRIBUTIONS to bypass):\n"
        )
      message(FATAL_ERROR "${errors}")
    endif()
  endif()

  set(distributions "${LLVM_DISTRIBUTIONS}")
  if(NOT distributions)
    # CMake seemingly doesn't distinguish between an empty list and a list
    # containing one element which is the empty string, so just use a special
    # marker to denote the default (unnamed) distribution and fix it in the
    # loop.
    set(distributions "<DEFAULT>")
  endif()

  get_property(LLVM_DRIVER_TOOL_SYMLINKS GLOBAL PROPERTY LLVM_DRIVER_TOOL_SYMLINKS)

  foreach(distribution ${distributions})
    if(distribution STREQUAL "<DEFAULT>")
      set(distribution_target distribution)
      # Preserve legacy behavior for LLVM_DISTRIBUTION_COMPONENTS.
      set(distribution_components ${LLVM_DISTRIBUTION_COMPONENTS} ${LLVM_RUNTIME_DISTRIBUTION_COMPONENTS})
    else()
      string(TOLOWER "${distribution}" distribution_lower)
      set(distribution_target ${distribution_lower}-distribution)
      set(distribution_components ${LLVM_${distribution}_DISTRIBUTION_COMPONENTS})
    endif()

    add_custom_target(${distribution_target})
    add_custom_target(install-${distribution_target})
    add_custom_target(install-${distribution_target}-stripped)
    get_subproject_title(subproject_title)
    set_target_properties(
        ${distribution_target} 
        install-${distribution_target}
        install-${distribution_target}-stripped
      PROPERTIES 
        FOLDER "${subproject_title}/Distribution"
    )

    foreach(target ${distribution_components})
      # Note that some distribution components may not have an actual target, but only an install-FOO target.
      # This happens for example if a target is an INTERFACE target.
      if(TARGET ${target})
        add_dependencies(${distribution_target} ${target})
      endif()

      if(TARGET install-${target})
        add_dependencies(install-${distribution_target} install-${target})
      elseif(TARGET install-llvm-driver AND ${target} IN_LIST LLVM_DRIVER_TOOL_SYMLINKS)
        add_dependencies(install-${distribution_target} install-llvm-driver)
      else()
        message(SEND_ERROR "Specified distribution component '${target}' doesn't have an install target")
      endif()

      if(TARGET install-${target}-stripped)
        add_dependencies(install-${distribution_target}-stripped install-${target}-stripped)
      elseif(TARGET install-llvm-driver-stripped AND ${target} IN_LIST LLVM_DRIVER_TOOL_SYMLINKS)
        add_dependencies(install-${distribution_target}-stripped install-llvm-driver-stripped)
      else()
        message(SEND_ERROR
                "Specified distribution component '${target}' doesn't have an install-stripped target."
                " Its installation target creation should be changed to use add_llvm_install_targets,"
                " or you should manually create the 'install-${target}-stripped' target.")
      endif()
    endforeach()
  endforeach()
endfunction()
