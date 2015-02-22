#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <mutex>
#include "../../RemoteControlProtocol/src/Packet.h"
#include "../../RemoteControlProtocol/src/RcpSocket.h"

#include <conio.h>

using namespace std;


int main() {
	RcpSocket sock1;
	RcpSocket sock2;
	mutex ioMutex;

	bool isBound = false;
	isBound = sock1.bind(5630) && sock2.bind(5631);
	cout << "s1: " << sock1.getLocalPort() << endl;
	cout << "s2: " << sock2.getLocalPort() << endl;
	
	if (!isBound) {
		cout << "binding failed lel" << endl;
		return 1;
	}

	thread socketThread1(
		[&]() {
			sock1.connect("localhost", 5631);
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
		sock2.connect("localhost", 5630);
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
	
	cout << "Press any key to exit...\n";
	_getch();
	return 0;
}