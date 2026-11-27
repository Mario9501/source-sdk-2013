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
#include "team_train_watcher.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "nav_mesh/tf_nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_engineer_forward_offset( "tf_bot_engineer_forward_offset", "200", FCVAR_CHEAT,
	"Forward-style engineers nest at least this far (incursion distance) behind the team's main group (M3b)" );
ConVar tf_bot_engineer_advance_gap( "tf_bot_engineer_advance_gap", "600", FCVAR_CHEAT,
	"Payload: forward-style engineers move the nest once the cart is this far past it (M3b)" );
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
// Where the team's main group is: the incursion distance (from our spawn) of
// the teammate a third of the way back from the most advanced one, so one
// scout deep in enemy territory doesn't drag the nest forward. Spies, snipers
// and engineers don't count. -1 if fewer than two teammates are out.
//-----------------------------------------------------------------------------
struct TFBotFrontMate_t
{
	float m_incursion;
	Vector m_pos;
};

static int CompareFrontMates( const TFBotFrontMate_t *a, const TFBotFrontMate_t *b )
{
	return ( a->m_incursion > b->m_incursion ) ? -1 : ( a->m_incursion < b->m_incursion ) ? 1 : 0;
}

static float TeamFrontIncursion( CTFBot *me, Vector *frontPos )
{
	int team = me->GetTeamNumber();
	CUtlVector< TFBotFrontMate_t > mates;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *mate = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !mate || mate == me || !mate->IsAlive() || mate->GetTeamNumber() != team )
			continue;
		if ( mate->IsPlayerClass( TF_CLASS_SPY ) || mate->IsPlayerClass( TF_CLASS_SNIPER ) || mate->IsPlayerClass( TF_CLASS_ENGINEER ) )
			continue;
		CTFNavArea *area = static_cast< CTFNavArea * >( mate->GetLastKnownArea() );
		if ( !area || area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
			continue;
		float incursion = area->GetIncursionDistance( team );
		if ( incursion < 0.0f )
			continue;
		TFBotFrontMate_t entry = { incursion, mate->GetAbsOrigin() };
		mates.AddToTail( entry );
	}
	if ( mates.Count() < 2 )
		return -1.0f;

	mates.Sort( CompareFrontMates );
	const TFBotFrontMate_t &front = mates[ mates.Count() / 3 ];
	if ( frontPos )
		*frontPos = front.m_pos;
	return front.m_incursion;
}


//-----------------------------------------------------------------------------
static int s_sortTeam = TF_TEAM_RED;

static int CompareAreasMostAdvanced( CTFNavArea * const *a, CTFNavArea * const *b )
{
	float ia = (*a)->GetIncursionDistance( s_sortTeam );
	float ib = (*b)->GetIncursionDistance( s_sortTeam );
	return ( ia > ib ) ? -1 : ( ia < ib ) ? 1 : 0;
}

static bool PickWeightedBySize( const CUtlVector< CTFNavArea * > &areas, int count, Vector *location )
{
	float totalWeight = 0.0f;
	for ( int i = 0; i < count; ++i )
		totalWeight += areas[i]->GetSizeX() * areas[i]->GetSizeY();
	if ( count <= 0 || totalWeight <= 0.0f )
		return false;

	float pick = RandomFloat( 0.0f, totalWeight );
	for ( int i = 0; i < count; ++i )
	{
		pick -= areas[i]->GetSizeX() * areas[i]->GetSizeY();
		if ( pick <= 0.0f )
		{
			*location = areas[i]->GetRandomPoint();
			return true;
		}
	}
	*location = areas[ count - 1 ]->GetRandomPoint();
	return true;
}


//-----------------------------------------------------------------------------
bool TFBotEngineer::PickNestLocation( CTFBot *me, const CUtlVector< CTFNavArea * > &objectiveAreas, Vector *location )
{
	if ( !IsActive( me ) || !NestsForward( me ) )
		return false;

	Vector frontPos;
	float front = TeamFrontIncursion( me, &frontPos );
	if ( front < 0.0f )
		return false;		// the team isn't out yet: stock choice

	const int team = me->GetTeamNumber();

	if ( GetStyle( me ) == TFBOT_ENGINEER_BATTLE )
	{
		// Battle engineers drop minis at the fight, just behind the main group.
		const float target = front - 150.0f;
		const float window = 250.0f;
		CUtlVector< CTFNavArea * > candidates;
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
		}
		if ( !PickWeightedBySize( candidates, candidates.Count(), location ) )
			return false;
		TFBotFeatures::CountEvent( "eng_battle_nest" );
		return true;
	}

	// Forward engineers still cover the objective: of the areas the stock
	// logic found (they see the point with a clear line of fire), take the
	// most advanced half of those behind the team's main group.
	CUtlVector< CTFNavArea * > behind;
	const float limit = front - tf_bot_engineer_forward_offset.GetFloat();
	FOR_EACH_VEC( objectiveAreas, i )
	{
		float incursion = objectiveAreas[i]->GetIncursionDistance( team );
		if ( incursion >= 0.0f && incursion <= limit )
			behind.AddToTail( objectiveAreas[i] );
	}
	if ( behind.Count() == 0 )
		return false;		// the team hasn't reached the objective: stock choice

	s_sortTeam = team;
	behind.Sort( CompareAreasMostAdvanced );
	if ( !PickWeightedBySize( behind, MAX( 1, ( behind.Count() + 1 ) / 2 ), location ) )
		return false;
	TFBotFeatures::CountEvent( "eng_forward_nest" );
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

	const int team = me->GetTeamNumber();
	bool advance = false;
	if ( GetStyle( me ) == TFBOT_ENGINEER_BATTLE )
	{
		// Minis follow the fight.
		sentry->UpdateLastKnownArea();
		CTFNavArea *nestArea = static_cast< CTFNavArea * >( sentry->GetLastKnownArea() );
		float front = TeamFrontIncursion( me, NULL );
		advance = nestArea && front > 0.0f && front - nestArea->GetIncursionDistance( team ) > 700.0f;
	}
	else if ( TFGameRules()->GetGameType() == TF_GAMETYPE_ESCORT )
	{
		// Forward nests keep up with the cart; stock waits until it is a full
		// sentry range past. Elsewhere the stock check moves the nest when the
		// objective moves.
		CTeamTrainWatcher *train = ( team == TF_TEAM_BLUE ) ? TFGameRules()->GetPayloadToPush( team ) : TFGameRules()->GetPayloadToBlock( team );
		if ( train )
		{
			float sentryAlong = 0.0f;
			train->ProjectPointOntoPath( sentry->GetAbsOrigin(), NULL, &sentryAlong );
			advance = train->GetTrainDistanceAlongTrack() > sentryAlong + tf_bot_engineer_advance_gap.GetFloat();
		}
	}

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
