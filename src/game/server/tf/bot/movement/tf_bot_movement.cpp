//========= TFBot overhaul: combat movement (M4) ==============================//
//
// See tf_bot_movement.h and docs/tfbot/DESIGN.md M4.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_movement.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/profile/tf_bot_profile.h"
#include "bot/mobility/tf_bot_mobility.h"
#include "tf_weapon_compound_bow.h"
#include "tf_weapon_medigun.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

struct TFBotMoveState_t
{
	int m_strafeDir;			// -1 right, 0 none, +1 left
	float m_nextChange;
	float m_nextJump;
	int m_doubleJumpStage;		// scouts: 0 idle, 1 first jump pressed, 2 waiting to press again
	float m_doubleJumpTime;
};

static TFBotMoveState_t s_move[ MAX_PLAYERS + 1 ];


//-----------------------------------------------------------------------------
bool TFBotMovement::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_MOVEMENT );
}


//-----------------------------------------------------------------------------
// Conditions under which nobody should be strafing.
//-----------------------------------------------------------------------------
static bool ShouldHoldStill( CTFBot *me, bool *isHealing )
{
	*isHealing = false;

	if ( me->m_Shared.IsInvulnerable() || me->m_Shared.InCond( TF_COND_ZOOMED ) || me->m_Shared.InCond( TF_COND_TAUNTING ) )
		return true;
	if ( me->HasAttribute( CTFBot::DISABLE_DODGE ) )
		return true;
	if ( me->GetIntentionInterface()->ShouldHurry( me ) == ANSWER_YES )
		return true;
	if ( me->m_Shared.InCond( TF_COND_DISGUISED ) || me->m_Shared.InCond( TF_COND_DISGUISING ) || me->m_Shared.IsStealthed() )
		return true;
	if ( me->IsPlayerClass( TF_CLASS_ENGINEER ) && me->m_Shared.IsCarryingObject() )
		return true;

	CTFWeaponBase *gun = (CTFWeaponBase *)me->Weapon_GetSlot( TF_WPN_TYPE_PRIMARY );
	if ( gun && gun->IsWeapon( TF_WEAPON_COMPOUND_BOW ) && static_cast< CTFCompoundBow * >( gun )->GetCurrentCharge() > 0.0f )
		return true;	// drawing the bow: hold still

	if ( me->IsPlayerClass( TF_CLASS_MEDIC ) )
	{
		CWeaponMedigun *medigun = dynamic_cast< CWeaponMedigun * >( me->GetActiveTFWeapon() );
		if ( medigun && medigun->GetHealTarget() )
		{
			*isHealing = true;
			return false;
		}
	}

	return !me->IsCombatWeapon( MY_CURRENT_GUN );
}


//-----------------------------------------------------------------------------
bool TFBotMovement::CombatMove( CTFBot *me )
{
	if ( !IsActive( me ) )
		return false;

	int index = me->entindex();
	if ( index < 1 || index > MAX_PLAYERS )
		return false;
	TFBotMoveState_t &st = s_move[ index ];
	const float now = gpGlobals->curtime;

	// Finish a scout double jump that is in progress.
	if ( st.m_doubleJumpStage == 1 && now - st.m_doubleJumpTime > 0.1f )
	{
		st.m_doubleJumpStage = 2;		// released; press again shortly
	}
	else if ( st.m_doubleJumpStage == 2 && now - st.m_doubleJumpTime > 0.3f )
	{
		if ( !me->GetLocomotionInterface()->IsOnGround() )
		{
			me->PressJumpButton( 0.1f );
			st.m_strafeDir = -st.m_strafeDir;	// change direction mid-air
			TFBotFeatures::CountEvent( "move_double_jump" );
		}
		st.m_doubleJumpStage = 0;
	}

	bool isHealing;
	if ( ShouldHoldStill( me, &isHealing ) || TFBotMobility::IsBlastJumping( me ) )
		return true;

	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( !threat || !threat->IsVisibleRecently() )
	{
		st.m_strafeDir = 0;
		return true;
	}

	// Only dodge if they can actually shoot at us.
	if ( !me->IsLineOfFireClear( threat->GetLastKnownPosition() ) )
		return true;

	const float strafe = TFBotProfiles::GetEffective( me ).Get( TFBOT_PROFILE_STRAFE );

	if ( now >= st.m_nextChange )
	{
		// Stronger strafers change direction sooner and pause less.
		float pauseChance = Lerp( strafe, 0.35f, 0.08f );
		if ( isHealing )
			pauseChance += 0.2f;
		if ( RandomFloat() < pauseChance )
			st.m_strafeDir = 0;
		else if ( st.m_strafeDir == 0 )
			st.m_strafeDir = RandomInt( 0, 1 ) ? 1 : -1;
		else
			st.m_strafeDir = ( RandomFloat() < 0.8f ) ? -st.m_strafeDir : st.m_strafeDir;

		TFBotFeatures::CountEvent( "move_strafe_change" );
		float lo = Lerp( strafe, 0.6f, 0.25f );
		float hi = Lerp( strafe, 1.1f, 0.65f );
		st.m_nextChange = now + RandomFloat( lo, hi );
	}

	if ( st.m_strafeDir != 0 )
	{
		Vector forward;
		me->EyeVectors( &forward );
		Vector left( -forward.y, forward.x, 0.0f );
		left.NormalizeInPlace();

		const float sideStepSize = 30.0f;
		Vector step = me->GetAbsOrigin() + ( st.m_strafeDir > 0 ? sideStepSize : -sideStepSize ) * left;
		if ( me->GetLocomotionInterface()->HasPotentialGap( me->GetAbsOrigin(), step ) )
		{
			st.m_strafeDir = -st.m_strafeDir;	// don't strafe off a ledge
		}
		else if ( st.m_strafeDir > 0 )
		{
			me->PressLeftButton();
		}
		else
		{
			me->PressRightButton();
		}
	}

	// Jumps: mobile classes, more for aggressive strafers. Never while healing.
	bool canJump = !isHealing && me->GetLocomotionInterface()->IsOnGround() &&
				   ( me->IsPlayerClass( TF_CLASS_SCOUT ) || me->IsPlayerClass( TF_CLASS_SOLDIER ) ||
					 me->IsPlayerClass( TF_CLASS_DEMOMAN ) || me->IsPlayerClass( TF_CLASS_PYRO ) );
	if ( canJump && now >= st.m_nextJump )
	{
		st.m_nextJump = now + RandomFloat( Lerp( strafe, 4.0f, 1.5f ), Lerp( strafe, 8.0f, 3.5f ) );
		if ( RandomFloat() < 0.4f + 0.5f * strafe )
		{
			me->PressJumpButton( 0.1f );
			TFBotFeatures::CountEvent( "move_jump" );
			if ( me->IsPlayerClass( TF_CLASS_SCOUT ) && RandomFloat() < 0.5f + 0.4f * strafe )
			{
				st.m_doubleJumpStage = 1;
				st.m_doubleJumpTime = now;
			}
		}
	}

	return true;
}
