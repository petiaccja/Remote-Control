#include "ServoProviderDummy.h"
#include <cassert>
#include <algorithm>
#include <iostream>


ServoProviderDummy::ServoProviderDummy(int numChannels) : servoStates(numChannels), logStream(&std::cout) {
	for (auto& v : servoStates) {
		v = 0.0f;
	}
}

int ServoProviderDummy::GetNumPorts() const {
	return (int)servoStates.size();
}

void ServoProviderDummy::SetState(float state, int port) {
	assert(port < GetNumPorts());
	state = std::min(1.0f, std::max(-1.0f, state));
	if (logStream) {
		*logStream << "port " << port << " = " << state << std::endl;
	}
	servoStates[port] = state;
}

float ServoProviderDummy::GetState(int port) const {
	assert(port < GetNumPorts());
	return servoStates[port];
}

void ServoProviderDummy::SetLogStream(std::ostream& logStream) {
	this->logStream = &logStream;
}

