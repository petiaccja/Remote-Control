#pragma once

#include "IProviderBase.h"

////////////////////////////////////////////////////////////////////////////////
/// IServoProvider is an interface for servo signal generators.
/// A servo signal is normally a PWM signal with 1ms (minimum steering) to
/// 2ms (maximum steering) width and a frquency of about 50Hz. An implementation
/// should emit such a signal on a real hardware pin.
////////////////////////////////////////////////////////////////////////////////

class IServoProvider : public IProviderBase {
public:
	/// Set servo steering.
	/// \param state Servo state, -1 for minimum steering, +1 for maximum steering.
	/// \param port The index of the output port. Ranges from 0 to GetNumPorts().
	virtual void SetState(float state, int port = 0) = 0;
	/// Get servo state.
	/// \param port The index of the queried port. Ranges from 0 to GetNumPorts().
	/// \return Current steering of the servo.
	virtual float GetState(int port = 0) const = 0;
};