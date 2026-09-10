/* Measurement-unit definitions shared by MBDyn output producers. */
#ifndef MBDYN_UNITS_H
#define MBDYN_UNITS_H

#include <iosfwd>
#include <string>
#include <unordered_map>

class MBDynParser;

class MBUnits {
public:
        enum struct Dimensions {
                Dimensionless,
                Boolean,
                Length,
                Mass,
                Time,
                Current,
                Temperature,
                Angle,
                Area,
                Force,
                Velocity,
                Acceleration,
                AngularVelocity,
                AngularAcceleration,
                Momentum,
                MomentaMoment,
                MomentumDerivative,
                MomentaMomentDerivative,
                StaticMoment,
                MomentOfInertia,
                LinearStrain,
                AngularStrain,
                LinearStrainRate,
                AngularStrainRate,
                ForceUnitSpan,
                Work,
                Power,
                Pressure,
                Moment,
                Voltage,
                Charge,
                Resistance,
                Capacitance,
                Inductance,
                Frequency,
                deg,
                rad,
                MassFlow,
                Jerk,
                VoltageDerivative,
                TemperatureDerivative,
                UnknownDimension
        };

        MBUnits();

        void ReadOutputUnits(std::ostream& log, MBDynParser& parser);
        const std::string& GetUnits(Dimensions dimension) const;

private:
        std::unordered_map<Dimensions, std::string> units;

        void SetDerivedUnits();
        void SetUnspecifiedUnits();
        void SetMKSUnits();
        void SetCGSUnits();
        void SetMMTMSUnits();
        void SetMMKGMSUnits();
};

#endif /* MBDYN_UNITS_H */
