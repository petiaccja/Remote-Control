#pragma once

#include "RemoteControlServer.h"
#include "Message.h"
#include <RemoteControlProtocol/RcpSocket.h>

void RCS_RunAllTest();


/*
class TEST_RemoteControlServer {
public:
	TEST_RemoteControlServer();

	// Tests for the RemoteControlServer class
	bool RCS_Listen_Okay();
	bool RCS_Listen_SocketFailure();
	bool RCS_Listen_ProtocolVoilation();

	bool RCS_Reply_Positive();
	bool RCS_Reply_Negative();
	bool RCS_Reply_SocketFailure();

	bool RCS_Disconnect();

	// Helper functions
	bool SendMessage(RcpSocket& socket, const MessageBase& msg) {
		auto raw = msg.Serialize();
		try {
			socket.send(raw.data(), raw.size(), true);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	bool RecvMessage(RcpSocket& socket, MessageBase& msg) {
		RcpPacket packet;
		try {
			if (!socket.receive(packet, 5000)) {
				return false;
			}
			return msg.Deserlialize(packet.getData(), packet.getDataSize());
		}
		catch (...) {
			return false;
		}
	}
private:
	RcpSocket socket;
};
*/