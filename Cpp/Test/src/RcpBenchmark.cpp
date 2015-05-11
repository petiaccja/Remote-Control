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

#include "../../RemoteControlProtocol/src/Packet.h"
#include "../../RemoteControlProtocol/src/RcpSocket.h"

using namespace std;
using namespace std::chrono;

// Tests included:
// - Latency test: 
//		Attempts to compute average latency.
// - Loss test:
//		Analyzes the percentage of packet loss.
// - Bandwidth test:
//		Checks bandwidth with large packets.

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
void Loss(size_t numPackets, nanoseconds interval, size_t packetSize);
void Bandwidth(size_t numPackets, nanoseconds interval, size_t packetSize);

enum eTest {
	LATENCY,
	LOSS,
	BANDWIDTH
};


RcpSocket* cancelSocket = nullptr;
void CancelSocket(int sgn) {
	if (cancelSocket)
		cancelSocket->cancel();
	signal(SIGINT, CancelSocket);
}

int RcpBenchmark() {
	signal(SIGINT, SIG_IGN);

	// vars
	size_t numPackets = 10;
	nanoseconds interval((long long)1e8);
	size_t packetSize = 0;
	RcpSocket socket;

	// intro
	cout << "Rcp benchmark" << endl << endl;

	// read commands
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

			cout << "send <message> - send a simple text message which the other end displays\n";

			cout << "set parameter value - set common parameters of testing\n";
			cout.flush();
		}
		else if (command == "latency") {

		}
		else if (command == "loss") {

		}
		else if (command == "bandwidth") {

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
			}
		}
		else if (command == "exit") {
			// empty
		}
		else {
			cout << "Invalid command, use \"help\" for more information." << endl;
		}


	} while (command != "exit");

	return 0;
}







void Latency(size_t numPackets, nanoseconds interval, size_t packetSize) {
	cout << "Testing latency...\n";
	

}


void Loss(size_t numPackets, nanoseconds interval, size_t packetSize) {
	cout << "Testing packet loss...\n";
}

void Bandwidth(size_t numPackets, nanoseconds interval, size_t packetSize) {
	cout << "Testing bandwidth...\n";
}