#pragma once

#include "IServoProvider.h"
#include <vector>
#include <ostream>

class ServoProviderDummy : public IServoProvider {
public:
	ServoProviderDummy(int numChannels);

	int GetNumPorts() const override;
	void SetState(float state, int port = 0) override;
	float GetState(int port = 0) const override;
	void SetLogStream(std::ostream& logStream);
private:
	std::ostream* logStream;
	std::vector<float> servoStates;
};