# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(linky-matter-over-thread)

# Matter needs the GNU flavour of C++17, and the sample helpers of the nRF
# Connect SDK (Matter init, board LED and button, data model glue).
include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)

ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

target_include_directories(app PRIVATE
  src
  src/tic
  ${ZAP_PARENT_DIR}
)

target_sources(app PRIVATE
  src/main.cpp
  src/electrical_sensor.cpp
  src/tic_reader.c
  src/vin_monitor.c
  src/tic/tic_parser.c
  src/tic/tic_meter.c
)

# Builds the code generated from src/default_zap/linky.zap.
ncs_configure_data_model()
