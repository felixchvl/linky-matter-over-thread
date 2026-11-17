/*
 * Matter side of the meter. Endpoint 1 is an Electrical Sensor with the
 * Power Topology, Electrical Power Measurement and Electrical Energy
 * Measurement clusters, for the whole meter. Endpoints 2 to 7 carry the
 * energy of supplier indexes 1 to 6, shown once the meter reports them.
 * Call both functions on the Matter thread.
 */
#pragma once

#include "tic_meter.h"

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

namespace ElectricalSensor {

CHIP_ERROR Init(chip::EndpointId endpoint);

void Update(const tic_reading &reading, uint64_t nowMs);

} // namespace ElectricalSensor
