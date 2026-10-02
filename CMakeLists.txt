# Copyright (c) 2026 The safety-toolbox contributors
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SAFE_DATA)
  zephyr_library()
  zephyr_library_sources(src/safe_data.c)
  zephyr_include_directories(include)
endif()
