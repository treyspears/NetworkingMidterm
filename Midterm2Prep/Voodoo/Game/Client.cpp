#include "Client.hpp"

#include "Engine/Rendering/ConsoleLog.hpp"
#include "Engine/Rendering/OpenGLRenderer.hpp"
#include "Engine/Utilities/ErrorWarningAssert.hpp"
#include "Engine/Utilities/CommandRegistry.hpp"
#include "Engine/Utilities/CommonUtilities.hpp"
#include "Engine/Utilities/Clock.hpp"
#include "Engine/Primitives/Vector4.hpp"


//-----------------------------------------------------------------------------------------------
const char* STARTING_SERVER_IP_AS_STRING = "127.0.0.1";
const u_short STARTING_PORT = 5000;

const PlayerID LOCAL_PLAYER_ID = Color( Orange );
const float SEND_TO_HOST_FREQUENCY = 0.025f;
const float SEND_TO_HOST_MAX_DELAY = 1.f;

//FUTUR EDIT: Game should be a singleton and I should be able to ask for this
const int SCREEN_WIDTH =  500;
const int SCREEN_HEIGHT = 500;
const float ARENA_WIDTH = 500.f;
const float ARENA_HEIGHT = 500.f;
const PlayerID BAD_IT_PLAYER = Color( Black );


float g_currentElapsedSendToHostSeconds = 0.f;

//-----------------------------------------------------------------------------------------------
//Public Methods
//-----------------------------------------------------------------------------------------------
Client::Client()
	: m_currentState( CLIENT_GAME_NOT_STARTED )
	, m_currentItPlayerID( BAD_IT_PLAYER )
	, m_connectionToHostID( 0 )
	, m_currentHostIPAddressAsString( STARTING_SERVER_IP_AS_STRING )
	, m_currentHostPort( STARTING_PORT )
	, m_sendPacketsToHostFrequency( SEND_TO_HOST_FREQUENCY )
	, m_localPlayerMovementMagnitude( 0.f )
	, m_mostRecentlyProcessedUnreliablePacketNum( 0 )
	, m_nextExpectedReliablePacketNumToProcess( 1 )
{
	m_localPlayer.id = LOCAL_PLAYER_ID;
}


//-----------------------------------------------------------------------------------------------
Client::~Client()
{

}


//-----------------------------------------------------------------------------------------------
void Client::StartUp()
{
	Network& theNetwork = Network::GetInstance();

	m_connectionToHostID = theNetwork.CreateUDPSocketFromIPAndPort( m_currentHostIPAddressAsString.c_str(), m_currentHostPort );
}


//-----------------------------------------------------------------------------------------------
void Client::ShutDown()
{
	Network& theNetwork = Network::GetInstance();

	theNetwork.ShutDown();
}


//-----------------------------------------------------------------------------------------------
void Client::UpdateLocalPlayerMovementMagnitude( float magnitude )
{
	m_localPlayerMovementMagnitude = magnitude;
}

//-----------------------------------------------------------------------------------------------
void Client::Update()
{
	Clock& appClock = Clock::GetMasterClock();

	ReceiveMessagesFromHostIfAny();

	if( m_currentState == CLIENT_GAME_NOT_STARTED )
	{
		PotentiallySendGameStartAckPacketToServer();
	}
	else
	{
		UpdatePlayers();
		PotentiallySendUpdatePacketToServer();
		CheckForCollision();
	}


	g_currentElapsedSendToHostSeconds += static_cast< float >( appClock.m_currentDeltaSeconds );
}


//-----------------------------------------------------------------------------------------------
void Client::Render() const
{
	RenderPlayers();
}


//-----------------------------------------------------------------------------------------------
void Client::ChangeHost( const std::string& ipAddressAsString, const std::string& portAsString )
{
	Network& theNetwork = Network::GetInstance();

	m_currentHostIPAddressAsString = ipAddressAsString;
	m_currentHostPort = u_short( atoi( portAsString.c_str() ) );

	m_connectionToHostID = theNetwork.CreateUDPSocketFromIPAndPort( m_currentHostIPAddressAsString.c_str(), m_currentHostPort );


	m_otherClientsPlayers.clear();

	m_localPlayer.Reset();
}


//-----------------------------------------------------------------------------------------------
ClientPlayer& Client::GetLocalPlayer()
{
	return m_localPlayer;
}


//-----------------------------------------------------------------------------------------------
std::vector< ClientPlayer >& Client::GetOtherClientsPlayers()
{
	return m_otherClientsPlayers;
}


//-----------------------------------------------------------------------------------------------
//Private Methods
//-----------------------------------------------------------------------------------------------
void Client::ReceiveMessagesFromHostIfAny()
{
	Network& theNetwork = Network::GetInstance();
	std::set< CS6Packet, PacketComparator > packetQueueForThisFrame;

	int bytesReceived = 0;
	CS6Packet receivedPacket;

	do 
	{
		bytesReceived = 0;
		ZeroMemory( &receivedPacket, sizeof( receivedPacket ) );

		bytesReceived = theNetwork.ReceiveUDPMessage( ( char* )&receivedPacket, sizeof( receivedPacket ), m_connectionToHostID );

		if( bytesReceived == sizeof( CS6Packet ) )
		{
			packetQueueForThisFrame.insert( receivedPacket );
		}
	} 
	while ( bytesReceived > 0 );


	auto packetQueueIterator = packetQueueForThisFrame.begin();
	
	for( ; packetQueueIterator != packetQueueForThisFrame.end(); ++packetQueueIterator )
	{
		ProcessPacket( *packetQueueIterator );
	}
}


//-----------------------------------------------------------------------------------------------
void Client::UpdatePlayers()
{
	if( m_currentState == CLIENT_GAME_NOT_STARTED )
	{
		return;
	}

	m_localPlayer.SetCurrentVelocityAndPositionFromMagnitude( m_localPlayerMovementMagnitude );

	auto iter = m_otherClientsPlayers.begin();

	for( ; iter != m_otherClientsPlayers.end(); ++ iter )
	{
		iter->SeekTarget();
	}
}


//-----------------------------------------------------------------------------------------------
void Client::CheckForCollision()
{
	if( m_currentState == CLIENT_GAME_NOT_STARTED )
	{
		return;
	}

	for( auto iter = m_otherClientsPlayers.begin(); iter != m_otherClientsPlayers.end(); ++iter )
	{
		if( !iter->isIt )
		{
			continue;
		}

		if( Vector2f::Distance( m_localPlayer.currentPos, iter->currentPos ) < 10.f )
		{
			//SendVictoryPacketToServer();
		}
	}
}


//-----------------------------------------------------------------------------------------------
void Client::PotentiallySendGameStartAckPacketToServer()
{
	if( g_currentElapsedSendToHostSeconds < m_sendPacketsToHostFrequency )
	{
		return;
	}


	CS6Packet gameStartAckPacket = GetGameStartAckPacket();

	Network& theNetwork = Network::GetInstance();
	theNetwork.SendUDPMessage( ( char* )&gameStartAckPacket, sizeof( gameStartAckPacket ), m_connectionToHostID );

	g_currentElapsedSendToHostSeconds = 0.f;

}

//-----------------------------------------------------------------------------------------------
void Client::PotentiallySendUpdatePacketToServer()
{
	if( g_currentElapsedSendToHostSeconds < m_sendPacketsToHostFrequency )
	{
		return;
	}

	CS6Packet updatePacketToSend = GetUpdatePacketFromPlayer( m_localPlayer );

	Network& theNetwork = Network::GetInstance();
	theNetwork.SendUDPMessage( ( char* )&updatePacketToSend, sizeof( updatePacketToSend ), m_connectionToHostID );

	g_currentElapsedSendToHostSeconds = 0.f;
}


//-----------------------------------------------------------------------------------------------
void Client::AckBackSuccessfulReliablePacketReceive( const CS6Packet& packet )
{
	CS6Packet ackBackPacket;

	ZeroMemory( &ackBackPacket, sizeof( ackBackPacket ) );

	ackBackPacket.packetType = TYPE_Acknowledge;
	ackBackPacket.data.acknowledged.packetType = packet.packetType;
	ackBackPacket.data.acknowledged.packetNumber = packet.packetNumber;

	Network& theNetwork = Network::GetInstance();
	theNetwork.SendUDPMessage( ( char* )&ackBackPacket, sizeof( ackBackPacket ), m_connectionToHostID );
}


//-----------------------------------------------------------------------------------------------
void Client::RenderPlayers() const
{
	if( m_currentState == CLIENT_GAME_NOT_STARTED )
	{
		return;
	}

	RenderPlayer( m_localPlayer );

	for( auto iter = m_otherClientsPlayers.begin(); iter != m_otherClientsPlayers.end(); ++iter )
	{
		RenderPlayer( *iter );
	}
}


//-----------------------------------------------------------------------------------------------
void Client::RenderPlayer( const ClientPlayer& playerToRender ) const
{
	OpenGLRenderer::UseShaderProgram( OpenGLRenderer::s_fixedFunctionPipelineShaderID );
	OpenGLRenderer::Uniform1i( OpenGLRenderer::s_fixedFunctionUseTexturesUniformLocation, 0 );

	OpenGLRenderer::s_rendererStack.Push( MatrixStack44f::MODEL_STACK );
	{
		OpenGLRenderer::Disable( GL_DEPTH_TEST );
		OpenGLRenderer::Disable( GL_CULL_FACE );

		const float ASPECT_RATIO = static_cast< float >( SCREEN_HEIGHT ) / SCREEN_WIDTH;

		OpenGLRenderer::s_rendererStack.ApplyOrtho( 0.f, ARENA_WIDTH, 0.f, ARENA_HEIGHT * ASPECT_RATIO, 0.f, 1.f ) ;
		OpenGLRenderer::LoadMatrix( OpenGLRenderer::s_rendererStack.GetMVWithProjection() );

		playerToRender.RenderShip();
	}
	OpenGLRenderer::s_rendererStack.Pop( MatrixStack44f::MODEL_STACK );
}


//-----------------------------------------------------------------------------------------------
void Client::ProcessPacket( const CS6Packet& packet )
{
	bool shouldProcessReliablePacketsInQueue = false;

	if( packet.IsReliablePacket() )
	{
		//FUTURE EDIT: Ack to Server that client received reliable packet!

		AckBackSuccessfulReliablePacketReceive( packet );

		if( packet.packetNumber > m_nextExpectedReliablePacketNumToProcess )
		{
			m_queueOfReliablePacketsToParse.insert( packet );
			return;
		}
		else
		{
			++m_nextExpectedReliablePacketNumToProcess;
			shouldProcessReliablePacketsInQueue = true;	
		}
	}
	else
	{
		if( packet.packetNumber < m_mostRecentlyProcessedUnreliablePacketNum )
		{
			return;
		}
	}

	PacketType typeOfPacket = packet.packetType;

	if( typeOfPacket == TYPE_Update )
	{
		OnReceiveUpdatePacket( packet );
	}
	else if( typeOfPacket == TYPE_Reset )
	{
		OnReceiveResetPacket( packet );
	}
	else if( typeOfPacket == TYPE_Acknowledge )
	{
		OnReceiveAckPacket( packet );
	}
	else if( typeOfPacket == TYPE_GameStart )
	{
		OnReceiveGameStartPacket( packet );
	}
	else if( typeOfPacket == TYPE_Victory )
	{
		OnReceiveVictoryPacket( packet );
	}

	//actually may not need this since we return from if statement
	if( shouldProcessReliablePacketsInQueue )
	{
		ProcessAnyQueuedReliablePackets();
	}
}


//-----------------------------------------------------------------------------------------------
void Client::ProcessAnyQueuedReliablePackets()
{
	while( !m_queueOfReliablePacketsToParse.empty() && m_queueOfReliablePacketsToParse.begin()->packetNumber > m_nextExpectedReliablePacketNumToProcess )
	{
		ProcessPacket( *m_queueOfReliablePacketsToParse.begin() );

		m_queueOfReliablePacketsToParse.erase( m_queueOfReliablePacketsToParse.begin() );
	}
}


//-----------------------------------------------------------------------------------------------
void Client::OnReceiveUpdatePacket( const CS6Packet& updatePacket )
{
	PlayerID idFromPacket;
	memcpy( &idFromPacket, &updatePacket.playerColorAndID, sizeof( updatePacket.playerColorAndID ) );

	Vector2f newPosition( updatePacket.data.updated.xPosition, updatePacket.data.updated.yPosition );
	Vector2f newVelocity( updatePacket.data.updated.xVelocity, updatePacket.data.updated.yVelocity );

	float newOrientationDegrees( updatePacket.data.updated.yawDegrees );

	auto iter = m_otherClientsPlayers.begin();

	for( ; iter != m_otherClientsPlayers.end(); ++ iter )
	{
		if( iter->id == idFromPacket )
		{
			iter->desiredPos = newPosition;
			iter->desiredVelocity = newVelocity;
			iter->orientationAsDegrees = updatePacket.data.updated.yawDegrees;

			return;
		}
	}


	if( !( m_localPlayer.id == idFromPacket ) )
	{
		ClientPlayer newPlayer;

		newPlayer.currentPos = newPosition;
		newPlayer.currentVelocity = newVelocity;
		newPlayer.desiredPos = newPosition;
		newPlayer.desiredVelocity = newVelocity;

		newPlayer.orientationAsDegrees = updatePacket.data.updated.yawDegrees;

		newPlayer.orientationAsDegrees = newOrientationDegrees;
		newPlayer.id = idFromPacket;

		m_otherClientsPlayers.push_back( newPlayer );
	}
}


//-----------------------------------------------------------------------------------------------
void Client::OnReceiveGameStartPacket( const CS6Packet& updatePacket )
{
	m_localPlayer.currentPos.x = updatePacket.data.gameStart.playerXPosition;
	m_localPlayer.currentPos.y = updatePacket.data.gameStart.playerYPosition;

	memcpy( &m_localPlayer.id, &updatePacket.data.gameStart.playerColorAndID, sizeof( updatePacket.data.gameStart.playerColorAndID ) );
	memcpy( &m_currentItPlayerID, &updatePacket.data.gameStart.itPlayerColorAndID, sizeof( updatePacket.data.gameStart.itPlayerColorAndID ) );

	if( m_localPlayer.id == m_currentItPlayerID )
	{
		m_localPlayer.isIt = true;
	}

	m_currentState = CLIENT_IN_GAME;
}


//-----------------------------------------------------------------------------------------------
void Client::OnReceiveResetPacket( const CS6Packet& updatePacket )
{

}


//-----------------------------------------------------------------------------------------------
void Client::OnReceiveVictoryPacket( const CS6Packet& updatePacket )
{

}


//-----------------------------------------------------------------------------------------------
void Client::OnReceiveAckPacket( const CS6Packet& updatePacket )
{
	PacketType ackType = updatePacket.data.acknowledged.packetType;

	//if( ackType == )
}


//-----------------------------------------------------------------------------------------------
CS6Packet Client::GetGameStartAckPacket()
{
	CS6Packet gameStartAckPacket;

	ZeroMemory( &gameStartAckPacket, sizeof( gameStartAckPacket ) );

	gameStartAckPacket.packetType = TYPE_Acknowledge;
	gameStartAckPacket.data.acknowledged.packetType = TYPE_Acknowledge;

	return gameStartAckPacket;
}


//-----------------------------------------------------------------------------------------------
CS6Packet Client::GetUpdatePacketFromPlayer( const ClientPlayer& player )
{
	CS6Packet updatePacket;

	ZeroMemory( &updatePacket, sizeof( updatePacket ) );

	updatePacket.packetType = TYPE_Update;

	updatePacket.data.updated.xPosition = player.currentPos.x;
	updatePacket.data.updated.yPosition = player.currentPos.y;

	updatePacket.data.updated.xVelocity = player.currentVelocity.x;
	updatePacket.data.updated.yVelocity = player.currentVelocity.y;

	updatePacket.data.updated.yawDegrees = player.orientationAsDegrees;

	memcpy( &updatePacket.playerColorAndID, &player.id, sizeof( player.id ) );

	return updatePacket;
}


//-----------------------------------------------------------------------------------------------
void Client::SetPlayerParameters( NamedProperties& parameters )
{
	std::string playerRedAsString;
	std::string playerGreenAsString;
	std::string playerBlueAsString;

	parameters.Get( "param1", playerRedAsString );
	parameters.Get( "param2", playerGreenAsString );
	parameters.Get( "param3", playerBlueAsString );

	std::set< ErrorType > errors, additionalErrors;

	errors = ValidateIsInt( playerRedAsString );
	additionalErrors = ValidateIsInt( playerGreenAsString );
	errors.insert( additionalErrors.begin(), additionalErrors.end() );

	additionalErrors = ValidateIsUChar( playerBlueAsString );
	errors.insert( additionalErrors.begin(), additionalErrors.end() );

	RECOVERABLE_ASSERTION( errors.empty(), "Command: playerColor did not receive the correct parameters.\nplayerColor expects three parameters of type int." );

	if( !errors.empty() )
		return;

	m_localPlayer.id.r = static_cast< uchar >( strtol( playerRedAsString.c_str(), 0, 10 ) );
	m_localPlayer.id.g = static_cast< uchar >( strtol( playerGreenAsString.c_str(), 0, 10 ) );
	m_localPlayer.id.b = static_cast< uchar >( strtol( playerBlueAsString.c_str(), 0, 10 ) );
}


//-----------------------------------------------------------------------------------------------
void Client::SetServerIpFromParameters( NamedProperties& parameters )
{
	std::string ipAddressAsString;

	parameters.Get( "param1", ipAddressAsString );

	FATAL_ASSERTION( ipAddressAsString != "", "Command: ip did not receive the correct parameters.\nip expects a non empty string." );

	m_currentHostIPAddressAsString = ipAddressAsString;
}


//-----------------------------------------------------------------------------------------------
void Client::SetServerPortFromParameters( NamedProperties& parameters )
{
	std::string portAsString;

	parameters.Get( "param1", portAsString );

	FATAL_ASSERTION( portAsString != "", "Command: port did not receive the correct parameters.\nport expects a non empty string." );

	m_currentHostPort = u_short( atoi( portAsString.c_str() ) );
}

