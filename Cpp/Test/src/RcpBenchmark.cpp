#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <future>
#include <cassert>
#include <csignal>
#include <cstring>

#include "../../RemoteControlProtocol/src/Packet.h"
#include "../../RemoteControlProtocol/src/RcpSocket.h"

using namespace std;
using namespace std::chrono;

// Warning:
// This file is a little messed up as it servers only for testing.

// Tests included:
// - Latency test: 
//		Attempts to compute average latency.
// - Loss test:
//		Analyzes the percentage of packet loss.
// - Bandwidth test:
//		Checks bandwidth with large packets.


enum class eMessageType : uint32_t {
	CHAT,
	LATENCY,
	BANDWIDTH,
	CONTROL,
};

enum class eMessageParam : uint32_t {
	SUCCESS,
	INVALID_REQUEST,
	BEGIN,
	END,
	QUERY,
};

enum class eState {
	IDLE,
	SLAVE,
	LATENCY,
	BANDWIDTH,
};


// globals
eState state = eState::IDLE;
RcpSocket socket;
size_t numPackets = 10;
nanoseconds interval((long long)1e8);
size_t packetSize = 0;
atomic_bool runBackgroundThread;
thread backgroundThread;

// helpers
bool StartsWith(string str, string sub) {
	return (str.size() >= sub.size() && str.substr(0, sub.size()) == sub);
}

vector<string> Tokenize(string str, char delim = ' ') {
	vector<string> v;
	size_t offset = 0;
	size_t index;
	do {
		index = str.find(' ', offset);
		v.push_back(str.substr(offset, index - offset));
		offset = index + 1;
	} while (index != string::npos);
	return v;
}


void Latency(size_t numPackets, nanoseconds interval, size_t packetSize);
void Bandwidth(size_t numPackets, nanoseconds interval, size_t packetSize);



RcpSocket* cancelSocket = nullptr;
void CancelSocket(int sgn) {
	if (cancelSocket)
		cancelSocket->cancel();
	signal(SIGINT, CancelSocket);
}


void CancelSlave(int sgn) {
	state = eState::IDLE;
	signal(SIGINT, SIG_IGN);
}


// serialization is for pussies
struct Message {
	static const int HeaderSize = sizeof(eMessageType) + sizeof(eMessageParam) + sizeof(uint32_t);
	union {
		struct {
			eMessageType msg;
			eMessageParam param1;
			uint32_t param2;
			char data[RcpSocket::MaxDatagramSize - HeaderSize];
		};
		char raw[RcpSocket::MaxDatagramSize];
	};
};
Message m;
bool DecodeMessage(const Packet& packet, Message& msg) {
	if (packet.getDataSize() < Message::HeaderSize) {
		return false;
	}
	else {
		memcpy(msg.raw, packet.getData(), std::min(packet.getDataSize(), sizeof(msg.raw)));
		return true;
	}
}



void BackgroundThreadFunc() {
	Packet recvPacket;
	Packet sendPacket;
	Message msg;
	size_t bytesRecieved;

	while (runBackgroundThread) {
		// receive stuff
		if (!socket.receive(recvPacket)) {
			this_thread::sleep_for(chrono::milliseconds(100)); // not so nice
			continue;
		}

		// decode stuff
		if (!DecodeMessage(recvPacket, msg)) {
			continue;
		}

		// process incoming packet
		switch (state)
		{
		case eState::IDLE:
			// in idle state, we don't give a shit about what the other guy sais, just respond with an error
			if (msg.msg != eMessageType::CONTROL) {
				msg.msg = eMessageType::CONTROL;
				msg.param1 = eMessageParam::INVALID_REQUEST;
				socket.send(msg.raw, Message::HeaderSize, true);
			}
			break;
		case eState::SLAVE:
			// seek for LATENCY/LOSS/BW start requests, and reply with accept control message
			if (msg.msg == eMessageType::LATENCY && msg.param1 == eMessageParam::BEGIN) {
				state = eState::LATENCY;
				cout << "Doing latency & loss test..." << endl;
				// reply
				msg.msg = eMessageType::CONTROL;
				msg.param1 = eMessageParam::SUCCESS;
				socket.send(msg.raw, Message::HeaderSize, true);
			}
			else if (msg.msg == eMessageType::BANDWIDTH && msg.param1 == eMessageParam::BEGIN) {
				state = eState::BANDWIDTH;
				bytesRecieved = 0;
				cout << "Doing bandwidth test..." << endl;
				// reply
				msg.msg = eMessageType::CONTROL;
				msg.param1 = eMessageParam::SUCCESS;
				socket.send(msg.raw, Message::HeaderSize, true);
			}
			// occasionally print CHAT messages
			else if (msg.msg == eMessageType::CHAT) {
				// append a zero-terminator in case it's missing
				msg.raw[recvPacket.getDataSize() >= RcpSocket::MaxDatagramSize ? recvPacket.getDataSize() : RcpSocket::MaxDatagramSize - 1] = '\0';
				// print dat shit
				cout << "Message: " << msg.data << endl;
			}
			break;
		case eState::LATENCY:
			if (msg.msg == eMessageType::LATENCY && msg.param1 == eMessageParam::END) {
				state = eState::SLAVE;
				cout << "Latency test ended." << endl;
				break;
			}
			// echo as soon as possible!
			socket.send(&msg, Message::HeaderSize, false);
			cout << "Echo sent!" << endl;
			break;
		case eState::BANDWIDTH:
			if (msg.msg == eMessageType::BANDWIDTH && msg.param1 == eMessageParam::END) {
				state = eState::SLAVE;
				cout << "Bandwidth test ended." << endl;
				break;
			}
			else if (msg.msg == eMessageType::BANDWIDTH && msg.param1 == eMessageParam::QUERY) {
				msg.msg = eMessageType::BANDWIDTH;
				msg.param1 = eMessageParam::QUERY;
				msg.param2 = bytesRecieved;
				socket.send(msg.raw, msg.HeaderSize, true);
				break;
			}
			// update bytes counter
			bytesRecieved += recvPacket.getDataSize();
			break;
		default:
			break;
		}
	}
}


int RcpBenchmark() {
	signal(SIGINT, SIG_IGN);

	//socket.setTiming(1000 * 60 * 3, 2000); // WARNING!!! only for debugging, otherwise these are unusable values
	//socket.debug_enableLog(true);

	// intro
	cout << "Rcp benchmark" << endl << endl;

	// launch background receive thread
	runBackgroundThread = true;
	backgroundThread = thread(BackgroundThreadFunc);


	// process commands
	string command;
	string input;
	do {
		// print console shit
		cout << "rcpbench > ";

		// read command
		getline(cin, input);
		if (!cin.good()) {
			cin.clear();
			cout << endl;
			continue;
		}
		if (input.size() == 0) {
			continue;
		}

		// process command
		auto tokens = Tokenize(input);
		if (tokens.size() == 0) {
			continue;
		}
		command = tokens[0];
		if (command == "help") {
			cout << "Available commands:\n";

			cout << "connect <host> <port> - specify the other end to perform the tests with\n";
			cout << "    host - host name or ip address\n";
			cout << "    port - remote peer's port\n";
			cout << "disconnect - close connection gracefully\n";
			cout << "listen - listens for incoming connections\n";
			cout << "bind <port> - bind to specified port\n";
			cout << "unbind - unbind socket\n";
			cout << "status - display information about current connection state";

			cout << "latency - tests latency of the connection\n";
			cout << "loss - test the probability of packet loss\n";
			cout << "bandwidth - tests the bandwidth of the connection\n";
			cout << "slave - go into slave mode\n"
				<< "    In slave mode, the program serves as the other and of a foreign test launcher.\n";

			cout << "send <message> - send a simple text message which the other end displays\n";

			cout << "set <parameter> <value> - set common parameters of testing\n";
			cout << "    Parameter can be one of the following:\n";
			cout << "    number / count - the number of packets to use for a single test\n";
			cout << "             0 means until you interrupt manually\n";
			cout << "    interval - interval between packets during tests, milliseconds\n";
			cout << "    size - the size of each packet used for tests, byte\n";
			cout.flush();
		}
		else if (command == "latency") {
			Latency(numPackets, interval, packetSize);
		}
		else if (command == "loss") {
			cout << "Launch a latency test -- it has been merged with packet loss test, and reports loss as well." << endl;
		}
		else if (command == "bandwidth") {
			Bandwidth(numPackets, interval, packetSize);
		}
		else if (command == "slave") {
			if (!socket.isConnected()) {
				cout << "Please connect first!" << endl;
				continue;
			}
			state = eState::SLAVE;
			// register a SIGINT handler
			signal(SIGINT, CancelSlave);
			cout << "Slaving around, press Control+C to stop..." << endl;
			// ugly, promises might help, but it's good enough
			while (state != eState::IDLE) {
				this_thread::sleep_for(chrono::milliseconds(50));
			}
		}
		else if (command == "connect") {
			// check binding
			if (!socket.isBound()) {
				cout << "Please bind the socket first!" << endl;
				continue;
			}

			// extract address and port
			if (tokens.size() != 3) {
				cout << "Usage: connect <host> <port>" << endl;
				continue;
			}
			string address = tokens[1];
			uint16_t port = atoi(tokens[2].c_str());

			// try to connect
			bool result = socket.connect(address, port);

			// check result
			if (!result) {
				cout << "Could not connect to remote peer specified." << endl;
			}
			else {
				cout << "Connected to " << socket.getRemoteAddress() << ":" << socket.getRemotePort() << "!" << endl;
			}
		}
		else if (command == "disconnect") {
			socket.disconnect();
			if (socket.isConnected()) {
				cout << "Hoppa geci, itt valami baj van!!" << endl;
				assert(false);
			}
			else {
				cout << "Disconnected." << endl;
			}
		}
		else if (command == "listen") {
			// check binding
			if (!socket.isBound()) {
				cout << "Please bind the socket first!" << endl;
				continue;
			}

			// display help
			cout << "Listening on port " << socket.getLocalPort() << ", press CTRL+C to cancel..." << endl;

			// register signal handler for SIGINT -> cancel listening
			cancelSocket = &socket;
			signal(SIGINT, CancelSocket);

			// start accepting
			if (socket.accept()) {
				cout << "Connection accepted from " << socket.getRemoteAddress() << ":" << socket.getRemotePort() << "!" << endl;
			}
			else {
				cout << "Could not accept connection." << endl;
			}

			// unregister signal handler
			signal(SIGINT, SIG_IGN);
			cancelSocket = nullptr;
		}
		else if (command == "bind") {
			// extract address and port
			if (tokens.size() != 2) {
				cout << "Usage: bind <port>" << endl;
				continue;
			}
			uint16_t port = atoi(tokens[1].c_str());

			if (socket.bind(port)) {
				cout << "Succesfully bound!" << endl;
			}
			else {
				cout << "Could not bind to port, is it already in use?" << endl;
			}
		}
		else if (command == "unbind") {
			socket.unbind();
		}
		else if (command == "status") {
			if (socket.isBound()) {
				cout << "Bound to port " << socket.getLocalPort() << endl;
			}
			else {
				cout << "Not bound." << endl;
			}
			if (socket.isConnected()) {
				cout << "Connected to " << socket.getRemoteAddress() << ":" << socket.getRemotePort() << endl;
			}
			else {
				cout << "Disconnected" << endl;
			}
		}
		else if (command == "send") {
			if (input.size() > 5) {
				string message = input.substr(5, string::npos);
				Message msg;
				msg.msg = eMessageType::CHAT;
				msg.param1 = eMessageParam::SUCCESS;
				size_t strSize = std::min(sizeof(msg.data), message.size() + 1);
				memcpy(msg.data, message.c_str(), strSize);
				msg.data[sizeof(msg.data) - 1] = '\0';
				socket.send(msg.raw, strSize + Message::HeaderSize, true);
				cout << "Message sent!" << endl;
			}
		}
		else if (command == "exit") {
			socket.disconnect();
			break;
		}
		else if (command == "set") {
			if (tokens.size() != 3) {
				cout << "Usage: set <parameter> <value>" << endl;
				continue;
			}
			string parameter = tokens[1];
			int value = atoi(tokens[2].c_str());
			if (parameter == "number" || parameter == "count") {
				if (value == 0) {
					cout << "This parameter cannot be 0!" << endl;
					continue;
				}
				numPackets = value;
			}
			else if (parameter == "interval") {
				if (value == 0) {
					cout << "This parameter cannot be 0!" << endl;
					continue;
				}
				interval = milliseconds(value);
			}
			else if (parameter == "size") {
				packetSize = value;
			}
			else {
				cout << "Invalid parameter!" << endl;
			}
		}
		else {
			cout << "Invalid command, use \"help\" for more information." << endl;
		}


	} while (command != "exit");

	// join background thread
	runBackgroundThread = false;
	if (backgroundThread.joinable()) {
		backgroundThread.join();
	}

	return 0;
}







void Latency(size_t numPackets, nanoseconds interval, size_t packetSize) {
	cout << "Testing latency...\n";
	Message msg;
	Message decmsg;
	Packet packet;
	bool flag;

	if (numPackets == 0) {
		numPackets = 4;;
	}

	// kill background thread
	runBackgroundThread = false;
	socket.cancel();
	if (backgroundThread.joinable()) {
		backgroundThread.join();
	}
	// restart background thread when we quit
	struct S {
		~S() {
			try {
				runBackgroundThread = true;
				backgroundThread = thread(BackgroundThreadFunc);
			}
			catch (...) {

			}
		}
	} s;

	// try to start latency test
	if (!socket.isConnected()) {
		return;
	}
	msg.msg = eMessageType::LATENCY;
	msg.param1 = eMessageParam::BEGIN;
	msg.param2 = 1000;
	socket.send(&msg, Message::HeaderSize, true);
	flag = false;
	auto fut = std::async([&] {this_thread::sleep_for(milliseconds(3000)); if (!flag) socket.cancel(); });
	if (!socket.receive(packet)) {
		msg.param1 = eMessageParam::END;
		socket.send(&msg, Message::HeaderSize, true);
		cout << "Could not start test, the other end did not answer." << endl;
		return;
	}
	flag = true;
	DecodeMessage(packet, decmsg);
	if (decmsg.msg != eMessageType::CONTROL || decmsg.param1 != eMessageParam::SUCCESS) {
		cout << "Bitch did not accept the request." << endl;
		return;
	}

	// perform latency check
	unordered_map<unsigned, high_resolution_clock::time_point> sendMap;
	unordered_map<unsigned, high_resolution_clock::time_point> recvMap;
	sendMap.reserve(10000);
	recvMap.reserve(10000);

	// start a dumb thread that infinitely receives packets with low internal latency
	socket.setBlocking(false);
	volatile bool run = true;
	thread receiveThread([&] {
		Packet p;
		Message m;
		while (run) {
			if (socket.receive(p) && DecodeMessage(p, m)) {
				recvMap.insert({ m.param2, high_resolution_clock::now() });
			}
		}
	});

	// start sending latency messages
	msg.msg = eMessageType::LATENCY;
	msg.param1 = eMessageParam::QUERY;
	msg.param2 = 0;

	for (size_t i = 0; i < numPackets && i < 10000; ++i) {
		msg.msg = eMessageType::LATENCY;
		msg.param1 = eMessageParam::QUERY;
		msg.param2 = i;
		if (socket.send(msg.raw, Message::HeaderSize, false)) {
			sendMap.insert({ msg.param2, high_resolution_clock::now() });
		}
		this_thread::sleep_for(interval);
	}

	// send a closing signal
	msg.msg = eMessageType::LATENCY;
	msg.param1 = eMessageParam::END;
	msg.param2 = 10000;
	socket.send(msg.raw, msg.HeaderSize, true);

	// wait till all messages arrive
	this_thread::sleep_for(milliseconds(1000));

	// finish off by closing resources
	run = false;
	if (receiveThread.joinable()) {
		receiveThread.join();
	}
	fut.wait();

	socket.setBlocking(true);

	// analyize results
	double latency = 0.0;
	double maxLatency = 0.0;
	double minLatency = 1e+30;
	for (auto& v : sendMap) {
		auto it = recvMap.find(v.first);
		if (it != recvMap.end()) {
			double l = 1e-9 * duration_cast<nanoseconds>(it->second - v.second).count();
			latency += l;
			maxLatency = max(l, maxLatency);
			minLatency = min(l, minLatency);
		}
	}

	latency /= recvMap.size();
	cout << "Packets sent:   " << sendMap.size() << endl;
	cout << "Packets recv'd: " << recvMap.size() << endl;

	cout << "Latency (retour):" << endl
		<< "Min = " << 1000 * minLatency << " ms" << endl
		<< "Avg. = " << 1000 * latency << " ms" << endl
		<< "Max = " << 1000 * maxLatency << " ms" << endl;

	cout << "Packet loss:" << endl
		<< 100 * (1 - (double)recvMap.size() / (double)sendMap.size()) << " % for echos" << endl
		<< 100 * (1 - sqrt((double)recvMap.size() / (double)sendMap.size())) << " % estmiated for one way message" << endl;
}


void Bandwidth(size_t numPackets, nanoseconds interval, size_t packetSize) {
	cout << "Testing bandwidth...\n";

	Message msg;
	Message decmsg;
	Packet packet;
	bool flag;

	if (numPackets == 0) {
		numPackets = 4;;
	}

	// kill background thread
	runBackgroundThread = false;
	socket.cancel();
	if (backgroundThread.joinable()) {
		backgroundThread.join();
	}
	// restart background thread when we quit
	struct S {
		~S() {
			try {
				runBackgroundThread = true;
				backgroundThread = thread(BackgroundThreadFunc);
			}
			catch (...) {

			}
		}
	} s;

	// try to start latency test
	if (!socket.isConnected()) {
		return;
	}
	msg.msg = eMessageType::BANDWIDTH;
	msg.param1 = eMessageParam::BEGIN;
	msg.param2 = 1000;
	socket.send(&msg, Message::HeaderSize, true);
	flag = false;
	auto fut = std::async([&] {this_thread::sleep_for(milliseconds(3000)); if (!flag) socket.cancel(); });
	if (!socket.receive(packet)) {
		msg.param1 = eMessageParam::END;
		socket.send(&msg, Message::HeaderSize, true);
		cout << "Could not start test, the other end did not answer." << endl;
		return;
	}
	flag = true;
	DecodeMessage(packet, decmsg);
	if (decmsg.msg != eMessageType::CONTROL || decmsg.param1 != eMessageParam::SUCCESS) {
		cout << "Remote peer did not accept the request." << endl;
		return;
	}

	// perform bandwidth test
	auto startTime = chrono::steady_clock::now();

	// flood remote peer with packets
	for (size_t i = 0; i < numPackets; i++) {
		Message m;
		m.msg = eMessageType::BANDWIDTH;
		m.param1 = eMessageParam::SUCCESS;
		m.param2 = 0;
		socket.send(msg.raw, min((size_t)packetSize, (size_t)RcpSocket::MaxDatagramSize), false);
	}

	auto endTime = chrono::steady_clock::now();
	nanoseconds elapsed = endTime - startTime;

	// query how much stuff was sent
	size_t dataSent;
	msg.msg = eMessageType::BANDWIDTH;
	msg.param1 = eMessageParam::QUERY;
	msg.param2 = 0;
	socket.send(msg.raw, msg.HeaderSize, true);
	socket.receive(packet);
	DecodeMessage(packet, msg);
	dataSent = msg.param2;

	// send a closing signal
	msg.msg = eMessageType::BANDWIDTH;
	msg.param1 = eMessageParam::END;
	msg.param2 = 10000;
	socket.send(msg.raw, msg.HeaderSize, true);

	cout << "Data sent: " << dataSent / 1048576 << " MB" << endl;
	cout << "Elapsed:   " << (double)elapsed.count() / 1e9 << " seconds" << endl;
	cout << "Bandwidth: " << (double)dataSent / 1048576 / (double)elapsed.count() * 1e9 << " MBps" << endl;
}
