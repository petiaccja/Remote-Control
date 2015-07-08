#pragma once

#include "ChannelManagerBase.h"
#include "IServoProvider.h"

////////////////////////////////////////////////////////////////////////////////
/// ChannelManagerServo is the channel manager of RC servo controller hardware
/// devices.
/// To register a servo signal Provider, the Provider must implement the 
/// IServoProvider interface.
/// A legit Provider should generate a ~4V PWM servo signal on some physical pins,
/// having a width of 1ms (minimum steering) to 2ms (maximum steering) and a
/// frequency of ~50Hz.
////////////////////////////////////////////////////////////////////////////////

class ChannelManagerServo : public ChannelManagerBase<IServoProvider> {
public:
	/// Set state of a servo output channel.
	/// \param state Steering, may range from -1 for minimum to +1 for maxmimum.
	/// \param channel The channel to modify.
	void SetState(float state, int channel);
	/// Get state of a servo output channel.
	/// \param channel Which channel to query.
	/// \return Current steering of the servo. NaN if channel does not exist.
	float GetState(int channel);
};