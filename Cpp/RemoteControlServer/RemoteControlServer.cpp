#include "RemoteControlServer.h"
#include <functional>
#include <iostream>

using namespace std::placeholders;

////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor

RemoteControlServer::RemoteControlServer() {
	// register handlers
	messageDecoder.SetHandler(eMessageType::CONNECTION, std::bind(&RemoteControlServer::MH_Authentication, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::ENUM_DEVICES, std::bind(&RemoteControlServer::MH_DeviceEnum, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::ENUM_CHANNELS, std::bind(&RemoteControlServer::MH_ChannelEnum, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::DEVICE_SERVO, std::bind(&RemoteControlServer::MH_Servo, this, _1, _2));

	return;
}


RemoteControlServer::~RemoteControlServer() {
	// close all connections
	// blablabla
}




bool RemoteControlServer::Listen() {
	// try accepting a connection on the socket
	try {
		RcpPacket packet;
		socket.accept();
		socket.receive(packet);
		messageDecoder.ProcessMessage(packet.getData(), packet.getDataSize());
		if (state == HALF_OPEN) {
			return false;
		}
		else {
			socket.disconnect();
			return false;
		}
	}
	catch (RcpException& e) {
		std::cout << e.what() << std::endl;
		socket.disconnect(); // whatever state it is in, just close it
		return false;
	}
}


////////////////////////////////////////////////////////////////////////////////
// Connection and authentication

bool RemoteControlServer::Authenticate() {
	return false;
}

bool RemoteControlServer::Reply(bool accept) {
	if (state != HALF_OPEN && state != AUTHENTICATED) {
		return false;
	}

	AuthenticationMessage message;
	message.action = AuthenticationMessage::CONNECTION_REPLY;
	message.isOk = accept;
	
	auto data = message.Serialize();
	try {
		socket.send(data.data(), data.size(), true);
		state = accept ? CONNECTED : DISCONNECTED;
		return accept;
	}
	catch (RcpException& e) {
		std::cout << e.what() << std::endl;
		state = DISCONNECTED;
		return false;
	}
}

void RemoteControlServer::Disconnect() {
	if (state == HALF_OPEN || state == AUTHENTICATED || state == DISCONNECTED) {
		AuthenticationMessage msg;
		RcpPacket packet;
		msg.action = AuthenticationMessage::DISCONNECT;
		auto data = msg.Serialize();

		try {
			socket.send(data.data(), data.size(), true);
			socket.receive(packet);
			messageDecoder.ProcessMessage(packet.getData(), packet.getDataSize()); // closes connection as well
		}
		catch (RcpException& e) {
			std::cout << e.what() << std::endl;
			state = DISCONNECTED;
			socket.disconnect();
		}
	}
}


////////////////////////////////////////////////////////////////////////////////
// General behaviour

void RemoteControlServer::SetPassword(const std::vector<uint8_t>& password) {
	this->password = password;
}

const std::vector<uint8_t>& RemoteControlServer::GetPassword() const {
	return password;
}

bool RemoteControlServer::SetLocalPort(uint16_t port) {
	return socket.bind(port);
}

uint16_t RemoteControlServer::GetLocalPort() const {
	return socket.getLocalPort();
}

bool RemoteControlServer::IsConnected() const {
	return state == CONNECTED;
}

auto RemoteControlServer::GetConnectionState() const -> eConnectionState {
	return state;
}

uint16_t RemoteControlServer::GetRemotePort() const {
	return socket.getRemotePort();
}

std::string RemoteControlServer::GetRemoteAddress() const {
	return socket.getRemoteAddress();
}


////////////////////////////////////////////////////////////////////////////////
// Message handlers

void RemoteControlServer::MH_Authentication(const void* message, size_t length) {
	AuthenticationMessage msg;
	bool isValid = msg.Deserlialize(message, length);

	if (!isValid) {
		return;
	}

	switch (msg.action) {
		case AuthenticationMessage::CONNECTION_REQUEST:
			if (state == DISCONNECTED) {
				state = HALF_OPEN;
			}
			break;
		case AuthenticationMessage::PASSWORD_REQUEST:
			// server should never get this
			break;
		case AuthenticationMessage::PASSWORD_REPLY:
			if (state != HALF_OPEN) {
				break;
			}
			// check is the password is correct
			if (msg.password.size() == password.size() && memcmp(msg.password.data(), msg.password.data(), password.size())) {
				state = AUTHENTICATED;
			}
			break;
		case AuthenticationMessage::CONNECTION_REPLY:
			// server should never get this
			break;
		case AuthenticationMessage::DISCONNECT:
			state = DISCONNECTED;
			socket.disconnect();
	}
}

void RemoteControlServer::MH_Servo(const void* message, size_t length) {

}

void RemoteControlServer::MH_DeviceEnum(const void* message, size_t length) {

}

void RemoteControlServer::MH_ChannelEnum(const void* message, size_t length) {

}