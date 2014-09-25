#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>

#include "Engine/Networking/Network.hpp"
#include "Engine/Utilities/EventSystem.hpp"

#include "CS6Packet.hpp"
#include "ConnectedClient.hpp"

//-----------------------------------------------------------------------------------------------
class Server
{

public:

	Server();
	~Server();

	void StartUp();
	void ShutDown();

	void Update();
	void Render() const;

private:

	void ReceiveMessagesFromClientsIfAny();
	void ProcessPacket( const CS6Packet& packet );
	void OnReceiveUpdatePacket( const CS6Packet& packet );
	void RemoveInactiveClients();

	void SendUpdatePacketsToAllClients();
	void BroadCastMessageToAllClients( const CS6Packet& messageAsPacket, int messageLength );
	void SendMessageToClient( const CS6Packet& messageAsPacket, ConnectedClient& clientToSendTo );

	void AddOrUpdateConnectedClient( const CS6Packet& packet );

	void RenderConnectedClients() const;

	int m_listenConnectionID;

	std::string m_currentServerIPAddressAsString;
	u_short		m_currentServerPort;

	std::vector< ConnectedClient > m_connectedAndActiveClients;
};

#endif