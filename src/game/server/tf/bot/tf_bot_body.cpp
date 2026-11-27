//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_body.cpp
// Team Fortress NextBot body interface
// Michael Booth, May 2010

#include "cbase.h"

#include "tf_bot.h"
#include "tf_bot_body.h"
#include "bot/profile/tf_bot_profile.h"	// TFBot overhaul
#include "bot/aim/tf_bot_aim.h"			// TFBot overhaul


//---------------------------------------------------------------------------------------------
// TFBot overhaul (M2): head turn speed from the profile when the 'aim' feature is on
float CTFBotBody::GetMaxHeadAngularVelocity( void ) const
{
	CTFBot *me = (CTFBot *)GetBot();
	if ( TFBotAim::IsActive( me ) )
		return TFBotAim::GetMaxHeadAngularVelocity( me );

	return PlayerBody::GetMaxHeadAngularVelocity();
}


// 
// Return how often we should sample our target's position and 
// velocity to update our aim tracking, to allow realistic slop in tracking
//
float CTFBotBody::GetHeadAimTrackingInterval( void ) const
{
	CTFBot *me = (CTFBot *)GetBot();

	// don't let Spies in MvM mode aim too precisely
	if ( TFGameRules()->IsMannVsMachineMode() && me->IsPlayerClass( TF_CLASS_SPY ) )
	{
		return 0.25f;
	}

	// TFBot overhaul: per-bot tracking lag from its profile
	if ( const TFBotProfile_t *profile = TFBotProfiles::Get( me ) )
	{
		return profile->Get( TFBOT_PROFILE_AIM_TRACKING );
	}

	switch( me->GetDifficulty() )
	{
	case CTFBot::EXPERT:
		return 0.05f;

	case CTFBot::HARD:
		return 0.1f;

	case CTFBot::NORMAL:
		return 0.25f;

	case CTFBot::EASY:
		return 1.0f;
	}

	return 0.0f;
}
