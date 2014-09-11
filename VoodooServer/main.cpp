#define WIN32_LEAN_AND_MEAN
#define UNUSED(x) (void)(x);

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <vector>
#include <map>
#include <time.h>

#include "Time.hpp"
#include "CS6Packet.hpp"

#pragma comment( lib, "WS2_32" ) // Link in the WS2_32.lib static library

//-----------------------------------------------------------------------------------------------

struct uniqueClient
{
	uniqueClient()
		: mostRecentUpdateInfo()
		, ID( "" )
		, ipAddressAsString( "" )
		, portAsString( "" )
		, timeSinceLastRecieve( 0.f )
		, numMessagesSent( 0 )
	{
		ZeroMemory( &mostRecentUpdateInfo, sizeof( mostRecentUpdateInfo ) );
		ZeroMemory( &colorID, sizeof( colorID ) );
	}

	uniqueClient( const std::string& ipAddress, const std::string& port )
		: mostRecentUpdateInfo()
		, ID( port + ipAddress )
		, ipAddressAsString( ipAddress )
		, portAsString( port )
		, timeSinceLastRecieve( 0.f )
		, numMessagesSent( 0 )
	{
		ZeroMemory( &mostRecentUpdateInfo, sizeof( mostRecentUpdateInfo ) );
		ZeroMemory( &colorID, sizeof( colorID ) );
	}

	uniqueClient( const std::string& ipAddress, const std::string& port, const UpdatePacket& incomingPacket )
		: mostRecentUpdateInfo( incomingPacket )
		, ID( port + ipAddress )
		, ipAddressAsString( ipAddress )
		, portAsString( port )
		, timeSinceLastRecieve( 0.f )
		, numMessagesSent( 0 )
	{
		ZeroMemory( &colorID, sizeof( colorID ) );
	}

	std::string ID;
	std::string ipAddressAsString;
	std::string portAsString;
	UpdatePacket mostRecentUpdateInfo;
	unsigned char colorID[ 3 ];
	float timeSinceLastRecieve;
	int numMessagesSent;

};

const int BUFFER_LIMIT = 512;
const float MAX_SECONDS_OF_INACTIVITY = 5.f;
const float SEND_DELAY = 0.05f;
const float RAND_MAX_INVERSE = 1.f / (float)RAND_MAX;
const float DISPLAY_CONNECTED_USERS_TIME = 2.f;

const char BAD_COLOR[ 3 ] = { 0, 0, 0 };

const float RESEND_GUARANTEED_MAX_DELAY = 0.25f;

float currentDisplayConnectedUsersElapsedTime = 0.f;
float currentSendElapsedTime = 0.f;

u_long BLOCKING = 0;
u_long NON_BLOCKING = 1;


//-----------------------------------------------------------------------------------------------
void ToUpperCaseString( std::string& outString );

void PromptUserForIPAndPortInfo( std::string& ipAddress_out, std::string& port_out );
bool InitializeWinSocket();
bool CreateAddressInfoForUDP( const std::string& ipAddress, const std::string& udpPort, sockaddr_in& udpAddressInfo );
bool CreateUDPSocket( SOCKET& newSocket, sockaddr_in& addressInfo );
bool BindSocket( SOCKET& socketToBind, sockaddr_in& addressInfo );

bool UDPSendMessageServer( const SOCKET& socketToSendThru, const sockaddr_in& sendToAddrInfo, const char* message, int messageLength );
bool UDPReceiveMessageServer( const SOCKET& socketToRecieveMessageThru );
bool RunUDPServer( SOCKET& udpServerSocket );

void AddClientToListOfConnectedClients( const sockaddr_in& senderInfo, const char* receiveBuffer );
void UDPServerRemoveInactiveClients();
void UDPServerDisplayConnectedClients();
void UDPServerBroadcastUpdatePackets( SOCKET& udpSocket );
void UDPServerUpdateClientTimes();

void ProcessMessageFromClient( const CS6Packet& packet, const sockaddr_in& senderInfo );
void OnReceiveUpdate( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnReceiveGameStart( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnReceiveReset( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnReceiveAck( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnReceiveVictory( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnAckAcknowledge( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnAckGameStart( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );
void OnAckReset( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo );

void ResendAnyGuaranteedPacketsIfThresholdIsMet();

void GetColor( int index, unsigned char* colorArray );

std::string ipAddressAsString;
std::string portAsString;

sockaddr_in UDPServerAddrInfo;
std::vector< uniqueClient > g_relevantClients;

SOCKET UDPServerSocket = INVALID_SOCKET;

unsigned char g_currentItPlayer[ 3 ];

//-----------------------------------------------------------------------------------------------
int __cdecl main(int argc, char **argv)
{
	UNUSED( argc );
	UNUSED( argv );

	srand( ( unsigned int )time ( NULL ) );
	
	Time::InitializeTime();
	PromptUserForIPAndPortInfo( ipAddressAsString, portAsString );

	bool serverRunResult = true;

	serverRunResult = InitializeWinSocket();
	serverRunResult &= CreateAddressInfoForUDP( ipAddressAsString, portAsString, UDPServerAddrInfo );
	serverRunResult &= CreateUDPSocket( UDPServerSocket, UDPServerAddrInfo );
	ioctlsocket( UDPServerSocket, FIONBIO, &NON_BLOCKING );
	serverRunResult &= BindSocket( UDPServerSocket, UDPServerAddrInfo );

	if( serverRunResult )
	{
		RunUDPServer( UDPServerSocket );

		shutdown( UDPServerSocket, SD_BOTH );
		closesocket( UDPServerSocket );
		WSACleanup();
	}

	system("pause");
	if( !serverRunResult )
	{
		return 1;
	}
	return 0;
}


//-----------------------------------------------------------------------------------------------
void ToUpperCaseString( std::string& outString )
{
	for(int i = 0; i < (int)outString.length(); ++ i)
	{
		outString[i] = static_cast<char>(toupper(static_cast<int>(outString[i])));
	}
}


//-----------------------------------------------------------------------------------------------
void PromptUserForIPAndPortInfo( std::string& ipAddress_out, std::string& port_out )
{
	std::printf( "Enter IP Address for the server: " );
	std::cin >> ipAddress_out;

	std::printf( "IP Address of the server is %s\n\n", ipAddress_out.c_str() );

	std::printf( "Enter Port for server: " );
	std::cin >> port_out;

	std::printf( "Port of the server is %s\n\n", port_out.c_str() );
}


//-----------------------------------------------------------------------------------------------
bool InitializeWinSocket()
{
	WSAData winSocketAData;

	int initializationResult;
	initializationResult = WSAStartup( MAKEWORD( 2,2 ), &winSocketAData );

	if( initializationResult != 0 )
	{
		printf( "WSAStartup failed %d\n", initializationResult );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------------------------
bool CreateAddressInfoForUDP( const std::string& ipAddress, const std::string& udpPort, sockaddr_in& udpAddressInfo )
{
	UNUSED( ipAddress );

	ZeroMemory( &udpAddressInfo, sizeof( udpAddressInfo ) );
	udpAddressInfo.sin_family = AF_INET;
	udpAddressInfo.sin_addr.S_un.S_addr = INADDR_ANY;
	udpAddressInfo.sin_port = htons( u_short( atoi( udpPort.c_str() ) ) );

	return true;
}


//-----------------------------------------------------------------------------------------------
bool CreateUDPSocket( SOCKET& newSocket, sockaddr_in& addressInfo )
{
	char optval;

	newSocket = socket( addressInfo.sin_family, SOCK_DGRAM, IPPROTO_UDP );
	setsockopt( newSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof( optval ) );

	if( newSocket == INVALID_SOCKET )
	{
		printf( "socket failed with error: %ld\n", WSAGetLastError() );

		shutdown( newSocket, SD_BOTH );
		closesocket( newSocket );
		WSACleanup();

		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------------------------
bool BindSocket( SOCKET& socketToBind, sockaddr_in& addressInfo )
{
	int bindResultAsInt;

	bindResultAsInt = bind( socketToBind, (struct sockaddr *)&addressInfo, sizeof( addressInfo ) );

	if( bindResultAsInt == SOCKET_ERROR )
	{
		printf( "bind failed with error: %d\n", WSAGetLastError() );

		shutdown( socketToBind, SD_BOTH );
		closesocket( socketToBind );
		WSACleanup();

		return false;
	}	

	return true;
}


//-----------------------------------------------------------------------------------------------
bool UDPSendMessageServer( const SOCKET& socketToSendThru, const sockaddr_in& sendToAddrInfo, const char* message, int messageLength )
{
	int sendResultAsInt;

	sendResultAsInt = sendto( socketToSendThru, message, messageLength, 0, (struct sockaddr *)&sendToAddrInfo, sizeof( sockaddr_in ) );

	if ( sendResultAsInt == SOCKET_ERROR ) 
	{
		printf( "send failed with error: %d\n", WSAGetLastError() );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------------------------
bool RunUDPServer( SOCKET& udpServerSocket )
{
	bool shouldQuit = false;

	while( !shouldQuit )
	{
		shouldQuit &= UDPReceiveMessageServer( udpServerSocket );
		UDPServerRemoveInactiveClients();
		UDPServerDisplayConnectedClients();
		UDPServerBroadcastUpdatePackets( udpServerSocket );
		UDPServerUpdateClientTimes();
	}

	return shouldQuit;
}


//-----------------------------------------------------------------------------------------------
void UDPServerRemoveInactiveClients()
{
	for( int i = 0; i < static_cast< int >( g_relevantClients.size() ); ++i )
	{
		if( g_relevantClients[ i ].timeSinceLastRecieve >= MAX_SECONDS_OF_INACTIVITY )
		{
			uniqueClient temp = g_relevantClients.back();

			if( i < static_cast< int >( g_relevantClients.size() ) - 1 )
			{
				g_relevantClients[ i ] = temp;
			}

			g_relevantClients.pop_back();
			--i;
		}
	}
}

//-----------------------------------------------------------------------------------------------
void UDPServerDisplayConnectedClients()
{
	if( currentDisplayConnectedUsersElapsedTime >= DISPLAY_CONNECTED_USERS_TIME )
	{

		system( "cls" );
		std::printf( "Connected Users: ");

		auto iter = g_relevantClients.begin();

		for( ; iter != g_relevantClients.end(); ++iter )
		{
			std::printf( "\nUser: ID -> %s, IP -> %s, Port -> %s", iter->ID.c_str(), iter->ipAddressAsString.c_str(), iter->portAsString.c_str() );
		}	
		currentDisplayConnectedUsersElapsedTime = 0.f;
	}
}

//-----------------------------------------------------------------------------------------------
void UDPServerBroadcastUpdatePackets( SOCKET& udpSocket )
{
	if( currentSendElapsedTime < SEND_DELAY )
	{
		return;
	}

	int sizeOfPacket = sizeof( CS6Packet );
	auto iter = g_relevantClients.begin();

	sockaddr_in clientInfo;
	int clientInfoLength = sizeof( clientInfo );

	CS6Packet packetToSend;
	

	for( ; iter != g_relevantClients.end(); ++iter )
	{
		memset( (char *) &clientInfo, 0, clientInfoLength );

		clientInfo.sin_family = AF_INET;
		clientInfo.sin_port = htons( u_short( atoi( iter->portAsString.c_str() ) ) );
		clientInfo.sin_addr.S_un.S_addr = inet_addr( iter->ipAddressAsString.c_str() );

		for( int i = 0; i < static_cast< int >( g_relevantClients.size() ); ++i )
		{
			++g_relevantClients[ i ].numMessagesSent;

			ZeroMemory( &packetToSend, sizeof( packetToSend) );

			packetToSend.packetType = TYPE_Update;

			packetToSend.playerColorAndID[ 0 ] = g_relevantClients[ i ].colorID[ 0 ];
			packetToSend.playerColorAndID[ 1 ] = g_relevantClients[ i ].colorID[ 1 ];
			packetToSend.playerColorAndID[ 2 ] = g_relevantClients[ i ].colorID[ 2 ];

			packetToSend.timestamp = Time::GetCurrentTimeInSeconds();

			packetToSend.data.updated = g_relevantClients[ i ].mostRecentUpdateInfo;

			char* message = ( char* )&packetToSend;
			
			UDPSendMessageServer( udpSocket, clientInfo, message, sizeOfPacket );
		}
	}
	currentSendElapsedTime = 0.f;
}

//-----------------------------------------------------------------------------------------------
void UDPServerUpdateClientTimes()
{
	static double timeAtLastUpdate = Time::GetCurrentTimeInSeconds();
	double timeNow = Time::GetCurrentTimeInSeconds();

	float deltaSeconds = static_cast< float >( timeNow - timeAtLastUpdate );

	auto iter = g_relevantClients.begin();

	for( ; iter != g_relevantClients.end(); ++iter )
	{
		iter->timeSinceLastRecieve += deltaSeconds;
	}

	currentSendElapsedTime += deltaSeconds;
	currentDisplayConnectedUsersElapsedTime += deltaSeconds;

	timeAtLastUpdate = timeNow;
}

//-----------------------------------------------------------------------------------------------
bool UDPReceiveMessageServer( const SOCKET& socketToRecieveMessageThru )
{
	bool shouldQuitApp = false;

	sockaddr_in clientAddrInfo;
	int addrInfoLength = sizeof( sockaddr_in );
	int bytesReceived = 0;

	CS6Packet receivedPacket;

	do 
	{
		ZeroMemory( &receivedPacket, sizeof( receivedPacket ) );
		ZeroMemory( &clientAddrInfo, sizeof( clientAddrInfo ) );

		bytesReceived = 0;

		bytesReceived = recvfrom( socketToRecieveMessageThru, ( char* )&receivedPacket, sizeof( CS6Packet ), 0, (struct sockaddr *)&clientAddrInfo, &addrInfoLength );

		if( bytesReceived == sizeof( CS6Packet ) )
		{
			ProcessMessageFromClient( receivedPacket, clientAddrInfo );
		}
	} 
	while ( bytesReceived > 0 );

	return shouldQuitApp;
}

//-----------------------------------------------------------------------------------------------
void ProcessMessageFromClient( const CS6Packet& packet, const sockaddr_in& senderInfo )
{
	PacketType typeOfPacket = packet.packetType;

	if( typeOfPacket == TYPE_Update )
	{
		OnReceiveUpdate( packet, senderInfo );
	}
	else if( typeOfPacket == TYPE_GameStart )
	{
		OnReceiveGameStart( packet, senderInfo );
	}
	else if( typeOfPacket == TYPE_Reset )
	{
		OnReceiveReset( packet, senderInfo );
	}
	else if( typeOfPacket == TYPE_Acknowledge )
	{
		OnReceiveAck( packet, senderInfo );
	}
	else if( typeOfPacket == TYPE_Victory )
	{
		OnReceiveVictory( packet, senderInfo );
	}
}


void OnReceiveUpdate( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{
	char buffer[ 64 ];

	std::string portAsString = _itoa( int( ntohs( senderInfo.sin_port ) ), buffer, 10 );
	std::string ipAddressAsString = inet_ntoa( senderInfo.sin_addr );
	std::string clientID = portAsString + ipAddressAsString;

	auto iter = g_relevantClients.begin();


	for( ; iter != g_relevantClients.end(); ++iter )
	{
		if( iter->ID == clientID )
		{
			iter->timeSinceLastRecieve = 0.f;

			memcpy( &iter->mostRecentUpdateInfo, &bufferAsPacket.data.updated, sizeof( bufferAsPacket.data.updated ) );

			break;
		}
	}
}

void OnReceiveGameStart( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{

}

void OnReceiveReset( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{

}

void OnReceiveAck( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{
	PacketType typeOfAck = bufferAsPacket.data.acknowledged.packetType;

	if( typeOfAck == TYPE_GameStart )
	{
		OnAckGameStart( bufferAsPacket, senderInfo );
	}
	else if( typeOfAck == TYPE_Acknowledge )
	{
		OnAckAcknowledge( bufferAsPacket, senderInfo );
	}
	else if( typeOfAck == TYPE_Reset )
	{
		OnAckReset( bufferAsPacket, senderInfo );
	}
	else if( typeOfAck == TYPE_Victory )
	{
		//blank for now
	}
	else if( typeOfAck == TYPE_Update )
	{
		//blank for now
	}
}

void OnReceiveVictory( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{

}


//-----------------------------------------------------------------------------------------------
void OnAckAcknowledge( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{
	uniqueClient clientSendingAck;
	char buffer[ 64 ];

	clientSendingAck.portAsString = _itoa( int( ntohs( senderInfo.sin_port ) ), buffer, 10 );
	clientSendingAck.ipAddressAsString = inet_ntoa( senderInfo.sin_addr );
	clientSendingAck.ID = clientSendingAck.portAsString + clientSendingAck.ipAddressAsString;
	
	auto iter = g_relevantClients.begin();

	for( ; iter != g_relevantClients.end(); ++iter )
	{
		if( iter->ID == clientSendingAck.ID )
		{
			iter->timeSinceLastRecieve = 0.f;
			break;
		}
	}

	if( iter == g_relevantClients.end() )
	{
		GetColor( g_relevantClients.size(), clientSendingAck.colorID );

		clientSendingAck.mostRecentUpdateInfo.xPosition = float( rand() % 500 );
		clientSendingAck.mostRecentUpdateInfo.yPosition = float( rand() % 500 );
		clientSendingAck.mostRecentUpdateInfo.xVelocity = 0.f;
		clientSendingAck.mostRecentUpdateInfo.yVelocity = 0.f;
		clientSendingAck.mostRecentUpdateInfo.yawDegrees = 0.f;

		CS6Packet gameStartPacket;
		ZeroMemory( &gameStartPacket, sizeof( gameStartPacket ) );

		gameStartPacket.packetType = TYPE_GameStart;
		memcpy( &gameStartPacket.data.gameStart.playerColorAndID, &clientSendingAck.colorID, sizeof( clientSendingAck.colorID ) );

		gameStartPacket.data.gameStart.playerXPosition = clientSendingAck.mostRecentUpdateInfo.xPosition;
		gameStartPacket.data.gameStart.playerYPosition = clientSendingAck.mostRecentUpdateInfo.yPosition;

		if( g_relevantClients.size() == 0 )
		{
			memcpy( &g_currentItPlayer, &clientSendingAck.colorID, sizeof( clientSendingAck.colorID ) );
		}

		memcpy( &gameStartPacket.data.gameStart.itPlayerColorAndID, &g_currentItPlayer, sizeof( g_currentItPlayer ) );

		g_relevantClients.push_back( clientSendingAck );

		++clientSendingAck.numMessagesSent;
		UDPSendMessageServer( UDPServerSocket, senderInfo, ( char* )&gameStartPacket, sizeof( gameStartPacket ) );
	}
}


//-----------------------------------------------------------------------------------------------
void OnAckGameStart( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{

}


//-----------------------------------------------------------------------------------------------
void OnAckReset( const CS6Packet& bufferAsPacket, const sockaddr_in& senderInfo )
{

}


//-----------------------------------------------------------------------------------------------
void GetColor( int index, unsigned char* colorArray )
{
	if( index == 0 )
	{
		colorArray[ 0 ] = 255;
		colorArray[ 1 ] = 0;
		colorArray[ 2 ] = 0;
	}
	else if( index == 1 )
	{
		colorArray[ 0 ] = 0;
		colorArray[ 1 ] = 255;
		colorArray[ 2 ] = 0;
	}
	else if( index == 2 )
	{
		colorArray[ 0 ] = 0;
		colorArray[ 1 ] = 0;
		colorArray[ 2 ] = 255;
	}
	else if( index == 3 )
	{
		colorArray[ 0 ] = 255;
		colorArray[ 1 ] = 255;
		colorArray[ 2 ] = 0;
	}
	else if( index == 4 )
	{
		colorArray[ 0 ] = 255;
		colorArray[ 1 ] = 0;
		colorArray[ 2 ] = 255;
	}
	else if( index == 5 )
	{
		colorArray[ 0 ] = 0;
		colorArray[ 1 ] = 255;
		colorArray[ 2 ] = 255;
	}
	else
	{
		do 
		{
			colorArray[ 0 ] = rand() % 255;
			colorArray[ 1 ] = rand() % 255;
			colorArray[ 2 ] = rand() % 255;

		} while ( colorArray[ 0 ] != BAD_COLOR[ 0 ] || colorArray[ 1 ] != BAD_COLOR[ 1 ] || colorArray[ 2 ] != BAD_COLOR[ 2 ] );
	}
}