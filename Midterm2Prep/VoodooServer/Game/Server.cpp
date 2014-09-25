#include "Server.hpp"

#include <sstream>
#include <algorithm>

#include "Engine/Rendering/OpenGLRenderer.hpp"
#include "Engine/Rendering/BitmapFont.hpp"
#include "Engine/Primitives/Vector3.hpp"
#include "Engine/Utilities/Clock.hpp"
#include "Engine/Utilities/Time.hpp"

const float MAX_SECONDS_OF_INACTIVITY = 5.f;
const float RESEND_RELIABLE_PACKET_TIME = 2.f;
const float SEND_DELAY = 0.05f;

const char* IP_AS_STRING = "127.0.0.1";
const u_short STARTING_PORT = 5000;

float g_currentSendElapsedTime = 0.f;

const PlayerID BAD_IT_PLAYER = Color( Black );

//-----------------------------------------------------------------------------------------------
//Public Methods
//-----------------------------------------------------------------------------------------------
Server::Server()
	: m_listenConnectionID( 0 )
	, m_currentServerIPAddressAsString( IP_AS_STRING )
	, m_currentServerPort( STARTING_PORT )
	, m_currentItPlayerID( BAD_IT_PLAYER )
{

}


//-----------------------------------------------------------------------------------------------
Server::~Server()
{

}


//-----------------------------------------------------------------------------------------------
void Server::StartUp()
{
	Network& theNetwork = Network::GetInstance();

	m_listenConnectionID = theNetwork.CreateUDPSocketFromIPAndPort( "0.0.0.0", m_currentServerPort );
	theNetwork.BindSocket( m_listenConnectionID );
}


//-----------------------------------------------------------------------------------------------
void Server::ShutDown()
{
	Network& theNetwork = Network::GetInstance();

	theNetwork.ShutDown();
}


//-----------------------------------------------------------------------------------------------
void Server::Update()
{
	Clock& appClock = Clock::GetMasterClock();

	ReceiveMessagesFromClientsIfAny();
	RemoveInactiveClients();
	ResendAnyGuaranteedPacketsThatHaveTimedOut();
	SendUpdatePacketsToAllClients();

	g_currentSendElapsedTime += static_cast< float >( appClock.m_currentDeltaSeconds );
}


//-----------------------------------------------------------------------------------------------
void Server::Render() const
{
	RenderConnectedClients();
}


//-----------------------------------------------------------------------------------------------
//Private Methods
//-----------------------------------------------------------------------------------------------
void Server::ReceiveMessagesFromClientsIfAny()
{
	Network& theNetwork = Network::GetInstance();

	int bytesReceived = 0;
	CS6Packet receivedPacket;

	do 
	{
		bytesReceived = 0;
		ZeroMemory( &receivedPacket, sizeof( receivedPacket ) );

		bytesReceived = theNetwork.ReceiveUDPMessage( ( char* )&receivedPacket, sizeof( receivedPacket ), m_listenConnectionID );

		if( bytesReceived == sizeof( CS6Packet ) )
		{
			ProcessPacket( receivedPacket );
		}
	} 
	while ( bytesReceived > 0 );
}


//-----------------------------------------------------------------------------------------------
void Server::ProcessPacket( const CS6Packet& packet )
{
	PacketType typeOfPacket = packet.packetType;

	if( typeOfPacket == TYPE_Update )
	{
		OnReceiveUpdatePacket( packet );
	}
	else if( typeOfPacket == TYPE_Reset )
	{
		//OnReceiveResetPacket( packet );
	}
	else if( typeOfPacket == TYPE_Acknowledge )
	{
		OnReceiveAckPacket( packet );
	}
	else if( typeOfPacket == TYPE_GameStart )
	{
		//OnReceiveGameStartPacket( packet );
	}
	else if( typeOfPacket == TYPE_Victory )
	{
		//OnReceiveVictoryPacket( packet );
	}


	 //this will need to be moved to on a received ack of type ack!
}


//-----------------------------------------------------------------------------------------------
void Server::OnReceiveUpdatePacket( const CS6Packet& packet )
{
	AddOrUpdateConnectedClient( packet );
}


//-----------------------------------------------------------------------------------------------
void Server::OnReceiveAckPacket( const CS6Packet& packet )
{
	PacketType typeOfAck = packet.data.acknowledged.packetType;

	if( typeOfAck == TYPE_Acknowledge )
	{
		OnAckAcknowledge( packet );
	}
	else if( typeOfAck == TYPE_GameStart )
	{
		//FUTURE EDIT
	}
}


//-----------------------------------------------------------------------------------------------
void Server::OnAckAcknowledge( const CS6Packet& packet )
{
	AddOrUpdateConnectedClient( packet );
}


//-----------------------------------------------------------------------------------------------
void Server::OnAckGameStart( const CS6Packet& packet )
{
	std::string ipAddressOfClient = "";
	std::string portAsString = "";

	Network& theNetwork = Network::GetInstance();

	ipAddressOfClient = theNetwork.GetIPAddressAsStringFromConnection( m_listenConnectionID );
	portAsString = theNetwork.GetPortAsStringFromConnection( m_listenConnectionID );

	if( ipAddressOfClient == "" || portAsString == "" )
	{
		return;
	}

	std::string idOfClient = portAsString + ipAddressOfClient;

	auto foundIter = m_connectedAndActiveClients.begin();

	for( ; foundIter != m_connectedAndActiveClients.end(); ++foundIter )
	{
		if( foundIter->clientID == idOfClient )
		{
			break;
		}
	}

	if( foundIter != m_connectedAndActiveClients.end() )
	{
		foundIter->m_reliablePacketsAwaitingAckBack.erase( packet.data.acknowledged.packetNumber );
	}
}


//-----------------------------------------------------------------------------------------------
void Server::RemoveInactiveClients()
{
	Clock& appClock = Clock::GetMasterClock();
	float deltaSeconds = static_cast< float >( appClock.m_currentDeltaSeconds );

	for( int i = 0; i < static_cast< int >( m_connectedAndActiveClients.size() ); ++ i )
	{
		m_connectedAndActiveClients[ i ].timeSinceLastReceivedMessage += deltaSeconds;

		if( m_connectedAndActiveClients[ i ].timeSinceLastReceivedMessage >= MAX_SECONDS_OF_INACTIVITY )
		{
			if( i < static_cast< int >( m_connectedAndActiveClients.size() ) -1 )
			{
				m_connectedAndActiveClients[ i ] = m_connectedAndActiveClients.back();
				--i;
			}

			m_connectedAndActiveClients.pop_back();
		}
	}
}


//-----------------------------------------------------------------------------------------------
void Server::ResendAnyGuaranteedPacketsThatHaveTimedOut()
{
	CS6Packet packetToSend;
	Network& theNetwork = Network::GetInstance();

	float currentTime = Time::GetCurrentTimeInSeconds();

	for( auto iter = m_connectedAndActiveClients.begin(); iter != m_connectedAndActiveClients.end(); ++iter )
	{
		for( auto guaranteedPacketIter = iter->m_reliablePacketsAwaitingAckBack.begin(); guaranteedPacketIter != iter->m_reliablePacketsAwaitingAckBack.end(); ++ guaranteedPacketIter )
		{
			if( currentTime - guaranteedPacketIter->second.timestamp > RESEND_RELIABLE_PACKET_TIME )
			{
				packetToSend = guaranteedPacketIter->second;

				packetToSend.timestamp = Time::GetCurrentTimeInSeconds();

				theNetwork.SetIPAddressAsStringForConnection( m_listenConnectionID, iter->ipAddressAsString );
				theNetwork.SetPortAsStringForConnection( m_listenConnectionID,iter->portAsString );

				theNetwork.SendUDPMessage( ( char* )&packetToSend, sizeof( packetToSend ), m_listenConnectionID );
			}
		}
	}
};


//-----------------------------------------------------------------------------------------------
void Server::SendUpdatePacketsToAllClients()
{
	CS6Packet updatePacket;

	updatePacket.packetType = TYPE_Update;

	for( auto iter = m_connectedAndActiveClients.begin(); iter != m_connectedAndActiveClients.end(); ++iter )
	{
		updatePacket.data.updated = iter->mostRecentUpdateInfo;
		memcpy( &updatePacket.playerColorAndID, &iter->playerIDAsRGB, sizeof( updatePacket.playerColorAndID ) );
		
		BroadCastMessageToAllClients( updatePacket, sizeof( updatePacket ) );
	}
}


//-----------------------------------------------------------------------------------------------
void Server::BroadCastMessageToAllClients( const CS6Packet& messageAsPacket, int messageLength )
{
	for( auto iter = m_connectedAndActiveClients.begin(); iter != m_connectedAndActiveClients.end(); ++iter )
	{
		SendMessageToClient( messageAsPacket, *iter );
	}
}


//-----------------------------------------------------------------------------------------------
void Server::SendAGameStartPacketToNewClient( ConnectedClient& clientToSendTo )
{
	CS6Packet packetToSend;

	ZeroMemory( &packetToSend, sizeof( packetToSend ) );

	packetToSend.packetType = TYPE_GameStart;
	packetToSend.data.gameStart.playerXPosition = rand() % 500;
	packetToSend.data.gameStart.playerYPosition = rand() % 500;

	if( m_connectedAndActiveClients.size() <= 1 )
	{
		m_currentItPlayerID = clientToSendTo.playerIDAsRGB;
	}

	memcpy( &packetToSend.data.gameStart.itPlayerColorAndID, &m_currentItPlayerID, sizeof( packetToSend.data.gameStart.itPlayerColorAndID ) );
	memcpy( &packetToSend.data.gameStart.playerColorAndID, &clientToSendTo.playerIDAsRGB, sizeof( packetToSend.data.gameStart.playerColorAndID ) );

	SendMessageToClient( packetToSend, clientToSendTo );
};


//-----------------------------------------------------------------------------------------------
void Server::SendMessageToClient( const CS6Packet& messageAsPacket, ConnectedClient& clientToSendTo )
{
	Network& theNetwork = Network::GetInstance();

	CS6Packet packetToSend = messageAsPacket;

	if( messageAsPacket.IsReliablePacket() )
	{
		++clientToSendTo.numReliableMessagesSent;
		packetToSend.packetNumber = clientToSendTo.numReliableMessagesSent;

		clientToSendTo.m_reliablePacketsAwaitingAckBack[ packetToSend.packetNumber ] = packetToSend; 
	}
	else
	{
		++clientToSendTo.numUnreliableMessagesSent;
		packetToSend.packetNumber = clientToSendTo.numUnreliableMessagesSent;
	}

	packetToSend.timestamp = Time::GetCurrentTimeInSeconds();

	theNetwork.SetIPAddressAsStringForConnection( m_listenConnectionID, clientToSendTo.ipAddressAsString );
	theNetwork.SetPortAsStringForConnection( m_listenConnectionID, clientToSendTo.portAsString );

	theNetwork.SendUDPMessage( ( char* )&messageAsPacket, sizeof( messageAsPacket ), m_listenConnectionID );
}


//-----------------------------------------------------------------------------------------------
void Server::AddOrUpdateConnectedClient( const CS6Packet& packet )
{
	std::string ipAddressOfClient = "";
	std::string portAsString = "";

	Network& theNetwork = Network::GetInstance();

	ipAddressOfClient = theNetwork.GetIPAddressAsStringFromConnection( m_listenConnectionID );
	portAsString = theNetwork.GetPortAsStringFromConnection( m_listenConnectionID );

	if( ipAddressOfClient == "" || portAsString == "" )
	{
		return;
	}

	std::string idOfClient = portAsString + ipAddressOfClient;


	auto iter = m_connectedAndActiveClients.begin();

	for( ; iter != m_connectedAndActiveClients.end(); ++iter )
	{
		if( idOfClient == iter->clientID )
		{
			iter->timeSinceLastReceivedMessage = 0.f;

			if( packet.packetType == TYPE_Update )
			{
				iter->mostRecentUpdateInfo = packet.data.updated;
			}
			
			break;
		}
	}

	if( packet.packetType != TYPE_Acknowledge || packet.data.acknowledged.packetType != TYPE_Acknowledge )
	{
		return;
	}

	if( iter == m_connectedAndActiveClients.end() )
	{
		ConnectedClient newConnectedClient( ipAddressOfClient, portAsString );
		newConnectedClient.playerIDAsRGB = Color( uchar( m_connectedAndActiveClients.size() ) );

		if( packet.packetType == TYPE_Update )
		{
			newConnectedClient.mostRecentUpdateInfo = packet.data.updated;
		}

		m_connectedAndActiveClients.push_back( newConnectedClient );

		SendAGameStartPacketToNewClient( newConnectedClient );
	}

	//Loop thru active clients and update the client whose id matches, else add new client to list

}


//-----------------------------------------------------------------------------------------------
void Server::RenderConnectedClients() const
{
	//do this later

	const float fontSize = 24.f;
	const Vector3f offsetVector( 0.f, fontSize, 0.f );
	const Vector3f initialPosition( 0.f, 650.f, 0.f ); //LAZY

	BitmapFont* font = BitmapFont::CreateOrGetFont( "Data/Fonts/MainFont_EN_00.png", "Data/Fonts/MainFont_EN.FontDef.xml" );
	std::ostringstream outputStringStream;
	std::string stringToRender;

	Vector3f textPosition = initialPosition;

	outputStringStream.str( "" );
	outputStringStream << "Connected Clients:";
	stringToRender = outputStringStream.str();

	OpenGLRenderer::RenderText( stringToRender, font, fontSize, textPosition );
	textPosition -= offsetVector;

	auto iter = m_connectedAndActiveClients.begin();

	for( ; iter != m_connectedAndActiveClients.end(); ++iter )
	{
		outputStringStream.str( "" );
		outputStringStream << "Client Address: " << iter->ipAddressAsString << ", Port: " << iter->portAsString;
		stringToRender = outputStringStream.str();

		OpenGLRenderer::RenderText( stringToRender, font, fontSize, textPosition );
		textPosition -= offsetVector;
	}

	if( m_connectedAndActiveClients.size() == 0 )
	{
		outputStringStream.str( "" );
		outputStringStream << "None.";
		stringToRender = outputStringStream.str();

		OpenGLRenderer::RenderText( stringToRender, font, fontSize, textPosition );
	}
	

}