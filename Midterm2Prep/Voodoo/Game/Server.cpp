#include "Server.hpp"

#include <sstream>

#include "Engine/Rendering/OpenGLRenderer.hpp"
#include "Engine/Rendering/BitmapFont.hpp"
#include "Engine/Primitives/Vector3.hpp"
#include "Engine/Utilities/Clock.hpp"
#include "Engine/Utilities/Time.hpp"

const float MAX_SECONDS_OF_INACTIVITY = 5.f;
const float SEND_DELAY = 0.05f;

const char* IP_AS_STRING = "127.0.0.1";
const u_short STARTING_PORT = 5000;

float g_currentSendElapsedTime = 0.f;

//-----------------------------------------------------------------------------------------------
//Public Methods
//-----------------------------------------------------------------------------------------------
Server::Server()
	: m_listenConnectionID( 0 )
	, m_currentServerIPAddressAsString( IP_AS_STRING )
	, m_currentServerPort( STARTING_PORT )
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
			AddOrUpdateConnectedClient( receivedPacket ); //this will need to be moved to on a received ack of type ack!
			ProcessPacket( receivedPacket );
		}
	} 
	while ( bytesReceived > 0 );
}


//-----------------------------------------------------------------------------------------------
void Server::ProcessPacket( const CS6Packet& packet )
{

}


//-----------------------------------------------------------------------------------------------
void Server::OnReceiveUpdatePacket( const CS6Packet& packet )
{

}


//-----------------------------------------------------------------------------------------------
void Server::SendUpdatePacketsToAllClients()
{
	CS6Packet updatePacket;

	updatePacket.packetType = TYPE_Update;
	updatePacket.timestamp = Time::GetCurrentTimeInSeconds();

	for( auto iter = m_connectedAndActiveClients.begin(); iter != m_connectedAndActiveClients.end(); ++iter )
	{
		updatePacket.data.updated = iter->mostRecentUpdateInfo;
		//updatePacket.packetNumber = iter->numMessagesSent;
		memcpy( &updatePacket.playerColorAndID, &iter->playerIDAsRGB, sizeof( updatePacket.playerColorAndID ) );

		BroadCastMessageToAllClients( ( char* )&updatePacket, sizeof( updatePacket ) );
	}
}


//-----------------------------------------------------------------------------------------------
void Server::BroadCastMessageToAllClients( void* message, int messageLength )
{
	for( auto iter = m_connectedAndActiveClients.begin(); iter != m_connectedAndActiveClients.end(); ++iter )
	{
		SendMessageToClient( message, messageLength, *iter );
	}
}


//-----------------------------------------------------------------------------------------------
void Server::SendMessageToClient( void* message, int messageLength, const ConnectedClient& clientToSendTo )
{
	Network& theNetwork = Network::GetInstance();

	theNetwork.SetIPAddressAsStringForConnection( m_listenConnectionID, clientToSendTo.ipAddressAsString );
	theNetwork.SetPortAsStringForConnection( m_listenConnectionID, clientToSendTo.portAsString );

	theNetwork.SendUDPMessage( message, messageLength, m_listenConnectionID );
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


	if( iter == m_connectedAndActiveClients.end() )
	{
		ConnectedClient newConnectedClient( ipAddressOfClient, portAsString );
		memcpy( &newConnectedClient.playerIDAsRGB, &packet.playerColorAndID, sizeof( packet.playerColorAndID ) );

		if( packet.packetType == TYPE_Update )
		{
			newConnectedClient.mostRecentUpdateInfo = packet.data.updated;
		}

		m_connectedAndActiveClients.push_back( newConnectedClient );
	}

	//if( iter == m_connectedAndActiveClients.end() )
	//{
	//	ConnectedClient newConnectedClient( ipAddressOfClient, portAsString );
	//	newConnectedClient.playerIDAsRGB = Color( uchar( m_connectedAndActiveClients.size() ) );

	//	memcpy( &newConnectedClient.playerIDAsRGB, &newConnectedClient.playerIDAsRGB, sizeof( packet.playerColorAndID ) );

	//	if( packet.packetType == TYPE_Update )
	//	{
	//		newConnectedClient.mostRecentUpdateInfo = packet.data.updated;
	//	}

	//	m_

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