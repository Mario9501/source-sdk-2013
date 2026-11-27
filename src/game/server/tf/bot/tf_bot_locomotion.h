//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_locomotion.h
// Team Fortress NextBot locomotion interface
// Michael Booth, May 2010

#ifndef TF_BOT_LOCOMOTION_H
#define TF_BOT_LOCOMOTION_H

#include "NextBot/Player/NextBotPlayerLocomotion.h"

//----------------------------------------------------------------------------
class CTFBotLocomotion : public PlayerLocomotion
{
public:
	DECLARE_CLASS( CTFBotLocomotion, PlayerLocomotion );

	CTFBotLocomotion( INextBot *bot ) : PlayerLocomotion( bot )
	{
	}

	virtual ~CTFBotLocomotion() { }

	virtual void Update( void );								// (EXTEND) update internal state

	virtual void Approach( const Vector &pos, float goalWeight = 1.0f );	// move directly towards the given position

	virtual float GetMaxJumpHeight( void ) const;				// return maximum height of a jump
	virtual float GetDeathDropHeight( void ) const;			// distance at which we will die if we fall

	virtual float GetRunSpeed( void ) const;				// get maximum running speed

	virtual bool IsAreaTraversable( const CNavArea *baseArea ) const;	// return true if given area can be used for navigation
	virtual bool IsEntityTraversable( CBaseEntity *obstacle, TraverseWhenType when = EVENTUALLY ) const;

	//
	// ILocomotion modifiers
	//
	virtual void Jump( void ) OVERRIDE;								// initiate a simple undirected jump in the air
	virtual bool ClimbUpToLedge( const Vector &landingGoal, const Vector &landingForward, const CBaseEntity *obstacle ) OVERRIDE;	// TFBot overhaul (M7): blast jump high ledges

	// TFBot overhaul (M7): the stock climb (jump + steer to the landing goal),
	// started by the blast-jump state machine at the right moment.
	void StartClimbJump( const Vector &landingGoal, const Vector &landingForward ) { BaseClass::ClimbUpToLedge( landingGoal, landingForward, NULL ); }

protected:
	virtual void AdjustPosture( const Vector &moveGoal ) { }	// never crouch to navigate
};

// GetMaxJumpHeight: 72 (http://developer.valvesoftware.com/wiki/TF2/Team_Fortress_2_Mapper%27s_Reference),
// or more for bots able to blast jump (TFBot overhaul M7) - see tf_bot_locomotion.cpp.

#endif // TF_BOT_LOCOMOTION_H
