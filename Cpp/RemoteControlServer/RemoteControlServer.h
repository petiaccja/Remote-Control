#pragma once

#include "ChannelManagerServo.h"
#include "ChannelAdapterServo.h"
#include "Message.h"

#include <RemoteControlProtocol/RcpSocket.h>
#include <RemoteControlProtocol/RcpPacket.h>

#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

class RemoteControlServer {
public:
	friend class TEST_RemoteControlServer; // ONLY FOR TESTING


	enum eConnectionState {
		DISCONNECTED,
		HALF_OPEN,
		AUTHENTICATED,
		CONNECTED,
	};

public:
	// --- --- ctor & dtor --- --- //
	RemoteControlServer();
	~RemoteControlServer();
	RemoteControlServer(const RemoteControlServer&) = delete;
	RemoteControlServer& operator=(const RemoteControlServer&) = delete;

	// --- --- network connection --- --- //

	/// Listen for incoming connection request from clients.
	/// \return True if a client is waiting for further action, false if an error
	/// occured and no connection was opened.
	bool Listen(int timeout = std::numeric_limits<int>::max());

	/// Optionally ask a client for password.
	/// Can only be called on a half open connection resulting after Listen completed.
	/// \return True if the client has given the correct password.
	bool Authenticate(int timeout = std::numeric_limits<int>::max());

	/// Reply to the clients connection request.
	/// Can only be called on a half open connection after Listen. Optionally you can
	/// Authenticate before answering, and decide to accept or deciline based on if the
	/// client knew the password.
	/// \param accept Whether to accept or deciline the client's request.
	/// \return True indicates that the connection was successfully established, false
	/// suggests a network error, the client misbehaving or simply declining the connection.
	bool Reply(bool accept, int timeout = std::numeric_limits<int>::max());

	/// Gracefully close an existing connection.
	void Disconnect();


	// --- --- connection parameters --- --- //

	void SetPassword(const std::vector<uint8_t>& password);
	const std::vector<uint8_t>& GetPassword() const;

	bool SetLocalPort(uint16_t port);
	uint16_t GetLocalPort() const;

	bool IsConnected() const;
	eConnectionState GetConnectionState() const;
	uint16_t GetRemotePort() const;
	std::string GetRemoteAddress() const;


	// --- --- manage hardware interfaces --- --- //

	ChannelManagerServo& GetManagerServo();
	const ChannelManagerServo& GetManagerServo() const;

	// DEBUG
	eConnectionState DBG_State() const { return state; }
	const std::thread& DBG_MessageThread() const { return messageThread; }
	const std::atomic_bool& DBG_RunMessageThread() const { return runMessageThread; }
	const RcpSocket& DBG_Socket() const { return socket; }

private:
	// --- --- message handlers --- --- //
	void MH_Authentication(const void* message, size_t length);
	void MH_Servo(const void* message, size_t length);
	void MH_DeviceEnum(const void* message, size_t length);
	void MH_ChannelEnum(const void* message, size_t length);

	// message processor thread
	void MessageThreadFunc();
	void StartMessageThread();
	void StopMessageThread();
private:
	// connection
	std::vector<uint8_t> password;
	std::atomic<eConnectionState> state;
	RcpSocket socket;

	// processing
	std::thread messageThread;
	std::atomic_bool runMessageThread = false;
	MessageDecoder messageDecoder;

	// answers to the client
	std::mutex answerQueueLock;
	std::queue<MessageBase*> answerQueue;

	// device channels
	ChannelManagerServo servoManager;
	ChannelAdapterServo servoAdapter;
};