/*
 * Matter side of the meter: endpoint 1 is an Electrical Sensor with the
 * Power Topology, Electrical Power Measurement and Electrical Energy
 * Measurement clusters. Call both functions on the Matter thread.
 */
#pragma once

#include "tic_meter.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

namespace ElectricalSensor {

CHIP_ERROR Init(chip::EndpointId endpoint);

void Update(const tic_reading &reading, uint64_t nowMs);

} // namespace ElectricalSensor
