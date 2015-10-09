#include "RcpSocket.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cstring>

// needed for colored console text while debugging
#ifdef _MSC_VER
#include <Windows.h>
#pragma warning(disable : 4244)
#endif

using namespace std;
using namespace std::chrono; // long names were unreadable :(


////////////////////////////////////////////////////////////////////////////////
// Constructors and Destructor

RcpSocket::RcpSocket() {
	state = CLOSED;
	socket.setBlocking(false);
	isBlocking = true;
	localSeqNum = localBatchNum = 0;
	remoteSeqNum = remoteBatchNum = 0;
	remoteBatchNumReserved = remoteBatchNum;
	runIoThread = false;

	cancelCallId = 896345; // any number will suffice
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
	return state == CONNECTED || state == CLOSING;
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

void RcpSocket::accept(int timeout) {
	cancelCallId++;
	auto timeoutStart = steady_clock::now();

	if (state != CLOSED) {
		throw RcpInvalidCallException("already connected");
	}
	if (!isBound()) {
		throw RcpInvalidCallException("must bind the socket first");
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
		long long timeoutLeft = duration_cast<milliseconds>(timeoutStart - steady_clock::now()).count() + timeout;
		sf::Time waitTime =
			timeout == std::numeric_limits<int>::max() ? sf::Time::Zero : sf::milliseconds(timeoutLeft);
		if (timeoutLeft <= 0 || !selector.wait(waitTime)) {
			throw RcpTimeoutException("requested timeout is over");
		}

		// receive packet
		socket.receive(packet, remoteAddress, remotePort); // intentionally read into remoteWhatever

		// decode header
		if (!header.deserialize(packet.getData(), packet.getDataSize())) {
			continue;
		}
		debugPrintMsg(header, RECV);

		// react to header
		if (header.flags == CANCEL && cancelCallId == header.sequenceNumber) {
			throw RcpInterruptedException("function call was cancelled");
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
	long long timeLeft; // milliseconds
	bool isAck;
	do {
		// how much time is left to get get a packet
		timeLeft = duration_cast<milliseconds>(waitBegin - steady_clock::now()).count() + TIMEOUT_TOTAL;
		long long timeoutLeft = duration_cast<milliseconds>(timeoutStart - steady_clock::now()).count() + timeout;
		timeLeft = min(timeoutLeft, timeLeft);

		// wait for a packet
		if (timeLeft <= 0 || !selector.wait(sf::milliseconds(timeLeft))) {
			throw RcpNetworkException("client's response timed out");
		}
		socket.receive(packet, responseAddress, responsePort);

		// decode packet
		if (header.deserialize(packet.getData(), packet.getDataSize()) || remoteAddress != responseAddress || remotePort != responsePort) {
			continue;
		}
		debugPrintMsg(header, RECV);

		// react to packet
		if (header.flags == CANCEL && header.sequenceNumber == cancelCallId) {
			throw RcpInterruptedException("function call was cancelled");
		}
		else if (header.flags == ACK && header.sequenceNumber == remoteSeqNum + 1 && header.batchNumber == remoteBatchNum) {
			isAck = true;
			break;
		}
	} while (timeLeft > 0);

	if (!isAck) {
		throw RcpNetworkException("ACK was expected, received other packet");
	}

	remoteSeqNum = header.sequenceNumber;
	remoteBatchNum = header.batchNumber;
	remoteBatchNumReserved = remoteBatchNum;

	// succesful connection
	// finalize connection states, fire up IO thread
	timeLastSend = ::steady_clock::now();
	state = CONNECTED;
	startIoThread();
}

void RcpSocket::connect(std::string address, uint16_t port, int timeout) {
	cancelCallId++;

	if (state != CLOSED) {
		throw RcpInvalidCallException("already connected");
	}
	if (!isBound()) {
		throw RcpInvalidCallException("must bind the socket first");
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
		timeLeft = duration_cast<milliseconds>(waitBegin - steady_clock::now()).count() + min((unsigned)timeout, TIMEOUT_TOTAL);

		// wait for a packet
		if (timeLeft <= 0 || !selector.wait(sf::milliseconds(timeLeft))) {
			throw RcpNetworkException("client's response timed out");
		}
		socket.receive(packet, responseAddress, responsePort);

		// decode packet
		if (!header.deserialize(packet.getData(), packet.getDataSize()) || remoteAddress != responseAddress || remotePort != responsePort) {
			continue;
		}
		debugPrintMsg(header, RECV);

		// react to packet
		if (header.flags == CANCEL && header.sequenceNumber == cancelCallId) {
			throw RcpInterruptedException("function call was cancelled");
		}
		else if (header.flags == (SYN | ACK)) {
			isSynAck = true;
			break;
		}
	} while (timeLeft > 0);

	if (!isSynAck) {
		throw RcpNetworkException("SYN/ACK was expected, received other packet");
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
}


void RcpSocket::disconnect2() {
	stopIoThread();

	// we are initiating the shutdown
	if (state == CONNECTED) {
		
	}
	// the other party has initiated the shutdown
	else if (state == CLOSE_WAIT) {

	}
}


// ISSUE:
// This doesn't push pending reliable packets, just kills the connection. Not really good...
void RcpSocket::disconnect1() {
	// This whole method is only to be performed when state is CONNECTED.
	// However, on must still call disconnect if the other peer cancelled the connection.
	// In this case, the socket falls into CLOSING state. All communication is ceased, but
	// the internal structure remains intact, holding pending messages.
	// This is to allow receiving pending messages even if the connection is dead by then.
	if (state == CLOSING) {
		reset();
		state = CLOSED;
		return;
	}
	// Closing state is handled above. If that was not the case, but CONNECTED instead,
	// go on with the method.
	if (state != CONNECTED) {
		return;
	}

	// stop io thread
	stopIoThread();

	// try to push pending reliable packets
	// WARNING: this loop has not been tested, and is not expected to work!
	while (recentPackets.size() > 0) {
		// set to true if timeout is fatal
		bool timedOut = false;

		// select the one with shortest expiration
		RecentPacketInfo* resendThis = nullptr;
		auto now = steady_clock::now();
		microseconds sleepTime(2 * TIMEOUT_TOTAL * 1000);
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
				state = CLOSED;
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
			RcpHeader header;
			if (responseAddress == remoteAddress && remotePort == responsePort && header.deserialize(packet.getData(), packet.getDataSize())) {
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
			header.deserialize(packet.getData(), packet.getDataSize());
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
	state = CLOSED;

	// interrupt pending receives
	recvCondvar.notify_all();
}



////////////////////////////////////////////////////////////////////////////////
// Traffic

void RcpSocket::send(const void * data, size_t size, bool reliable) {
	return sendEx(data, size, reliable ? (uint32_t)REL : 0);
}

void RcpSocket::send(RcpPacket& packet) {
	return send(packet.getData(), packet.getDataSize(), packet.isReliable());
}

// data does NOT include header
void RcpSocket::sendEx(const void* data, size_t size, uint32_t flags) {
	// check errors
	if (state != CONNECTED) {
		throw RcpInvalidCallException("socket must be connected to send");
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
		throw RcpInvalidArgumentException("packet could not be sent, might be to big");
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
}


bool RcpSocket::receive(RcpPacket& packet, int timeout) {
	cancelCallId++;

	// check errors
	if (state != CONNECTED && state != CLOSING) {
		throw RcpInvalidCallException("socket must be connected to receive");
	}

	// wait for packet queue to have data

	// lock mutex
	std::unique_lock<std::mutex> lk(socketMutex);

	// wait on condvar
	bool isData = false;
	//auto IsDataPred = [this] { return (recvQueue.size() > 0 && recvQueue.front().second == true) || cancelNotify == cancelCallId || (state != CONNECTED && state != CLOSING); };
	enum eReasonForWakeUp : uint32_t {
		AVAILABLE_DATA,
		CANCELLED,
		CONNECTION_CLOSING,
	};
	atomic<eReasonForWakeUp> reasonForWakeUp;
	auto NotifyPredicate = [this, &reasonForWakeUp] {
		if (recvQueue.size() > 0 && recvQueue.front().second == true) {
			// there's a valid incoming packet
			reasonForWakeUp = AVAILABLE_DATA;
			return true;
		}
		else if (cancelNotify == cancelCallId) {
			// call was cancelled
			reasonForWakeUp = CANCELLED;
			return true;
		}
		else if (recvQueue.size() == 0 && state != CONNECTED) {
			// connection is closing, but there's no data
			reasonForWakeUp = CONNECTION_CLOSING;
			return true;
		}
		else {
			return false;
		}
	};

	if (isBlocking) {
		if (!recvCondvar.wait_for(lk, milliseconds(timeout), NotifyPredicate)) {
			return false;
		}
		switch (reasonForWakeUp) {
			case AVAILABLE_DATA:
				isData = true;
				break;
			case CANCELLED:
				throw RcpInterruptedException("function call was cancelled, bitch");
			case CONNECTION_CLOSING:
				isData = false;
		}
	}
	else {
		isData = (recvQueue.size() > 0 && recvQueue.front().second == true);
	}

	if (!isData) {
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
		// ioThreadFunction returns true if it got a FIN, false otherwise
		if (ioThreadFunction()) {
			replyClose();

			// clean up message queue
			lock_guard<mutex> lk(socketMutex);

			decltype(recvQueue) cleanRecvQueue;
			while (recvQueue.size() > 0) {
				if (recvQueue.front().second == true) {
					cleanRecvQueue.push(std::move(recvQueue.front()));
				}
				recvQueue.pop();
			}
			recvQueue = std::move(cleanRecvQueue);

			// notify receive calls
			recvCondvar.notify_all();
		}

		runIoThread = false;
	});
}


void RcpSocket::stopIoThread() {
	runIoThread = false;
	if (ioThread.joinable()) {
		ioThread.join();
	}
}

bool RcpSocket::ioThreadFunction() {
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
					auto hseq = header.serialize();
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
					state = CLOSED;
					return false;
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
					auto ackData = ackHeader.serialize();
					socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
					debugPrintMsg(ackHeader, SEND);

					break;
				}
				case FIN: {
					state = CLOSE_WAIT;
					return true;
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
	return false;
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
			RcpHeader header;
			header.deserialize(packet.getData(), 12);
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
	RcpHeader header;
	header.deserialize(packet.getData(), packet.getDataSize());

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


bool RcpSocket::decodeHeader(const sf::Packet& packet, RcpHeader& header) {
	// size must be at least 12 to contain the RCP header
	if (packet.getDataSize() < 12) {
		return false;
	}
	header.deserialize(packet.getData(), packet.getDataSize());
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
// New architecture

void RcpSocket::ioThreadFunction2() {
	switch (state)
	{
		case RcpSocket::CLOSED: {
			break;
		}

		case RcpSocket::INITIATE: {

		}

		case RcpSocket::SYN_SENT: {
			sf::Packet packet;
			sf::IpAddress responseAddress;
			uint16_t responsePort;
			RcpHeader header;

			// Flood with SYN until SYN_ACK or SYN or ACK is received.
			steady_clock::time_point start = steady_clock::now();
			RcpHeader synHeader{ localSeqNum++, localBatchNum++, eFlags::SYN };
			auto data = synHeader.serialize();

			do {
				if (!socket.send(data.data(), data.size(), remoteAddress, remotePort)) {
					state = CLOSED;
					break;
				}
				if (selector.wait(sf::milliseconds(TIMEOUT_SHORT))) {
					// Get and decode the datagram.
					socket.receive(packet, responseAddress, responsePort);
					if (!decodeHeader(packet, header) || responseAddress != remoteAddress || responsePort != remotePort) {
						continue;
					}
					// Handle different flags.
					if (header.flags & CANCEL && cancelCallId == header.sequenceNumber) {
						state = CLOSED;
					}
					else if (header.flags == (SYN|ACK)) {
						break;
					}
					else if (header.flags == SYN) {
						state = SYN_SIMOULTANEOUS;
					}
					else if (header.flags == ACK) {
						state = SYN_SIMOULTANEOUS_ACKED;
					}
				}
			} while (steady_clock::now() - start > milliseconds(TIMEOUT_TOTAL));

			// Normal connecting procedure.
			if (header.flags == (SYN | ACK)) {
				// We're under friendly SYN|ACK flood.
				// Reply with an ACK for every SYN|ACK.
				// Wait for 2*TIMEOUT_SHORT of silence to ensure remote peer received our ACK.				
				bool isData = false;
				milliseconds waitTime(2 * TIMEOUT_SHORT);
				steady_clock::time_point totalStart = steady_clock::now();
				do {
					auto ackData = RcpHeader{ localSeqNum++, localBatchNum++, ACK }.serialize();
					socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
					steady_clock::time_point start = steady_clock::now();
					isData = selector.wait(sf::milliseconds(waitTime.count()));
					if (isData) {
						socket.receive(packet, responseAddress, responsePort);
						RcpHeader nheader;
						bool isHeaderOk = nheader.deserialize(packet.getData(), packet.getDataSize());
						if (isHeaderOk
							&& nheader.flags == SYN | ACK
							&& responseAddress == remoteAddress
							&& responsePort == responsePort)
						{
							waitTime = milliseconds(2 * TIMEOUT_SHORT);
						}
						else if (isHeaderOk && nheader.flags == CANCEL && cancelCallId == nheader.sequenceNumber) {
							break;
						}
						else {
							// false wake-up packets cause a reduced consequent wait time.
							waitTime -= duration_cast<milliseconds>(start-steady_clock::now());
						}
					}
				} while (isData && (totalStart - steady_clock::now() < milliseconds(TIMEOUT_TOTAL)));
				if (!isData) {
					state = CONNECTED;
				}
				else {
					state = CLOSED;
				}
			}
			break;
		}	

		case RcpSocket::SYN_ACK_SENT: {
			sf::Packet packet;			
			sf::IpAddress responseAddress;
			uint16_t responsePort;
			RcpHeader header{ 0,0,0 };

			// Flood with SYN|ACK until and ACK is received.
			steady_clock::time_point start = steady_clock::now();
			RcpHeader synAckHeader{ localSeqNum++, localBatchNum++, eFlags::SYN | eFlags::ACK };
			auto data = synAckHeader.serialize();

			do {
				if (!socket.send(data.data(), data.size(), remoteAddress, remotePort)) {
					state = CLOSED;
					break;
				}
				if (selector.wait(sf::milliseconds(TIMEOUT_SHORT))) {
					// Get and decode the datagram.
					socket.receive(packet, responseAddress, responsePort);
					if (!decodeHeader(packet, header) || responseAddress != remoteAddress || responsePort != remotePort) {
						continue;
					}
					// Handle different flags.
					if (header.flags & CANCEL && cancelCallId == header.sequenceNumber) {
						state = CLOSED;
					}
					else if (header.flags == ACK) {
						state = CONNECTED;
					}
				}
			} while (steady_clock::now() - start > milliseconds(TIMEOUT_TOTAL));

			break;
		}

		case RcpSocket::SYN_WAIT: {
			sf::Packet packet;
			RcpHeader header;
			// Wait for an incoming SYN to initiate connection.
			socket.receive(packet, remoteAddress, remotePort);

			// Decode packet.
			if (!header.deserialize(packet.getData(), packet.getDataSize())) {
				state = eState::CLOSED;
				break;
			}
			if (header.flags & eFlags::CANCEL && cancelCallId == header.sequenceNumber) {
				state = eState::CLOSED;
				break;
			}

			state = eState::SYN_ACK_SENT;
			break;
		}
		case RcpSocket::SYN_SIMOULTANEOUS: {
			sf::Packet packet;
			sf::IpAddress responseAddress;
			uint16_t responsePort;
			RcpHeader header{ 0,0,0 };

			// We wait for the ACK and keep flooding the remote bitch with SYNs.
			steady_clock::time_point start = steady_clock::now();

			do {
				if (selector.wait(sf::milliseconds(TIMEOUT_SHORT))) {
					socket.receive(packet, responseAddress, responsePort);
					if (!decodeHeader(packet, header)) {
						continue;
					}
					else if (remotePort = responsePort && remoteAddress == responseAddress && header.flags == ACK) {
						state = CONNECTED;
						break;
					}
					else if (header.flags & CANCEL && cancelCallId == header.sequenceNumber) {
						state = CLOSED;
						break;
					}
				}
				// send another syn
				auto synData = RcpHeader{ localSeqNum, localBatchNum, SYN }.serialize();
				socket.send(synData.data(), synData.size(), remoteAddress, remotePort);
			} while (steady_clock::now() - start < milliseconds(TIMEOUT_TOTAL));

			if (state != CONNECTED) {
				state = CLOSED;
			}

			break;
		}
		case RcpSocket::SYN_SIMOULTANEOUS_ACKED: {
			sf::Packet packet;
			sf::IpAddress responseAddress;
			uint16_t responsePort;
			RcpHeader header{ 0,0,0 };

			steady_clock::time_point totalStart = steady_clock::now();
			milliseconds waitTime(TIMEOUT_TOTAL);

			// We wait for SYN and reply with ACK.
						

			// Each ACK must be followed by a 2*TIMEOUT_SHORT wait for another SYN.
			bool isData = false;
			waitTime = milliseconds(2 * TIMEOUT_SHORT);
			
			do {
				auto ackData = RcpHeader{ localSeqNum++, localBatchNum++, ACK }.serialize();
				socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
				steady_clock::time_point start = steady_clock::now();
				isData = selector.wait(sf::milliseconds(waitTime.count()));
				if (isData) {
					socket.receive(packet, responseAddress, responsePort);
					RcpHeader nheader;
					bool isHeaderOk = nheader.deserialize(packet.getData(), packet.getDataSize());
					if (isHeaderOk
						&& nheader.flags == SYN
						&& responseAddress == remoteAddress
						&& responsePort == responsePort)
					{
						waitTime = milliseconds(2 * TIMEOUT_SHORT);
					}
					else if (isHeaderOk && nheader.flags == CANCEL && cancelCallId == nheader.sequenceNumber) {
						break;
					}
					else {
						// false wake-up packets cause a reduced consequent wait time.
						waitTime -= duration_cast<milliseconds>(start - steady_clock::now());
					}
				}
			} while (isData && (totalStart - steady_clock::now() < milliseconds(TIMEOUT_TOTAL)));
			if (!isData) {
				state = CONNECTED;
			}
			else {
				state = CLOSED;
			}
		}

		case RcpSocket::CONNECTED: {
			break;
		}
		case RcpSocket::CLOSE_WAIT:
			break;
		case RcpSocket::FIN_WAIT:
			break;
		case RcpSocket::CLOSING:
			break;
		default:
			break;
	}




}






















////////////////////////////////////////////////////////////////////////////////
// Helpers

std::array<unsigned char, 12> RcpSocket::RcpHeader::serialize() const {
	std::array<unsigned char, 12> v;
	v[0] = sequenceNumber >> 24;
	v[1] = sequenceNumber >> 16;
	v[2] = sequenceNumber >> 8;
	v[3] = sequenceNumber;

	v[4] = batchNumber >> 24;
	v[5] = batchNumber >> 16;
	v[6] = batchNumber >> 8;
	v[7] = batchNumber;

	v[8] = flags >> 24;
	v[9] = flags >> 16;
	v[10] = flags >> 8;
	v[11] = flags;

	return v;
}


bool RcpSocket::RcpHeader::deserialize(const void* data, size_t size) {
	RcpHeader h;
	h.sequenceNumber = h.batchNumber = h.flags = 0;

	if (size < 12) {
		return false;
	}
	auto cdata = (unsigned char*)data;

	sequenceNumber |= (cdata[0] << 24);
	sequenceNumber |= (cdata[1] << 16);
	sequenceNumber |= (cdata[2] << 8);
	sequenceNumber |= (cdata[3]);

	batchNumber |= (cdata[4] << 24);
	batchNumber |= (cdata[5] << 16);
	batchNumber |= (cdata[6] << 8);
	batchNumber |= (cdata[7]);

	flags |= (cdata[8] << 24);
	flags |= (cdata[9] << 16);
	flags |= (cdata[10] << 8);
	flags |= (cdata[11]);

	return true;
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
		case RcpSocket::CLOSED:
			ss << "disconnected";
			break;
		case RcpSocket::CONNECTED:
			ss << "connected to ";
			break;
		case RcpSocket::CLOSING:
			ss << "closing connection with ";
			break;
		default:
			break;
	}
	if (state != CONNECTED && state != CLOSING) {
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
	if (state != CLOSED) {
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
	state = CLOSED;
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
