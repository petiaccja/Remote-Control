#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <mutex>
#include <chrono>
#include "../../RemoteControlProtocol/src/Packet.h"
#include "../../RemoteControlProtocol/src/RcpSocket.h"

#include <conio.h>

using namespace std;

void CommunicationTest();


class SocketTester {
public:
	// variables
	RcpSocket socket;
	RcpTester tester;
	Packet packet;
	RcpTester::RcpHeader header;
	RcpTester::RcpHeader send_header;

	bool recieve() {
		for (int i = 0; i < 10; i++) {
			if (tester.receive(packet, header)) {
				return true;
			}
			this_thread::sleep_for(chrono::milliseconds(100));
		}
		return false;
	};
	bool PrintRecieve() {
		if (tester.receive(packet, header)) {
			cout << "recieved: " << header.sequenceNumber << ", " << header.batchNumber << ", ";
			if (header.flags == 0) {
				cout << "0" << endl;
			}
			bool isFirst = true;
			if (header.flags & RcpTester::ACK) {
				if (!isFirst)
					cout << " | ";
				cout << "ACK";
			}
			if (header.flags & RcpTester::REL) {
				if (!isFirst)
					cout << " | ";
				cout << "REL";
			}
			if (header.flags & RcpTester::KEP) {
				if (!isFirst)
					cout << " | ";
				cout << "KEP";
			}
			if (header.flags & RcpTester::SYN) {
				if (!isFirst)
					cout << " | ";
				cout << "SYN";
			}
			if (header.flags & RcpTester::FIN) {
				if (!isFirst)
					cout << " | ";
				cout << "FIN";
			}
			cout << endl << endl;
			return true;
		}
		else {
			return false;
		}
	};

	void PrintState() { cout << socket.debug_PrintState() << endl << endl; };
	void Sleep(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); };

	SocketTester() {
		// bind and init stuff
		socket.bind(5630);
		tester.bind(5631);

		
		socket.debug_connect("localhost", 5631);
	}

	~SocketTester() {
		// disconnect socket
		socket.disconnect();
	}

	void ReorderingTest() {
		uint32_t index = 0;

		// send some packets to the socket
		tester.send({0, 0, 0}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({5, 4, 0}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({6, 4, 0}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({1, 0, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({3, 2, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		// resend reliable packet: duplication with reserved spaces
		tester.send({3, 2, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}
		// --

		tester.send({2, 1, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({4, 3, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		// resend reliable packet: duplication without reserved spaces
		tester.send({4, 3, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}

		tester.send({4, 3, RcpTester::REL}, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		while (PrintRecieve()) {}
		// --


		socket.setBlocking(false);
		Packet p;
		cout << "Packets recieved by socket:" << endl;
		while (socket.receive(p)) {
			cout << p.getSequenceNumber() << " ";
		}
		cout << endl;
	}

	void ReliableSendTest() {
		Packet p;

		PrintState();

		// send a reliable packet from the socket and see how it waits for the ack
		p.setReliable(true);
		socket.send(p);
		p.setReliable(false);
		socket.send(p);
		p.setReliable(true);
		socket.send(p);
		Sleep(100);
		PrintState();

		while (PrintRecieve()) {}
		Sleep(1000);
		while (PrintRecieve()) {}
		tester.send({0, 0, RcpTester::ACK}, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}
		PrintState();

		this_thread::sleep_for(chrono::milliseconds(200));
		socket.disconnect();
	}


};


////////////////////////////////////////////////////////////////////////////////
// MAIN

int main() {
	SocketTester tester;
	//tester.ReliableSendTest();
	tester.ReorderingTest();
	//CommunicationTest();

	cout << "Press any key to exit...\n";
	_getch();
	return 0;
}
//
////////////////////////////////////////////////////////////////////////////////



void CommunicationTest() {
	RcpSocket sock1;
	RcpSocket sock2;
	mutex ioMutex;

	bool isBound = false;
	isBound = sock1.bind(5630) && sock2.bind(5631);
	cout << "s1: " << sock1.getLocalPort() << endl;
	cout << "s2: " << sock2.getLocalPort() << endl;

	if (!isBound) {
		cout << "binding failed lel" << endl;
		return;
	}

	thread socketThread1(
		[&]() {
		bool result;
		result = sock1.accept();
		if (result) {
			cout << "accepted" << endl;
		}
		else {
			cout << "could not accept connection" << endl;
		}
		sock1.setBlocking(false);

		Packet packet;
		char data[64] = {0};
		packet.setReliable(false);

		mt19937 rne;
		uniform_int_distribution<unsigned> rng(1, 400);
		// send stuff with random sleep
		for (int i = 0; i < 10; i++) {
			sprintf(data, "%d", i);

			// send stuff
			packet.setData(data, sizeof(data));
			if (i == 5) {
				packet.setReliable(true);
			}
			else {
				packet.setReliable(false);
			}
			sock1.send(packet);

			// recv all msg
			Packet recp;
			while (sock1.receive(recp)) {
				lock_guard<mutex> lk(ioMutex);
				cout << sock1.getLocalPort() << ": " << (const char*)recp.getData() << endl;
			}

			this_thread::sleep_for(chrono::milliseconds(rng(rne)));
		}

		sock1.disconnect();
	});

	thread socketThread2(
		[&]() {
		bool result;
		result = sock2.connect("localhost", 5630);
		if (result) {
			cout << "connected" << endl;
		}
		else {
			cout << "could not connect" << endl;
		}
		sock2.setBlocking(false);

		Packet packet;
		char data[64] = {0};
		packet.setReliable(false);

		mt19937 rne;
		uniform_int_distribution<unsigned> rng(1, 400);
		// send stuff with random sleep
		for (int i = 0; i < 10; i++) {
			sprintf(data, "%d", i);

			// send stuff
			packet.setData(data, sizeof(data));
			sock2.send(packet);

			// recv all msg
			Packet recp;
			while (sock2.receive(recp)) {
				lock_guard<mutex> lk(ioMutex);
				cout << sock2.getLocalPort() << ": " << (const char*)recp.getData() << endl;
			}

			this_thread::sleep_for(chrono::milliseconds(rng(rne)));
		}

		sock2.disconnect();
	});



	socketThread1.join();
	socketThread2.join();
}