#include "tests.h"
#include "ThreadEnqueue.h"
#include "Barrier.h"

#include <RemoteControlServer/ChannelManagerServo.h>
#include <RemoteControlServer/ServoProviderDummy.h>
#include <RemoteControlServer/Serializer.h>
#include <RemoteControlServer/Message.h>
#include <RemoteControlServer/RemoteCOntrolServer.h>


#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <future>


#ifdef _MSC_VER
#pragma warning(disable : 4305)
#pragma warning(disable : 4309)
#endif


using namespace std;



bool TestSerializer();
bool TestMessageSerialization();
bool TestDecoder();
void TestServoManager();
bool TestServerConnection();

int RcsTest() {
	/*
	// test serializetion of primitives
	if (TestSerializer()) {
		cout << "Serializer works!" << endl;
	}
	else {
		cout << "Serializer failed" << endl;
	}

	// test serialization of messages 
	if (TestMessageSerialization()) {
		cout << "Message serialization succeeded!" << endl;
	}
	else {
		cout << "Message serialization failed" << endl;
	}

	// test message decoder/demuxer of messages 
	if (TestDecoder()) {
		cout << "Message decoder works!" << endl;
	}
	else {
		cout << "Message decoder failed" << endl;
	}
	*/
	if (TestServerConnection()) {
		cout << "Server connection works!" << endl;
	}
	else {
		cout << "Server connection failed" << endl;
	}

	_getch();
	return 0;
}


// Test Serializer class for serializing primitives
bool TestSerializer() {
	Serializer ser;
	uint8_t u8 = 0xDEADBEEF;
	uint16_t u16 = 0xDEADBEEF;
	uint32_t u32 = 0xDEADBEEF;
	uint64_t u64 = 0xDEADBEEFDEADBEEFULL;
	int8_t i8 = -0x5EADBEEF;
	int16_t i16 = -0x5EADBEEF;
	int32_t i32 = -0x5EADBEEF;
	int64_t i64 = -0x5EADBEEFDEADBEEFLL;
	float f = 314.15;
	double d = 314.15;
	bool b = true;
	void* p = (void*)0x12345678;

	uint8_t u8_ = 0;
	uint16_t u16_ = 0;
	uint32_t u32_ = 0;
	uint64_t u64_ = 0ULL;
	int8_t i8_ = -0;
	int16_t i16_ = -0;
	int32_t i32_ = -0;
	int64_t i64_ = -0LL;
	float f_ = 0;
	double d_ = 0;
	bool b_ = false;
	void* p_ = (void*)0;

	ser << u8 << u16 << u32 << u64;
	ser << i8 << i16 << i32 << i64;
	ser << f << d << b << p;

	ser >> p_ >> b_ >> d_ >> f_;
	ser >> i64_ >> i32_ >> i16_ >> i8_;
	ser >> u64_ >> u32_ >> u16_ >> u8_;

	if (ser.Size() != 0) {
		return false;
	}
	if (!
		(u8_ == u8 &&
			u16_ == u16 &&
			u32_ == u32 &&
			u64_ == u64 &&
			i8_ == i8 &&
			i16_ == i16 &&
			i32_ == i32 &&
			i64_ == i64 &&
			f_ == f &&
			d_ == d &&
			b_ == b &&
			p_ == p))
	{
		return false;
	}

	return true;
}



// Test serialization of Messages.
bool TestMessageSerialization() {
	std::vector<uint8_t> data;

	//-------------------------
	// ServoMessage
	//-------------------------

	ServoMessage servo;
	servo.action = ServoMessage::SET;
	servo.state = 0.5f;
	servo.channel = 3;
	data = servo.Serialize();
	memset(&servo, 0, sizeof(servo));
	
	if (!servo.Deserlialize(data.data(), data.size()) || servo.action != ServoMessage::SET || servo.state != 0.5f || servo.channel != 3) {
		return false;
	}

	//----------------------------
	// ConnectionMessage 
	//----------------------------
	ConnectionMessage auth;
	auth.isOk = false;
	const unsigned char password[] = "password";

	// connection request, no special data
	auth.action = ConnectionMessage::CONNECTION_REQUEST;
	data = auth.Serialize();
	auth.action = (ConnectionMessage::eAction)7832456;
	if (!auth.Deserlialize(data.data(), data.size()) || auth.action != ConnectionMessage::CONNECTION_REQUEST) {
		return false;
	}

	// connection reply: isOk must be false
	auth.action = ConnectionMessage::CONNECTION_REPLY;
	data = auth.Serialize();
	auth.action = (ConnectionMessage::eAction)873465;
	auth.isOk = true;
	if (!auth.Deserlialize(data.data(), data.size()) || auth.action != ConnectionMessage::CONNECTION_REPLY || auth.isOk != false) {
		return false;
	}

	// password reply: password must be correct
	for (auto v : password) {
		auth.password.push_back(v);
	}
	auth.action = ConnectionMessage::PASSWORD_REPLY;
	data = auth.Serialize();
	auth.action = (ConnectionMessage::eAction)8743658;
	auth.password.clear();
	if (!auth.Deserlialize(data.data(), data.size()) ||
		auth.action != ConnectionMessage::PASSWORD_REPLY ||
		auth.password.size() != sizeof(password) ||
		memcmp(auth.password.data(), password, sizeof(password)) != 0)
	{
		return false;
	}

	// password request should work...	


	//----------------------
	// EnumDevicesMessage
	//----------------------
	EnumDevicesMessage enumdev;
	enumdev.devices = {
		{EnumDevicesMessage::SERVO, 6},
		{EnumDevicesMessage::PWM, 8}
	};
	data = enumdev.Serialize();
	enumdev.devices.clear();
	if (!enumdev.Deserlialize(data.data(), data.size()) ||
		enumdev.devices.size() != 2 ||
		enumdev.devices[0].type != EnumDevicesMessage::SERVO ||
		enumdev.devices[0].channelCount != 6 ||
		enumdev.devices[1].type != EnumDevicesMessage::PWM ||
		enumdev.devices[1].channelCount != 8)
	{
		return false;
	}

	//--------------------
	// EnumChannelsMessage
	//--------------------
	EnumChannelsMessage enumch;
	enumch.type = EnumChannelsMessage::SERVO;
	enumch.channels = { 122, 123, 124 };
	data = enumch.Serialize();
	if (!enumch.Deserlialize(data.data(), data.size()) ||
		enumch.type != EnumChannelsMessage::SERVO ||
		enumch.channels.size() != 3 ||
		enumch.channels[0] != 122 || enumch.channels[1] != 123 || enumch.channels[2] != 124
		)
	{
		return false;
	}

	return true;
}


// Test message decoder/demuxer
bool TestDecoder() {
	MessageDecoder dec;
	int servoMsgCount = 0;
	int authMsgCount = 0;

	dec.SetHandler(eMessageType::CONNECTION, 
		[&](const void* data, size_t size) {
			ConnectionMessage msg;
			if (msg.Deserlialize(data, size))
				authMsgCount++;
	});

	dec.SetHandler(eMessageType::DEVICE_SERVO, 
		[&](const void* data, size_t size) {
			ServoMessage msg;
			if (msg.Deserlialize(data, size))
				servoMsgCount++;
	});

	ServoMessage servoMsg;
	servoMsg.action = ServoMessage::SET;
	servoMsg.channel = 1;
	servoMsg.state = 0.5f;
	auto servoMsgData = servoMsg.Serialize();

	ConnectionMessage authMsg;
	authMsg.action = ConnectionMessage::CONNECTION_REQUEST;
	auto authMsgData = authMsg.Serialize();

	dec.ProcessMessage(servoMsgData.data(), servoMsgData.size());
	dec.ProcessMessage(authMsgData.data(), authMsgData.size());
	dec.ProcessMessage(servoMsgData.data(), servoMsgData.size());
	dec.ProcessMessage(authMsgData.data(), authMsgData.size());
	dec.ProcessMessage(servoMsgData.data(), servoMsgData.size());

	return servoMsgCount == 3 && authMsgCount == 2;
}



void TestServoManager() {
	ServoProviderDummy provider8(8);
	ServoProviderDummy provider4(4);
	ServoProviderDummy provider6(6);
	ChannelManagerServo manager;

	auto ListChannels = [&] {
		cout << "Providers[" << manager.GetNumProviders() << "] = { " << endl;
		for (auto it = manager.ProviderBegin(); it != manager.ProviderEnd(); it++) {
			cout << "   " << *it << endl;
		}
		cout << "}" << endl;

		cout << "Channel listing[" << manager.GetNumChannels() << "] = { " << endl;
		for (auto it = manager.ChannelBegin(); it != manager.ChannelEnd(); it++) {
			cout << "   ch " << it->channel << " @ " << it->provider << ":" << it->port << endl;
		}
		cout << "}" << endl;
	};

	ListChannels();
	cout << manager.AddProvider(&provider8, 2) << endl;
	ListChannels();
	cout << manager.AddProvider(&provider4, 12) << endl;
	ListChannels();
	cout << manager.AddProvider(&provider6, 20) << endl;
	ListChannels();
	cout << manager.AddProvider(&provider6, 26) << endl;
	ListChannels();
	manager.RemoveProvider(&provider4);
	ListChannels();

	float state = -1.0f;
	for (auto it = manager.ChannelBegin(); it != manager.ChannelEnd(); it++) {
		manager.SetState(state, it->channel);
		state += 2.0f / (manager.GetNumChannels() - 1);
	}
	for (auto it = manager.ChannelBegin(); it != manager.ChannelEnd(); it++) {
		cout << "ch " << it->channel << " = " << manager.GetState(it->channel) << endl;
	}

	manager.ClearProviders();
	ListChannels();
}



bool TestServerConnection() {
	RemoteControlServer server;
	RcpSocket socket;
	Barrier barrier;
	Sequence AT;
	bool clientOk = true;
	bool serverOk = true;
	RcpPacket packet;
	
	// server
	thread serverThread([&] {
		server.SetLocalPort(5630);
		server.SetPassword({ 'a', 's', 'd' });

		// listen for incoming connection
		AT(0);
		if (!server.Listen()) {
			serverOk = false;
			return;
		}
		
		// reply to client
		AT(400);
		if (!server.Reply(true)) {
			serverOk = false;
			return;
		}

		// disconnect
		AT(800);
		server.Disconnect();
	});

	// simulated client
	thread clientThread([&] {
		ConnectionMessage connMsg;
		vector<uint8_t> data;

		socket.bind(5631);
		
		// connect to server
		AT(200);
		socket.connect("localhost", 5630);
		connMsg.action = ConnectionMessage::CONNECTION_REQUEST;
		data = connMsg.Serialize();
		socket.send(data.data(), data.size(), true);

		// get server's reply
		AT(600);
		socket.receive(packet);
		connMsg.Deserlialize(packet.getData(), packet.getDataSize());
		if (!(connMsg.action == ConnectionMessage::CONNECTION_REPLY && connMsg.isOk)) {
			clientOk = false;
			return;
		}

		// get server's disconnect msg
		socket.receive(packet);
		connMsg.Deserlialize(packet.getData(), packet.getDataSize());
		if (!(connMsg.action == ConnectionMessage::DISCONNECT)) {
			clientOk = false;
			return;
		}

		// answer to server's disconnest msg
		socket.send(packet);
	});

	serverThread.join();
	clientThread.join();

	return true;
}
