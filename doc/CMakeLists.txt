# Copyright (c) 2026 The safety-toolbox contributors
# SPDX-License-Identifier: Apache-2.0
#
# Safe Data API documentation, built by the zdocs engine. Every document is
# declared once in documents.yaml; this file only sets the project facts the
# engine needs and hands the registry to it.
#
#   cmake -S safety-toolbox/doc -B build/doc
#   cmake --build build/doc            # all documents, then doc-check

cmake_minimum_required(VERSION 3.20.0)
project(safe-data-doc LANGUAGES)

# zdocs is a Zephyr module: find_package sets ZEPHYR_ZDOCS_MODULE_DIR. The
# second hint is where west.yml puts Zephyr (<topdir>/zephyr, next to this
# repository), so the build needs neither ZEPHYR_BASE nor `west zephyr-export`.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE} ${CMAKE_CURRENT_LIST_DIR}/../../zephyr COMPONENTS doc)

if(NOT DEFINED ZEPHYR_ZDOCS_MODULE_DIR)
  message(
    FATAL_ERROR
    "zdocs module not found. It is declared in west.yml at tools/zdocs — "
    "run `west update`, or point at a local checkout with:\n"
    "  -DEXTRA_ZEPHYR_MODULES=<path to zdocs>\n"
    "NOTE: EXTRA_ZEPHYR_MODULES, not EXTRA_MODULES — the latter is silently "
    "ignored by the Zephyr module system."
  )
endif()

get_filename_component(SAFE_DATA_BASE ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

execute_process(
  COMMAND west topdir
  WORKING_DIRECTORY ${SAFE_DATA_BASE}
  OUTPUT_VARIABLE WEST_TOPDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE west_result
  ERROR_QUIET
)
if(NOT west_result EQUAL 0)
  message(FATAL_ERROR "not inside a west workspace (`west topdir` failed)")
endif()

set(ZDOCS_PROJECT_BASE ${SAFE_DATA_BASE})
set(ZDOCS_WEST_TOPDIR ${WEST_TOPDIR})
set(ZDOCS_DOXYGEN_INC_ROOTS ${SAFE_DATA_BASE}/include)
set(ZDOCS_NEEDS_CONFIG ${CMAKE_CURRENT_LIST_DIR}/needs_config.toml)

# The twister run the test report is generated from. The default matches the
# README's `west twister ... -O twister-out` run from the workspace root.
set(ZDOCS_TWISTER_OUT ${WEST_TOPDIR}/twister-out CACHE PATH "twister output directory")

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_ZDOCS_MODULE_DIR}/cmake)
include(zdocs)

set(DOC_REGISTRY ${CMAKE_CURRENT_LIST_DIR}/documents.yaml)

# The test report is built from a twister run. OFF leaves it out: the build
# uses a copy of the registry without it, so no peer links to it. The CI
# workflow sets OFF; local builds keep the full set.
option(SAFE_DATA_DOC_TEST_REPORT "Build the test report (needs a twister run)" ON)
if(NOT SAFE_DATA_DOC_TEST_REPORT)
  set(_registry_without ${CMAKE_CURRENT_LIST_DIR}/scripts/registry_without.py)
  set(_filtered_registry ${CMAKE_CURRENT_BINARY_DIR}/registry/documents.yaml)
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} ${_registry_without}
            --registry ${DOC_REGISTRY} --out ${_filtered_registry} --drop test-report
    RESULT_VARIABLE _registry_result
  )
  if(NOT _registry_result EQUAL 0)
    message(FATAL_ERROR "cannot write the registry without the test report")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${DOC_REGISTRY} ${_registry_without})
  set(DOC_REGISTRY ${_filtered_registry})
  message(STATUS "Test report left out (SAFE_DATA_DOC_TEST_REPORT=OFF)")
endif()

add_docs_from_registry(REGISTRY ${DOC_REGISTRY})
add_doc_check(REGISTRY ${DOC_REGISTRY})
