#pragma once


class ChannelManagerServo;
struct ServoMessage;

////////////////////////////////////////////////////////////////////////////////
/// Translates commands to Servo Channel Manager function calls.
////////////////////////////////////////////////////////////////////////////////

class ChannelAdapterServo {
public:
	/// Create an adapter, optionally speficy manager.
	/// \param manager The manager to associate this adapter with.
	ChannelAdapterServo(ChannelManagerServo* manager = nullptr);
	ChannelAdapterServo(const ChannelAdapterServo&) = delete;
	ChannelAdapterServo& operator=(const ChannelAdapterServo&) = delete;

	/// Associate a manager to the adapter.
	void SetManager(ChannelManagerServo* manager);
	/// Get associated manager.
	ChannelManagerServo* GetManager() const;

	/// Process a message.
	/// \param message The message to be processed.
	/// \param result A reply to the original message. Can be reference to the same object as the message.
	/// \return True if there's an answer.
	bool ProcessCommand(const ServoMessage& message, ServoMessage& reply);
private:
	ChannelManagerServo* manager;
};