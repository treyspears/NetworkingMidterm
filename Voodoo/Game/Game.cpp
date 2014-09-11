#define UNUSED(x) (void)(x);

#define BUFFER_LIMIT 512
#define LOCAL_IP_ADDRESS "127.0.0.1"
#define LOCAL_PORT "0"

#define STARTING_SERVER_IP "127.0.0.1"
#define STARTING_SERVER_PORT "6665"

#include "Game.hpp"

#include <time.h>
#include <random>
#include <string>

#include <stdlib.h>
#include <stdio.h>
#include "Engine/Rendering/OpenGLRenderer.hpp"
#include "Engine/Rendering/BitmapFont.hpp"
#include "Engine/Utilities/Time.hpp"
#include "Engine/Utilities/Clock.hpp"
#include "Engine/Utilities/XMLUtilities.hpp"
#include "Engine/Utilities/ProfileSection.hpp"
#include "Engine/Utilities/ErrorWarningAssert.hpp"
#include "Engine/Primitives/Vector2.hpp"
#include "Engine/Primitives/Color.hpp"

#include "CS6Packet.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment( lib, "WS2_32" ) // Link in the WS2_32.lib static library

const playerID BAD_COLOR( 0, 0, 0 );
//-----------------------------------------------------------------------------------------------

const int SCREEN_WIDTH = 500;
const int SCREEN_HEIGHT = 500;
const float ARENA_WIDTH = 500.f;
const float ARENA_HEIGHT = 500.f;

const double MAX_APP_FRAME_TIME = 0.1;
const float LOCAL_LAG_TIME = 0.0125f;
const float SEND_FREQUENCY = 0.025f;
const float SEND_MAX_DELAY = 1.f;

const playerID PLAYER_ID( 255, 127, 55 );
const Vector2f INITIAL_PLAYER_POSITION( 0.f, 0.f );
const float PLAYER_SPEED = 50.f;

FMOD::System* Game::m_soundSystem = nullptr;
FMOD::Sound *test = nullptr;
FMOD::Channel *channel = nullptr;

Clock* Game::s_appClock = nullptr;

int g_currentLogIndex = 0;
bool g_isDelayingInput = false;

u_long BLOCKING = 0;
u_long NON_BLOCKING = 1;

player g_localPlayer;
playerID g_itPlayerID = BAD_COLOR;
std::vector< player > g_players;

int g_currentReceivedPacketCount = 0;
float g_currentElapsedTimeToSendToServer = 0.f;

sockaddr_in UDPServerAddressInfo;
SOCKET sendToServerSocket = INVALID_SOCKET;

std::string g_serverIPAsString = STARTING_SERVER_IP;
std::string g_serverPortAsString = STARTING_SERVER_PORT;

enum GameState
{
	STATE_NotStarted,
	STATE_InGame,
};

GameState g_currentState = STATE_NotStarted;

//-----------------------------------------------------------------------------------------------
bool InitializeWinSocket();
bool CreateAddressInfoForUDP( const std::string& ipAddress, const std::string& udpPort, sockaddr_in& udpAddressInfo );
bool CreateUDPSocket( SOCKET& newSocket, sockaddr_in& addressInfo );
bool BindSocket( SOCKET& socketToBind, sockaddr_in& addressInfo );
bool ChangeServerInfo( const std::string& serverIP, const std::string& serverPort, sockaddr_in& UDPServerAddressInfo_out );

bool UDPSendMessageToServer( const SOCKET& socketToSendThru, sockaddr_in& udpServerAddressInfo, const char* message, int messageLength );

bool UDPClientRecieveMessage( const SOCKET& receiveFromSocket );
void ProcessPacket( const CS6Packet& packet );
void OnReceiveGameStart( const CS6Packet& packet );
void OnReceiveReset( const CS6Packet& packet );
void OnReceiveUpdate( const CS6Packet& buffer );
void OnReceiveAck( const CS6Packet& bufferAsPacket );
void OnReceiveVictory( const CS6Packet& bufferAsPacket );

//-----------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------
void Command_Quit(const ConsoleCommandArgs& args)
{
	UNUSED(args);
	InputHandler::QuitApplication();
	return;
}

void Command_Clear(const ConsoleCommandArgs& args)
{
	UNUSED(args);
	if(ConsoleLog::s_currentLog != nullptr)
	{
		ConsoleLog::s_currentLog->ClearConsoleLog();
	}
}

//-----------------------------------------------------------------------------------------------
Game::Game()
{
	srand( ( unsigned int )time ( NULL ) );

	g_localPlayer.id.r = PLAYER_ID.r;
	g_localPlayer.id.g = PLAYER_ID.g;
	g_localPlayer.id.b = PLAYER_ID.b;
	g_localPlayer.currentPos = INITIAL_PLAYER_POSITION;
}

//-----------------------------------------------------------------------------------------------
Game::~Game()
{
	for( int i = 0; i < static_cast< int >( ConsoleLog::s_logs.size() ); ++ i )
	{
		delete ConsoleLog::s_logs[ i ];
	}
}

//-----------------------------------------------------------------------------------------------
void Game::Initialize(HINSTANCE applicationInstanceHandle)
{
	OpenGLRenderer::CreateOpenGLWindow(applicationInstanceHandle, SCREEN_WIDTH, SCREEN_HEIGHT);

	Clock& masterClock = Clock::GetMasterClock();
	s_appClock = new Clock( masterClock, MAX_APP_FRAME_TIME );

	InitializeLog();
	Time::InitializeTime();

	FMOD::System_Create( &Game::m_soundSystem );

	Game::m_soundSystem->init( 100, FMOD_INIT_NORMAL, 0 );

	m_inputHandler.Initialize( OpenGLRenderer::GetWindow() );

	RegisterEvent("quit", Command_Quit);
	RegisterEvent("clear",Command_Clear);

	bool winsocketSetup = true;

	winsocketSetup = InitializeWinSocket();
	FATAL_ASSERTION( winsocketSetup == true, "Winsocket didn't initiliaze." );
	
	winsocketSetup = CreateAddressInfoForUDP( g_serverIPAsString, g_serverPortAsString, UDPServerAddressInfo );
	FATAL_ASSERTION( winsocketSetup == true, "Creating addr info for server failed." );

	CreateUDPSocket( sendToServerSocket, UDPServerAddressInfo );
	ioctlsocket( sendToServerSocket, FIONBIO, &NON_BLOCKING );
}

//-----------------------------------------------------------------------------------------------
void Game::InitializeLog()
{
	BitmapFont* font = BitmapFont::CreateOrGetFont("Data/Fonts/MainFont_EN_00.png", "Data/Fonts/MainFont_EN.FontDef.xml");

	ConsoleLog::s_logs.push_back( new ConsoleLog( font,18.f ) );
	ConsoleLog::s_currentLog = ConsoleLog::s_logs.back();
}

//-----------------------------------------------------------------------------------------------
void Game::Run()
{
	while(!m_inputHandler.ShouldQuit())
	{
		m_inputHandler.RunMessagePump();
		m_inputHandler.RunXboxMessagePump();
		m_soundSystem->update();
		ProcessInput();
		Update();
		Render();
	}

	shutdown( sendToServerSocket, SD_BOTH );
	closesocket( sendToServerSocket );
	WSACleanup();
}

//-----------------------------------------------------------------------------------------------
void Game::Update()
{
	static double timeAtLastUpdate = Time::GetCurrentTimeInSeconds();
	double timeNow = Time::GetCurrentTimeInSeconds();

	float deltaSeconds = static_cast< float >(timeNow - timeAtLastUpdate);

	ConsoleLog::s_currentLog->Update( deltaSeconds );

	Clock& masterClock = Clock::GetMasterClock();
	masterClock.AdvanceTime( timeNow - timeAtLastUpdate );

	UDPClientRecieveMessage( sendToServerSocket );

	g_currentElapsedTimeToSendToServer += static_cast< float >( masterClock.m_currentDeltaSeconds );

	if( g_currentState == STATE_NotStarted )
	{
		PotentiallyRequestGameStartFromServer();
	}
	if( g_currentState == STATE_InGame )
	{
		if( g_localPlayer.isIt )
		{
			CheckCollisionAndSendVictoryIfSuccessfulTag();
		}
		PotentiallySendUpdatePacketToServer();
	}
	
	timeAtLastUpdate = timeNow;
}

//-----------------------------------------------------------------------------------------------
void Game::Render()
{
	const Vector4f clearColor = Color( DarkGray ).ToVector4fNormalized();

	OpenGLRenderer::ClearColor( clearColor.x, clearColor.y, clearColor.z, clearColor.w );
	OpenGLRenderer::Clear( GL_COLOR_BUFFER_BIT );

	if( g_currentState == STATE_InGame )
	{	
		RenderPlayers();
	}
	
	ConsoleLog::s_currentLog->Render();
	SwapBuffers( OpenGLRenderer::displayDeviceContext );
}

//-----------------------------------------------------------------------------------------------
void Game::RenderPlayers()
{
	RenderPlayer( g_localPlayer );

	for( auto iter = g_players.begin(); iter != g_players.end(); ++iter )
	{
		RenderPlayer( *iter );
	}
}

//-----------------------------------------------------------------------------------------------
void Game::RenderPlayer( player& playerToRender )
{
	Color renderColor;
	Vector3f renderColorAsNormalizedVector;
	Vector2f minPosition;
	Vector2f maxPosition;
	float offset = 10.f;

	if( playerToRender.isIt )
	{
		offset = 15.f;
	}

	renderColor.r = playerToRender.id.r;
	renderColor.g = playerToRender.id.g;
	renderColor.b = playerToRender.id.b;
	renderColorAsNormalizedVector = renderColor.ToVector3fNormalizedAndFullyOpaque();

	minPosition = playerToRender.currentPos;

	maxPosition = Vector2f( minPosition.x + offset, minPosition.y + offset );

	OpenGLRenderer::UseShaderProgram( OpenGLRenderer::s_fixedFunctionPipelineShaderID );
	OpenGLRenderer::Uniform1i( OpenGLRenderer::s_fixedFunctionUseTexturesUniformLocation, 0 );

	OpenGLRenderer::s_rendererStack.Push( MatrixStack44f::MODEL_STACK );
	{
		OpenGLRenderer::Disable( GL_DEPTH_TEST );
		OpenGLRenderer::Disable( GL_CULL_FACE );

		const float ASPECT_RATIO = static_cast< float >( SCREEN_HEIGHT ) / SCREEN_WIDTH;

		OpenGLRenderer::s_rendererStack.ApplyOrtho( 0.f, ARENA_WIDTH, 0.f, ARENA_HEIGHT * ASPECT_RATIO, 0.f, 1.f ) ;
		OpenGLRenderer::LoadMatrix( OpenGLRenderer::s_rendererStack.GetMVWithProjection() );

		OpenGLRenderer::Render2DQuad( minPosition, maxPosition, renderColorAsNormalizedVector );
	}
	OpenGLRenderer::s_rendererStack.Pop( MatrixStack44f::MODEL_STACK );
}

//-----------------------------------------------------------------------------------------------
void Game::ProcessInput()
{
	const InputHandler::InputHandlerState& currentState = InputHandler::GetInputState();

	if( InputHandler::isKeyPressed(VK_OEM_3) )
	{
		ConsoleLog::s_currentLog->ToggleLogToRender();

		if( currentState == InputHandler::INPUTHANDLER_GAME )
			InputHandler::SetInputState(InputHandler::INPUTHANDLER_DEV_CONSOLE);
		else
			InputHandler::SetInputState(InputHandler::INPUTHANDLER_GAME);
	}

	if( currentState == InputHandler::INPUTHANDLER_DEV_CONSOLE )
	{
		UpdateConsoleLogOnInput();
	}
	else if( g_currentState == STATE_InGame )
	{
		Vector2f movementVector = Vector2f::Zero();

		if( InputHandler::isKeyDown( VK_UP ) || InputHandler::isKeyDown( 'W' ) )
		{
			movementVector += Vector2f( 0.f, 1.f );
		}
		else if( InputHandler::isKeyDown( VK_DOWN ) || InputHandler::isKeyDown( 'S' ) )
		{
			movementVector += Vector2f( 0.f, -1.f );
		}

		if( InputHandler::isKeyDown( VK_LEFT ) || InputHandler::isKeyDown( 'A' ) )
		{
			movementVector += Vector2f( -1.f, 0.f );
		}
		else if( InputHandler::isKeyDown( VK_RIGHT ) || InputHandler::isKeyDown( 'D' ) )
		{
			movementVector += Vector2f( 1.f, 0.f );
		}

		Clock& clock = Clock::GetMasterClock();
		float speed = PLAYER_SPEED;

		if( g_localPlayer.isIt )
		{
			speed *= 1.25f;
		}

		g_localPlayer.currentPos += movementVector * speed * static_cast< float >( clock.m_currentDeltaSeconds );

		if( movementVector != Vector2f::Zero() )
		{
			std::string alarmName = "delayedInput";
			Vector2f delayedMovement = Vector2f::Zero();

			if( g_isDelayingInput )
			{
				NamedProperties& parameters = clock.GetPropertiesFromAlarmWithName( alarmName );
				
				parameters.Get( "movement", delayedMovement );
				movementVector += delayedMovement;
				

				parameters.Set( "movement", movementVector );
			}
			else
			{
				NamedProperties parameters = NamedProperties::EmptyProperties();

				parameters.Set( "movement", movementVector );

				clock.AddAlarmWithLengthAndParameters( alarmName, LOCAL_LAG_TIME, parameters );
				g_isDelayingInput = true;
			}
		}
	}
}

//-----------------------------------------------------------------------------------------------
void Game::UpdateConsoleLogOnInput()
{
	if(InputHandler::isKeyPressed(VK_PRIOR))
	{
		ConsoleLog::s_currentLog->ScrollDownLog();
	}
	else if(InputHandler::isKeyPressed(VK_NEXT))
	{
		ConsoleLog::s_currentLog->ScrollUpLog();
	}
	else if( InputHandler::isKeyPressed(VK_RIGHT))
	{
		++g_currentLogIndex;
		ConsoleLog::s_currentLog->ToggleLogToRender();
		if( g_currentLogIndex > static_cast< int >( ConsoleLog::s_logs.size() ) - 1 )
		{
			g_currentLogIndex = 0;
		}
		ConsoleLog::s_currentLog = ConsoleLog::s_logs[ g_currentLogIndex ];
		ConsoleLog::s_currentLog->ToggleLogToRender();
	}
	else if( InputHandler::isKeyPressed(VK_LEFT))
	{
		--g_currentLogIndex;
		ConsoleLog::s_currentLog->ToggleLogToRender();
		if( g_currentLogIndex < 0 )
		{
			g_currentLogIndex = ConsoleLog::s_logs.size() - 1;
		}
		ConsoleLog::s_currentLog = ConsoleLog::s_logs[ g_currentLogIndex ];
		ConsoleLog::s_currentLog->ToggleLogToRender();
	}
	else if(InputHandler::isKeyPressed(VK_ESCAPE))
	{
		int logLength = ConsoleLog::s_currentLog->GetCommandPromptLength();
		if(logLength == 1)
		{
			InputHandler::SetInputState(InputHandler::INPUTHANDLER_GAME);
			ConsoleLog::s_currentLog->ToggleLogToRender();
			return;
		}
		else
		{
			ConsoleLog::s_currentLog->ClearCommandPrompt();
		}
	}
	else if(InputHandler::isKeyPressed(VK_RETURN))
	{
		int logLength = ConsoleLog::s_currentLog->GetCommandPromptLength();
		if(logLength == 1)
		{
			InputHandler::SetInputState(InputHandler::INPUTHANDLER_GAME);
			ConsoleLog::s_currentLog->ToggleLogToRender();
			return;
		}
		else
		{
			//need to add thing here
			ConsoleLog::s_currentLog->EnterCommand();
			ConsoleLog::s_currentLog->ClearCommandPrompt();
		}
	}
	else if(InputHandler::isKeyPressed(VK_BACK))
	{
		ConsoleLog::s_currentLog->BackSpace();
	}
	else
	{
		for(int i = 0; i < NUM_VIRTUAL_KEYS; ++i)
		{
			if(InputHandler::m_currentCharState[i])
			{
				std::string string;
				string.push_back((uchar)i);
				ConsoleLog::s_currentLog->PrintToCommandPrompt(string);
				InputHandler::m_currentCharState[i] = false;
			}
		}
	}
}

//-----------------------------------------------------------------------------------------------
void Game::PotentiallyRequestGameStartFromServer()
{
	if( g_currentElapsedTimeToSendToServer < SEND_FREQUENCY )
	{
		return;
	}

	CS6Packet requestGameStartPacket;

	ZeroMemory( &requestGameStartPacket, sizeof( requestGameStartPacket ) );

	requestGameStartPacket.packetType = TYPE_Acknowledge;
	requestGameStartPacket.data.acknowledged.packetType = TYPE_Acknowledge;

	UDPSendMessageToServer( sendToServerSocket, UDPServerAddressInfo, ( char* )&requestGameStartPacket, sizeof( CS6Packet ) );

	g_currentElapsedTimeToSendToServer = 0.f;

}

//-----------------------------------------------------------------------------------------------
void Game::PotentiallySendUpdatePacketToServer()
{
	if( g_currentElapsedTimeToSendToServer < SEND_FREQUENCY )
	{
		return;
	}

	CS6Packet updatePacket;

	ZeroMemory( &updatePacket, sizeof( updatePacket ) );

	updatePacket.packetType = TYPE_Update;
	updatePacket.data.updated.xPosition = g_localPlayer.currentPos.x;
	updatePacket.data.updated.yPosition = g_localPlayer.currentPos.y;

	updatePacket.data.updated.xVelocity = g_localPlayer.currentVelocity.x;
	updatePacket.data.updated.yVelocity = g_localPlayer.currentVelocity.y;

	updatePacket.data.updated.yawDegrees = g_localPlayer.orientationAsDegrees;

	UDPSendMessageToServer( sendToServerSocket, UDPServerAddressInfo, ( char* )&updatePacket, sizeof( updatePacket ) );

	g_currentElapsedTimeToSendToServer = 0.f;
}

//-----------------------------------------------------------------------------------------------
void Game::CheckCollisionAndSendVictoryIfSuccessfulTag()
{
	const float DISTANCE_TO_TAG = 15.f;

	if( g_currentElapsedTimeToSendToServer < SEND_FREQUENCY )
	{
		return;
	}

	//FUTURE EDIT, may need this.
	//g_currentElapsedTimeToSendToServer = 0.f;

	float distance = 0.f;

	auto iter = g_players.begin();

	for ( ; iter != g_players.end(); ++iter )
	{
		if( iter->id == g_localPlayer.id )
		{
			continue;
		}
		
		distance = Vector2f::Distance( g_localPlayer.currentPos, iter->currentPos );

		if( distance <= DISTANCE_TO_TAG )
		{
			CS6Packet victoryPacketToSendToServer;
			ZeroMemory( &victoryPacketToSendToServer, sizeof( victoryPacketToSendToServer ) );

			victoryPacketToSendToServer.packetType = TYPE_Victory;
			victoryPacketToSendToServer.data.victorious.playerColorAndID[ 0 ] = iter->id.r;
			victoryPacketToSendToServer.data.victorious.playerColorAndID[ 1 ] = iter->id.g;
			victoryPacketToSendToServer.data.victorious.playerColorAndID[ 2 ] = iter->id.b;

			UDPSendMessageToServer( sendToServerSocket, UDPServerAddressInfo, ( char* )&victoryPacketToSendToServer, sizeof( victoryPacketToSendToServer ) );

			break;
		}
	}
}

//-----------------------------------------------------------------------------------------------
void Game::SetPlayerParameters( NamedProperties& parameters )
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

	g_localPlayer.id.r = static_cast< uchar >( strtol( playerRedAsString.c_str(), 0, 10 ) );
	g_localPlayer.id.g = static_cast< uchar >( strtol( playerGreenAsString.c_str(), 0, 10 ) );
	g_localPlayer.id.b = static_cast< uchar >( strtol( playerBlueAsString.c_str(), 0, 10 ) );
}

//-----------------------------------------------------------------------------------------------
void Game::SetServerIpFromParameters( NamedProperties& parameters )
{
	std::string ipAddressAsString;

	parameters.Get( "param1", ipAddressAsString );

	FATAL_ASSERTION( ipAddressAsString != "", "Command: ip did not receive the correct parameters.\nip expects a non empty string." );

	g_serverIPAsString = ipAddressAsString;
}

//-----------------------------------------------------------------------------------------------
void Game::SetServerPortFromParameters( NamedProperties& parameters )
{
	std::string portAsString;

	parameters.Get( "param1", portAsString );

	FATAL_ASSERTION( portAsString != "", "Command: port did not receive the correct parameters.\nport expects a non empty string." );

	g_serverPortAsString = portAsString;
}


//-----------------------------------------------------------------------------------------------
void Game::CreateSound(const char* songPath, FMOD::Sound* &sound)
{
	FMOD_RESULT result;
	result = Game::m_soundSystem->createSound( songPath,FMOD_DEFAULT, 0, &sound );
	assert( result == FMOD_OK );
}

//-----------------------------------------------------------------------------------------------
void Game::PlaySound( FMOD::Sound* &sound )
{
	FMOD_RESULT result;
	result = Game::m_soundSystem->playSound( FMOD_CHANNEL_FREE, sound, false, 0 );
	assert( result == FMOD_OK );
}

//-----------------------------------------------------------------------------------------------
void Game::PlayRandomSound( std::vector<FMOD::Sound*> &sounds )
{
	int soundToPlay = rand() % sounds.size();
	PlaySound( sounds[soundToPlay] );
}


//-----------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------
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

	ZeroMemory( &udpAddressInfo, sizeof( udpAddressInfo ) );
	udpAddressInfo.sin_family = AF_INET;
	udpAddressInfo.sin_addr.S_un.S_addr = inet_addr( ipAddress.c_str() );
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
bool UDPSendMessageToServer( const SOCKET& socketToSendThru, sockaddr_in& udpServerAddressInfo, const char* message, int messageLength )
{
	int sendResultAsInt;

	char receiveBuffer[ BUFFER_LIMIT ];
	memset( receiveBuffer, '\0', BUFFER_LIMIT );

	int senderInfoLength = sizeof( udpServerAddressInfo );

	sendResultAsInt = sendto( socketToSendThru, message, messageLength, 0, (struct sockaddr *)&udpServerAddressInfo, senderInfoLength );
	if ( sendResultAsInt == SOCKET_ERROR ) 
	{
		printf( "send failed with error: %d\n", WSAGetLastError() );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------------------------
bool ChangeServerInfo( const std::string& serverIP, const std::string& serverPort, sockaddr_in& UDPServerAddressInfo_out )
{
	return CreateAddressInfoForUDP( serverIP, serverPort, UDPServerAddressInfo_out );
}


//-----------------------------------------------------------------------------------------------
bool UDPClientRecieveMessage( const SOCKET& receiveFromSocket )
{
	int bytesReceived = 0;
	sockaddr_in senderInfo;
	int senderInfoLength = sizeof( senderInfo );

	CS6Packet receivedPacket;

	do 
	{
		ZeroMemory( &receivedPacket, sizeof( receivedPacket ) );
		ZeroMemory( &senderInfo, senderInfoLength );

		bytesReceived = 0;

		bytesReceived = recvfrom( receiveFromSocket, ( char* )&receivedPacket, sizeof( CS6Packet ), 0, (struct sockaddr *)&senderInfo, &senderInfoLength );

		if( bytesReceived == sizeof( CS6Packet ) )
		{
			++g_currentReceivedPacketCount;
			ProcessPacket( receivedPacket );
		}

	} 
	while ( bytesReceived > 0 );

	if( bytesReceived < 0 )
	{
		bytesReceived = WSAGetLastError();
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------------------------
void ProcessPacket( const CS6Packet& packet )
{
	PacketType typeOfPacket = packet.packetType;

	if( typeOfPacket == TYPE_Update )
	{
		OnReceiveUpdate( packet );
	}
	else if( typeOfPacket == TYPE_GameStart )
	{
		OnReceiveGameStart( packet );
	}
	else if( typeOfPacket == TYPE_Reset )
	{
		OnReceiveReset( packet );
	}
	else if( typeOfPacket == TYPE_Acknowledge )
	{
		OnReceiveAck( packet );
	}
	else if( typeOfPacket == TYPE_Victory )
	{
		OnReceiveVictory( packet );
	}
}


//-----------------------------------------------------------------------------------------------
void OnReceiveUpdate( const CS6Packet& bufferAsPacket )
{
	//if( ( int )bufferAsPacket.packetNumber < g_currentReceivedPacketCount )
	//{
	//	return;
	//}

	playerID idFromPacket( bufferAsPacket.playerColorAndID[ 0 ], bufferAsPacket.playerColorAndID[ 1 ], bufferAsPacket.playerColorAndID[ 2 ] );

	Vector2f newPosition( bufferAsPacket.data.updated.xPosition, bufferAsPacket.data.updated.yPosition );
	Vector2f newVelocity( bufferAsPacket.data.updated.xVelocity, bufferAsPacket.data.updated.yVelocity );
	float newOrientationDegrees(  bufferAsPacket.data.updated.yawDegrees );

	//FUTURE EDIT, NEEDS DEAD RECKONING

	auto iter = g_players.begin();

	for( ; iter != g_players.end(); ++ iter )
	{
		if( iter->id == idFromPacket )
		{
			iter->currentPos = newPosition;
			iter->currentVelocity = newVelocity;
			iter->orientationAsDegrees = newOrientationDegrees;

			if( iter->id == g_itPlayerID )
			{
				iter->isIt = true;
				g_itPlayerID = BAD_COLOR;
			}

			return;
		}
	}


	if( !( g_localPlayer.id == idFromPacket ) )
	{
		player newPlayer;

		newPlayer.currentPos = newPosition;
		newPlayer.currentVelocity = newVelocity;
		newPlayer.orientationAsDegrees = newOrientationDegrees;
		newPlayer.id = idFromPacket;

		if( idFromPacket == g_itPlayerID )
		{
			newPlayer.isIt = true;
			g_itPlayerID = BAD_COLOR;
		}

		g_players.push_back( newPlayer );
	}
	else if( idFromPacket == g_itPlayerID )
	{
		g_localPlayer.isIt = true;
		g_itPlayerID = BAD_COLOR;
	}
}


//-----------------------------------------------------------------------------------------------
void OnReceiveGameStart( const CS6Packet& packet )
{
	g_localPlayer.id.r = packet.data.gameStart.playerColorAndID[ 0 ];
	g_localPlayer.id.g = packet.data.gameStart.playerColorAndID[ 1 ];
	g_localPlayer.id.b = packet.data.gameStart.playerColorAndID[ 2 ];

	g_localPlayer.currentPos = Vector2f( packet.data.gameStart.playerXPosition, packet.data.gameStart.playerYPosition );
	g_localPlayer.currentVelocity = Vector2f::Zero();
	g_localPlayer.orientationAsDegrees = 0.f;

	g_itPlayerID.r = packet.data.gameStart.itPlayerColorAndID[ 0 ];
	g_itPlayerID.g = packet.data.gameStart.itPlayerColorAndID[ 1 ];
	g_itPlayerID.b = packet.data.gameStart.itPlayerColorAndID[ 2 ];


	//Send ack packet to server
	CS6Packet acknowledgeGameStartPacket;
	ZeroMemory( &acknowledgeGameStartPacket, sizeof( acknowledgeGameStartPacket ) );

	acknowledgeGameStartPacket.packetType = TYPE_Acknowledge;
	acknowledgeGameStartPacket.data.acknowledged.packetType = TYPE_GameStart;
	acknowledgeGameStartPacket.data.acknowledged.packetNumber = packet.packetNumber;

	UDPSendMessageToServer( sendToServerSocket, UDPServerAddressInfo, ( char* )&acknowledgeGameStartPacket, sizeof( acknowledgeGameStartPacket ) );

	g_currentState = STATE_InGame;
}


//-----------------------------------------------------------------------------------------------
void OnReceiveReset( const CS6Packet& bufferAsPacket )
{
	g_localPlayer.currentPos = Vector2f( bufferAsPacket.data.reset.playerXPosition, bufferAsPacket.data.reset.playerYPosition );
	g_localPlayer.currentVelocity = Vector2f::Zero();
	g_localPlayer.orientationAsDegrees = 0.f;


	playerID itPlayerID( bufferAsPacket.data.reset.itPlayerColorAndID[ 0 ], bufferAsPacket.data.reset.itPlayerColorAndID[ 1 ], bufferAsPacket.data.reset.itPlayerColorAndID[ 2 ] );

	auto iter = g_players.begin(); 

	for( ; iter != g_players.end(); ++ iter )
	{
		iter->isIt = false;

		if( iter->id == itPlayerID )
		{
			iter->isIt = true;
		}
	}

	//Send ack packet to server
	CS6Packet acknowledgeResetPacket;
	ZeroMemory( &acknowledgeResetPacket, sizeof( acknowledgeResetPacket ) );

	acknowledgeResetPacket.packetType = TYPE_Acknowledge;
	acknowledgeResetPacket.data.acknowledged.packetType = TYPE_Reset;
	acknowledgeResetPacket.data.acknowledged.packetNumber = bufferAsPacket.packetNumber;

	UDPSendMessageToServer( sendToServerSocket, UDPServerAddressInfo, ( char* )&acknowledgeResetPacket, sizeof( acknowledgeResetPacket ) );
}


//-----------------------------------------------------------------------------------------------
void OnReceiveAck( const CS6Packet& bufferAsPacket )
{
	PacketType typeOfAck = bufferAsPacket.data.acknowledged.packetType;

	if( typeOfAck == TYPE_Victory )
	{
		//FUTURE EDIT, may need to do something
	}
	else if( typeOfAck == TYPE_Acknowledge )
	{
		//Think I can leave blank
	}
	else if( typeOfAck == TYPE_GameStart )
	{
		//Think I can leave this blank
	}
	else if( typeOfAck == TYPE_Reset )
	{
		//Think I can leave blank
	}
	else if( typeOfAck == TYPE_Update )
	{
		//Think I can leave blank
	}
}


//-----------------------------------------------------------------------------------------------
void OnReceiveVictory( const CS6Packet& bufferAsPacket )
{
	UNUSED( bufferAsPacket );
	//FUTURE EDIT, may want to do something here.
}



