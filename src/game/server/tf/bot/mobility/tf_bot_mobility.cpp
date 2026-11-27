//========= TFBot overhaul: mobility jumps (M7) ===============================//
//
// See tf_bot_mobility.h and docs/tfbot/DESIGN.md M7.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_mobility.h"
#include "bot/tf_bot.h"
#include "bot/tf_bot_locomotion.h"
#include "bot/features/tf_bot_features.h"
#include "tf_gamerules.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_blast_jump_height( "tf_bot_blast_jump_height", "300", FCVAR_CHEAT,
	"Ledge height capable soldiers/demos may plan to rocket/sticky jump up (M7)" );
ConVar tf_bot_blast_jump_min_health( "tf_bot_blast_jump_min_health", "0.6", FCVAR_CHEAT,
	"Health fraction a bot needs before it will blast jump (M7)" );

static const float kNormalJump = 72.0f;

enum EBlastPhase
{
	BLAST_NONE = 0,
	BLAST_PREPARE,		// equip, look down
	BLAST_ARMING,		// demo: sticky planted, waiting for it to arm
	BLAST_JUMP,			// just jumped, fire / detonate next
	BLAST_AIRBORNE,
};

struct TFBotBlastState_t
{
	int m_phase;
	float m_phaseStart;
	float m_started;
	Vector m_goal;
	Vector m_forward;
	float m_cooldownUntil;		// after a failed attempt, plan with a normal jump for a while
};

static TFBotBlastState_t s_blast[ MAX_PLAYERS + 1 ];

static TFBotBlastState_t *StateFor( const CTFBot *bot )
{
	int index = bot->entindex();
	return ( index >= 1 && index <= MAX_PLAYERS ) ? &s_blast[ index ] : NULL;
}


//-----------------------------------------------------------------------------
bool TFBotMobility::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_MOBILITY ) &&
		   !( TFGameRules() && TFGameRules()->IsMannVsMachineMode() );
}

// The weapon this bot blast jumps with, if it can right now.
static CTFWeaponBase *BlastWeapon( const CTFBot *bot )
{
	CTFBot *me = const_cast< CTFBot * >( bot );
	CTFWeaponBase *weapon = NULL;
	if ( me->IsPlayerClass( TF_CLASS_SOLDIER ) )
	{
		weapon = (CTFWeaponBase *)me->Weapon_GetSlot( TF_WPN_TYPE_PRIMARY );
		if ( weapon && !( weapon->IsWeapon( TF_WEAPON_ROCKETLAUNCHER ) || weapon->IsWeapon( TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT ) ||
						  weapon->IsWeapon( TF_WEAPON_PARTICLE_CANNON ) ) )
			weapon = NULL;
	}
	else if ( me->IsPlayerClass( TF_CLASS_DEMOMAN ) )
	{
		weapon = (CTFWeaponBase *)me->Weapon_GetSlot( TF_WPN_TYPE_SECONDARY );
		if ( weapon && !weapon->IsWeapon( TF_WEAPON_PIPEBOMBLAUNCHER ) )
			weapon = NULL;
	}
	if ( !weapon )
		return NULL;
	if ( weapon->Clip1() <= 0 && me->GetAmmoCount( weapon->GetPrimaryAmmoType() ) <= 0 )
		return NULL;
	return weapon;
}

static bool IsCapable( const CTFBot *bot )
{
	CTFBot *me = const_cast< CTFBot * >( bot );	// IsAlive/GetMaxHealth aren't const
	if ( !TFBotMobility::IsActive( bot ) || !me->IsAlive() )
		return false;
	const TFBotBlastState_t *st = StateFor( bot );
	if ( st && gpGlobals->curtime < st->m_cooldownUntil )
		return false;	// just failed one: the next repath should take another way
	if ( (float)me->GetHealth() < tf_bot_blast_jump_min_health.GetFloat() * me->GetMaxHealth() )
		return false;
	return BlastWeapon( bot ) != NULL;
}


//-----------------------------------------------------------------------------
float TFBotMobility::GetMaxJumpHeight( const CTFBot *bot )
{
	return IsCapable( bot ) ? MAX( kNormalJump, tf_bot_blast_jump_height.GetFloat() ) : kNormalJump;
}

bool TFBotMobility::IsBlastJumping( const CTFBot *bot )
{
	const TFBotBlastState_t *st = StateFor( bot );
	return st && st->m_phase != BLAST_NONE;
}


//-----------------------------------------------------------------------------
bool TFBotMobility::HandleClimb( CTFBot *bot, const Vector &landingGoal, const Vector &landingForward )
{
	TFBotBlastState_t *st = StateFor( bot );
	if ( !st )
		return false;
	if ( st->m_phase != BLAST_NONE )
		return true;	// already on it

	float height = landingGoal.z - bot->GetAbsOrigin().z;
	if ( height <= kNormalJump - 8.0f || !IsCapable( bot ) )
		return false;	// a normal jump will do (or we can't blast jump)

	st->m_phase = BLAST_PREPARE;
	st->m_phaseStart = st->m_started = gpGlobals->curtime;
	st->m_goal = landingGoal;
	st->m_forward = landingForward;
	TFBotFeatures::CountEvent( "blast_jump_start" );
	return true;
}


//-----------------------------------------------------------------------------
static void Abort( TFBotBlastState_t *st )
{
	if ( st->m_phase != BLAST_AIRBORNE )
	{
		TFBotFeatures::CountEvent( "blast_jump_abort" );
		st->m_cooldownUntil = gpGlobals->curtime + 10.0f;
	}
	st->m_phase = BLAST_NONE;
}

void TFBotMobility::Update( CTFBot *bot )
{
	TFBotBlastState_t *st = StateFor( bot );
	if ( !st || st->m_phase == BLAST_NONE )
		return;

	const float now = gpGlobals->curtime;
	const float inPhase = now - st->m_phaseStart;
	if ( !bot->IsAlive() || now - st->m_started > 4.0f )
	{
		Abort( st );
		return;
	}

	CTFBotLocomotion *loco = static_cast< CTFBotLocomotion * >( bot->GetLocomotionInterface() );
	CTFWeaponBase *weapon = BlastWeapon( bot );
	bool isSoldier = bot->IsPlayerClass( TF_CLASS_SOLDIER );

	// Look down, slightly behind us, so the blast pushes toward the ledge.
	Vector toGoal = st->m_goal - bot->GetAbsOrigin();
	toGoal.z = 0.0f;
	toGoal.NormalizeInPlace();
	// 10 units back from the feet is ~80 degrees down from eye height
	Vector blastSpot = bot->GetAbsOrigin() - toGoal * 10.0f;

	switch ( st->m_phase )
	{
	case BLAST_PREPARE:
	{
		if ( !weapon || inPhase > 1.5f )
		{
			Abort( st );
			return;
		}
		if ( bot->GetActiveTFWeapon() != weapon )
			bot->Weapon_Switch( weapon );
		bot->GetBodyInterface()->AimHeadTowards( blastSpot, IBody::MANDATORY, 0.3f, NULL, "Aiming a blast jump" );

		bool ready = bot->GetActiveTFWeapon() == weapon && weapon->m_flNextPrimaryAttack <= now &&
					 weapon->Clip1() > 0 && bot->EyeAngles().x > 65.0f && bot->GetLocomotionInterface()->IsOnGround();
		if ( !ready )
			return;

		if ( isSoldier )
		{
			loco->StartClimbJump( st->m_goal, st->m_forward );
			st->m_phase = BLAST_JUMP;
		}
		else
		{
			bot->PressFireButton( 0.05f );		// plant one sticky at our feet
			st->m_phase = BLAST_ARMING;
		}
		st->m_phaseStart = now;
		return;
	}

	case BLAST_ARMING:
		bot->GetBodyInterface()->AimHeadTowards( blastSpot, IBody::MANDATORY, 0.3f, NULL, "Waiting for my sticky to arm" );
		if ( inPhase > 0.9f )
		{
			loco->StartClimbJump( st->m_goal, st->m_forward );
			st->m_phase = BLAST_JUMP;
			st->m_phaseStart = now;
		}
		return;

	case BLAST_JUMP:
		bot->GetBodyInterface()->AimHeadTowards( blastSpot, IBody::MANDATORY, 0.2f, NULL, "Blast jumping" );
		if ( inPhase > 0.04f )
		{
			if ( isSoldier )
				bot->PressFireButton( 0.05f );
			else
				bot->PressAltFireButton( 0.05f );	// detonate
			st->m_phase = BLAST_AIRBORNE;
			st->m_phaseStart = now;
		}
		return;

	case BLAST_AIRBORNE:
		bot->PressCrouchButton( 0.1f );
		if ( inPhase > 0.3f && bot->GetLocomotionInterface()->IsOnGround() )
		{
			TFBotFeatures::CountEvent( "blast_jump_landed" );
			Abort( st );	// landed
		}
		return;
	}
}
