#include "TEST_RemoteControlServer.h"
#include <future>
#include <gtest/gtest.h>


using namespace std;



void RCS_RunAllTest() {
	int argc = 0;
	::testing::InitGoogleTest(&argc, (char**)nullptr);
	RUN_ALL_TESTS();
}



class TEST_RemoteControlServer : public ::testing::Test {
public:
	TEST_RemoteControlServer() {
		server.SetLocalPort(5630);
		socket.bind(5631);
	}

	RemoteControlServer server;
	RcpSocket socket;

	bool Send(const MessageBase& msg) {
		try {
			auto raw = msg.Serialize();
			socket.send(raw.data(), raw.size(), true);
			return true;
		}
		catch (...) {
			return false;
		}
	}
	bool Recv(MessageBase& msg, int timeout = std::numeric_limits<int>::max()) {
		RcpPacket packet;
		try {
			bool result = socket.receive(packet, timeout);
			if (!result) {
				return false;
			}
			bool valid = msg.Deserlialize(packet.getData(), packet.getDataSize());
			if (!valid) {
				return false;
			}
			return true;
		}
		catch (std::exception& e) {
			cout << e.what();
			return false;
		}
		catch (...) {
			return false;
		}
	}
	bool Connect() {
		try {
			socket.connect("localhost", 5630);
			return true;
		}
		catch (...) {
			return false;
		}
	}
	void Disconnect() {
		socket.disconnect();
	}
};


TEST_F(TEST_RemoteControlServer, Listen_Okay) {
	ConnectionMessage msg{ ConnectionMessage::CONNECTION_REQUEST };

	auto fut = async([&] {return server.Listen();});

	ASSERT_TRUE(Connect());
	ASSERT_TRUE(Send(msg));

	bool result = fut.get();

	ASSERT_TRUE(result);
	ASSERT_EQ(server.DBG_State(), RemoteControlServer::HALF_OPEN);
	ASSERT_FALSE(server.DBG_RunMessageThread());
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_TRUE(server.DBG_Socket().isConnected());
}


TEST_F(TEST_RemoteControlServer, Listen_SocketFailure) {
	auto fut = async([&] {return server.Listen();});

	ASSERT_TRUE(Connect());
	Disconnect();

	bool result = fut.get();

	ASSERT_FALSE(result);
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_FALSE(server.DBG_RunMessageThread());
	ASSERT_FALSE(server.DBG_Socket().isConnected());
	ASSERT_TRUE(server.DBG_State() == server.DISCONNECTED);
}


TEST_F(TEST_RemoteControlServer, Listen_ProtocolViolation) {
	auto fut = async([&] {return server.Listen();});
	
	ConnectionMessage msg{ ConnectionMessage::CONNECTION_REPLY, false }; // wrong message

	Connect();
	Send(msg);

	bool result = fut.get();

	ASSERT_FALSE(result);
	ASSERT_EQ(server.DBG_State(), server.DISCONNECTED);
	ASSERT_FALSE(server.DBG_Socket().isConnected());
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_FALSE(server.DBG_RunMessageThread());
}

TEST_F(TEST_RemoteControlServer, Reply_Positive) {
	ConnectionMessage requestMsg{ConnectionMessage::CONNECTION_REQUEST};
	ConnectionMessage replyMsg{ ConnectionMessage::CONNECTION_REPLY, true };
	std::future<bool> fut;

	fut = async([&] {return server.Listen();});
	Connect();
	Send(requestMsg);

	ASSERT_TRUE(fut.get());

	fut = async([&] {return server.Reply(true);});

	ASSERT_TRUE(fut.get());

	ConnectionMessage reply;
	ASSERT_TRUE(Recv(reply));

	ASSERT_TRUE(reply == replyMsg);
		
	ASSERT_EQ(server.DBG_State(), server.CONNECTED);
	ASSERT_TRUE(server.DBG_Socket().isConnected());
	ASSERT_TRUE(server.DBG_MessageThread().joinable());
	ASSERT_TRUE(server.DBG_RunMessageThread());
}


TEST_F(TEST_RemoteControlServer, Reply_Negative) {
	ConnectionMessage requestMsg{ ConnectionMessage::CONNECTION_REQUEST };
	ConnectionMessage replyMsg{ ConnectionMessage::CONNECTION_REPLY, false };
	std::future<bool> fut;

	fut = async([&] {return server.Listen();});
	Connect();
	Send(requestMsg);

	ASSERT_TRUE(fut.get());

	fut = async([&] {return server.Reply(false);});

	ASSERT_FALSE(fut.get());

	ConnectionMessage reply;
	ASSERT_TRUE(Recv(reply));

	ASSERT_TRUE(reply == replyMsg);

	ASSERT_EQ(server.DBG_State(), server.DISCONNECTED);
	ASSERT_FALSE(server.DBG_Socket().isConnected());
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_FALSE(server.DBG_RunMessageThread());
}


TEST_F(TEST_RemoteControlServer, Reply_SocketFailure) {
	ConnectionMessage requestMsg{ ConnectionMessage::CONNECTION_REQUEST };

	std::future<bool> fut;

	fut = async([&] {return server.Listen();});
	Connect();
	Send(requestMsg);
	Disconnect();

	ASSERT_TRUE(fut.get());

	fut = async([&] {return server.Reply(false);});

	ASSERT_FALSE(fut.get());

	ASSERT_EQ(server.DBG_State(), server.DISCONNECTED);
	ASSERT_FALSE(server.DBG_Socket().isConnected());
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_FALSE(server.DBG_RunMessageThread());
}


TEST_F(TEST_RemoteControlServer, Disconnect_Normal) {
	// This is the code of Reply_Positive
	ConnectionMessage requestMsg{ ConnectionMessage::CONNECTION_REQUEST };
	ConnectionMessage replyMsg{ ConnectionMessage::CONNECTION_REPLY, true };
	std::future<bool> fut;

	fut = async([&] {return server.Listen();});
	Connect();
	Send(requestMsg);

	ASSERT_TRUE(fut.get());

	fut = async([&] {return server.Reply(true);});

	ASSERT_TRUE(fut.get());

	ConnectionMessage reply;
	ASSERT_TRUE(Recv(reply));

	ASSERT_TRUE(reply == replyMsg);

	ASSERT_EQ(server.DBG_State(), server.CONNECTED);
	ASSERT_TRUE(server.DBG_Socket().isConnected());
	ASSERT_TRUE(server.DBG_MessageThread().joinable());
	ASSERT_TRUE(server.DBG_RunMessageThread());

	// Test for disconnect begins here
	ConnectionMessage discMsg{ ConnectionMessage::DISCONNECT };
	auto discFut = async([&] {server.Disconnect();});

	ASSERT_TRUE(Recv(reply));
	ASSERT_TRUE(reply == discMsg);
	ASSERT_TRUE(Send(discMsg));
	Disconnect();

	discFut.wait();

	ASSERT_EQ(server.DBG_State(), server.DISCONNECTED);
	ASSERT_FALSE(server.DBG_Socket().isConnected());
	ASSERT_FALSE(server.DBG_MessageThread().joinable());
	ASSERT_FALSE(server.DBG_RunMessageThread());
}
/*

TEST_RemoteControlServer::TEST_RemoteControlServer() {
	socket.bind(5631);
}


bool TEST_RemoteControlServer::RCS_Listen_Okay() {
	RemoteControlServer server;

	ConnectionMessage outbound[] = {
		{ ConnectionMessage::CONNECTION_REQUEST },
	};
	ConnectionMessage reply;

	server.SetLocalPort(5630);
	auto fut = async([&] {return server.Listen();});

	socket.connect("localhost", 5630);
	if (!SendMessage(socket, outbound[0])) {
		return false;
	}

	bool result = fut.get();

	// check server internal state
	if (server.runMessageThread == true
		|| server.messageThread.joinable()
		|| !server.socket.isConnected()
		|| server.state != server.HALF_OPEN
		|| !result)
	{
		return false;
	}

	socket.disconnect();

	return true;
}
bool TEST_RemoteControlServer::RCS_Listen_SocketFailure() {
	RemoteControlServer server;

	server.SetLocalPort(5630);
	auto fut = async([&] {return server.Listen();});

	socket.connect("localhost", 5630);
	socket.disconnect();

	bool result = fut.get();

	// check server internal state
	if (server.runMessageThread == true
		|| server.messageThread.joinable()
		|| server.socket.isConnected()
		|| server.state != server.DISCONNECTED
		|| result != false)
	{
		return false;
	}

	return true;
}
bool TEST_RemoteControlServer::RCS_Listen_ProtocolVoilation() {
	RemoteControlServer server;

	server.SetLocalPort(5630);
	auto fut = async([&] {return server.Listen();});

	socket.connect("localhost", 5630);
	ConnectionMessage msg{ ConnectionMessage::CONNECTION_REPLY, false }; // wrong message

	SendMessage(socket, msg);

	bool result = fut.get();

	// check server internal state
	if (server.runMessageThread == true
		|| server.messageThread.joinable()
		|| server.socket.isConnected()
		|| server.state != server.DISCONNECTED
		|| result != false)
	{
		return false;
	}

	socket.disconnect();

	return true;
}

bool TEST_RemoteControlServer::RCS_Reply_Positive() {
	return false;
}
bool TEST_RemoteControlServer::RCS_Reply_Negative() {
	return false;
}
bool TEST_RemoteControlServer::RCS_Reply_SocketFailure() {
	return false;
}

bool TEST_RemoteControlServer::RCS_Disconnect() {
	return false;
}

*/

