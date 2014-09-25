#include "ClientPlayer.hpp"

#include "Engine/Utilities/Clock.hpp"
#include "Engine/Rendering/OpenGLRenderer.hpp"
#include "Engine/Utilities/CommonUtilities.hpp"

const float PLAYER_SPEED = 3000.f;
const float ANGULAR_VELOCITY = 300.f;


//-----------------------------------------------------------------------------------------------
ClientPlayer::ClientPlayer()
	: id()
	, currentPos( Vector2f::Zero() )
	, currentVelocity( Vector2f::Zero() )
	, desiredPos( Vector2f::Zero() )
	, desiredVelocity( Vector2f::Zero() )
	, orientationAsDegrees( 0.f )
{
	CreateShipVerts();
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::SetCurrentVelocityAndPositionFromMagnitude( float magnitude )
{
	Clock& clock = Clock::GetMasterClock();

	float orientationRadians = PI_OVER_180 * orientationAsDegrees;
	Vector2f deltaVelocity;

	deltaVelocity = Vector2f( cos( orientationRadians ), sin( orientationRadians ) );
	deltaVelocity *= PLAYER_SPEED * magnitude;

	currentVelocity =deltaVelocity * static_cast< float >( clock.m_currentDeltaSeconds );

	currentPos += currentVelocity * static_cast< float >( clock.m_currentDeltaSeconds );
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::SeekTarget()
{
	currentVelocity = desiredVelocity;
	currentPos = desiredPos;
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::Reset()
{
	currentPos = Vector2f::Zero();
	currentVelocity = Vector2f::Zero();
	desiredPos = Vector2f::Zero();
	desiredVelocity = Vector2f::Zero();
	orientationAsDegrees = 0.f;
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::CreateShipVerts()
{
	m_shipVertexArray.push_back( Vector2f( 7.5f, 0.f )   );
	m_shipVertexArray.push_back( Vector2f( -7.5f, 5.f )  );
	m_shipVertexArray.push_back( Vector2f( -5.f, 2.f )   );
	m_shipVertexArray.push_back( Vector2f( -5.f, -2.f )  );
	m_shipVertexArray.push_back( Vector2f( -7.5f, -5.f ) );
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::RotateForTimeStep( RotationDirection direction )
{
	Clock& clock = Clock::GetMasterClock();

	float deltaSeconds = static_cast< float >( clock.m_currentDeltaSeconds );

	if( direction == ClientPlayer::LEFT )
	{
		orientationAsDegrees += ANGULAR_VELOCITY * deltaSeconds;
	}
	else
	{
		orientationAsDegrees += -ANGULAR_VELOCITY * deltaSeconds;
	}
}


//-----------------------------------------------------------------------------------------------
void ClientPlayer::RenderShip() const
{
	Vector3f shipColorAsVector3 = id.ToVector3fNormalizedAndFullyOpaque();

	OpenGLRenderer::PushMatrix();
	OpenGLRenderer::Translatef( currentPos.x, currentPos.y, 0.f );
	OpenGLRenderer::Rotatef( orientationAsDegrees, 0.f, 0.f, 1.f );

	OpenGLRenderer::Begin( GL_LINE_LOOP );
	{
		OpenGLRenderer::Color3f( shipColorAsVector3.x, shipColorAsVector3.y, shipColorAsVector3.z );
		for(int i = 0; i < (int)m_shipVertexArray.size(); i++)
		{
			OpenGLRenderer::Vertex2f( m_shipVertexArray[i].x, m_shipVertexArray[i].y );
		}
	}
	OpenGLRenderer::End();

	OpenGLRenderer::Color3f( 1.f, 1.f, 1.f );

	OpenGLRenderer::PopMatrix();
}