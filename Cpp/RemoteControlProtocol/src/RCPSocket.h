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
#include <map>
#include "random_access_queue.h"

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
	friend class RcpTester;
	struct RcpHeader {
		uint32_t sequenceNumber;
		uint32_t batchNumber;
		uint32_t flags;
		static std::array<unsigned char, 12> serialize(RcpHeader h);
		static RcpHeader deserialize(const void* data, size_t size);
		inline std::array<unsigned char, 12> serialize() { return serialize(*this); }
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

	// returns status
	bool isConnected();

	// set blocking mode
	void setBlocking(bool isBlocking);

	uint16_t getLocalPort() { return socket.getLocalPort(); }

	std::string debug_PrintState();
	void debug_connect(std::string address, uint16_t port);
private:
	// network communication and synchronization
	void startIoThread();
	void stopIoThread();
	std::atomic_bool runIoThread;

	sf::UdpSocket socket; // communication socket
	sf::SocketSelector selector; // allows to wait for a certain time for this socket

	std::thread ioThread; // recieves packets constantly, orders and enqueues them for reading, sends keep alives
	random_access_queue<std::pair<Packet, bool>> recvQueue;
	std::condition_variable recvCondvar;
	struct ReservedInfo {
		size_t index;
		std::chrono::steady_clock::time_point sendTime;
	};
	using ReservedMapT = std::map<uint32_t, ReservedInfo>;
	ReservedMapT recvReserved; // batch number and index of reserved spaces in queue
	std::mutex socketMutex;
	std::chrono::steady_clock::time_point timeLastSend;

	struct RecentPacketInfo {
		RcpHeader header;
		std::vector<char> data;
		std::chrono::steady_clock::time_point send;
		std::chrono::steady_clock::time_point lastResend;
	};
	using RecentPacketMapT = std::unordered_map < uint32_t, RecentPacketInfo> ; // seq num, info
	RecentPacketMapT recentPackets; // set of recently sent reliable packets waiting to be ACKed

	// information about current session
	eState state; // current state of the connection
	uint32_t localSeqNum; // keeps track of the local sequence number, increase for each packet
	uint32_t localBatchNum; // keeps track of the local batch number, refresh for each reliable packet sent

	// information about current remote host
	uint32_t remoteSeqNum; // keeps track of the latest incoming packet's seqnum
	uint32_t remoteBatchNum; // keeps track of the latest reliable packet's batch num
	uint32_t remoteBatchNumReserved; // packets having this batch number or smaller should not cause reservation
	sf::IpAddress remoteAddress; // ip address of the remote partner
	uint16_t remotePort; // port of the remote partner
	
	// misc
	bool cancelOp;
	bool isBlocking;

	// bullshit
	static const unsigned TIMEOUT_TOTAL = 5000; // connection lost if no message for % ms
	static const unsigned TIMEOUT_SHORT = 200;
};








class RcpTester {
public:
	typedef RcpSocket::RcpHeader RcpHeader;
	enum eFlags {
		SYN = 1, // connection requested
		ACK = 2, // acknowledged
		FIN = 4, // no more messages
		KEP = 8, // keep alive
		REL = 16, // reliable packet, send back ack
	};

	RcpTester();

	bool bind(uint16_t localPort);
	bool send(RcpHeader header, void* data, size_t dataSize, std::string address, uint32_t port);
	bool receive(Packet& packet, RcpHeader& header);
private:
	sf::UdpSocket socket;
};