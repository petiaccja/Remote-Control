#pragma once

#include <boost/asio.hpp>
#include <string>
#include <cstdint>

#include "Packet.h"


class RCPSocket {
public:
	RCPSocket();
	
	void Connect(std::string address, uint32_t port);
	void Disconnect();

	void Send(const void* data, size_t size, bool reliable);
	Packet Receive();
private:
	boost::asio::ip::udp::socket socket;
	boost::asio::io_service ioService;
};