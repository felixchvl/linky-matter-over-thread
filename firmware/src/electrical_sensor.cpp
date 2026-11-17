#include "electrical_sensor.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/electrical-energy-measurement-server/electrical-energy-measurement-server.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <app/clusters/power-topology-server/power-topology-server.h>
#include <app/reporting/reporting.h>
#include <lib/support/CodeUtils.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensor, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

namespace EPM = ElectricalPowerMeasurement;
namespace EEM = ElectricalEnergyMeasurement;

using AccuracyRange = EPM::Structs::MeasurementAccuracyRangeStruct::Type;
using Accuracy = EPM::Structs::MeasurementAccuracyStruct::Type;

/*
 * Accuracy as the Linky reports it: voltage and current to the volt and the
 * ampere, apparent power to the volt-ampere (PAPP is rounded to 10 VA), and
 * active power estimated from 1 Wh steps over at least a minute.
 */
const AccuracyRange kVoltageRange[] = { { .rangeMin = 0, .rangeMax = 500'000, .fixedMax = MakeOptional<uint64_t>(1'000) } };
const AccuracyRange kCurrentRange[] = { { .rangeMin = 0, .rangeMax = 100'000, .fixedMax = MakeOptional<uint64_t>(1'000) } };
const AccuracyRange kActivePowerRange[] = { { .rangeMin = 0, .rangeMax = 36'000'000, .fixedMax = MakeOptional<uint64_t>(60'000) } };
const AccuracyRange kApparentPowerRange[] = { { .rangeMin = 0, .rangeMax = 36'000'000, .fixedMax = MakeOptional<uint64_t>(10'000) } };
const AccuracyRange kEnergyRange[] = { { .rangeMin = 0, .rangeMax = 999'999'999'000, .fixedMax = MakeOptional<uint64_t>(1'000) } };

const Accuracy kPowerAccuracies[] = {
	{ .measurementType = EPM::MeasurementTypeEnum::kVoltage, .measured = true, .minMeasuredValue = 0,
	  .maxMeasuredValue = 500'000, .accuracyRanges = DataModel::List<const AccuracyRange>(kVoltageRange) },
	{ .measurementType = EPM::MeasurementTypeEnum::kActiveCurrent, .measured = true, .minMeasuredValue = 0,
	  .maxMeasuredValue = 100'000, .accuracyRanges = DataModel::List<const AccuracyRange>(kCurrentRange) },
	{ .measurementType = EPM::MeasurementTypeEnum::kActivePower, .measured = true, .minMeasuredValue = 0,
	  .maxMeasuredValue = 36'000'000, .accuracyRanges = DataModel::List<const AccuracyRange>(kActivePowerRange) },
	{ .measurementType = EPM::MeasurementTypeEnum::kApparentPower, .measured = true, .minMeasuredValue = 0,
	  .maxMeasuredValue = 36'000'000, .accuracyRanges = DataModel::List<const AccuracyRange>(kApparentPowerRange) },
};

const Accuracy kEnergyAccuracy = { .measurementType = EPM::MeasurementTypeEnum::kElectricalEnergy,
				   .measured = true,
				   .minMeasuredValue = 0,
				   .maxMeasuredValue = 999'999'999'000,
				   .accuracyRanges = DataModel::List<const AccuracyRange>(kEnergyRange) };

class PowerDelegate : public EPM::Delegate {
public:
	EPM::PowerModeEnum GetPowerMode() override { return EPM::PowerModeEnum::kAc; }
	uint8_t GetNumberOfMeasurementTypes() override { return MATTER_ARRAY_SIZE(kPowerAccuracies); }

	CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetAccuracyByIndex(uint8_t index, Accuracy &accuracy) override
	{
		VerifyOrReturnError(index < MATTER_ARRAY_SIZE(kPowerAccuracies), CHIP_ERROR_PROVIDER_LIST_EXHAUSTED);
		accuracy = kPowerAccuracies[index];
		return CHIP_NO_ERROR;
	}
	CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetRangeByIndex(uint8_t, EPM::Structs::MeasurementRangeStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

	DataModel::Nullable<int64_t> GetVoltage() override { return mVoltage; }
	DataModel::Nullable<int64_t> GetActiveCurrent() override { return mCurrent; }
	DataModel::Nullable<int64_t> GetActivePower() override { return mActivePower; }
	DataModel::Nullable<int64_t> GetApparentPower() override { return mApparentPower; }
	DataModel::Nullable<int64_t> GetReactiveCurrent() override { return {}; }
	DataModel::Nullable<int64_t> GetApparentCurrent() override { return {}; }
	DataModel::Nullable<int64_t> GetReactivePower() override { return {}; }
	DataModel::Nullable<int64_t> GetRMSVoltage() override { return {}; }
	DataModel::Nullable<int64_t> GetRMSCurrent() override { return {}; }
	DataModel::Nullable<int64_t> GetRMSPower() override { return {}; }
	DataModel::Nullable<int64_t> GetFrequency() override { return {}; }
	DataModel::Nullable<int64_t> GetPowerFactor() override { return {}; }
	DataModel::Nullable<int64_t> GetNeutralCurrent() override { return {}; }

	void SetVoltage(DataModel::Nullable<int64_t> v) { Set(mVoltage, v, EPM::Attributes::Voltage::Id); }
	void SetCurrent(DataModel::Nullable<int64_t> v) { Set(mCurrent, v, EPM::Attributes::ActiveCurrent::Id); }
	void SetActivePower(DataModel::Nullable<int64_t> v) { Set(mActivePower, v, EPM::Attributes::ActivePower::Id); }
	void SetApparentPower(DataModel::Nullable<int64_t> v) { Set(mApparentPower, v, EPM::Attributes::ApparentPower::Id); }

private:
	void Set(DataModel::Nullable<int64_t> &field, DataModel::Nullable<int64_t> value, AttributeId id)
	{
		if (field != value) {
			field = value;
			MatterReportingAttributeChangeCallback(mEndpointId, EPM::Id, id);
		}
	}

	DataModel::Nullable<int64_t> mVoltage;
	DataModel::Nullable<int64_t> mCurrent;
	DataModel::Nullable<int64_t> mActivePower;
	DataModel::Nullable<int64_t> mApparentPower;
};

class TopologyDelegate : public PowerTopology::Delegate {
public:
	CHIP_ERROR GetAvailableEndpointAtIndex(size_t, EndpointId &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
	CHIP_ERROR GetActiveEndpointAtIndex(size_t, EndpointId &) override { return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED; }
};

EndpointId sEndpoint = kInvalidEndpointId;
PowerDelegate sPowerDelegate;
TopologyDelegate sTopologyDelegate;
EPM::Instance *sPower;
PowerTopology::Instance *sTopology;
EEM::ElectricalEnergyMeasurementAttrAccess *sEnergy;

tic_power_estimator sEstimator;
uint32_t sLastEnergyWh;
uint64_t sLastEnergyMs;
bool sEnergySent;

DataModel::Nullable<int64_t> Milli(bool present, uint32_t value)
{
	return present ? DataModel::MakeNullable(static_cast<int64_t>(value) * 1000) : DataModel::Nullable<int64_t>();
}

void SendEnergy(uint32_t totalWh, uint64_t nowMs)
{
	EEM::Structs::EnergyMeasurementStruct::Type imported;

	imported.energy = static_cast<int64_t>(totalWh) * 1000; /* mWh */
	imported.endSystime.SetValue(nowMs);
	EEM::NotifyCumulativeEnergyMeasured(sEndpoint, MakeOptional(imported), NullOptional);
	MatterReportingAttributeChangeCallback(sEndpoint, EEM::Id, EEM::Attributes::CumulativeEnergyImported::Id);
	sLastEnergyWh = totalWh;
	sLastEnergyMs = nowMs;
	sEnergySent = true;
}

} // namespace

namespace ElectricalSensor {

CHIP_ERROR Init(EndpointId endpoint)
{
	sEndpoint = endpoint;

	static PowerTopology::Instance topology(endpoint, sTopologyDelegate,
						BitMask<PowerTopology::Feature>(PowerTopology::Feature::kNodeTopology),
						BitMask<PowerTopology::OptionalAttributes>(0));
	sTopology = &topology;
	ReturnErrorOnFailure(sTopology->Init());

	static EPM::Instance power(endpoint, sPowerDelegate,
				   BitMask<EPM::Feature>(EPM::Feature::kAlternatingCurrent),
				   BitMask<EPM::OptionalAttributes>(EPM::OptionalAttributes::kOptionalAttributeVoltage,
								    EPM::OptionalAttributes::kOptionalAttributeActiveCurrent,
								    EPM::OptionalAttributes::kOptionalAttributeApparentPower));
	sPower = &power;
	ReturnErrorOnFailure(sPower->Init());

	static EEM::ElectricalEnergyMeasurementAttrAccess energy(
		BitMask<EEM::Feature>(EEM::Feature::kImportedEnergy, EEM::Feature::kCumulativeEnergy),
		BitMask<EEM::OptionalAttributes>(0));
	sEnergy = &energy;
	ReturnErrorOnFailure(sEnergy->Init());
	ReturnErrorOnFailure(EEM::SetMeasurementAccuracy(endpoint, kEnergyAccuracy));

	tic_power_init(&sEstimator, CONFIG_LINKY_POWER_WINDOW_S * 1000u);
	return CHIP_NO_ERROR;
}

void Update(const tic_reading &r, uint64_t nowMs)
{
	VerifyOrReturn(sEndpoint != kInvalidEndpointId);

	sPowerDelegate.SetVoltage(Milli(r.fields & TIC_HAS_VOLTAGE, r.voltage_v));
	sPowerDelegate.SetCurrent(Milli(r.fields & TIC_HAS_CURRENT, r.current_a));
	sPowerDelegate.SetApparentPower(Milli(r.fields & TIC_HAS_APPARENT_POWER, r.apparent_power_va));

	if (!(r.fields & TIC_HAS_TOTAL)) {
		return;
	}
	if (tic_power_update(&sEstimator, r.total_wh, nowMs)) {
		sPowerDelegate.SetActivePower(Milli(sEstimator.has_power, sEstimator.power_w));
	}

	/* The energy counter only moves in 1 Wh steps; send it when it moved, at most once a minute. */
	bool due = !sEnergySent ||
		   (r.total_wh != sLastEnergyWh && nowMs - sLastEnergyMs >= CONFIG_LINKY_ENERGY_REPORT_INTERVAL_S * 1000ull);
	if (due) {
		SendEnergy(r.total_wh, nowMs);
	}
}

} // namespace ElectricalSensor
