#pragma once

#include <cstdint>
#include <vector>

class RemoteControlServer {
public:
	bool Listen();
	bool Authenticate();
	void Disconnect();

	void SetPassword(std::vector<uint8_t> password);
	std::vector<uint8_t> GetPassword();

	bool SetPort(uint16_t port);
	uint16_t GetPort();
};