#include "tests.h"

#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <mutex>
#include <chrono>
#include "../../RemoteControlProtocol/src/Packet.h"
#include "../../RemoteControlProtocol/src/RcpSocket.h"
#include <future>
#include <SFML/Network.hpp>

#ifdef _MSC_VER
#include <conio.h>
#else 
char _getch() {
	return cin.get();
}
#endif

using namespace std;

void CommunicationTest();


// SocketTester
// Contains several methods to test critical points of the socket manually with an RcpTester.
class SocketTester {
public:
	// variables
	RcpSocket socket;
	RcpTester tester;
	Packet packet;
	RcpTester::RcpHeader header;
	RcpTester::RcpHeader send_header;

	// Recieve stuff on the RcpTester
	bool recieve() {
		for (int i = 0; i < 10; i++) {
			if (tester.receive(packet, header)) {
				return true;
			}
			this_thread::sleep_for(chrono::milliseconds(100));
		}
		return false;
	};
	// Recieve a packet on tester and print it
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
				isFirst = false;
			}
			if (header.flags & RcpTester::REL) {
				if (!isFirst)
					cout << " | ";
				cout << "REL";
				isFirst = false;
			}
			if (header.flags & RcpTester::KEP) {
				if (!isFirst)
					cout << " | ";
				cout << "KEP";
				isFirst = false;
			}
			if (header.flags & RcpTester::SYN) {
				if (!isFirst)
					cout << " | ";
				cout << "SYN";
				isFirst = false;
			}
			if (header.flags & RcpTester::FIN) {
				if (!isFirst)
					cout << " | ";
				cout << "FIN";
				isFirst = false;
			}
			cout << endl << endl;
			return true;
		}
		else {
			return false;
		}
	};

	// Print the state of the socket
	void PrintState() { cout << socket.debug_PrintState() << endl << endl; };

	// Sleep for some milliseconds
	void Sleep(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); };

	// Ctor
	SocketTester() {
		// bind and init stuff
		socket.bind(5630);
		tester.bind(5631);
	}

	// Dtor
	~SocketTester() {
	}


	// Tests the reordering of reliable packets
	void ReorderingTest() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|------------------ ReorderingTest -----------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		socket.debug_connect("localhost", 5631);


		uint32_t index = 0;

		// send some packets to the socket
		tester.send({ 0, 0, 0 }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 5, 4, 0 }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 6, 4, 0 }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 1, 1, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 3, 3, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		// resend reliable packet: duplication with reserved spaces
		tester.send({ 3, 3, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		Packet p;
		cout << "-----------------------------\n";
		socket.receive(p);
		socket.receive(p);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}
		// --
		Sleep(100);

		tester.send({ 2, 2, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 4, 4, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		// resend reliable packet: duplication without reserved spaces
		tester.send({ 4, 4, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}

		tester.send({ 4, 4, RcpTester::REL }, &++index, 4, "localhost", 5630);
		Sleep(100);
		PrintState();
		//while (PrintRecieve()) {}
		// --

		tester.send({ 7, 7, RcpTester::REL }, &++index, 4, "localhost", 5630);

		Sleep(100);
		PrintState();

		socket.setBlocking(false);
		cout << "Packets recieved by socket:" << endl;
		while (socket.receive(p)) {
			cout << p.getSequenceNumber() << " ";
		}
		cout << endl;


		socket.debug_kill();
	}

	// Tests reliable packets and ack to them
	void ReliableTest() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|------------------- ReliableTest ------------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		socket.debug_connect("localhost", 5631);

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
		tester.send({ 0, 1, RcpTester::ACK }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}
		PrintState();

		this_thread::sleep_for(chrono::milliseconds(200));
		socket.debug_kill();
	}

	// Test closing of the socket
	void CloseTest() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|-------------------- CloseTest --------------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		// NORMAL CLOSING
		cout << "--------------------------------------\n";
		cout << "Normal closing:\n\n";

		socket.debug_connect("localhost", 5631);

		// send a packet to simulate some communication
		tester.send({ 0, 0, RcpTester::REL }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		// send a FIN
		tester.send({ 0, 0, RcpTester::FIN }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		// send ACK
		tester.send({ 0, 0, RcpTester::ACK }, nullptr, 0, "localhost", 5630);

		// print socket state
		Sleep(100);
		while (PrintRecieve()) {}
		PrintState();
		socket.debug_kill();


		// FORGET TO SEND ACK
		cout << "--------------------------------------\n";
		cout << "Last ACK lost:\n\n";

		socket.debug_connect("localhost", 5631);

		// send a packet to simulate some communication
		tester.send({ 0, 0, RcpTester::REL }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		// send a FIN
		tester.send({ 0, 0, RcpTester::FIN }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		// forget to send ack

		// print socket state
		cout << "Immediatly after FIN" << endl;
		PrintState();

		// wait until timeout and check socket state again
		cout << "After 6000 ms" << endl;
		Sleep(6000);
		while (PrintRecieve()) {}
		PrintState();
		socket.debug_kill();


		// NOT KEPT ALIVE
		cout << "--------------------------------------\n";
		cout << "Not kept alive:\n\n";

		socket.debug_connect("localhost", 5631);

		// send a packet to simulate some communication
		tester.send({ 0, 0, RcpTester::REL }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		// print socket state
		PrintState();

		// wait until timeout and check socket state again
		Sleep(6000);
		cout << "Not kept alive for 6000 ms:" << endl;
		while (PrintRecieve()) {}
		PrintState();
		socket.debug_kill();


		// DISCONNECT THE SOCKET NORMALLY
		cout << "--------------------------------------\n";
		cout << "Disconnect the socket itself normally:\n\n";

		socket.debug_connect("localhost", 5631);

		PrintState();

		cout << "Disconnect initiated on socket:\n";
		std::future<void> f1 = std::async([&] {socket.disconnect();});
		Sleep(100);
		PrintRecieve();

		Sleep(100);
		tester.send({ 0, 0, RcpTester::FIN | RcpTester::ACK }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		cout << "FIN/ACK sent to socket:\n";
		PrintState();

		f1.wait();
		cout << "Disconnect returned" << endl;
		while (PrintRecieve()) {}
		PrintState();
		socket.debug_kill();

		// DISCONNECT THE SOCKET FORGET FIN/ACK
		cout << "--------------------------------------\n";
		cout << "Disconnect the socket forget FIN/ACK:\n\n";

		socket.debug_connect("localhost", 5631);

		PrintState();

		cout << "Disconnect initiated on socket:\n";
		std::future<void> f2 = std::async([&] {socket.disconnect();});
		Sleep(100);
		PrintRecieve();

		f2.wait();
		cout << "Disconnect returned" << endl;
		while (PrintRecieve()) {}
		PrintState();
		socket.debug_kill();
	}

	void ConnectTest() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|------------------- ConnectTest -------------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		// CONNECT NORMALLY
		cout << "--------------------------------------\n";
		cout << "Connect normally:\n\n";

		PrintState();

		std::future<void> f1 = std::async([&] {socket.accept();});

		Sleep(100);
		tester.send({ 1000, 10, RcpTester::SYN }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}
		tester.send({ 1001, 10, RcpTester::ACK }, nullptr, 0, "localhost", 5630);

		f1.wait();
		PrintState();

		socket.debug_kill();
		Sleep(100);
		while (PrintRecieve()) {}

		// CONNECT FORGET ACK
		cout << "--------------------------------------\n";
		cout << "Connect forget ACK:\n\n";

		PrintState();

		std::future<void> f2 = std::async([&] {socket.accept();});

		Sleep(100);
		tester.send({ 1000, 10, RcpTester::SYN }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		f2.wait();
		PrintState();

		while (PrintRecieve()) {}
		socket.debug_kill();

		// CONNECT FROM SOCKET NORMAL
		cout << "--------------------------------------\n";
		cout << "Connect from socket normal:\n\n";

		PrintState();

		std::future<void> f3 = std::async([&] {socket.connect("localhost", 5631);});

		Sleep(100);
		while (PrintRecieve()) {}
		tester.send({ 1000, 10, RcpTester::SYN | RcpTester::ACK }, nullptr, 0, "localhost", 5630);
		Sleep(100);
		while (PrintRecieve()) {}

		f3.wait();
		PrintState();

		while (PrintRecieve()) {}
		socket.debug_kill();

		// CONNECT FROM SOCKET FORGET SYN/ACK
		cout << "--------------------------------------\n";
		cout << "Connect from socket forget SYN/ACK:\n\n";

		PrintState();

		std::future<void> f4 = std::async([&] {socket.connect("localhost", 5631);});

		Sleep(100);
		while (PrintRecieve()) {}

		f4.wait();
		PrintState();

		while (PrintRecieve()) {}
		socket.debug_kill();
	}


	void ReservedStarvation() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|---------------- ReservedStarvation ---------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		socket.debug_connect("localhost", 5631);

		PrintState();

		tester.send({ 0, 1, RcpTester::REL }, nullptr, 0, "localhost", 5630);
		tester.send({ 4, 4, RcpTester::REL }, nullptr, 0, "localhost", 5630);
		Sleep(100);

		PrintState();

		Sleep(3000);
		tester.send({ 1, 0, RcpTester::KEP }, nullptr, 0, "localhost", 5630);
		Sleep(3000);

		PrintState();

		socket.debug_kill();
	}

	void AckStarvation() {
		cout << "+---------------------------------------------------+" << endl;
		cout << "|------------------ AckStarvation ------------------|" << endl;
		cout << "+---------------------------------------------------+" << endl << endl;

		socket.debug_connect("localhost", 5631);

		PrintState();

		socket.send(nullptr, 0, true);
		socket.send(nullptr, 0, false);
		socket.send(nullptr, 0, true);

		tester.send({ 0, 1, RcpTester::ACK }, nullptr, 0, "localhost", 5630);
		Sleep(100);

		PrintState();

		Sleep(3000);
		tester.send({ 1, 0, RcpTester::KEP }, nullptr, 0, "localhost", 5630);
		Sleep(3000);

		PrintState();

		socket.debug_kill();
	}
};



void CommunicationTest() {
	cout << "+---------------------------------------------------+" << endl;
	cout << "|---------------- CommunicationTest ----------------|" << endl;
	cout << "+---------------------------------------------------+" << endl << endl;

	RcpSocket sock1;
	RcpSocket sock2;
	mutex ioMutex;

	sock1.debug_enableLog(true);
	sock2.debug_enableLog(true);

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
			//cout << "accepted" << endl;
		}
		else {
			//cout << "could not accept connection" << endl;
		}
		sock1.setBlocking(false);

		Packet packet;
		char data[64] = { 0 };
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
				//cout << sock1.getLocalPort() << ": " << (const char*)recp.getData() << endl;
			}

			this_thread::sleep_for(chrono::milliseconds(rng(rne)));
		}

		cout << "disconnecting...\n";
		sock1.disconnect();
	});

	thread socketThread2(
		[&]() {
		bool result;
		result = sock2.connect("localhost", 5630);
		if (result) {
			//cout << "connected" << endl;
		}
		else {
			//cout << "could not connect" << endl;
		}
		sock2.setBlocking(false);

		Packet packet;
		char data[64] = { 0 };
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
				//cout << sock2.getLocalPort() << ": " << (const char*)recp.getData() << endl;
			}

			this_thread::sleep_for(chrono::milliseconds(rng(rne)));
		}
	});



	socketThread1.join();
	socketThread2.join();
}




////////////////////////////////////////////////////////////////////////////////
// MAIN

int RcpTest() {
	/*/
	{
		SocketTester tester;

		tester.ReliableTest();
		tester.ReorderingTest();
		tester.AckStarvation();
		tester.ReservedStarvation();
		tester.ConnectTest();
		tester.CloseTest();
	}
	//*/
	//*/
	{
		CommunicationTest();
	}
	//*/
	cout << "Press any key to exit...\n";
	_getch();
	return 0;
}
//
////////////////////////////////////////////////////////////////////////////////