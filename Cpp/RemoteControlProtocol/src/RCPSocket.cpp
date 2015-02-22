#include "RCPSocket.h"

#include <iostream>


RcpSocket::RcpSocket() {
	state = DISCONNECTED;
	socket.setBlocking(false);
	recvPred = false;
	cancelOp = false;
	localSeqNum = localBatchNum = 10000;
}

RcpSocket::~RcpSocket() {
	disconnect();
}


bool RcpSocket::bind(uint16_t port) {
	return socket.bind(port) == sf::UdpSocket::Done;
}

void RcpSocket::unbind() {
	if (state != CONNECTED) {
		socket.unbind();
	}
}

////////////////////////////////////////////////////////////////////////////////
// establish connection

bool RcpSocket::accept() {

	// do handshake
	// - wait for SYN
	// - send SYN-ACK
	// - wait for ACK
	// TODO...

	// test simple communication without this accept stuff
	return false;
}

bool RcpSocket::connect(std::string address, uint16_t port) {
	// only if not connected
	if (state != DISCONNECTED) {
		return false;
	}

	// set remote parameters
	remoteAddress = address;
	remotePort = port;

	// do handshake
	// - send SYN
	// - wait for SYN-ACK
	// - send ACK
	// TODO...

	// start io thread, set states
	timeLastSend = std::chrono::steady_clock::now();
	selector.add(socket);
	state = CONNECTED;
	startIoThread();

	return false;
}

void RcpSocket::disconnect() {
	// only if connected
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
	selector.clear();
	state = DISCONNECTED;
}


////////////////////////////////////////////////////////////////////////////////
// communicate


void RcpSocket::startIoThread() {
	runIoThread = true;
	ioThread = std::thread(
	[this]() {
		while (runIoThread) {
			// This block in brief:
			// select the event closest in time, and wait for it
			// events are the following (event -> action on timout):
			// - resend a packet which was not ACK-ed yet -> resend packet
			// - ACK for a repeatedly sent reliable packet must arrive -> connection lost
			// - should send a keepalive (timeLastSend) -> send a keepalive
			// - incoming message timout -> lost connection
			// - wait incoming reliable packet -> lost connection
			// if a message interrupts, pump the incoming message to the queue

			// select closest event in time
			enum eClosestEventType {
				ACK_RESEND,
				ACK_EXPIRE,
				KEEPALIVE,
				TIMEOUT,
			};
			std::chrono::microseconds eventRemaining(1000000000ULL);
			eClosestEventType eventType;

			// check packets waiting for ACK
			std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
			std::chrono::steady_clock::time_point oldestAck = now;
			std::chrono::steady_clock::time_point oldestSend = now;
			Packet resendPacket;
			uint32_t resendSeqnum = 0;
			uint32_t expireSeqnum = 0;
			bool isResend = false;
			bool isExpire = false;
			socketMutex.lock(); // lock mutex
			for (auto item : recentPackets) {
				if (item.second.lastResend < oldestAck) {
					oldestAck = item.second.lastResend;
					resendSeqnum = item.first;
					isResend = true;
				}
				if (item.second.send < oldestSend) {
					oldestSend = item.second.send;
					expireSeqnum = item.first;
					isExpire = true;
				}
			}
			if (isResend) {
				resendPacket = recentPackets.find(resendSeqnum)->second.packet;
				eventRemaining = std::chrono::duration_cast<std::chrono::microseconds>(oldestAck + std::chrono::milliseconds(200) - now);
				eventType = ACK_RESEND;
			}
			socketMutex.unlock(); // unlock mutex
			// check packets for expiring ACK
			std::chrono::microseconds expireRemaining;
			if (isExpire && (expireRemaining = std::chrono::duration_cast<std::chrono::microseconds>(oldestSend + std::chrono::milliseconds(5000) - now)) < eventRemaining) {
				eventRemaining = expireRemaining;
				eventType = ACK_EXPIRE;
			}
			// check keepalives
			std::chrono::microseconds keepaliveRemaining;
			if ((keepaliveRemaining = std::chrono::duration_cast<std::chrono::microseconds>(timeLastSend + std::chrono::milliseconds(200) - now)) < eventRemaining) {
				eventRemaining = keepaliveRemaining;
				eventType = KEEPALIVE;
			}
			// check timeout
			if (eventRemaining > std::chrono::microseconds(5000000)) {
				eventRemaining = std::chrono::microseconds(5000000);
			}

			// now we gotta sleep for 200 ms in a loop, not caring about normal waiting
			uint64_t timeWait = 200*1000; // in microseconds

			bool isData = selector.wait(sf::microseconds(/*timeWait*/eventRemaining.count()));
			if (!isData) {
				std::lock_guard<std::mutex> lk(socketMutex); // lock mutex (guarded)

				switch (eventType)
				{
					case ACK_RESEND: {


						break;
					}
					case ACK_EXPIRE: {


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
						timeLastSend = std::chrono::steady_clock::now();

						break;
					}
					case TIMEOUT: {
						// forcefully close the socket
						selector.clear();
						state = DISCONNECTED;
						return;
						break;
					}
				}
			}
			else {
				// pump the message to the message queue and notify
				// deal with special messages
				
				// lock mutex
				std::lock_guard<std::mutex> lk(socketMutex);

				// get message
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

				if (header.flags & KEP) {
					std::cout << getLocalPort() << ": kep" << std::endl;
					// keepalives are just... to keep the connection alive
					// don't do anything, just drop the packet
					continue;
				}
				else if (header.flags & ACK) {
					std::cout << getLocalPort() << ": ack: " << packet.getSequenceNumber() << std::endl;
					// remove the packet from the recent ack list
					auto it = recentPackets.find(packet.getSequenceNumber()); // ack's sequence number if the packet it acknowledges, not a real seqnum
					if (it != recentPackets.end()) {
						recentPackets.erase(it);
					}
					continue;
				}
				if (header.flags & REL) {
					std::cout << getLocalPort() << ": rel: " << packet.getSequenceNumber() << std::endl;
					// send an acknowledgement
					RcpHeader ackHeader;
					ackHeader.sequenceNumber = packet.getSequenceNumber();
					ackHeader.batchNumber = 0;
					ackHeader.flags = ACK;
					auto ackData = RcpHeader::serialize(ackHeader);
					socket.send(ackData.data(), ackData.size(), remoteAddress, remotePort);
				}
				
				// check originality of incoming packet:
				// - address must be correct
				// - port must be correct
				// - sequence number must be reasonal
				if (address != remoteAddress || port != remotePort) {
					continue;
				}

				// push packet
				recvQueue.push(packet);

				// set recv flag true
				recvPred = true;

				// unlock mutex and notify
				recvCondvar.notify_all();
			}

		}
	});
}


void RcpSocket::stopIoThread() {
	runIoThread = false;
	ioThread.join();
}


bool RcpSocket::send(Packet& packet) {
	// check erros
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
	if (packet.isReliable()) {
		recentPackets.insert(RecentPacketMapT::value_type(header.sequenceNumber, {packet, std::chrono::steady_clock::now(), std::chrono::steady_clock::now()}));
	}

	// set next batch and sequence numbers
	if (packet.isReliable()) {
		localBatchNum = localSeqNum;
	}
	localSeqNum++;

	// set last sending time to manage timeouts
	timeLastSend = std::chrono::steady_clock::now();

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
	if (isBlocking) {
		while (!cancelOp && !isData) {
			isData = recvCondvar.wait_for(lk, std::chrono::milliseconds(200), [this]{ return recvPred; });
		}
	}
	else {
		isData = recvPred;
	}

	if (!isData) {
		lk.unlock();
		return false;
	}

	// now we have the mutex again, modifying queue is safe
	assert(recvQueue.size() > 0); // failure means a bug in code, as this should never happen
	packet = recvQueue.front();
	recvQueue.pop();
	if (recvQueue.size() == 0) {
		recvPred = false;
	}
	
	// finally, unlock mutex
	lk.unlock();

	return true;
}



void RcpSocket::setBlocking(bool isBlocking) {
	this->isBlocking = isBlocking;
}



////////////////////////////////////////////////////////////////////////////////
// helpers

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


