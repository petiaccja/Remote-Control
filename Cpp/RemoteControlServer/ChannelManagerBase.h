#pragma once

#include "IProviderBase.h"
#include <map>
#include <set>
#include <type_traits>

////////////////////////////////////////////////////////////////////////////////
/// ChannelManagerBase contains the base code for maintaining channel to provider
/// mappings.
/// Channel Managers maintain a list of abstract channels. A channel can be a 
/// physical or virtual output or input, such as a servo controller, pwm signal,
/// or various other devices. The Channel Manager shows an interface to interact
/// with the abstract device by manipulating the channels. Under the hood, 
/// channels are realized by providers. A Provider must satisfy a certain interface,
/// then it can be registered with the Channel Manager to realize certain channels.
/// This class is not usable alone, inherit from it to create specific managers
/// for each hardware device type.
////////////////////////////////////////////////////////////////////////////////

template <class ProviderT>
class ChannelManagerBase {
	// ProviderT must inherit from IProviderBase.
	static_assert(std::is_base_of<IProviderBase, ProviderT>::value, "Providers must inherit from IProviderBase to have required interface.");

	/// Helper structure to store a channel->provider:port mapping record.
	struct ChannelMapping {
		ChannelMapping(int channel = -1, ProviderT* provider = nullptr, int port = 0) 
			: channel(channel), provider(provider), port(port) {}
		int channel;
		ProviderT* provider;
		int port;

		bool operator<(const ChannelMapping& rhs) const { return channel < rhs.channel; }
	};
public:
	/// Iterator over registered hardware providers.
	class ProviderIterator : public std::map<ProviderT*, int>::const_iterator {
	public:
		using std::map<ProviderT*, int>::const_iterator::const_iterator;
		ProviderIterator(typename std::map<ProviderT*, int>::const_iterator it) : 
			std::map<ProviderT*, int>::const_iterator(it) {}
		ProviderT* operator*() { 
			return std::map<ProviderT*, int>::const_iterator::operator->()->first;
		}
		ProviderT** operator->() {
			return &(std::map<ProviderT*, int>::const_iterator::operator->()->first);
		}
	};
	/// Iterator over each channel of each provider.
	/// Goes through channels in order of channel identifier.
	using ChannelIterator = typename std::set<ChannelMapping>::const_iterator;

	/// Add a provider.
	/// \param provider Pointer to the provider class.
	/// \param startChannel The channel to which to bind the provider's first port.
	/// \return False if there was a collision of channels.
	bool AddProvider(ProviderT* provider, int startChannel);
	/// Remove a registered provider.
	/// \param provider Provider to remove.
	void RemoveProvider(ProviderT* provider);
	/// Remove all providers.
	void ClearProviders();
	/// Get the number of providers currently registered.
	/// \return Number of providers currently registered.
	size_t GetNumProviders() const;
	/// Get the number of channels.
	/// \return The number of channels.
	size_t GetNumChannels() const;
	/// Get iterator to the first provider.
	ProviderIterator ProviderBegin() const;
	/// Get iterator to the provider past the last provider.
	ProviderIterator ProviderEnd() const;
	/// Get iterator to the first channel.
	ChannelIterator ChannelBegin() const;
	/// Get iterator to the channel past the last channel.
	ChannelIterator ChannelEnd() const;
	/// Get iterator to arbitrary channel
	ChannelIterator FindChannel(int channel) const;

protected:
	bool FindChannel(int channel, ProviderT*& provider, int& port);
private:
	std::set<ChannelMapping> channelMappings;
	std::map<ProviderT*, int> startChannels;
};


template <class ProviderT>
bool ChannelManagerBase<ProviderT>::AddProvider(ProviderT* provider, int startChannel) {
	int numPorts = provider->GetNumPorts();

	// register new provider with its start channel
	auto insres = startChannels.insert({provider, startChannel});
	if (!insres.second) { // error if provider is already registered
		return false;
	}

	// set channel mappings
	for (int i = 0; i < numPorts; i++) {
		auto insresc = channelMappings.insert({ startChannel + i, provider, i });
		if (!insresc.second) { // error: channel already taken
			// perform a rollback and break the loop
			for (int j = i-1; j >= 0; j--) {
				channelMappings.erase(startChannel + j);
			}
			return false;
		}
	}

	return true;
}


template <class ProviderT>
void ChannelManagerBase<ProviderT>::RemoveProvider(ProviderT* provider) {
	auto it = startChannels.find(provider);
	if (it == startChannels.end()) {
		return;
	}

	int numPorts = it->first->GetNumPorts();
	for (int i = 0; i < numPorts; i++) {
		channelMappings.erase(it->second + i); // start port + index
	}

	startChannels.erase(it);
}


template <class ProviderT>
void ChannelManagerBase<ProviderT>::ClearProviders() {
	channelMappings.clear();
	startChannels.clear();
}


template <class ProviderT>
size_t ChannelManagerBase<ProviderT>::GetNumProviders() const {
	return startChannels.size();
}

template <class ProviderT>
size_t ChannelManagerBase<ProviderT>::GetNumChannels() const {
	return channelMappings.size();
}

template <class ProviderT>
auto ChannelManagerBase<ProviderT>::ProviderBegin() const ->ProviderIterator {
	return startChannels.begin();
}


template <class ProviderT>
auto ChannelManagerBase<ProviderT>::ProviderEnd() const ->ProviderIterator {
	return startChannels.end();
}


template <class ProviderT>
bool ChannelManagerBase<ProviderT>::FindChannel(int channel, ProviderT*& provider, int& port) {
	auto it = channelMappings.find(channel);
	if (it == channelMappings.end()) {
		return false;
	}
	else {
		provider = it->provider;
		port = it->port;
		return true;
	}
}

template <class ProviderT>
auto ChannelManagerBase<ProviderT>::ChannelBegin() const ->ChannelIterator {
	return channelMappings.cbegin();
}

template <class ProviderT>
auto ChannelManagerBase<ProviderT>::ChannelEnd() const ->ChannelIterator {
	return channelMappings.cend();
}

template <class ProviderT>
auto ChannelManagerBase<ProviderT>::FindChannel(int channel) const ->ChannelIterator {
	return channelMappings.find(channel);
}