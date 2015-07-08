#pragma once

#include "IServoProvider.h"
#include <vector>

class ServoProviderDummy : public IServoProvider {
public:
	ServoProviderDummy(int numChannels);

	int GetNumPorts() override;
	void SetState(float state, int port = 0) override;
	float GetState(int port = 0) override;
private:


};