#include "RemoteControlServer.h"
#include <functional>
#include <iostream>
#include <chrono>

using namespace std::placeholders;
using namespace std::chrono;

////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor

RemoteControlServer::RemoteControlServer() {
	// set initial state
	state = DISCONNECTED;


	// register handlers
	messageDecoder.SetHandler(eMessageType::CONNECTION, std::bind(&RemoteControlServer::MH_Authentication, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::ENUM_DEVICES, std::bind(&RemoteControlServer::MH_DeviceEnum, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::ENUM_CHANNELS, std::bind(&RemoteControlServer::MH_ChannelEnum, this, _1, _2));
	messageDecoder.SetHandler(eMessageType::DEVICE_SERVO, std::bind(&RemoteControlServer::MH_Servo, this, _1, _2));

	return;
}


RemoteControlServer::~RemoteControlServer() {
	Disconnect();
}



////////////////////////////////////////////////////////////////////////////////
// Connection and authentication

bool RemoteControlServer::Listen(int timeout) {
	// try accepting a connection on the socket
	try {
		RcpPacket packet;
		socket.accept();
		if (!socket.receive(packet)) {
			socket.disconnect();
			return false;
		}
		ConnectionMessage msg;
		bool isGood = msg.Deserlialize(packet.getData(), packet.getDataSize());
		if (isGood && msg.action == ConnectionMessage::CONNECTION_REQUEST) {
			state = HALF_OPEN;
			return true;
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

bool RemoteControlServer::Authenticate(int timeout) {
	if (state != HALF_OPEN) {
		return false;
	}
	try {
		// send a password request to the client
		ConnectionMessage msg;
		RcpPacket packet;
		msg.action = ConnectionMessage::PASSWORD_REQUEST;
		auto data = msg.Serialize();
		socket.send(data.data(), data.size(), true);

		// wait for client's response:
		// it must be a PASSWORD_REPLY with the correct password
		socket.receive(packet);
		if (!msg.Deserlialize(packet.getData(), packet.getDataSize())) {
			return false;
		}

		// compare passwords
		bool isCorrect =
			msg.password.size() == password.size() &&
			memcmp(msg.password.data(), password.data(), password.size()) == 0;
		if (isCorrect) {
			state = AUTHENTICATED;
		}
		return isCorrect;
	}
	catch (RcpException e) {
		if (!socket.isConnected()) {
			state = DISCONNECTED;
		}
		return false;
	}

}

bool RemoteControlServer::Reply(bool accept, int timeout) {
	if (state != HALF_OPEN && state != AUTHENTICATED) {
		return false;
	}

	ConnectionMessage message{ ConnectionMessage::CONNECTION_REPLY, accept };
	
	auto data = message.Serialize();
	try {
		socket.send(data.data(), data.size(), true);

		if (accept == true) {
			state = CONNECTED;
			StartMessageThread();
		}
		else {
			state = DISCONNECTED;
			socket.disconnect();
		}
		return accept;
	}
	catch (RcpException& e) {
		std::cout << e.what() << std::endl;
		state = DISCONNECTED;
		socket.disconnect();
		return false;
	}
}

void RemoteControlServer::Disconnect() {
	if (state == HALF_OPEN || state == AUTHENTICATED || state == CONNECTED) {
		ConnectionMessage msg;
		RcpPacket packet;
		msg.action = ConnectionMessage::DISCONNECT;
		auto data = msg.Serialize();

		high_resolution_clock::time_point start, end; // DEBUG
		high_resolution_clock::time_point start2, end2; // DEBUG

		try {
			// shut down message thread
			StopMessageThread();

			// send a disconnect indication
			socket.send(data.data(), data.size(), true);

			// receive a disconnect response
			int timeout = 5000;
			auto startTime = steady_clock::now();
			int timeLeft;
			do {
				timeLeft = duration_cast<milliseconds>(steady_clock::now() - startTime).count() + timeout;
				std::cout << timeLeft << std::endl;
				start = high_resolution_clock::now();
				if (!socket.receive(packet, timeLeft)) {
					//std::cout << "did not get disconnect response";
					continue;
				}
				end = high_resolution_clock::now();
				eMessageType type = *(eMessageType*)packet.getData();
				if (type == eMessageType::CONNECTION) {
					bool p = msg.Deserlialize(packet.getData(), packet.getDataSize());
					if (p && msg.action == ConnectionMessage::DISCONNECT) {
						//std::cout << "got proper disconnect response" << std::endl;
						break;
					}
					else {
						continue;
					}
				}
				else {
					messageDecoder.ProcessMessage(packet.getData(), packet.getDataSize());
				}
			} while (timeLeft > 0);
		}
		catch (RcpException& e) {
			std::cout << e.what() << std::endl;
		}

		state = DISCONNECTED;
		start2 = high_resolution_clock::now();
		socket.disconnect();
		end2 = high_resolution_clock::now();

		std::cout << (double)duration_cast<microseconds>(end - start).count() * 0.001 << " ms" << std::endl; // DEBUG
		std::cout << (double)duration_cast<microseconds>(end2 - start2).count() * 0.001 << " ms" << std::endl; // DEBUG

	}
}


////////////////////////////////////////////////////////////////////////////////
// General behaviour

// Dumbshit one-line functions, consider them finished

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
	ConnectionMessage msg;
	bool isValid = msg.Deserlialize(message, length);

	if (!isValid) {
		return;
	}

	switch (msg.action) {
		case ConnectionMessage::CONNECTION_REQUEST:
			// handled explicitly
			break;
		case ConnectionMessage::PASSWORD_REQUEST:
			// server should never get this
			break;
		case ConnectionMessage::PASSWORD_REPLY:
			// handled explicitly
			break;
		case ConnectionMessage::CONNECTION_REPLY:
			// server should never get this
			break;
		case ConnectionMessage::DISCONNECT:
			// send a disconnect response to client
			try {
				socket.send(message, length, true);
			}
			catch (...) {}
			// close connection
			state = DISCONNECTED;
			socket.disconnect();
			// stop message thread
			runMessageThread = false;
	}
}

void RemoteControlServer::MH_Servo(const void* message, size_t length) {

}

void RemoteControlServer::MH_DeviceEnum(const void* message, size_t length) {

}

void RemoteControlServer::MH_ChannelEnum(const void* message, size_t length) {

}



void RemoteControlServer::MessageThreadFunc() {
	while (runMessageThread) {
		try {
			RcpPacket packet;
			socket.receive(packet);
			messageDecoder.ProcessMessage(packet.getData(), packet.getDataSize());
		}
		catch (RcpException& e) {
			
		}
	}
}

void RemoteControlServer::StartMessageThread() {
	if (!runMessageThread) {
		// join old thread, if not done yet
		if (messageThread.joinable()) {
			messageThread.join();
		}

		// start a new thread
		runMessageThread = true;
		messageThread = std::thread(
			[this] { MessageThreadFunc(); }
		);
	}
}

void RemoteControlServer::StopMessageThread() {
	runMessageThread = false;
	socket.cancel();
	if (messageThread.joinable()) {
		messageThread.join();
	}
}
