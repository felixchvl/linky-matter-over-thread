/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Starts Matter with the nRF Connect SDK helpers, then the TIC reader, and
 * hands every valid frame over to the Matter thread.
 */

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

#include <cstdlib>

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

namespace {

constexpr chip::EndpointId kMeterEndpoint = 1;

/*
 * The TIC thread keeps only the newest frame here and schedules at most one
 * update on the Matter thread at a time, which then takes that newest frame.
 */
k_spinlock sFrameLock;
tic_reading sNewestFrame;
bool sUpdateScheduled;

void ApplyNewestFrame(intptr_t)
{
	k_spinlock_key_t key = k_spin_lock(&sFrameLock);
	tic_reading frame = sNewestFrame;

	sUpdateScheduled = false;
	k_spin_unlock(&sFrameLock, key);

	ElectricalSensor::Update(frame, static_cast<uint64_t>(k_uptime_get()));
}

void OnFrame(const tic_reading *frame)
{
	k_spinlock_key_t key = k_spin_lock(&sFrameLock);
	bool schedule = !sUpdateScheduled;

	sNewestFrame = *frame;
	sUpdateScheduled = true;
	k_spin_unlock(&sFrameLock, key);

	if (schedule) {
		chip::DeviceLayer::PlatformMgr().ScheduleWork(ApplyNewestFrame);
	}
}

CHIP_ERROR StartMatter()
{
	/* The clusters of the meter are set up on the Matter thread, right after the server. */
	Nrf::Matter::InitData init{};

	init.mPostServerInitClbk = [] { return ElectricalSensor::Init(kMeterEndpoint); };
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer(init));

	/* Status LED, and the button: short press to advertise, long press to factory reset. */
	VerifyOrReturnError(Nrf::GetBoard().Init(), CHIP_ERROR_INCORRECT_STATE);
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

	return Nrf::Matter::StartServer();
}

} // namespace

int main()
{
	CHIP_ERROR err = StartMatter();

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Matter did not start: %" CHIP_ERROR_FORMAT, err.Format());
		return EXIT_FAILURE;
	}

	if (int ret = vin_monitor_start(); ret) {
		LOG_WRN("Input voltage monitor not started: %d", ret);
	}
	if (int ret = tic_reader_start(OnFrame); ret) {
		LOG_ERR("TIC reader not started: %d", ret);
		return EXIT_FAILURE;
	}

	for (;;) {
		Nrf::DispatchNextTask();
	}
}
