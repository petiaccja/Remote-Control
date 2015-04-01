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

	// Custructors & Destructor
	RcpSocket();
	~RcpSocket();

	// Modifiers
	bool bind(uint16_t port);
	void unbind();
	bool isConnected() const;
	void setBlocking(bool isBlocking);
	bool getBlocking() const;
	void cancel();
	uint16_t getLocalPort() const { return socket.getLocalPort(); }

	// Connection setup
	bool accept();
	bool connect(std::string address, uint16_t port);
	void disconnect();

	// Traffic
	bool send(const void* data, size_t size, bool reliable);
	bool send(Packet& packet);
	bool receive(Packet& packet);
	
	// Debug
	std::string debug_PrintState();
	void debug_connect(std::string address, uint16_t port);
private:
	// --- Network resources --- //

	sf::UdpSocket socket; // communication socket
	sf::SocketSelector selector; // allows to wait for a certain time for this socket

	// --- IO thread --- //

	// this thread performs background socket communication
	std::thread ioThread;
	void startIoThread();
	void stopIoThread();
	std::atomic_bool runIoThread;
	void ioThreadFunction();

	// --- Traffic data structures --- //

	// Incoming packets	
	random_access_queue<std::pair<Packet, bool>> recvQueue; // recieved valid packets are put here
	std::condition_variable recvCondvar;	// notified when stuff is recieved

	// Incoming packet place reservation
	struct ReservedInfo {
		size_t index;
		std::chrono::steady_clock::time_point sendTime;
	};
	using ReservedMapT = std::map<uint32_t, ReservedInfo>;
	ReservedMapT recvReserved; // batch number and index-in-recvQueue of reserved places
	uint32_t remoteBatchNumReserved; // packets having this batch number or smaller should not cause reservation

	// Packets waiting to be ACK'd
	struct RecentPacketInfo {
		RcpHeader header;
		std::vector<char> data;
		std::chrono::steady_clock::time_point send;
		std::chrono::steady_clock::time_point lastResend;
	};
	using RecentPacketMapT = std::unordered_map < uint32_t, RecentPacketInfo> ; // seq num, info
	RecentPacketMapT recentPackets; // set of recently sent reliable packets waiting to be ACKed

	// --- Session description --- //
	// Local state
	eState state; // current state of the connection
	uint32_t localSeqNum; // keeps track of the local sequence number, increase for each packet
	uint32_t localBatchNum; // keeps track of the local batch number, refresh for each reliable packet sent

	// Remote partner's state
	uint32_t remoteSeqNum; // keeps track of the latest incoming packet's seqnum
	uint32_t remoteBatchNum; // keeps track of the latest reliable packet's batch num
	sf::IpAddress remoteAddress; // ip address of the remote partner
	uint16_t remotePort; // port of the remote partner
	
	// --- Miscallaneous --- //
	std::mutex socketMutex; // lock whenever accessing data shared b/w main & IO thread
	std::chrono::steady_clock::time_point timeLastSend; // the time of last packet send, including ACKs & KEPs

	bool cancelOp; // uhm, it is supposed to cancel lengthy operation that are in progress, but I don't know yet
	bool isBlocking; // sets if calls block caller or return immediatly

	// Well, remove this shit from here and make it configurable and tidy
	static const unsigned TIMEOUT_TOTAL = 5000; // connection lost if no message for % ms
	static const unsigned TIMEOUT_SHORT = 200; // resend packet, resend kep, granularity of longer operations
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