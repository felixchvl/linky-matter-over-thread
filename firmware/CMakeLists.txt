#
# Based on the nRF Connect SDK Matter template sample.
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(linky-matter-over-thread)

include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)

# NORDIC SDK APP START
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
  src/app_task.cpp
  src/electrical_sensor.cpp
  src/tic_reader.c
  src/vin_monitor.c
  src/tic/tic_parser.c
  src/tic/tic_meter.c
)

ncs_configure_data_model()
# NORDIC SDK APP END
