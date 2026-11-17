/*
 * Based on the nRF Connect SDK Matter template sample.
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "electrical_sensor.h"
#include "tic_reader.h"
#include "vin_monitor.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"

#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace {

constexpr EndpointId kMeterEndpoint = 1;

/* Latest frame, handed from the TIC thread to the Matter thread. */
struct k_spinlock sLock;
tic_reading sLatest;
bool sUpdatePending;

void UpdateOnMatterThread(intptr_t)
{
	tic_reading reading;
	k_spinlock_key_t key = k_spin_lock(&sLock);

	reading = sLatest;
	sUpdatePending = false;
	k_spin_unlock(&sLock, key);

	ElectricalSensor::Update(reading, static_cast<uint64_t>(k_uptime_get()));
}

/* Runs in the TIC thread for every valid frame. */
void OnTicReading(const tic_reading *reading)
{
	k_spinlock_key_t key = k_spin_lock(&sLock);
	bool schedule = !sUpdatePending;

	sLatest = *reading;
	sUpdatePending = true;
	k_spin_unlock(&sLock, key);

	/* One pending update is enough: it always picks up the newest frame. */
	if (schedule) {
		PlatformMgr().ScheduleWork(UpdateOnMatterThread);
	}
}

} // namespace

CHIP_ERROR AppTask::Init()
{
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer(Nrf::Matter::InitData{
		.mPostServerInitClbk = [] { return ElectricalSensor::Init(kMeterEndpoint); } }));

	if (!Nrf::GetBoard().Init()) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));
	ReturnErrorOnFailure(Nrf::Matter::StartServer());

	if (int err = vin_monitor_start(); err) {
		LOG_WRN("Input voltage monitor not started: %d", err);
	}
	if (int err = tic_reader_start(OnTicReading); err) {
		LOG_ERR("TIC reader not started: %d", err);
		return CHIP_ERROR_INCORRECT_STATE;
	}
	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
