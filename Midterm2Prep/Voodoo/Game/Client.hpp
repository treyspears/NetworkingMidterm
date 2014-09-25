#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <set>

#include "Engine/Networking/Network.hpp"
#include "Engine/Utilities/EventSystem.hpp"

#include "CS6Packet.hpp"
#include "ClientPlayer.hpp"

class PacketComparator
{
public:

	bool operator () ( const CS6Packet& lhs, const CS6Packet& rhs ) const
	{
		return lhs.packetNumber > rhs.packetNumber;
	}
};


class Client
{

public:

	Client();
	~Client();

	void	StartUp();
	void	ShutDown();

	void	UpdateLocalPlayerMovementMagnitude( float magnitude );
	void	Update();

	void	Render() const;

	void	ChangeHost( const std::string& ipAddressAsString, const std::string& portAsString );

	ClientPlayer&					GetLocalPlayer();
	std::vector< ClientPlayer >&	GetOtherClientsPlayers();

	inline void	RegisterForEvents();

private:

	void		ReceiveMessagesFromHostIfAny();
	void		UpdatePlayers();
	void		PotentiallySendUpdatePacketToServer();

	void		RenderPlayers() const;
	void		RenderPlayer( const ClientPlayer& playerToRender ) const;

	void		ProcessPacket( const CS6Packet& packet );
	void		ProcessAnyQueuedReliablePackets();

	void		OnReceivedUpdatePacket( const CS6Packet& updatePacket );
	void		OnRecieveGameStartPacket( const CS6Packet& updatePacket );
	void		OnReceiveResetPacket( const CS6Packet& updatePacket );
	void		OnReceiveVictoryPacket( const CS6Packet& updatePacket );
	void		OnReceiveAckPacket( const CS6Packet& updatePacket );

	CS6Packet	GetUpdatePacketFromPlayer( const ClientPlayer& player );

	void		SetPlayerParameters( NamedProperties& parameters );
	void		SetServerIpFromParameters( NamedProperties& parameters );
	void		SetServerPortFromParameters( NamedProperties& parameters );


	int							m_connectionToHostID;

	std::string					m_currentHostIPAddressAsString;
	u_short						m_currentHostPort;

	float						m_localPlayerMovementMagnitude;
	ClientPlayer				m_localPlayer;
	std::vector< ClientPlayer >	m_otherClientsPlayers;

	float						m_sendPacketsToHostFrequency;

	uint						m_mostRecentlyProcessedUnreliablePacketNum;
	uint						m_nextExpectedReliablePacketNumToProcess;

	std::set< CS6Packet, PacketComparator > m_queueOfReliablePacketsToParse;
};

//-----------------------------------------------------------------------------------------------
inline void Client::RegisterForEvents()
{
	EventSystem& eventSystem = EventSystem::GetInstance();

	eventSystem.RegisterEventWithCallbackAndObject( "playerColor", &Client::SetPlayerParameters, this );
	eventSystem.RegisterEventWithCallbackAndObject( "ip", &Client::SetServerIpFromParameters, this );
	eventSystem.RegisterEventWithCallbackAndObject( "port", &Client::SetServerPortFromParameters, this );
}

#endif