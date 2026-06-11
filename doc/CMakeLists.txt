# SPDX-License-Identifier: Apache-2.0
#
# Safe Data API documentation build (modelled on zephyr-safety/doc).
#
#   cmake -B <builddir> [-DSPHINXBUILD=...] <this dir>
#   cmake --build <builddir> --target docs            # everything
#   cmake --build <builddir> --target test-report-html
#   cmake --build <builddir> --target serve           # http://localhost:8000
#
# Outputs land in <builddir>/deploy/<document>/html. Every target has a
# <name>-nodeps twin that re-runs only that document's build.

cmake_minimum_required(VERSION 3.20.0)
project(safe-data-api-doc LANGUAGES)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}" DOC_BASE)
message(STATUS "Safe Data doc base: ${DOC_BASE}")

#-------------------------------------------------------------------------------
# Options

set(SPHINXOPTS "-q -j auto" CACHE STRING "Default Sphinx Options")
set(SPHINXOPTS_EXTRA "" CACHE STRING "Extra Sphinx Options (added to defaults)")
set(TWISTER_OUT "/workspace/build-safe_api/twister-out" CACHE PATH
    "Twister output directory consumed by the test report")
set(SAFE_DATA_DOC_BASE_URL "http://localhost:8000" CACHE STRING
    "Base URL under which deploy/ is served (cross-document need links)")

separate_arguments(SPHINXOPTS)
separate_arguments(SPHINXOPTS_EXTRA)

#-------------------------------------------------------------------------------
# Dependencies

find_package(Doxygen REQUIRED)

find_program(SPHINXBUILD sphinx-build)
if(NOT SPHINXBUILD)
  message(FATAL_ERROR "The 'sphinx-build' command was not found")
endif()
find_program(SPHINXAUTOBUILD sphinx-autobuild)

#-------------------------------------------------------------------------------
# Environment & Paths

set(DEPLOY_DIR ${CMAKE_CURRENT_BINARY_DIR}/deploy)

# Passed into every Sphinx run; the conf.py files read these to locate the
# sibling documents' needs.json exports, the twister results and the served
# base URL.
set(SPHINX_ENV
  SAFE_DATA_DOC_DEPLOY=${DEPLOY_DIR}
  SAFE_DATA_DOC_BASE_URL=${SAFE_DATA_DOC_BASE_URL}
  TWISTER_OUT=${TWISTER_OUT}
)

#-------------------------------------------------------------------------------
# Functions

# Create a custom doc target plus a '<name>-nodeps' twin (same command, no
# inter-document dependencies) for quick single-document rebuilds.
function(add_doc_target name)
  add_custom_target(${name} ${ARGN})
  add_custom_target(${name}-nodeps ${ARGN})
  message(STATUS "Created doc target: ${name} and ${name}-nodeps")
endfunction()

#
# Create Sphinx build targets for a documentation set living in
# ${DOC_BASE}/<doc_name>:
#
#   add_sphinx_target(<doc_name> BUILDERS <b1> [<b2> ...])
#
# Produces '<doc_name>-<builder>' targets; output in deploy/<doc_name>/<builder>.
#
function(add_sphinx_target doc_name)
  cmake_parse_arguments(ARGS "" "" "BUILDERS" ${ARGN})

  if(NOT ARGS_BUILDERS)
    message(FATAL_ERROR "add_sphinx_target for '${doc_name}' requires BUILDERS.")
  endif()

  set(DOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${doc_name})
  set(DOCS_CFG_DIR ${DOC_BASE}/${doc_name})
  set(DOCS_DOCTREE_DIR ${DOC_OUT_DIR}/doctrees)
  set(DOCS_BUILD_DIR ${DOC_OUT_DIR}/build)

  foreach(builder ${ARGS_BUILDERS})
    set(target_name "${doc_name}-${builder}")
    set(builder_out_dir "${DEPLOY_DIR}/${doc_name}/${builder}")
    set(sphinx_executable ${SPHINXBUILD})
    set(builder_arg ${builder})
    set(extra_sphinx_args "")

    if(builder STREQUAL "html-live")
      if(NOT SPHINXAUTOBUILD)
        message(WARNING "sphinx-autobuild not found, skipping '${target_name}'.")
        continue()
      endif()
      set(sphinx_executable ${SPHINXAUTOBUILD})
      set(builder_arg "html")
      set(builder_out_dir "${DEPLOY_DIR}/${doc_name}/html")
      set(extra_sphinx_args --watch ${DOCS_CFG_DIR} --ignore ${DOCS_BUILD_DIR})
    endif()

    file(MAKE_DIRECTORY ${DOCS_BUILD_DIR})

    add_doc_target(
      ${target_name}
      COMMAND ${CMAKE_COMMAND} -E env ${SPHINX_ENV} OUTPUT_DIR=${builder_out_dir}
      ${sphinx_executable}
      -b ${builder_arg}
      -c ${DOCS_CFG_DIR}
      -d ${DOCS_DOCTREE_DIR}
      -w ${DOCS_BUILD_DIR}/${builder}.log
      ${SPHINXOPTS}
      ${SPHINXOPTS_EXTRA}
      ${extra_sphinx_args}
      ${DOCS_CFG_DIR}
      ${builder_out_dir}
      USES_TERMINAL
      COMMENT "Running Sphinx ${builder} build for ${doc_name}..."
    )

    set_target_properties(
      ${target_name} ${target_name}-nodeps
      PROPERTIES
      ADDITIONAL_CLEAN_FILES "${builder_out_dir};${DOCS_DOCTREE_DIR};${DOCS_BUILD_DIR}"
    )
  endforeach()
endfunction()

#
# Create a Doxygen target 'doxygen-<name>' from ${DOC_BASE}/<name>.doxyfile.in;
# output in deploy/<deploy_name> (html/ and, if configured, xml/).
#
function(add_doxygen_target name deploy_name)
  set(DOXY_OUT ${DEPLOY_DIR}/${deploy_name})
  file(MAKE_DIRECTORY ${DOXY_OUT})

  set(DOXYFILE_IN ${DOC_BASE}/${name}.doxyfile.in)
  set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.doxyfile)

  configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

  add_doc_target(
    doxygen-${name}
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
    COMMENT "Running Doxygen for ${name}..."
  )

  set_target_properties(
    doxygen-${name} doxygen-${name}-nodeps
    PROPERTIES
    ADDITIONAL_CLEAN_FILES "${DOXY_OUT}"
  )
endfunction()

#-------------------------------------------------------------------------------
# Documents

add_sphinx_target(
  requirement-specification
  BUILDERS html
)

add_sphinx_target(
  test-specification
  BUILDERS html html-live
)

add_sphinx_target(
  test-report
  BUILDERS html html-live
)

# API documentation: doxygen HTML rendering of the in-source comments.
add_doxygen_target(safe-data-api api-documentation)
add_custom_target(api-documentation-html DEPENDS doxygen-safe-data-api)

# Test-specification doxygen pass: XML over the annotated ztest sources,
# consumed by the testmodule directive of the Sphinx test specification.
add_doxygen_target(safe-data-testspec doxygen-safe-data-testspec)

# Cross-document imports dictate the build order: requirements needs.json ->
# test specification (which also needs the testspec doxygen XML) ->
# test report.
add_dependencies(test-specification-html requirement-specification-html
                 doxygen-safe-data-testspec)
add_dependencies(test-report-html test-specification-html)

add_custom_target(
  docs
  DEPENDS
    requirement-specification-html
    test-specification-html
    test-report-html
    api-documentation-html
)

add_custom_target(
  serve
  COMMAND ${CMAKE_COMMAND} -E echo "Serving ${DEPLOY_DIR} on ${SAFE_DATA_DOC_BASE_URL}"
  COMMAND python3 -m http.server 8000
  WORKING_DIRECTORY ${DEPLOY_DIR}
  USES_TERMINAL
)
