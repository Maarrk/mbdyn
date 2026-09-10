#include "mbconfig.h"

#include <ostream>

#include "units.h"
#include "mbpar.h"
#include "dataman.h"

namespace {

const std::unordered_map<MBUnits::Dimensions, std::string> dimensionNames {
        {MBUnits::Dimensions::Dimensionless, "Dimensionless"},
        {MBUnits::Dimensions::Boolean, "Boolean"},
        {MBUnits::Dimensions::Length, "Length"},
        {MBUnits::Dimensions::Mass, "Mass"}, {MBUnits::Dimensions::Time, "Time"},
        {MBUnits::Dimensions::Current, "Current"},
        {MBUnits::Dimensions::Temperature, "Temperature"},
        {MBUnits::Dimensions::Angle, "Angle"},
        {MBUnits::Dimensions::Area, "Area"}, {MBUnits::Dimensions::Force, "Force"},
        {MBUnits::Dimensions::Velocity, "Velocity"},
        {MBUnits::Dimensions::Acceleration, "Acceleration"},
        {MBUnits::Dimensions::AngularVelocity, "Angular velocity"},
        {MBUnits::Dimensions::AngularAcceleration, "Angular acceleration"},
        {MBUnits::Dimensions::Momentum, "Momentum"},
        {MBUnits::Dimensions::MomentaMoment, "Momenta moment"},
        {MBUnits::Dimensions::MomentumDerivative, "Momentum derivative"},
        {MBUnits::Dimensions::MomentaMomentDerivative, "Momenta moment derivative"},
        {MBUnits::Dimensions::LinearStrain, "Linear strain"},
        {MBUnits::Dimensions::AngularStrain, "Angular strain"},
        {MBUnits::Dimensions::LinearStrainRate, "Linear strain rate"},
        {MBUnits::Dimensions::AngularStrainRate, "Angular strain rate"},
        {MBUnits::Dimensions::StaticMoment, "Static moment"},
        {MBUnits::Dimensions::MomentOfInertia, "Moment of inertia"},
        {MBUnits::Dimensions::ForceUnitSpan, "Force per unit span"},
        {MBUnits::Dimensions::Work, "Work"},
        {MBUnits::Dimensions::Power, "Power"},
        {MBUnits::Dimensions::Pressure, "Pressure"}, {MBUnits::Dimensions::Moment, "Moment"},
        {MBUnits::Dimensions::Voltage, "Voltage"},
        {MBUnits::Dimensions::Charge, "Charge"},
        {MBUnits::Dimensions::Resistance, "Resistance"},
        {MBUnits::Dimensions::Capacitance, "Capacitance"},
        {MBUnits::Dimensions::Inductance, "Inductance"},
        {MBUnits::Dimensions::Frequency, "Frequency"},
        {MBUnits::Dimensions::deg, "deg"},
        {MBUnits::Dimensions::rad, "rad"}, {MBUnits::Dimensions::MassFlow, "Mass flow"},
        {MBUnits::Dimensions::Jerk, "Jerk"},
        {MBUnits::Dimensions::VoltageDerivative, "Voltage derivative"},
        {MBUnits::Dimensions::TemperatureDerivative, "Temperature derivative"},
        {MBUnits::Dimensions::UnknownDimension, "Unknown dimension"}
};
}

MBUnits::MBUnits()
{
        SetUnspecifiedUnits();
}

const std::string& MBUnits::GetUnits(Dimensions dimension) const
{
        const auto i = units.find(dimension);
        if (i != units.end()) {
                return i->second;
        }
        static const std::string unknown("Unknown dimension");
        return unknown;
}

void MBUnits::ReadOutputUnits(std::ostream& log, MBDynParser& parser)
{
        if (parser.IsKeyWord("MKS")) {
                SetMKSUnits();
                log << "Unit for the whole model: MKS" << std::endl;
        } else if (parser.IsKeyWord("CGS")) {
                SetCGSUnits();
                log << "Unit for the whole model: CGS" << std::endl;
        } else if (parser.IsKeyWord("MMTMS")) {
                SetMMTMSUnits();
                log << "Unit for the whole model: MMTMS" << std::endl;
        } else if (parser.IsKeyWord("MMKGMS")) {
                SetMMKGMSUnits();
                log << "Unit for the whole model: MMKGMS" << std::endl;
        } else if (parser.IsKeyWord("Custom")) {
                log << "Unit for the whole model: Custom" << std::endl;
                const MBUnits::Dimensions base[] = {
                        MBUnits::Dimensions::Length,
                        MBUnits::Dimensions::Mass,
                        MBUnits::Dimensions::Time,
                        MBUnits::Dimensions::Current,
                        MBUnits::Dimensions::Temperature
                };
                for (MBUnits::Dimensions dimension : base) {
                        if (parser.IsKeyWord(dimensionNames.find(dimension)->second.c_str())) {
                                units[dimension] = parser.GetStringWithDelims();
                        } else {
                                silent_cerr("Error while reading Custom unit system at line"
                                        << parser.GetLineData() << "\nExpecting the definition of "
                                        << dimensionNames.find(dimension)->second << " units."
                                        << std::endl);
                                throw DataManager::ErrGeneric(MBDYN_EXCEPT_ARGS);
                        }
                }
                SetDerivedUnits();
        } else {
                silent_cerr("Error while reading the model Units at line"
                        << parser.GetLineData() << std::endl);
                throw DataManager::ErrGeneric(MBDYN_EXCEPT_ARGS);
        }
        for (const auto& dimension : dimensionNames) {
                log << "Unit for " << dimension.second << ": " << GetUnits(dimension.first) << std::endl;
        }
}

void MBUnits::SetUnspecifiedUnits()
{
        for (const auto& dimension : dimensionNames) {
                units[dimension.first] = dimension.second;
        }
}

void MBUnits::SetDerivedUnits()
{
        units[MBUnits::Dimensions::Angle] = "rad";
        units[MBUnits::Dimensions::Area] = units[MBUnits::Dimensions::Length] + "^2";
        units[MBUnits::Dimensions::Force] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Length] + " " + units[MBUnits::Dimensions::Time] + "^-2";
        units[MBUnits::Dimensions::Velocity] = units[MBUnits::Dimensions::Length] + " " + units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::Acceleration] = units[MBUnits::Dimensions::Length] + " " + units[MBUnits::Dimensions::Time] + "^-2";
        units[MBUnits::Dimensions::AngularVelocity] = units[MBUnits::Dimensions::Angle] + " " + units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::AngularAcceleration] = units[MBUnits::Dimensions::Angle] + " " + units[MBUnits::Dimensions::Time] + "^-2";
        units[MBUnits::Dimensions::Momentum] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Velocity];
        units[MBUnits::Dimensions::MomentaMoment] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Length] + "^2 " + units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::MomentumDerivative] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Acceleration];
        units[MBUnits::Dimensions::MomentaMomentDerivative] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Length] + "^2 " + units[MBUnits::Dimensions::Time] + "^-2";
        units[MBUnits::Dimensions::LinearStrain] = units[MBUnits::Dimensions::Dimensionless];
        units[MBUnits::Dimensions::AngularStrain] = units[MBUnits::Dimensions::Angle] + " " + units[MBUnits::Dimensions::Length] + "^-1";
        units[MBUnits::Dimensions::LinearStrainRate] = units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::AngularStrainRate] = units[MBUnits::Dimensions::Angle] + " " + units[MBUnits::Dimensions::Length] + "^-1 " + units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::StaticMoment] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Length];
        units[MBUnits::Dimensions::MomentOfInertia] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Length] + "^2";
        units[MBUnits::Dimensions::ForceUnitSpan] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-2";
        units[MBUnits::Dimensions::Work] = units[MBUnits::Dimensions::Force] + " " + units[MBUnits::Dimensions::Length];
        units[MBUnits::Dimensions::Power] = units[MBUnits::Dimensions::Force] + " " + units[MBUnits::Dimensions::Velocity];
        units[MBUnits::Dimensions::Pressure] = units[MBUnits::Dimensions::Force] + " " + units[MBUnits::Dimensions::Length] + "^-2";
        units[MBUnits::Dimensions::Moment] = units[MBUnits::Dimensions::Force] + " " + units[MBUnits::Dimensions::Length];
        units[MBUnits::Dimensions::Voltage] = units[MBUnits::Dimensions::Length] + "^2 " + units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-3 " + units[MBUnits::Dimensions::Current] + "^-1";
        units[MBUnits::Dimensions::Resistance] = units[MBUnits::Dimensions::Length] + "^2 " + units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-3 " + units[MBUnits::Dimensions::Current] + "^-2";
        units[MBUnits::Dimensions::Capacitance] = units[MBUnits::Dimensions::Length] + "^-2 " + units[MBUnits::Dimensions::Mass] + "^-1 " + units[MBUnits::Dimensions::Time] + "^4 " + units[MBUnits::Dimensions::Current] + "^2";
        units[MBUnits::Dimensions::Inductance] = units[MBUnits::Dimensions::Length] + "^2 " + units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-2 " + units[MBUnits::Dimensions::Current] + "^-2";
        units[MBUnits::Dimensions::Frequency] = units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::Charge] = units[MBUnits::Dimensions::Time] + " " + units[MBUnits::Dimensions::Current];
        units[MBUnits::Dimensions::deg] = "deg";
        units[MBUnits::Dimensions::rad] = "rad";
        units[MBUnits::Dimensions::MassFlow] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-1";
        units[MBUnits::Dimensions::Jerk] = units[MBUnits::Dimensions::Mass] + " " + units[MBUnits::Dimensions::Time] + "^-3";
        units[MBUnits::Dimensions::VoltageDerivative] = units[MBUnits::Dimensions::Voltage] + " " + units[MBUnits::Dimensions::Time] + "^-1";
        // Preserve the existing output convention: this dimension remains
        // unspecified unless a future unit system explicitly defines it.
        units[MBUnits::Dimensions::UnknownDimension] = "UnknownDimension";
}

void MBUnits::SetMKSUnits()
{
        units[MBUnits::Dimensions::Length] = "m";
        units[MBUnits::Dimensions::Mass]="kg";
        units[MBUnits::Dimensions::Time]="s";
        units[MBUnits::Dimensions::Current]="A";
        units[MBUnits::Dimensions::Temperature]="K";
        SetDerivedUnits();
        units[MBUnits::Dimensions::Force]="N";
        units[MBUnits::Dimensions::Moment]="N m";
        units[MBUnits::Dimensions::Work]="J";
        units[MBUnits::Dimensions::Power]="W";
        units[MBUnits::Dimensions::Pressure]="Pa";
        units[MBUnits::Dimensions::Voltage]="V";
        units[MBUnits::Dimensions::Charge]="C";
        units[MBUnits::Dimensions::Frequency]="Hz";
        }
void MBUnits::SetCGSUnits()
{
        units[MBUnits::Dimensions::Length] = "cm";
        units[MBUnits::Dimensions::Mass]="kg";
        units[MBUnits::Dimensions::Time]="s";
        units[MBUnits::Dimensions::Current]="A";
        units[MBUnits::Dimensions::Temperature]="K";
        SetDerivedUnits();
        units[MBUnits::Dimensions::Force]="dyn";
        units[MBUnits::Dimensions::Pressure]="dyn cm^-2";
        units[MBUnits::Dimensions::Moment]="dyn cm";
        units[MBUnits::Dimensions::Work]="erg";
        units[MBUnits::Dimensions::Power]="erg s^-1";
        units[MBUnits::Dimensions::Frequency]="Hz";
        units[MBUnits::Dimensions::Charge]="C";
        }
void MBUnits::SetMMTMSUnits()
{
        units[MBUnits::Dimensions::Length] = "mm";
        units[MBUnits::Dimensions::Mass]="ton";
        units[MBUnits::Dimensions::Time]="ms";
        units[MBUnits::Dimensions::Current]="A";
        units[MBUnits::Dimensions::Temperature]="K";
        SetDerivedUnits();
        units[MBUnits::Dimensions::Force]="N";
        units[MBUnits::Dimensions::Moment]="N mm";
        units[MBUnits::Dimensions::Work]="N mm";
        units[MBUnits::Dimensions::Power]="N mm s^-1";
        units[MBUnits::Dimensions::Pressure]="MPa";
        units[MBUnits::Dimensions::Frequency]="kHz";
        units[MBUnits::Dimensions::Charge]="mC";
        }
void MBUnits::SetMMKGMSUnits()
{
        units[MBUnits::Dimensions::Length] = "mm";
        units[MBUnits::Dimensions::Mass]="kg";
        units[MBUnits::Dimensions::Time]="ms";
        units[MBUnits::Dimensions::Current]="A";
        units[MBUnits::Dimensions::Temperature]="K";
        SetDerivedUnits();
        units[MBUnits::Dimensions::Force]="kN";
        units[MBUnits::Dimensions::Moment]="N m";
        units[MBUnits::Dimensions::Work]="kN mm";
        units[MBUnits::Dimensions::Power]="N m ms^-1";
        units[MBUnits::Dimensions::Pressure]="GPa";
        units[MBUnits::Dimensions::Frequency]="kHz";
        units[MBUnits::Dimensions::Charge]="mC";
        }
