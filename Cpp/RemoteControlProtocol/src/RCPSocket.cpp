#include "RCPSocket.h"

#include <iostream>
#include <sstream>


using namespace std;
using namespace std::chrono; // long names were unreadable :(


////////////////////////////////////////////////////////////////////////////////
// Constructors and Destructor

RcpSocket::RcpSocket() {
	state = DISCONNECTED;
	socket.setBlocking(false);
	cancelOp = false;
	localSeqNum = localBatchNum = 0;
	remoteSeqNum = remoteBatchNum = 0;
	remoteBatchNumReserved = remoteBatchNum;
}

RcpSocket::~RcpSocket() {
	if (ioThread.joinable()) {
		ioThread.join();
	}
	disconnect();
}


////////////////////////////////////////////////////////////////////////////////
// Modifiers

bool RcpSocket::bind(uint16_t port) {
	if (state == CONNECTED) {
		return false;
	}
	bool success = socket.bind(port) == sf::UdpSocket::Done;
	if (success) {
		selector.add(socket);
		return true;
	}
	else {
		return false;
	}
}

void RcpSocket::unbind() {
	if (state != CONNECTED) {
		socket.unbind();
		selector.clear();
	}
}

void RcpSocket::cancel() {
	// TODO
}

bool RcpSocket::isConnected() const {
	return state == CONNECTED;
}

void RcpSocket::setBlocking(bool isBlocking) {
	this->isBlocking = isBlocking;
}

bool RcpSocket::getBlocking() const {
	return isBlocking;
}

////////////////////////////////////////////////////////////////////////////////
// Connection setup

bool RcpSocket::accept() {
	if (state != DISCONNECTED) {
		return false;
	}

	// set local parameters
	localSeqNum = 15000;
	localBatchNum = 500;

	// do handshake
	// - wait for SYN
	// - send SYN-ACK
	// - wait for ACK
	RcpHeader header;
	std::array<uint8_t, 12> headerSer;
	sf::Packet packet;
	sf::IpAddress responseAddress;
	uint16_t responsePort;

	// - wait for SYN
	bool isData = false;
	while (!(isData = selector.wait(sf::milliseconds(TIMEOUT_SHORT)))) {
		// empty //
	}
	if (!isData) {
		return false;
	}
	socket.receive(packet, remoteAddress, remotePort); // intentionally read into remoteWhatever
	if (packet.getDataSize() < 12) {
		cout << "not enough data" << endl;
		return false;
	}
	header = RcpHeader::deserialize(packet.getData(), 12);
	if (header.flags != SYN) {
		cout << "not a SYN" << endl;
		return false;
	}
	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;

	// - send SYN-ACK
	header.sequenceNumber = localSeqNum++;
	header.batchNumber = localBatchNum;
	header.flags = SYN | ACK;
	headerSer = header.serialize();
	socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);

	// - wait for ACK
	if (!selector.wait(sf::milliseconds(TIMEOUT_TOTAL))) {
		cout << "ack did not arrive" << endl;
		return false;
	}
	socket.receive(packet, responseAddress, responsePort);
	if (packet.getDataSize() < 12 || remoteAddress != responseAddress || remotePort != responsePort) {
		cout << "not enough data, response from invalid address";
		return false;
	}
	header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());
	if (header.flags != ACK || header.sequenceNumber != remoteSeqNum + 1 || header.batchNumber != remoteBatchNum) {
		cout << "not ACK, seq or batch numbers not corresponding";
		return false;
	}
	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;

	// succesful connection
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();

	return true;
}

bool RcpSocket::connect(std::string address, uint16_t port) {
	if (state != DISCONNECTED) {
		return false;
	}

	// set remote parameters
	remoteAddress = address;
	remotePort = port;

	// set local parameters
	localSeqNum = 10000;
	localBatchNum = 100;

	// do handshake
	// - send SYN
	// - wait for SYN-ACK
	// - send ACK
	RcpHeader header;
	std::array<uint8_t, 12> headerSer;
	sf::Packet packet;
	sf::IpAddress responseAddress;
	uint16_t responsePort;

	// - send SYN
	header.sequenceNumber = localSeqNum++; // initial local sequence number
	header.batchNumber = localBatchNum; // initial local batch number
	header.flags = SYN;

	headerSer = header.serialize();
	socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);

	// - wait for SYN-ACK
	if (!selector.wait(sf::milliseconds(TIMEOUT_TOTAL))) {
		cout << "request timed out" << endl;
		return false;
	}
	socket.receive(packet, responseAddress, responsePort);
	if (responseAddress != remoteAddress || responsePort != remotePort || packet.getDataSize() < 12) {
		cout << "not remote host, wrong size" << endl;
		return false;
	}
	header = RcpHeader::deserialize(packet.getData(), 12);
	if ((header.flags & (SYN | ACK)) < 2) {
		cout << "syn-ack expected but not recieved";
		return false;
	}

	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;

	// send ACK
	header.sequenceNumber = localSeqNum++;
	header.batchNumber = localBatchNum;
	header.flags = ACK;

	headerSer = header.serialize();
	socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);

	// succesful connection
	// note that the other party will drop connection soon if he did not recieve our last ACK
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();

	return true;
}

void RcpSocket::disconnect() {
	// only when connected
	if (state != CONNECTED) {
		return;
	}
	// stop io thread
	stopIoThread();

	// perform closing procedure
	// - send FIN
	// - wait for FIN/ACK
	// - send ACK
	// TODO...

	// set disconnected state
	state = DISCONNECTED;
}



////////////////////////////////////////////////////////////////////////////////
// Traffic

bool RcpSocket::send(const void * data, size_t size, bool reliable) {
	Packet p;
	p.setData(data, size);
	p.setReliable(reliable);
	return send(p);
}

bool RcpSocket::send(Packet& packet) {
	// check errors
	if (state != CONNECTED) {
		return false;
	}
	if (packet.getDataSize() > sf::UdpSocket::MaxDatagramSize - 12) {
		false;
	}

	// create socket header + data block
	unsigned char rawData[sf::UdpSocket::MaxDatagramSize];
	memcpy(rawData + 12, packet.getData(), packet.getDataSize());

	RcpHeader header;
	header.sequenceNumber = localSeqNum;
	header.batchNumber = localBatchNum;
	header.flags = packet.isReliable() ? REL : 0;
	auto hseq = RcpHeader::serialize(header);
	memcpy(rawData, hseq.data(), hseq.size()); // size must be 12, but let's be tidy

											   // only one thread can access the socket and other variables at the same time
	std::lock_guard<std::mutex> lk(socketMutex);

	// send data on socket
	auto sfStatus = socket.send(rawData, packet.getDataSize() + 12, remoteAddress, remotePort);
	if (sfStatus != sf::Socket::Done) {
		return false;
	}

	// add packet to list of ack waiting packets
	auto sendTime = steady_clock::now();
	if (packet.isReliable()) {
		recentPackets.insert(RecentPacketMapT::value_type(
			header.batchNumber,
			{ header, std::vector<char>((const char*)packet.getData(), packet.getDataSize() + (const char*)packet.getData()), sendTime, sendTime })
			);
	}

	// set next batch and sequence numbers
	if (packet.isReliable()) {
		localBatchNum++;
	}
	localSeqNum++;

	// set last sending time to manage timeouts
	timeLastSend = steady_clock::now();

	return true;
}


bool RcpSocket::receive(Packet& packet) {
	// check errors
	if (state != CONNECTED) {
		return false;
	}

	// wait for packet queue to have data

	// lock mutex
	std::unique_lock<std::mutex> lk(socketMutex);

	// wait on condvar
	bool isData = false;
	auto IsDataPred = [this] { return recvQueue.size() > 0 && recvQueue.front().second == true; };
	if (isBlocking) {
		while (!cancelOp && !isData) {
			isData = recvCondvar.wait_for(lk, milliseconds(TIMEOUT_SHORT), IsDataPred);
		}
	}
	else {
		isData = IsDataPred();
	}

	if (!isData) {
		lk.unlock();
		return false;
	}

	// now we have the mutex again, modifying queue is safe
	assert(recvQueue.size() > 0); // failure means a bug in code, as this should never happen
	packet = recvQueue.front().first;
	recvQueue.pop();

	// remove packet from reserved list if it's contained
	recvReserved.erase(packet.sequenceNumber);
	// decrease all indices of reserved spaces
	for (auto it : recvReserved) {
		it.second.index--;
	}

	// finally, unlock mutex
	lk.unlock();

	return true;
}


void RcpSocket::startIoThread() {
	if (ioThread.joinable()) {
		ioThread.join();
	}
	runIoThread = true;
	ioThread = std::thread([this] { ioThreadFunction(); });
}


void RcpSocket::stopIoThread() {
	runIoThread = false;
	if (ioThread.joinable()) {
		ioThread.join();
	}
}

void RcpSocket::ioThreadFunction() {
	// This function in brief:
	// 1. Select the event closest in time, and wait that time
	//		Events are the following (event -> action on timout):
	//			- [1] Check if any reliable packet should be resent -> resend packet
	//			- [2] Check recvQueue places (expecting incoming reliable packet) -> connection lost
	//			- [3] Check outbound rel. packets without remote's acknowledgement -> lost connection
	//			- [4] Keep connection alive (timeLastSend) -> send a keepalive
	//			- [5] Total timeout (connection not kept alive by remote) -> lost connection
	// 2.1. Process incoming message if wait was interrupted
	//			- pump the incoming message to the queue
	// 2.2. Act according to what event timed out 

	steady_clock::time_point timeLastRecieved = steady_clock::now();

	while (runIoThread) {
		// ------------------------------------ //
		// --- Select closest event in time --- //
		// ------------------------------------ //

		// Event definitions
		enum eClosestEventType {
			ACK_RESEND,
			ACK_TIMEOUT,
			KEEPALIVE,
			RECV_TIMEOUT,
			RESERVE_TIMEOUT,
			RELOOP,
		};
		microseconds eventRemaining(TIMEOUT_SHORT * 1000);
		eClosestEventType eventType = RELOOP;

		steady_clock::time_point now = steady_clock::now();

		steady_clock::time_point oldestResend = now;
		steady_clock::time_point oldestSend = now;
		uint32_t resendBatchnum = 0; // this reliable packet has to be resent
		RecentPacketInfo* resendInfo = nullptr; // information about the packet to be resent
		bool isAckResend = false; // resend packet waiting for ack
		bool isAckTimedout = false; // packet waiting for ack timed out

		// lock mutex
		socketMutex.lock();

		// [1] Check if any reliable packet should be resent
		for (auto item : recentPackets) {
			if (item.second.lastResend <= oldestResend) {
				oldestResend = item.second.lastResend;
				resendBatchnum = item.first;
				isAckResend = true;
			}
			if (item.second.send <= oldestSend) {
				oldestSend = item.second.send;
				isAckTimedout = true;
			}
		}
		if (isAckResend) {
			auto it = recentPackets.find(resendBatchnum);
			resendInfo = &it->second;
			eventRemaining = duration_cast<microseconds>(oldestResend + milliseconds(TIMEOUT_SHORT) - now);
			eventType = ACK_RESEND;
		}

		// [2] Check recvQueue reserved places
		microseconds reservedRemaining;
		bool isReservedTimeout = false;
		for (auto it : recvReserved) {
			auto rem = now - it.second.sendTime + milliseconds(TIMEOUT_TOTAL);
			reservedRemaining = duration_cast<milliseconds>(reservedRemaining < rem ? reservedRemaining : rem);
			bool isReservedTimeout = true;
		}
		if (isReservedTimeout && reservedRemaining < eventRemaining) {
			eventRemaining = reservedRemaining;
			eventType = RESERVE_TIMEOUT;
		}

		// unlock mutex
		socketMutex.unlock();

		// [3] Check if we have an outbound reliable packet that was not ackowledged in time
		microseconds ackTimeoutRemaining;
		ackTimeoutRemaining = duration_cast<microseconds>(oldestSend + milliseconds(TIMEOUT_TOTAL) - now);
		if (isAckTimedout && ackTimeoutRemaining < eventRemaining) {
			eventRemaining = ackTimeoutRemaining;
			eventType = ACK_TIMEOUT;
		}
		// [4] Keep connection alive
		microseconds keepaliveRemaining;
		keepaliveRemaining = duration_cast<microseconds>(timeLastSend + milliseconds(TIMEOUT_SHORT) - now);
		if (keepaliveRemaining < eventRemaining) {
			eventRemaining = keepaliveRemaining;
			eventType = KEEPALIVE;
		}
		// [5] Check total timeout
		microseconds timeoutRemaining = duration_cast<microseconds>(timeLastRecieved + milliseconds(TIMEOUT_TOTAL) - now);
		if (eventRemaining > timeoutRemaining) {
			eventRemaining = timeoutRemaining;
			eventType = RECV_TIMEOUT;
		}

		// -------------------------------- //
		// --- Get incoming socket data --- //
		// -------------------------------- //

		// Calculate time in microseconds Selector should wait at most 
		long long usSleep = eventRemaining.count();
		usSleep = std::max(usSleep, 1ll); // for SFML, 0 means infinity, hence the "1"

		// Wait on the socket for incoming UDP traffic
		bool isData = selector.wait(sf::microseconds(usSleep));

		// ------------------------------------- //
		// --- Process incoming data, if any --- //
		// ------------------------------------- //

		// No data, perform timeout action described above
		if (!isData) {
			std::lock_guard<std::mutex> lk(socketMutex); // lock mutex (guarded)

			switch (eventType)
			{
				case RELOOP:
					continue;
				case ACK_RESEND: {
					// don't use the send function: it cannot lock the mutex, performs other unneeded stuff, etc...
					char buffer[sf::UdpSocket::MaxDatagramSize];
					auto hdrSer = RcpHeader::serialize(resendInfo->header);
					size_t bufferSize = resendInfo->data.size() + hdrSer.size();
					assert(bufferSize <= sf::UdpSocket::MaxDatagramSize); // should not be allowed in send socket function in the first place
					memcpy(buffer, hdrSer.data(), hdrSer.size());
					memcpy(buffer + hdrSer.size(), resendInfo->data.data(), resendInfo->data.size());
					socket.send(buffer, bufferSize, remoteAddress, remotePort);
					timeLastSend = steady_clock::now();
					resendInfo->lastResend = timeLastSend;
					break;
				}

				case KEEPALIVE: {
					RcpHeader header;
					header.sequenceNumber = localSeqNum;
					header.batchNumber = localBatchNum;
					header.flags = KEP;
					localSeqNum++;
					auto hseq = RcpHeader::serialize(header);
					socket.send(hseq.data(), hseq.size(), remoteAddress, remotePort);
					timeLastSend = steady_clock::now();

					break;
				}
				case RESERVE_TIMEOUT:
				case ACK_TIMEOUT:
				case RECV_TIMEOUT: {
					// forcefully close the socket
					state = DISCONNECTED;
					return;
					break;
				}
			}
		}
		// Process incoming packet
		else {
			// lock mutex (guarded)
			std::lock_guard<std::mutex> lk(socketMutex);

			// extract data from socket
			sf::Packet rawPacket;
			sf::IpAddress address;
			uint16_t port;
			socket.receive(rawPacket, address, port);

			// extract rcp header and payload from packet
			if (rawPacket.getDataSize() < 12) {
				continue;
			}
			RcpHeader header = RcpHeader::deserialize(rawPacket.getData(), 12);
			Packet packet;
			packet.setData((char*)rawPacket.getData() + 12, rawPacket.getDataSize() - 12);
			packet.reliable = (header.flags & REL) != 0;
			packet.sequenceNumber = header.sequenceNumber;

			// filter out false packets:
			// - address must be correct
			// - port must be correct
			// - sequence number must be reasonable
			if (address != remoteAddress || port != remotePort) {
				continue;
			}

			// set time of last valid packet
			timeLastRecieved = steady_clock::now();

			// handle special packets
			// got a keepalive: skip packet
			if (header.flags & KEP) {
				std::cout << getLocalPort() << ": kep" << std::endl;
				continue;
			}
			// recent packet was acknowledged: remove from waiting list
			else if (header.flags & ACK) {
				std::cout << getLocalPort() << ": ack: " << header.batchNumber << std::endl;
				// remove the packet from the recent ack list
				auto it = recentPackets.find(header.batchNumber); // ack packets batch number contains the acknowledged packet's b.n.
				if (it != recentPackets.end()) {
					recentPackets.erase(it);
				}
				continue;
			}
			// got a reliable packet: send an ack
			else if (header.flags & REL) {
				std::cout << getLocalPort() << ": rel: " << header.sequenceNumber << ":" << header.batchNumber << std::endl;
				// send an acknowledgement
				RcpHeader ackHeader;
				ackHeader.sequenceNumber = header.sequenceNumber;
				ackHeader.batchNumber = header.batchNumber;
				ackHeader.flags = ACK;
				auto ackData = RcpHeader::serialize(ackHeader);
				socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
			}

			// reserve space(s) in queue if batch number references a packet that has not arrived yet
			for (int i = 0; remoteBatchNumReserved < header.batchNumber && i < (ptrdiff_t)header.batchNumber - (ptrdiff_t)remoteBatchNum; i++) {
				recvReserved.insert(ReservedMapT::value_type(remoteBatchNum + i, { recvQueue.size(), steady_clock::now() }));
				recvQueue.push({ Packet(), false });
				remoteBatchNumReserved++;
			}

			// set counters for remote host
			remoteSeqNum = std::max(remoteSeqNum, packet.getSequenceNumber());
			if (packet.isReliable()) {
				remoteBatchNum = std::max(remoteBatchNum, header.batchNumber + 1);
			}

			// fill space if there's any reserved for this packet
			ReservedMapT::iterator it;
			if (header.flags & REL && header.batchNumber < remoteBatchNum) {
				if ((it = recvReserved.find(header.batchNumber)) != recvReserved.end()) {
					recvQueue[it->second.index].first = std::move(packet); // don't use this packet again; for performance reasons
					recvQueue[it->second.index].second = true;
					recvReserved.erase(it);
				}
				else {
					continue;
				}
			}
			// simply push packet to queue
			else {
				recvQueue.push({ packet, true });
			}

			// unlock mutex (lock_guard) and notify
			recvCondvar.notify_all();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// Helpers

std::array<unsigned char, 12> RcpSocket::RcpHeader::serialize(RcpHeader h) {
	std::array<unsigned char, 12> v;
	v[0] = h.sequenceNumber >> 24;
	v[1] = h.sequenceNumber >> 16;
	v[2] = h.sequenceNumber >> 8;
	v[3] = h.sequenceNumber;

	v[4] = h.batchNumber >> 24;
	v[5] = h.batchNumber >> 16;
	v[6] = h.batchNumber >> 8;
	v[7] = h.batchNumber;

	v[8] = h.flags >> 24;
	v[9] = h.flags >> 16;
	v[10] = h.flags >> 8;
	v[11] = h.flags;

	return v;
}

auto RcpSocket::RcpHeader::deserialize(const void* data, size_t size) -> RcpHeader {
	RcpHeader h;
	h.sequenceNumber = h.batchNumber = h.flags = 0;

	if (size != 12) {
		return h;
	}
	auto cdata = (unsigned char*)data;

	h.sequenceNumber |= (cdata[0] << 24);
	h.sequenceNumber |= (cdata[1] << 16);
	h.sequenceNumber |= (cdata[2] << 8);
	h.sequenceNumber |= (cdata[3]);

	h.batchNumber |= (cdata[4] << 24);
	h.batchNumber |= (cdata[5] << 16);
	h.batchNumber |= (cdata[6] << 8);
	h.batchNumber |= (cdata[7]);

	h.flags |= (cdata[8] << 24);
	h.flags |= (cdata[9] << 16);
	h.flags |= (cdata[10] << 8);
	h.flags |= (cdata[11]);

	return h;
}


////////////////////////////////////////////////////////////////////////////////
// Debug
std::string RcpSocket::debug_PrintState() {
	std::lock_guard<std::mutex> lk(socketMutex);

	std::stringstream ss;
	// connection state
	ss << "state = " << (state == CONNECTED ? "connected to " : "disconnected");
	if (state != CONNECTED) {
		return ss.str();
	}
	ss << remoteAddress.toString() << ":" << remotePort << std::endl;

	ss << "loc seq num =   " << localSeqNum << std::endl;
	ss << "loc batch num = " << localBatchNum << std::endl;
	ss << "rem seq num =   " << remoteSeqNum << std::endl;
	ss << "rem batch num = " << remoteBatchNum << std::endl;
	ss << "reserved b.n. = " << remoteBatchNumReserved << std::endl;

	// recv queue
	ss << "recv queue (" << recvQueue.size() << ") \t\t= {";
	for (size_t i = 0; i < recvQueue.size(); i++) {
		ss << (int)recvQueue[i].second << " ";
	}
	ss << "}\n";

	// reserved spaces in queue
	ss << "reserved places (" << recvReserved.size() << ") \t= {";
	for (auto it = recvReserved.begin(); it != recvReserved.end(); ++it) {
		ss << it->first << ":" << it->second.index << " ";
	}
	ss << "}\n";

	// packets waiting for ack
	ss << "waiting for ack (" << recentPackets.size() << ") \t= {";
	for (auto it = recentPackets.begin(); it != recentPackets.end(); ++it) {
		ss << it->first << " ";
	}
	ss << "}";

	return ss.str();
}

void RcpSocket::debug_connect(std::string address, uint16_t port) {
	// only if not connected
	if (state != DISCONNECTED) {
		return;
	}

	// set remote parameters
	remoteAddress = address;
	remotePort = port;

	// set local parameters
	localSeqNum = 0;
	localBatchNum = 0;

	// succesful connection
	// note that the other party will drop connection soon if he did not recieve our last ACK
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();
}




////////////////////////////////////////////////////////////////////////////////
// Tester class

RcpTester::RcpTester() {
	socket.setBlocking(false);
}

bool RcpTester::bind(uint16_t localPort) {
	return socket.bind(localPort) == sf::UdpSocket::Done;
}

bool RcpTester::send(RcpHeader header, void* data, size_t dataSize, std::string address, uint32_t port) {
	char* rawData = new char[12 + dataSize];
	auto rawHeader = RcpHeader::serialize(header);
	memcpy(rawData, rawHeader.data(), rawHeader.size());
	memcpy(rawData + 12, data, dataSize);
	return socket.send(rawData, dataSize + 12, address, port) == sf::UdpSocket::Done;
}

bool RcpTester::receive(Packet& packet, RcpHeader& header) {
	sf::Packet sfp;
	sf::IpAddress remoteAddress;
	uint16_t remotePort;

	if (sf::UdpSocket::Done == socket.receive(sfp, remoteAddress, remotePort)) {

		const void* data = sfp.getData();
		size_t dataSize = sfp.getDataSize();
		header = RcpHeader::deserialize(sfp.getData(), 12);

		packet.setData(12 + (const char*)data, dataSize - 12);
		packet.sequenceNumber = header.sequenceNumber;
		packet.reliable = (header.flags & RcpSocket::REL) != 0;

		return true;
	}
	else {
		return false;
	}

}