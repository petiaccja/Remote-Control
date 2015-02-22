#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <array>
#include <atomic>
#include <unordered_map>

#include <SFML/Network.hpp>

#include "Packet.h"


////////////////////////////////////////////////////////////////////////////////
// How it works.
//
//	*Threads*
//	There are 2 threads.
//	The first thread is the user's thread, on which calls to send and other
//	function are done. Nothing unusual.
//	The second thread is responsible for timing and tasks that need to be performed
//	even if there's no explicit user interaction (such as keepalives).
//	This threading scheme comes alive after the connection is established via either
//	accept or connect, and lives until disconnect is called.
//	
//	*Data flow*
//	Sending messages is done directly in send, nothing special.
//	However, recieving is done in the second thread, in an infinite loop.
//	The messages are then pushed into a queue, from which the recieve function
//	takes one out. The second thread also performs filtering and processing of
//	the messages. Protocol messages, such as keepalives are not pushed to the queue.
//	Acks are directly sent from this thread. If a batch number references a packet
//	that is not recieved yet, a place in the queue is reserved for it. The queue
//	is not committed until that packet arrives.
//
//	*Locking*
//	...
////////////////////////////////////////////////////////////////////////////////

class RcpSocket {
private:
	struct RcpHeader {
		uint32_t sequenceNumber;
		uint32_t batchNumber;
		uint32_t flags;
		static std::array<unsigned char, 12> serialize(RcpHeader h);
		static RcpHeader deserialize(const void* data, size_t size);
	};
	enum eFlags {
		SYN = 1, // connection requested
		ACK = 2, // acknowledged
		FIN = 4, // no more messages
		KEP = 8, // keep alive
		REL = 16, // reliable packet, send back ack
	};
public:
	enum eState {
		DISCONNECTED,
		CONNECTED,
	};

	RcpSocket();
	~RcpSocket();

	bool bind(uint16_t port);
	void unbind();

	// establish connection
	bool accept(); // long time
	bool connect(std::string address, uint16_t port); // some time

	// communicate
	bool send(const void* data, size_t size, bool reliable); // instant
	bool send(Packet& packet); // instant
	bool receive(Packet& packet); // long time

	// gracefully close connection
	void disconnect(); // short time

	// cancel pending operations (call from another thread)
	void cancel();

	// set blocking mode
	void setBlocking(bool isBlocking);

	uint16_t getLocalPort() { return socket.getLocalPort(); }
private:
	// network communication and synchronization
	void startIoThread();
	void stopIoThread();
	std::atomic_bool runIoThread;

	sf::UdpSocket socket; // communication socket
	sf::SocketSelector selector; // allows to wait for a certain time for this socket

	std::thread ioThread; // recieves packets constantly, orders and enqueues them for reading, sends keep alives
	std::queue<Packet> recvQueue;
	std::condition_variable recvCondvar;
	std::atomic_bool recvPred;
	std::mutex socketMutex;
	std::chrono::steady_clock::time_point timeLastSend;

	struct RecentPacketInfo {
		Packet packet;
		std::chrono::steady_clock::time_point send;
		std::chrono::steady_clock::time_point lastResend;
	};
	using RecentPacketMapT = std::unordered_map < uint32_t, RecentPacketInfo> ;
	RecentPacketMapT recentPackets; // set of recently sent reliable packets waiting to be ACKed

	// information about current session
	eState state;
	uint32_t localSeqNum;
	uint32_t localBatchNum;

	// information about current remote host
	uint32_t remoteSeqNum;
	sf::IpAddress remoteAddress;
	uint16_t remotePort;
	
	// misc
	bool cancelOp;
	bool isBlocking;

	// bullshit
	const unsigned timedOut = 5000; // connection lost if no message for % ms
};