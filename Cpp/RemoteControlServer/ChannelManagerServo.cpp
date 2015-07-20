#include "ChannelManagerServo.h"
#include "IServoProvider.h"


void ChannelManagerServo::SetState(float state, int channel) {
	IServoProvider* provider;
	int port;
	bool isValid = FindChannel(channel, provider, port);
	if (isValid) {
		provider->SetState(state, port);
	}
}

float ChannelManagerServo::GetState(int channel) {
	IServoProvider* provider;
	int port;
	bool isValid = FindChannel(channel, provider, port);
	if (isValid) {
		return provider->GetState(port);
	}
	else {
		return std::numeric_limits<float>::quiet_NaN();
	}
}