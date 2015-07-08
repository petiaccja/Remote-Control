#include "RcpSocket.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cstring>
#ifdef _MSC_VER
#include <Windows.h>
#endif

using namespace std;
using namespace std::chrono; // long names were unreadable :(


////////////////////////////////////////////////////////////////////////////////
// Constructors and Destructor

RcpSocket::RcpSocket() {
	state = DISCONNECTED;
	socket.setBlocking(false);
	isBlocking = true;
	localSeqNum = localBatchNum = 0;
	remoteSeqNum = remoteBatchNum = 0;
	remoteBatchNumReserved = remoteBatchNum;
	runIoThread = false;

	cancelCallId = 896345; // in the end it doesn't even matter | shit, I'm that programmer again... 
	cancelNotify = cancelCallId;

	initDebug(); // DEBUG
}

RcpSocket::~RcpSocket() {
	disconnect();
	if (ioThread.joinable()) {
		ioThread.join();
	}
}


////////////////////////////////////////////////////////////////////////////////
// Modifiers

bool RcpSocket::bind(uint16_t port) {
	if (state == CONNECTED) {
		return false;
	}
	if (port == AnyPort) {
		port = sf::UdpSocket::AnyPort;
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
		selector.remove(socket);
	}
}

bool RcpSocket::isBound() const {
	return socket.getLocalPort() != 0;
}

void RcpSocket::cancel() {
	std::lock_guard<std::mutex> lk(socketMutex);

	// send a cancel packet to self
	RcpHeader header;
	header.sequenceNumber = cancelCallId;
	header.batchNumber = cancelCallId;
	header.flags = CANCEL;
	auto rawData = makePacket(header, nullptr, 0);
	socket.send(rawData.data(), rawData.size(), "localhost", getLocalPort());

	// notify condvar
	cancelNotify = cancelCallId;
	recvCondvar.notify_all();
}

bool RcpSocket::isConnected() const {
	return state == CONNECTED;
}

std::string RcpSocket::getRemoteAddress() const {
	return remoteAddress.toString();
}

uint16_t RcpSocket::getRemotePort() const {
	return remotePort;
}

void RcpSocket::setBlocking(bool isBlocking) {
	this->isBlocking = isBlocking;
}

bool RcpSocket::getBlocking() const {
	return isBlocking;
}

uint16_t RcpSocket::getLocalPort() const {
	return socket.getLocalPort(); 
}

void RcpSocket::setTiming(long long totalMs, long long shortMs) {
	if (shortMs == 0) {
		shortMs = TIMEOUT_SHORT;
	}
	if (totalMs < shortMs) {
		totalMs = shortMs + 1;
	}
	TIMEOUT_TOTAL = totalMs;
	TIMEOUT_SHORT = shortMs;
}

////////////////////////////////////////////////////////////////////////////////
// Connection setup

bool RcpSocket::accept() {
	cancelCallId++;

	if (state != DISCONNECTED || !isBound()) {
		return false;
	}

	// set local parameters
	localSeqNum = 70000;
	localBatchNum = 10000;

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
	while (!isData) {
		if (!selector.wait()) {
			continue;
		}

		// receive packet
		socket.receive(packet, remoteAddress, remotePort); // intentionally read into remoteWhatever

		// decode header
		if (packet.getDataSize() < 12) {
			continue;
		}
		header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());
		debugPrintMsg(header, RECV);

		// react to header
		if (header.flags == CANCEL && cancelCallId == header.sequenceNumber) {
			return false;
		}
		else if (header.flags == SYN) {
			isData = true;
		}
	}

	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;
	remoteBatchNumReserved = remoteBatchNum;

	// - send SYN-ACK
	header.sequenceNumber = localSeqNum++;
	header.batchNumber = localBatchNum;
	header.flags = SYN | ACK;
	headerSer = header.serialize();
	socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);
	debugPrintMsg(header, SEND); // DEBUG

	// - wait for ACK
	auto waitBegin = steady_clock::now();
	long long timeLeft;
	bool isAck;
	do {
		// how much time is left to get get a packet
		timeLeft = duration_cast<milliseconds>(waitBegin - steady_clock::now()).count() + TIMEOUT_TOTAL;

		// wait for a packet
		if (!selector.wait(sf::milliseconds(timeLeft))) {
			return false;
		}
		socket.receive(packet, responseAddress, responsePort);

		// decode packet
		if (packet.getDataSize() < 12 || remoteAddress != responseAddress || remotePort != responsePort) {
			continue;
		}
		header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());
		debugPrintMsg(header, RECV);

		// react to packet
		if (header.flags == CANCEL && header.sequenceNumber == cancelCallId) {
			return false;
		}
		else if (header.flags == ACK && header.sequenceNumber == remoteSeqNum + 1 && header.batchNumber == remoteBatchNum) {
			isAck = true;
			break;
		}
	} while (timeLeft > 0);

	if (!isAck) {
		return false;
	}

	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;
	remoteBatchNumReserved = remoteBatchNum;

	// succesful connection
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();

	return true;
}

bool RcpSocket::connect(std::string address, uint16_t port) {
	cancelCallId++;

	if (state != DISCONNECTED || !isBound()) {
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
	debugPrintMsg(header, SEND); // DEBUG

	// - wait for SYN-ACK
	auto waitBegin = steady_clock::now();
	long long timeLeft;
	bool isSynAck = false;
	do {
		// how much time is left to get get a packet
		timeLeft = duration_cast<milliseconds>(waitBegin - steady_clock::now()).count() + TIMEOUT_TOTAL;

		// wait for a packet
		if (!selector.wait(sf::milliseconds(timeLeft))) {
			return false;
		}
		socket.receive(packet, responseAddress, responsePort);

		// decode packet
		if (packet.getDataSize() < 12 || remoteAddress != responseAddress || remotePort != responsePort) {
			continue;
		}
		header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());
		debugPrintMsg(header, RECV);

		// react to packet
		if (header.flags == CANCEL && header.sequenceNumber == cancelCallId) {
			return false;
		}
		else if (header.flags == (SYN | ACK)) {
			isSynAck = true;
			break;
		}
	} while (timeLeft > 0);

	if (!isSynAck) {
		return false;
	}

	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;
	remoteBatchNumReserved = remoteBatchNum;

	// - send ACK
	header.sequenceNumber = localSeqNum++;
	header.batchNumber = localBatchNum;
	header.flags = ACK;

	headerSer = header.serialize();
	socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);
	debugPrintMsg(header, SEND); // DEBUG

	// succesful connection
	// note that the other party will drop connection soon if he did not receive our last ACK
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();

	return true;
}


// ISSUE:
// This doesn't push pending reliable packets, just kills the connection. Not really good...
void RcpSocket::disconnect() {
	// only when connected
	if (state != CONNECTED) {
		return;
	}

	// stop io thread
	stopIoThread();

	state = CLOSING;

	// try to push pending reliable packets
	// WARNING: this loop has not been tested, and is not expected to work!
	while (recentPackets.size() > 0) {
		// set to true if timeout is fatal
		bool timedOut = false;

		// select the one with shortest expiration
		RecentPacketInfo* resendThis = nullptr;
		auto now = steady_clock::now();
		microseconds sleepTime(2*TIMEOUT_TOTAL*1000);
		for (auto& v : recentPackets) {
			microseconds timeRemainingTotal = duration_cast<microseconds>(v.second.send - now + chrono::milliseconds(TIMEOUT_SHORT));
			microseconds timeRemainingResend = duration_cast<microseconds>(v.second.lastResend - now + chrono::milliseconds(TIMEOUT_SHORT));
			if (timeRemainingTotal < sleepTime) {
				timedOut = true;
				sleepTime = timeRemainingTotal;
				resendThis = &v.second;
			}
			if (timeRemainingResend < sleepTime) {
				timedOut = false;
				sleepTime = timeRemainingResend;
				resendThis = &v.second;
			}
		}

		// try to receive
		if (!selector.wait(sf::microseconds(sleepTime.count()))) {
			if (timedOut) {
				// connection timed out: reset socket
				reset();
				state = DISCONNECTED;
				return;
			}
			else {
				// just resend that packet
				auto rawData = makePacket(resendThis->header, resendThis->data.data(), resendThis->data.size());
				socket.send(rawData.data(), rawData.size(), remoteAddress, remotePort);
			}
		}
		else {
			// decode packet and erase if it matches any pending packet
			sf::Packet packet;
			sf::IpAddress responseAddress;
			uint16_t responsePort;
			socket.receive(packet, responseAddress, responsePort);
			if (responseAddress == remoteAddress && remotePort == responsePort && packet.getDataSize() > 12) {
				auto header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());
				RecentPacketMapT::iterator it;
				if (header.flags == ACK && (it = recentPackets.find(header.batchNumber)) != recentPackets.end()) {
					recentPackets.erase(it);
				}
			}
		}
	}

	// perform closing procedure
	// - send FIN
	// - wait for FIN/ACK
	// - send ACK and hope the server gets it
	RcpHeader header;
	std::array<uint8_t, 12> headerSer;
	sf::Packet packet;
	sf::IpAddress responseAddress;
	uint16_t responsePort;

	// - send FIN and wait for FIN/ACK
	header.sequenceNumber = localSeqNum++;
	header.batchNumber = localBatchNum;
	header.flags = FIN;

	headerSer = header.serialize();
	
	long long waitTotal = 0;
	bool finAckownledged = false;
	while (!finAckownledged) {
		// repeatedly send FIN until it's answered
		do {
			socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);
			debugPrintMsg(header, SEND); // DEBUG
			waitTotal += TIMEOUT_SHORT;
		} while (waitTotal <= TIMEOUT_TOTAL && !selector.wait(sf::milliseconds(TIMEOUT_SHORT)));

		// look what we received
		if (sf::UdpSocket::Done != socket.receive(packet, responseAddress, responsePort)) {
			// nothing received
			break;
		}
		else if (responseAddress != remoteAddress || responsePort != remotePort || packet.getDataSize() < 12) {
			// invalid remote address
			continue;
		}
		else {
			// valid packet received
			header = RcpHeader::deserialize(packet.getData(), 12);
			debugPrintMsg(header, RECV);
			if (header.flags & (FIN | ACK)) {
				finAckownledged = true;

				// - Send back ACK for FIN/ACK
				header.sequenceNumber = localSeqNum++;
				header.batchNumber = localBatchNum;
				header.flags = ACK;
				headerSer = header.serialize();
				socket.send(headerSer.data(), headerSer.size(), remoteAddress, remotePort);
				debugPrintMsg(header, SEND);
			}
		}
	}
	

	// set disconnected state
	reset();
	state = DISCONNECTED;

	// interrupt pending receives
	recvCondvar.notify_all();
}



////////////////////////////////////////////////////////////////////////////////
// Traffic

bool RcpSocket::send(const void * data, size_t size, bool reliable) {
	return sendEx(data, size, reliable ? (uint32_t)REL : 0);
}

bool RcpSocket::send(RcpPacket& packet) {
	return send(packet.getData(), packet.getDataSize(), packet.isReliable());
}

// data does NOT include header
bool RcpSocket::sendEx(const void* data, size_t size, uint32_t flags) {
	// check errors
	if (state != CONNECTED) {
		return false;
	}
	// create socket header + data block
	RcpHeader header;
	header.sequenceNumber = ++localSeqNum;
	header.batchNumber = (flags & REL) ? ++localBatchNum : localBatchNum;
	header.flags = flags;
	
	auto rawData = makePacket(header, data, size);
	if (rawData.size() > sf::UdpSocket::MaxDatagramSize) {
		false;
	}

	// only one thread can access the socket and other variables at the same time
	std::lock_guard<std::mutex> lk(socketMutex);

	// send data on socket
	auto sfStatus = socket.send(rawData.data(), rawData.size(), remoteAddress, remotePort);
	debugPrintMsg(header, SEND); // DEBUG
	if (sfStatus != sf::Socket::Done) {
		return false;
	}

	// add packet to list of ack waiting packets
	auto sendTime = steady_clock::now();
	if ((flags & REL) != 0) {
		recentPackets.insert(RecentPacketMapT::value_type(
			header.batchNumber,
			{ header, std::vector<uint8_t>((const char*)data, size + (const char*)data), sendTime, sendTime })
			);
	}

	// set last sending time to manage timeouts
	timeLastSend = steady_clock::now();

	return true;
}


bool RcpSocket::receive(RcpPacket& packet) {
	cancelCallId++;

	// check errors
	if (state != CONNECTED) {
		return false;
	}

	// wait for packet queue to have data

	// lock mutex
	std::unique_lock<std::mutex> lk(socketMutex);

	// wait on condvar
	bool isData = false;
	auto IsDataPred = [this] { return (recvQueue.size() > 0 && recvQueue.front().second == true) || cancelNotify == cancelCallId || state != CONNECTED; };
	if (isBlocking) {
		recvCondvar.wait(lk, IsDataPred);
		if (cancelNotify == cancelCallId || state != CONNECTED) {
			return false;
		}
		else {
			isData = true;
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

	// this seems like utter bullshit, but it looks so confident I dare only comment it
	// remove packet from reserved list if it's contained
	// recvReserved.erase(packet.sequenceNumber);

	// decrease all indices of reserved spaces
	for (auto& it : recvReserved) {
		it.second.index--;
	}

	// finally, unlock mutex
	lk.unlock();

	return true;
}


void RcpSocket::startIoThread() {
	if (runIoThread == true) {
		return;
	}
	if (ioThread.joinable()) {
		ioThread.join();
	}
	runIoThread = true;
	ioThread = std::thread([this] { 
		ioThreadFunction();
		if (state == CLOSING) {
			replyClose();
			reset();
			state = DISCONNECTED;
		}
		runIoThread = false;

		// interrupt pending receives
		recvCondvar.notify_all();
	});
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

	timeLastreceived = steady_clock::now();

	while (runIoThread) {
		// ------------------------------------ //
		// --- Select event closest in time --- //
		// ------------------------------------ //

		EventArgs eventArgs;
		eClosestEventType eventType = getNextEvent(eventArgs);

		// -------------------------------- //
		// --- Get incoming socket data --- //
		// -------------------------------- //

		// Calculate time in microseconds Selector should wait at most 
		long long usSleep = eventArgs.remaining.count();
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
					auto rawData = makePacket(eventArgs.resendInfo->header, eventArgs.resendInfo->data.data(), eventArgs.resendInfo->data.size());
					socket.send(rawData.data(), rawData.size(), remoteAddress, remotePort);
					debugPrintMsg(eventArgs.resendInfo->header, SEND); // DEBUG
					timeLastSend = steady_clock::now();
					eventArgs.resendInfo->lastResend = timeLastSend;
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
					debugPrintMsg(header, SEND); // DEBUG
					timeLastSend = steady_clock::now();

					break;
				}
				case RESERVE_TIMEOUT:
				case ACK_TIMEOUT:
				case RECV_TIMEOUT: {
					// forcefully close the socket
					reset();
					state = DISCONNECTED;
					return;
				}
			}
		}
		// Process incoming packet
		else {
			// lock mutex (guarded)
			std::lock_guard<std::mutex> lk(socketMutex);

			// extract data from socket
			sf::Packet rawPacket;
			sf::IpAddress sender;
			uint16_t senderPort;
			socket.receive(rawPacket, sender, senderPort);

			// extract rcp header and payload from packet
			RcpHeader header;
			RcpPacket packet;
			bool isValid = decodeDatagram(rawPacket, sender, senderPort, header, packet);
			debugPrintMsg(header, RECV);
			if (!isValid) {
				continue;
			}

			// set time of last valid packet
			timeLastreceived = steady_clock::now();

			// handle special packets
			switch (header.flags) {
				case KEP: {
					continue;
				}
				case ACK: {
					// remove the packet from the recent ack list
					auto it = recentPackets.find(header.batchNumber); // ack packets batch number contains the acknowledged packet's b.n.
					if (it != recentPackets.end()) {
						recentPackets.erase(it);
					}

					continue;
				}
				case REL: {
					// send an acknowledgement
					RcpHeader ackHeader;
					ackHeader.sequenceNumber = header.sequenceNumber;
					ackHeader.batchNumber = header.batchNumber;
					ackHeader.flags = ACK;
					auto ackData = RcpHeader::serialize(ackHeader);
					socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
					debugPrintMsg(ackHeader, SEND);

					break;
				}
				case FIN: {
					state = CLOSING;
					return;
				}
				case CANCEL:
					continue;
				case 0: {
					break;
				}
				default:
					continue;
			}

			// reserve spaces in queue if batch number references a late packet
			long long numSpacesReserve = (long long)header.batchNumber - (long long)remoteBatchNumReserved;
			for (long long i = 0; i < numSpacesReserve; ++i) {
				remoteBatchNumReserved++;
				recvReserved.insert({ remoteBatchNumReserved,{ recvQueue.size(), steady_clock::now() } });
				recvQueue.push({ RcpPacket(), false });
			}

			// fill space if there's one reserved for this packet
			// reliable packets always have their space reserved because of the above lines!
			// if they don't, then they must be duplicates, and will be silently dropped
			if (header.flags & REL) {
				auto it = recvReserved.find(header.batchNumber);
				if (it != recvReserved.end()) {
					recvQueue[it->second.index].first = std::move(packet); // don't use this packet again; moving for performance reasons
					recvQueue[it->second.index].second = true;
					recvReserved.erase(it);
				}
				else {
					cout << "duplicate: " << header << endl;;
					continue;
				}
			}
			else {
				recvQueue.push({ packet, true });
			}


			// set parameters for remote peer
			remoteSeqNum = std::max(remoteSeqNum, header.sequenceNumber);
			remoteBatchNum = header.flags & REL ? std::max(remoteBatchNum, header.batchNumber) : remoteBatchNum;


			// unlock mutex (lock_guard) and notify
			recvCondvar.notify_all();
		}
	}
}




////////////////////////////////////////////////////////////////////////////////
// Internal helper functions

void RcpSocket::reset() {
	// reset data structures
	recvQueue = decltype(recvQueue)();
	recentPackets.clear();
	recvReserved.clear();
}

void RcpSocket::replyClose() {
	// assemble FIN/ACK reply packet
	RcpHeader replyHeader;
	replyHeader.flags = FIN | ACK;
	replyHeader.batchNumber = localBatchNum;
	replyHeader.sequenceNumber = localSeqNum++;
	auto replyHeaderSer = replyHeader.serialize();

	// response data
	sf::Packet packet;
	sf::IpAddress responseAddress;
	uint16_t responsePort;

	// start off with a reply, since we just got an ack
	socket.send(replyHeaderSer.data(), replyHeaderSer.size(), remoteAddress, remotePort);
	debugPrintMsg(replyHeader, SEND); // DEBUG

	// wait for last ACK response
	bool isLastFinAcked = false;
	long long waitTimeout = 0;
	while (!isLastFinAcked && waitTimeout < TIMEOUT_TOTAL) {
		// receive a packet
		if (selector.wait(sf::milliseconds(TIMEOUT_SHORT))) {
			socket.receive(packet, responseAddress, responsePort);

			// drop false packets
			if (responseAddress != remoteAddress || responsePort != remotePort || packet.getDataSize() < 12) {
				continue;
			}

			// reply accordingly
			RcpHeader header = RcpHeader::deserialize(packet.getData(), 12);
			debugPrintMsg(header, RECV);
			if (header.flags == ACK) {
				break;
			}
			else if (header.flags == FIN) {
				socket.send(replyHeaderSer.data(), replyHeaderSer.size(), remoteAddress, remotePort);
				debugPrintMsg(replyHeader, SEND); // DEBUG
			}
		}
		waitTimeout += TIMEOUT_SHORT;
	}
}


std::vector<uint8_t> RcpSocket::makePacket(const RcpHeader& header, const void* data, size_t size) {
	auto headerSer = header.serialize();
	vector<uint8_t> v(size + headerSer.size());
	memcpy(v.data(), headerSer.data(), headerSer.size());
	memcpy(v.data() + headerSer.size(), data, size);
	return v;
}



bool RcpSocket::decodeDatagram(const sf::Packet& packet, const sf::IpAddress& sender, uint16_t port, RcpHeader& rcpHeader, RcpPacket& rcpPacket) {
	// size must be at least 12 to contain the RCP header
	if (packet.getDataSize() < 12) {
		return false;
	}
	// sender and port must match remote peer's
	if (sender != remoteAddress && port != remotePort) {
		return false;
	}

	// decode the header
	RcpHeader header = RcpHeader::deserialize(packet.getData(), packet.getDataSize());

	// batch number and sequence number must be within a reasonable range
	if (header.flags & ACK) {
		if (abs((ptrdiff_t)localBatchNum - (ptrdiff_t)header.batchNumber) > 1000) {
			return false;
		}
		if (abs((ptrdiff_t)localSeqNum - (ptrdiff_t)header.sequenceNumber) > 5000) {
			return false;
		}
	}
	else {
		if (abs((ptrdiff_t)remoteBatchNum - (ptrdiff_t)header.batchNumber) > 1000) {
			return false;
		}
		if (abs((ptrdiff_t)remoteSeqNum - (ptrdiff_t)header.sequenceNumber) > 5000) {
			return false;
		}
	}

	// all fine, get the data and form a packet
	RcpPacket tmp;
	tmp.setData((char*)packet.getData() + 12, packet.getDataSize() - 12);
	tmp.sequenceNumber = header.sequenceNumber;
	tmp.reliable = (header.flags & REL) != 0;

	// set output parameters
	rcpPacket = std::move(tmp);
	rcpHeader = header;

	return true;
}


auto RcpSocket::getNextEvent(EventArgs& args) -> eClosestEventType {
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
	microseconds reservedRemaining(2 * TIMEOUT_TOTAL * 1000);
	bool isReservedTimeout = false;
	for (auto it : recvReserved) {
		microseconds rem = duration_cast<microseconds>(it.second.timestamp - now + milliseconds(TIMEOUT_TOTAL));
		reservedRemaining = duration_cast<milliseconds>(reservedRemaining < rem ? reservedRemaining : rem);
		isReservedTimeout = true;
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
	microseconds timeoutRemaining = duration_cast<microseconds>(timeLastreceived + milliseconds(TIMEOUT_TOTAL) - now);
	if (eventRemaining > timeoutRemaining) {
		eventRemaining = timeoutRemaining;
		eventType = RECV_TIMEOUT;
	}

	args.remaining = eventRemaining;
	args.resendInfo = resendInfo;
	return eventType;
}



////////////////////////////////////////////////////////////////////////////////
// Helpers

std::array<unsigned char, 12> RcpSocket::RcpHeader::serialize(const RcpHeader& h) {
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

	if (size < 12) {
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
	ss << "state = ";
	switch (state)
	{
	case RcpSocket::DISCONNECTED:
		ss << "disconnected";
		break;
	case RcpSocket::CONNECTED:
		ss << "connected to ";
		break;
	case RcpSocket::CLOSING:
		ss << "closing ( " << remoteAddress.toString() << ":" << remotePort << " )";
		break;
	default:
		break;
	}
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
	remoteSeqNum = 0;
	remoteBatchNum = 0;
	remoteBatchNumReserved = remoteBatchNum;

	// succesful connection
	// note that the other party will drop connection soon if he did not receive our last ACK
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();
}

void RcpSocket::debug_kill() {
	stopIoThread();
	reset();
	state = DISCONNECTED;
}


void RcpSocket::debug_enableLog(bool value) {
	debugLog = true;
}

std::ostream& operator<<(std::ostream& os, RcpSocket::RcpHeader header) {
	std::string flags;

	bool isFirst = true;
	if (header.flags & RcpSocket::ACK) {
		if (!isFirst)
			flags += " | ";
		flags += "ACK";
		isFirst = false;
	}
	if (header.flags & RcpSocket::REL) {
		if (!isFirst)
			flags += " | ";
		flags += "REL";
		isFirst = false;
	}
	if (header.flags & RcpSocket::KEP) {
		if (!isFirst)
			flags += " | ";
		flags += "KEP";
		isFirst = false;
	}
	if (header.flags & RcpSocket::SYN) {
		if (!isFirst)
			flags += " | ";
		flags += "SYN";
		isFirst = false;
	}
	if (header.flags & RcpSocket::FIN) {
		if (!isFirst)
			flags += " | ";
		flags += "FIN";
		isFirst = false;
	}
	if (header.flags & RcpSocket::CANCEL) {
		if (!isFirst)
			flags += " | ";
		flags += "CANCEL";
		isFirst = false;
	}
	if (isFirst) {
		flags = "-";
	}

	os << "{" << header.sequenceNumber << ", " << header.batchNumber << ", " << flags << "}";

	return os;
}

std::mutex RcpSocket::debugLock;


void RcpSocket::debugPrintMsg(RcpHeader header, eDir dir) {
	if (debugLog) {
		debugLock.lock();

		/*
#ifdef _MSC_VER
		HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hStdOut, color);
#endif
		*/

		cout << "[" << setw(5) << getLocalPort() << "]   ";
		if (dir == RECV) {
			cout << "received   ";
		}
		else {
			cout << "sent       ";
		}
		cout << header << endl;

		/*
#ifdef _MSC_VER
		SetConsoleTextAttribute(hStdOut, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
#endif
		*/

		debugLock.unlock();
	}
}

void RcpSocket::initDebug() {
	debugLog = false;
	color = colorSt % 14 + 1;
	colorSt++;
}


unsigned RcpSocket::colorSt = 1;


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

bool RcpTester::receive(RcpPacket& packet, RcpHeader& header) {
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
