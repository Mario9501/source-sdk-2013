//========= TFBot overhaul: engineers (M3b) ===================================//
//
// See tf_bot_engineer_overhaul.h and docs/tfbot/DESIGN.md M3b.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_engineer_overhaul.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/profile/tf_bot_profile.h"
#include "bot/behavior/engineer/tf_bot_engineer_move_to_build.h"
#include "bot/unlocks/tf_bot_unlocks.h"
#include "tf_obj.h"
#include "tf_obj_sentrygun.h"
#include "tf_weapon_builder.h"
#include "tf_gamerules.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "nav_mesh/tf_nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_engineer_forward_offset( "tf_bot_engineer_forward_offset", "400", FCVAR_CHEAT,
	"Forward-style engineers nest this far (incursion distance) behind the team's front (M3b)" );
ConVar tf_bot_engineer_advance_gap( "tf_bot_engineer_advance_gap", "1300", FCVAR_CHEAT,
	"Forward-style engineers move the nest up once the team front is this far ahead of it (M3b)" );
ConVar tf_bot_engineer_help_range( "tf_bot_engineer_help_range", "900", FCVAR_CHEAT,
	"How far an engineer goes to help a teammate's building (M3b)" );

struct TFBotEngineerState_t
{
	CountdownTimer m_advanceCheck;
	CountdownTimer m_helpCheck;
};
static TFBotEngineerState_t s_engineer[ MAX_PLAYERS + 1 ];

static TFBotEngineerState_t *StateFor( const CTFBot *bot )
{
	int index = bot->entindex();
	return ( index >= 1 && index <= MAX_PLAYERS ) ? &s_engineer[ index ] : NULL;
}


//-----------------------------------------------------------------------------
bool TFBotEngineer::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_ENGINEER ) &&
		   bot->IsPlayerClass( TF_CLASS_ENGINEER ) &&
		   !( TFGameRules() && TFGameRules()->IsMannVsMachineMode() );
}

int TFBotEngineer::GetStyle( const CTFBot *bot )
{
	int style = (int)( TFBotProfiles::GetEffective( bot ).Get( TFBOT_PROFILE_ENGINEER_STYLE ) + 0.5f );
	// Battle engineering needs the Gunslinger (M6 'unlocks'); without it, play forward.
	if ( style == TFBOT_ENGINEER_BATTLE && !TFBotUnlocks::HasGunslinger( bot ) )
		style = TFBOT_ENGINEER_FORWARD;
	return style;
}

static bool NestsForward( const CTFBot *bot )
{
	int style = TFBotEngineer::GetStyle( bot );
	return style == TFBOT_ENGINEER_FORWARD || style == TFBOT_ENGINEER_BATTLE;
}

bool TFBotEngineer::SkipDispenser( const CTFBot *bot )
{
	return IsActive( bot ) && GetStyle( bot ) == TFBOT_ENGINEER_BATTLE;
}

bool TFBotEngineer::ShouldFight( CTFBot *me, CObjectSentrygun *sentry )
{
	if ( !IsActive( me ) || !sentry || GetStyle( me ) != TFBOT_ENGINEER_BATTLE || sentry->IsBuilding() )
		return false;
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat( true );
	return threat && threat->GetEntity() && me->IsRangeLessThan( threat->GetEntity(), 1200.0f );
}


//-----------------------------------------------------------------------------
// The furthest-forward living teammate's incursion distance (from our spawn).
//-----------------------------------------------------------------------------
static float TeamFrontIncursion( CTFBot *me, Vector *frontPos )
{
	int team = me->GetTeamNumber();
	float front = -1.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *mate = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !mate || mate == me || !mate->IsAlive() || mate->GetTeamNumber() != team || mate->IsPlayerClass( TF_CLASS_SPY ) )
			continue;
		CTFNavArea *area = static_cast< CTFNavArea * >( mate->GetLastKnownArea() );
		if ( !area )
			continue;
		float incursion = area->GetIncursionDistance( team );
		if ( incursion > front )
		{
			front = incursion;
			if ( frontPos )
				*frontPos = mate->GetAbsOrigin();
		}
	}
	return front;
}


//-----------------------------------------------------------------------------
bool TFBotEngineer::PickNestLocation( CTFBot *me, Vector *location )
{
	if ( !IsActive( me ) || !NestsForward( me ) )
		return false;

	Vector frontPos;
	float front = TeamFrontIncursion( me, &frontPos );
	if ( front < 0.0f )
		return false;

	// Battle engineers drop minis right at the fight.
	const int team = me->GetTeamNumber();
	const float offset = ( GetStyle( me ) == TFBOT_ENGINEER_BATTLE ) ? 150.0f : tf_bot_engineer_forward_offset.GetFloat();
	const float target = front - offset;
	const float window = 250.0f;

	// Areas just behind the front, near the front player, with room to build.
	CUtlVector< CTFNavArea * > candidates;
	float totalWeight = 0.0f;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CTFNavArea *area = static_cast< CTFNavArea * >( TheNavAreas[i] );
		float incursion = area->GetIncursionDistance( team );
		if ( incursion < 0.0f || fabsf( incursion - target ) > window )
			continue;
		if ( area->GetSizeX() < 50.0f || area->GetSizeY() < 50.0f )
			continue;
		if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
			continue;
		if ( ( area->GetCenter() - frontPos ).IsLengthGreaterThan( 1500.0f ) )
			continue;
		candidates.AddToTail( area );
		totalWeight += area->GetSizeX() * area->GetSizeY();
	}
	if ( candidates.Count() == 0 )
		return false;

	float pick = RandomFloat( 0.0f, totalWeight );
	FOR_EACH_VEC( candidates, c )
	{
		pick -= candidates[c]->GetSizeX() * candidates[c]->GetSizeY();
		if ( pick <= 0.0f )
		{
			*location = candidates[c]->GetRandomPoint();
			TFBotFeatures::CountEvent( "eng_forward_nest" );
			return true;
		}
	}
	*location = candidates.Tail()->GetRandomPoint();
	return true;
}


//-----------------------------------------------------------------------------
bool TFBotEngineer::ShouldAdvanceNest( CTFBot *me, CObjectSentrygun *sentry, bool isUnderAttack )
{
	if ( !IsActive( me ) || !sentry || isUnderAttack || !NestsForward( me ) )
		return false;
	if ( TFGameRules()->InSetup() )
		return false;

	TFBotEngineerState_t *st = StateFor( me );
	if ( !st || ( st->m_advanceCheck.HasStarted() && !st->m_advanceCheck.IsElapsed() ) )
		return false;
	st->m_advanceCheck.Start( RandomFloat( 6.0f, 9.0f ) );

	if ( sentry->GetTimeSinceLastFired() < 5.0f || !me->CanPickupBuilding( sentry ) )
		return false;

	sentry->UpdateLastKnownArea();
	CTFNavArea *nestArea = static_cast< CTFNavArea * >( sentry->GetLastKnownArea() );
	if ( !nestArea )
		return false;

	float front = TeamFrontIncursion( me, NULL );
	float gap = ( GetStyle( me ) == TFBOT_ENGINEER_BATTLE ) ? 700.0f : tf_bot_engineer_advance_gap.GetFloat();
	bool advance = front > 0.0f && front - nestArea->GetIncursionDistance( me->GetTeamNumber() ) > gap;
	if ( advance )
		TFBotFeatures::CountEvent( "eng_advance_nest" );
	return advance;
}


//-----------------------------------------------------------------------------
bool TFBotEngineer::ShouldSaveSentry( CTFBot *me, CObjectSentrygun *sentry )
{
	if ( !IsActive( me ) || !sentry )
		return false;
	if ( sentry->GetHealth() > 0.3f * sentry->GetMaxHealth() || sentry->HasSapper() )
		return false;
	// only if we're close enough to grab it before it dies
	if ( me->IsRangeGreaterThan( sentry, 250.0f ) )
		return false;
	return me->CanPickupBuilding( sentry );
}

bool TFBotEngineer::FindRetreatSpot( CTFBot *me, Vector *spot )
{
	CTFNavArea *myArea = static_cast< CTFNavArea * >( me->GetLastKnownArea() );
	if ( !myArea )
		return false;

	const int team = me->GetTeamNumber();
	const float myIncursion = myArea->GetIncursionDistance( team );

	CUtlVector< CNavArea * > nearby;
	CollectSurroundingAreas( &nearby, myArea, 1500.0f );

	CTFNavArea *best = NULL;
	float bestScore = FLT_MAX;
	FOR_EACH_VEC( nearby, i )
	{
		CTFNavArea *area = static_cast< CTFNavArea * >( nearby[i] );
		float back = myIncursion - area->GetIncursionDistance( team );
		if ( back < 400.0f || back > 1000.0f || area->GetSizeX() < 50.0f || area->GetSizeY() < 50.0f )
			continue;
		if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
			continue;
		float score = fabsf( back - 650.0f );
		if ( score < bestScore )
		{
			bestScore = score;
			best = area;
		}
	}
	if ( !best )
		return false;
	*spot = best->GetCenter();
	TFBotFeatures::CountEvent( "eng_save_sentry" );
	return true;
}


//-----------------------------------------------------------------------------
CBaseObject *TFBotEngineer::FindBuildingToHelp( CTFBot *me, CObjectSentrygun *mySentry, bool isUnderAttack )
{
	if ( !IsActive( me ) || isUnderAttack )
		return NULL;

	TFBotEngineerState_t *st = StateFor( me );
	if ( !st || ( st->m_helpCheck.HasStarted() && !st->m_helpCheck.IsElapsed() ) )
		return NULL;
	st->m_helpCheck.Start( RandomFloat( 3.0f, 5.0f ) );

	// Own nest first: only help once our sentry is done.
	if ( !mySentry || mySentry->IsBuilding() || mySentry->GetUpgradeLevel() < mySentry->GetMaxUpgradeLevel() )
		return NULL;

	const int metal = me->GetAmmoCount( TF_AMMO_METAL );
	CBaseObject *best = NULL;
	float bestDist = tf_bot_engineer_help_range.GetFloat();
	for ( int i = 0; i < IBaseObjectAutoList::AutoList().Count(); ++i )
	{
		CBaseObject *obj = static_cast< CBaseObject * >( IBaseObjectAutoList::AutoList()[i] );
		if ( !obj || obj->GetTeamNumber() != me->GetTeamNumber() || obj->GetBuilder() == me || obj->IsCarried() || obj->IsPlacing() )
			continue;

		bool needsHelp = obj->HasSapper() || obj->IsBuilding() || obj->GetHealth() < obj->GetMaxHealth() ||
						 ( obj->GetUpgradeLevel() < obj->GetMaxUpgradeLevel() && metal >= 100 );
		if ( !needsHelp )
			continue;

		float dist = me->GetRangeTo( obj );
		if ( dist < bestDist )
		{
			bestDist = dist;
			best = obj;
		}
	}
	if ( best )
		TFBotFeatures::CountEvent( "eng_help_building" );
	return best;
}


//=============================================================================
// CTFBotEngineerPickupAndMove
//=============================================================================
CTFBotEngineerPickupAndMove::CTFBotEngineerPickupAndMove( CBaseObject *building )
	: m_building( building ), m_hasSpot( false ), m_spot( vec3_origin )
{
}

CTFBotEngineerPickupAndMove::CTFBotEngineerPickupAndMove( CBaseObject *building, const Vector &placeAt )
	: m_building( building ), m_hasSpot( true ), m_spot( placeAt )
{
}

ActionResult< CTFBot > CTFBotEngineerPickupAndMove::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_giveUpTimer.Start( 8.0f );
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	return Continue();
}

ActionResult< CTFBot > CTFBotEngineerPickupAndMove::Update( CTFBot *me, float interval )
{
	if ( me->m_Shared.IsCarryingObject() )
	{
		TFBotFeatures::CountEvent( "eng_pickup" );
		if ( m_hasSpot )
			return ChangeTo( new CTFBotEngineerPlaceCarried( m_spot ), "Carrying my building back to safety" );
		return ChangeTo( new CTFBotEngineerMoveToBuild, "Carrying my building to its new spot" );
	}

	CBaseObject *building = m_building;
	if ( !building )
		return Done( "My building is gone" );
	if ( building->HasSapper() )
		return Done( "Sapped - can't pick it up" );
	if ( m_giveUpTimer.IsElapsed() )
		return Done( "Couldn't pick my building up" );

	if ( me->IsRangeGreaterThan( building, 90.0f ) )
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			CTFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, building->GetAbsOrigin(), cost );
		}
		m_path.Update( me );
		return Continue();
	}

	me->StopLookingAroundForEnemies();
	me->GetBodyInterface()->AimHeadTowards( building->WorldSpaceCenter(), IBody::CRITICAL, 0.5f, NULL, "Picking up my building" );
	if ( me->GetBodyInterface()->IsHeadAimingOnTarget() && me->CanPickupBuilding( building ) )
		me->TryToPickupBuilding();

	return Continue();
}


//=============================================================================
// CTFBotEngineerPlaceCarried
//=============================================================================
CTFBotEngineerPlaceCarried::CTFBotEngineerPlaceCarried()
	: m_hasSpot( false ), m_spot( vec3_origin ), m_needToAim( true ), m_wanderWay( 0 )
{
}

CTFBotEngineerPlaceCarried::CTFBotEngineerPlaceCarried( const Vector &placeAt )
	: m_hasSpot( true ), m_spot( placeAt ), m_needToAim( true ), m_wanderWay( 0 )
{
}

ActionResult< CTFBot > CTFBotEngineerPlaceCarried::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_giveUpTimer.Start( 12.0f );
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	return Continue();
}

ActionResult< CTFBot > CTFBotEngineerPlaceCarried::Update( CTFBot *me, float interval )
{
	if ( !me->m_Shared.IsCarryingObject() )
	{
		TFBotFeatures::CountEvent( "eng_placed" );
		return Done( "Building placed" );
	}

	if ( m_hasSpot && me->IsRangeGreaterThan( m_spot, 40.0f ) && !m_giveUpTimer.IsElapsed() )
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 1.5f ) );
			CTFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, m_spot, cost );
		}
		m_path.Update( me );
		if ( m_path.IsValid() )
			return Continue();
		m_hasSpot = false;	// can't get there: place it here
	}

	if ( m_giveUpTimer.IsElapsed() && !m_hasSpot )
	{
		// Keep trying, but from a fresh state, so we don't loop on a bad spot.
		m_giveUpTimer.Start( 12.0f );
		m_needToAim = true;
	}

	CTFWeaponBuilder *builder = dynamic_cast< CTFWeaponBuilder * >( me->GetActiveTFWeapon() );
	if ( !builder )
		return Continue();	// the carried-object builder is being deployed

	// face where enemies come from, like a fresh build
	if ( m_needToAim )
	{
		CTFNavArea *myArea = static_cast< CTFNavArea * >( me->GetLastKnownArea() );
		if ( myArea )
		{
			const CUtlVector< CTFNavArea * > &invasion = myArea->GetEnemyInvasionAreaVector( me->GetTeamNumber() );
			if ( invasion.Count() > 0 )
				me->GetBodyInterface()->AimHeadTowards( invasion[ RandomInt( 0, invasion.Count() - 1 ) ]->GetCenter(), IBody::CRITICAL, 1.0f, NULL, "Orienting my carried building" );
		}
		m_needToAim = false;
	}

	if ( me->GetBodyInterface()->IsHeadSteady() )
	{
		if ( builder->IsValidPlacement() )
		{
			me->PressFireButton();
		}
		else
		{
			if ( m_searchTimer.IsElapsed() )
			{
				m_wanderWay = RandomInt( 0, 3 );
				m_needToAim = true;
				m_searchTimer.Start( RandomFloat( 0.1f, 0.25f ) );
			}
			switch ( m_wanderWay )
			{
			case 0: me->PressForwardButton();	break;
			case 1: me->PressBackwardButton();	break;
			case 2: me->PressRightButton();		break;
			case 3: me->PressLeftButton();		break;
			}
		}
	}

	return Continue();
}


//=============================================================================
// CTFBotEngineerHelpBuilding
//=============================================================================
CTFBotEngineerHelpBuilding::CTFBotEngineerHelpBuilding( CBaseObject *building )
	: m_building( building )
{
}

ActionResult< CTFBot > CTFBotEngineerHelpBuilding::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_giveUpTimer.Start( 15.0f );
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	return Continue();
}

ActionResult< CTFBot > CTFBotEngineerHelpBuilding::Update( CTFBot *me, float interval )
{
	CBaseObject *building = m_building;
	if ( !building || building->IsCarried() )
		return Done( "Building is gone" );
	if ( m_giveUpTimer.IsElapsed() )
		return Done( "Spent long enough helping" );
	if ( me->GetTimeSinceLastInjury() < 1.0f )
		return Done( "Under attack" );

	bool needsHelp = building->HasSapper() || building->IsBuilding() || building->GetHealth() < building->GetMaxHealth() ||
					 ( building->GetUpgradeLevel() < building->GetMaxUpgradeLevel() && me->GetAmmoCount( TF_AMMO_METAL ) > 0 );
	if ( !needsHelp )
		return Done( "Building is in good shape" );

	CBaseCombatWeapon *wrench = me->Weapon_GetSlot( TF_WPN_TYPE_MELEE );
	if ( wrench )
		me->Weapon_Switch( wrench );

	if ( me->IsRangeGreaterThan( building, 70.0f ) )
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			CTFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, building->GetAbsOrigin(), cost );
		}
		m_path.Update( me );
		return Continue();
	}

	me->StopLookingAroundForEnemies();
	me->GetBodyInterface()->AimHeadTowards( building->WorldSpaceCenter(), IBody::CRITICAL, 1.0f, NULL, "Helping a teammate's building" );
	me->PressFireButton();
	return Continue();
}
