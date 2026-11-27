//========= TFBot overhaul: team blackboard, voice, perception (M3) ===========//
//
// See tf_bot_blackboard.h and docs/tfbot/DESIGN.md M3.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_blackboard.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/profile/tf_bot_profile.h"
#include "bot/clone/tf_bot_clone.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_obj_sentrygun.h"
#include "tf_weapon_medigun.h"
#include "team_train_watcher.h"
#include "teamplay_round_timer.h"
#include "GameEventListener.h"
#include "NextBot/NextBotKnownEntity.h"
#include "nav_mesh/tf_nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_bb_regroup_time( "tf_bot_bb_regroup_time", "15", FCVAR_CHEAT, "Seconds a team holds its push after being wiped (M3)" );
ConVar tf_bot_bb_uber_wait_time( "tf_bot_bb_uber_wait_time", "10", FCVAR_CHEAT, "Seconds a team holds its push while an uber is nearly ready (M3)" );
ConVar tf_bot_bb_hold_lead( "tf_bot_bb_hold_lead", "600", FCVAR_CHEAT, "While holding, bots this much closer to the objective than the team average wait (M3)" );
ConVar tf_bot_bb_callout_range( "tf_bot_bb_callout_range", "1500", FCVAR_CHEAT, "How far a callout carries to teammates (M3)" );
ConVar tf_bot_bb_debug( "tf_bot_bb_debug", "0", FCVAR_CHEAT, "Print TFBot blackboard events" );

static const float kUpdateInterval = 0.25f;
static const int kMaxSentries = 8;
static const int kMaxSpies = 8;
static const int kDeathLog = 32;

enum { BB_RED = 0, BB_BLU, BB_TEAMS };

static int Slot( int team )
{
	return team == TF_TEAM_RED ? BB_RED : ( team == TF_TEAM_BLUE ? BB_BLU : -1 );
}

static int TeamOfSlot( int slot )
{
	return slot == BB_RED ? TF_TEAM_RED : TF_TEAM_BLUE;
}

struct TFBotBBSentry_t
{
	CHandle< CObjectSentrygun > m_sentry;
	float m_lastSeen;
};

struct TFBotBBSpy_t
{
	CHandle< CTFPlayer > m_spy;
	float m_time;
};

struct TFBotBBTeam_t
{
	TFBotBBSentry_t m_sentries[ kMaxSentries ];
	TFBotBBSpy_t m_spies[ kMaxSpies ];
	float m_deaths[ kDeathLog ];
	int m_deathHead;
	float m_holdUntil;
	int m_holdReason;				// 0 none, 1 regroup, 2 uber
	bool m_uberWaitArmed;
	EHANDLE m_focus;
	float m_focusTime;
	float m_lastTeamCallout;
	bool m_medicReadyCalled;
};

struct TFBotBBBot_t
{
	float m_lastCallout;
	float m_lastMedicCall;
	float m_nextPerception;
};

static TFBotBBTeam_t s_team[ BB_TEAMS ];
static TFBotBBBot_t s_bot[ MAX_PLAYERS + 1 ];


//-----------------------------------------------------------------------------
bool TFBotBlackboard::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_BLACKBOARD );
}

bool TFBotBlackboard::MedicAnswersBotCalls( const CTFBot *medic )
{
	return IsActive( medic );
}

static bool HoldActive( int slot )
{
	return slot >= 0 && gpGlobals->curtime < s_team[ slot ].m_holdUntil;
}

static bool TimeIsShort()
{
	if ( !TFGameRules() || TFGameRules()->InOvertime() || TFGameRules()->InSetup() )
		return true;
	CTeamRoundTimer *timer = TFGameRules()->GetActiveRoundTimer();
	return timer && timer->GetTimeRemaining() < 90.0f;
}

// A team that has something to push toward.
static bool IsAttacking( int team )
{
	if ( !TFGameRules() )
		return false;
	if ( TFGameRules()->GetGameType() == TF_GAMETYPE_ESCORT )
		return TFGameRules()->GetPayloadToPush( team ) != NULL;
	return !( TFGameRules()->IsAttackDefenseMode() && team == TF_TEAM_RED );
}


//-----------------------------------------------------------------------------
// Voice
//-----------------------------------------------------------------------------
static bool Speak( CTFBot *bot, int concept, bool important )
{
	if ( !TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_VOICE ) || !bot->IsAlive() )
		return false;

	int index = bot->entindex();
	int slot = Slot( bot->GetTeamNumber() );
	if ( index < 1 || index > MAX_PLAYERS || slot < 0 )
		return false;

	const float now = gpGlobals->curtime;
	if ( now - s_bot[ index ].m_lastCallout < 5.0f || now - s_team[ slot ].m_lastTeamCallout < 1.5f )
		return false;

	// Quiet bots skip routine callouts.
	float rate = TFBotProfiles::GetEffective( bot ).Get( TFBOT_PROFILE_CALLOUT_RATE );
	if ( !important && RandomFloat() > rate )
		return false;

	if ( !bot->SpeakConceptIfAllowed( concept ) )
		return false;

	s_bot[ index ].m_lastCallout = now;
	s_team[ slot ].m_lastTeamCallout = now;
	TFBotFeatures::CountEvent( "voice_callout" );
	return true;
}


//-----------------------------------------------------------------------------
// Sentries and spies
//-----------------------------------------------------------------------------
static bool PostSentry( int slot, CObjectSentrygun *sentry )
{
	TFBotBBTeam_t &team = s_team[ slot ];
	int freeIndex = -1;
	for ( int i = 0; i < kMaxSentries; ++i )
	{
		if ( team.m_sentries[i].m_sentry == sentry )
		{
			team.m_sentries[i].m_lastSeen = gpGlobals->curtime;
			return false;
		}
		if ( freeIndex < 0 && team.m_sentries[i].m_sentry == NULL )
			freeIndex = i;
	}
	if ( freeIndex < 0 )
		return false;
	team.m_sentries[ freeIndex ].m_sentry = sentry;
	team.m_sentries[ freeIndex ].m_lastSeen = gpGlobals->curtime;
	TFBotFeatures::CountEvent( "bb_sentry_learned" );
	if ( tf_bot_bb_debug.GetBool() )
		DevMsg( "TFBot BB: team %d learned of sentry %d\n", TeamOfSlot( slot ), sentry->entindex() );
	return true;
}

static bool SpyRecentlyPosted( int slot, CTFPlayer *spy )
{
	for ( int i = 0; i < kMaxSpies; ++i )
	{
		const TFBotBBSpy_t &e = s_team[ slot ].m_spies[i];
		if ( e.m_spy == spy && gpGlobals->curtime - e.m_time < 4.0f )
			return true;
	}
	return false;
}

static void PostSpy( int slot, CTFPlayer *spy )
{
	TFBotBBTeam_t &team = s_team[ slot ];
	int oldest = 0;
	for ( int i = 0; i < kMaxSpies; ++i )
	{
		if ( team.m_spies[i].m_spy == spy || team.m_spies[i].m_spy == NULL )
		{
			oldest = i;
			break;
		}
		if ( team.m_spies[i].m_time < team.m_spies[ oldest ].m_time )
			oldest = i;
	}
	team.m_spies[ oldest ].m_spy = spy;
	team.m_spies[ oldest ].m_time = gpGlobals->curtime;
}

// Teammate bots within earshot learn the spy (they heard "Spy!").
static void ShareSpy( int team, CTFPlayer *spy, const Vector &where, CTFBot *except )
{
	float range = tf_bot_bb_callout_range.GetFloat();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFBot *mate = ToTFBot( UTIL_PlayerByIndex( i ) );
		if ( !mate || mate == except || !mate->IsAlive() || mate->GetTeamNumber() != team )
			continue;
		if ( !TFBotBlackboard::IsActive( mate ) || mate->IsKnownSpy( spy ) )
			continue;
		if ( mate->IsRangeGreaterThan( where, range ) )
			continue;
		mate->RealizeSpy( spy );
		TFBotFeatures::CountEvent( "bb_spy_shared" );
	}
}

void TFBotBlackboard::OnBotRealizedSpy( CTFBot *bot, CTFPlayer *spy )
{
	if ( !IsActive( bot ) || !spy )
		return;
	int slot = Slot( bot->GetTeamNumber() );
	if ( slot < 0 || SpyRecentlyPosted( slot, spy ) )
		return;

	PostSpy( slot, spy );
	Speak( bot, MP_CONCEPT_PLAYER_CLOAKEDSPY, true );
	ShareSpy( bot->GetTeamNumber(), spy, bot->GetAbsOrigin(), bot );
}


//-----------------------------------------------------------------------------
// Human (and bot) voice commands
//-----------------------------------------------------------------------------
void TFBotBlackboard::OnPlayerSpoke( CTFPlayer *player, int concept )
{
	if ( !player || !player->IsAlive() )
		return;

	TFBotClone::OnPlayerSpoke( player, concept );	// M4b playstyle recording shares this hook
	int team = player->GetTeamNumber();
	int slot = Slot( team );
	if ( slot < 0 )
		return;

	// Bots post their own knowledge directly; only humans' calls need reading.
	if ( player->IsBot() )
		return;
	TFBotFeatures::CountEvent( "bb_human_voice" );

	Vector forward;
	player->EyeVectors( &forward );
	const Vector eye = player->EyePosition();

	switch ( concept )
	{
	case MP_CONCEPT_PLAYER_GO:
	case MP_CONCEPT_PLAYER_MOVEUP:
		s_team[ slot ].m_holdUntil = 0.0f;
		break;

	case MP_CONCEPT_PLAYER_CLOAKEDSPY:
		// The human saw a spy near them: nearby bots check the players around
		// the caller, which unmasks enemy spies standing close by.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFPlayer *spy = ToTFPlayer( UTIL_PlayerByIndex( i ) );
			if ( spy && spy->IsAlive() && spy->GetTeamNumber() != team && spy->IsPlayerClass( TF_CLASS_SPY ) &&
				 ( spy->GetAbsOrigin() - player->GetAbsOrigin() ).IsLengthLessThan( 500.0f ) )
			{
				PostSpy( slot, spy );
				ShareSpy( team, spy, player->GetAbsOrigin(), NULL );
			}
		}
		break;

	case MP_CONCEPT_PLAYER_SENTRYAHEAD:
		// Sentries in front of the caller that the caller can see.
		for ( int i = 0; i < IBaseObjectAutoList::AutoList().Count(); ++i )
		{
			CBaseObject *obj = static_cast< CBaseObject * >( IBaseObjectAutoList::AutoList()[i] );
			if ( !obj || obj->GetType() != OBJ_SENTRYGUN || obj->GetTeamNumber() == team )
				continue;
			Vector to = obj->WorldSpaceCenter() - eye;
			float dist = to.NormalizeInPlace();
			if ( dist > 2500.0f || DotProduct( to, forward ) < 0.5f )
				continue;
			trace_t tr;
			UTIL_TraceLine( eye, obj->WorldSpaceCenter(), MASK_BLOCKLOS, player, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 1.0f || tr.m_pEnt == obj )
				PostSentry( slot, static_cast< CObjectSentrygun * >( obj ) );
		}
		break;

	case MP_CONCEPT_PLAYER_INCOMING:
	{
		// Nearby bots look where the caller is looking.
		Vector spot = eye + forward * 800.0f;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFBot *mate = ToTFBot( UTIL_PlayerByIndex( i ) );
			if ( mate && mate->IsAlive() && mate->GetTeamNumber() == team && IsActive( mate ) &&
				 !mate->IsRangeGreaterThan( player, tf_bot_bb_callout_range.GetFloat() ) )
			{
				mate->GetBodyInterface()->AimHeadTowards( spot, IBody::INTERESTING, 1.5f, NULL, "Teammate called incoming" );
			}
		}
		break;
	}

	default:
		break;
	}
}


//-----------------------------------------------------------------------------
// Holding the push
//-----------------------------------------------------------------------------
bool TFBotBlackboard::ShouldHoldAdvance( CTFBot *me, const Vector &objective )
{
	if ( !IsActive( me ) )
		return false;
	int team = me->GetTeamNumber();
	int slot = Slot( team );
	if ( !HoldActive( slot ) || TimeIsShort() )
		return false;

	float myDist = ( me->GetAbsOrigin() - objective ).Length();
	if ( myDist < 300.0f )
		return false;	// already on the objective: keep it (cart keeps moving)

	float sum = 0.0f;
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *mate = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( mate && mate->IsAlive() && mate->GetTeamNumber() == team )
		{
			sum += ( mate->GetAbsOrigin() - objective ).Length();
			++n;
		}
	}
	if ( n < 2 )
		return false;

	return myDist < sum / n - tf_bot_bb_hold_lead.GetFloat();
}


//-----------------------------------------------------------------------------
// Focus fire
//-----------------------------------------------------------------------------
const CKnownEntity *TFBotBlackboard::PreferFocusTarget( CTFBot *me, const CKnownEntity *threat1, const CKnownEntity *threat2, const CKnownEntity *chosen )
{
	if ( !IsActive( me ) || !threat1 || !threat2 )
		return chosen;
	int slot = Slot( me->GetTeamNumber() );
	if ( slot < 0 )
		return chosen;

	const TFBotBBTeam_t &team = s_team[ slot ];
	CBaseEntity *focus = team.m_focus.Get();
	if ( !focus || gpGlobals->curtime - team.m_focusTime > 1.5f )
		return chosen;

	const CKnownEntity *focusThreat = ( threat1->GetEntity() == focus ) ? threat1 : ( threat2->GetEntity() == focus ? threat2 : NULL );
	if ( !focusThreat || !focusThreat->IsVisibleRecently() )
		return chosen;

	// Don't ignore someone in our face for a distant focus target.
	const CKnownEntity *other = ( focusThreat == threat1 ) ? threat2 : threat1;
	if ( other->GetEntity() && me->IsRangeLessThan( other->GetEntity(), 350.0f ) && me->IsRangeGreaterThan( focus, 1200.0f ) )
		return chosen;

	return focusThreat;
}


//-----------------------------------------------------------------------------
// Periodic update: team knowledge, push state, perception, routine callouts.
//-----------------------------------------------------------------------------
class CTFBotBlackboardSystem : public CAutoGameSystemPerFrame, public CGameEventListener
{
public:
	CTFBotBlackboardSystem() : CAutoGameSystemPerFrame( "CTFBotBlackboardSystem" ), m_listening( false ), m_nextUpdate( 0.0f ) {}

	virtual void LevelInitPostEntity() OVERRIDE
	{
		if ( !m_listening )
		{
			ListenForGameEvent( "player_death" );
			ListenForGameEvent( "teamplay_round_start" );
			m_listening = true;
		}
		Reset();
	}

	virtual void Shutdown() OVERRIDE
	{
		StopListeningForAllEvents();
		m_listening = false;
	}

	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		if ( FStrEq( event->GetName(), "teamplay_round_start" ) )
		{
			Reset();
			return;
		}

		// player_death: feeds the wipe detector
		if ( event->GetInt( "death_flags" ) & TF_DEATH_FEIGN_DEATH )
			return;
		CTFPlayer *victim = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		int slot = victim ? Slot( victim->GetTeamNumber() ) : -1;
		if ( slot < 0 )
			return;
		TFBotBBTeam_t &team = s_team[ slot ];
		team.m_deaths[ team.m_deathHead ] = gpGlobals->curtime;
		team.m_deathHead = ( team.m_deathHead + 1 ) % kDeathLog;
	}

	virtual void FrameUpdatePostEntityThink() OVERRIDE
	{
		if ( gpGlobals->curtime < m_nextUpdate )
			return;
		m_nextUpdate = gpGlobals->curtime + kUpdateInterval;

		if ( !TFGameRules() || TFGameRules()->IsMannVsMachineMode() )
			return;

		for ( int slot = 0; slot < BB_TEAMS; ++slot )
			UpdateTeam( slot );
	}

private:
	void Reset()
	{
		V_memset( s_team, 0, sizeof( s_team ) );
		for ( int s = 0; s < BB_TEAMS; ++s )
			s_team[s].m_uberWaitArmed = true;
		V_memset( s_bot, 0, sizeof( s_bot ) );
	}

	void UpdateTeam( int slot )
	{
		const int team = TeamOfSlot( slot );
		TFBotBBTeam_t &ts = s_team[ slot ];
		const float now = gpGlobals->curtime;

		int teamSize = 0;
		bool anyActive = false;
		float maxCharge = 0.0f;
		bool releasing = false;

		// votes for the focus target
		EHANDLE voteEnt[ MAX_PLAYERS ];
		int voteCount[ MAX_PLAYERS ];
		int numVotes = 0;

		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || player->GetTeamNumber() != team )
				continue;
			++teamSize;
			if ( !player->IsAlive() )
				continue;

			if ( player->IsPlayerClass( TF_CLASS_MEDIC ) )
			{
				CWeaponMedigun *medigun = dynamic_cast< CWeaponMedigun * >( player->Weapon_OwnsThisID( TF_WEAPON_MEDIGUN ) );
				if ( medigun )
				{
					maxCharge = MAX( maxCharge, medigun->GetChargeLevel() );
					releasing = releasing || medigun->IsReleasingCharge();
				}
			}

			CTFBot *bot = ToTFBot( player );
			if ( !bot )
				continue;

			bool active = TFBotBlackboard::IsActive( bot );
			anyActive = anyActive || active;
			if ( active )
				UpdateBotKnowledge( bot, slot, voteEnt, voteCount, &numVotes );
			if ( TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_PERCEPTION ) )
				UpdatePerception( bot );
			if ( TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_VOICE ) )
				UpdateRoutineCallouts( bot, slot, maxCharge );
		}

		// expire
		for ( int i = 0; i < kMaxSentries; ++i )
		{
			CObjectSentrygun *sentry = ts.m_sentries[i].m_sentry;
			if ( sentry && ( !sentry->IsAlive() || now - ts.m_sentries[i].m_lastSeen > 60.0f ) )
				ts.m_sentries[i].m_sentry = NULL;
		}

		// focus target: the enemy most teammates are shooting at (2+)
		int best = -1;
		for ( int v = 0; v < numVotes; ++v )
		{
			if ( voteCount[v] >= 2 && ( best < 0 || voteCount[v] > voteCount[ best ] ) )
				best = v;
		}
		if ( best >= 0 )
		{
			ts.m_focus = voteEnt[ best ];
			ts.m_focusTime = now;
		}

		if ( !anyActive )
			return;

		ShareSentries( slot );
		UpdateHold( slot, teamSize, maxCharge, releasing );
	}

	void UpdateBotKnowledge( CTFBot *bot, int slot, EHANDLE *voteEnt, int *voteCount, int *numVotes )
	{
		CUtlVector< CKnownEntity > known;
		bot->GetVisionInterface()->CollectKnownEntities( &known );
		FOR_EACH_VEC( known, k )
		{
			CBaseEntity *ent = known[k].GetEntity();
			if ( !ent || !known[k].IsVisibleRecently() || ent->GetTeamNumber() == bot->GetTeamNumber() )
				continue;
			if ( ent->IsBaseObject() && static_cast< CBaseObject * >( ent )->GetType() == OBJ_SENTRYGUN )
			{
				if ( PostSentry( slot, static_cast< CObjectSentrygun * >( ent ) ) )
					Speak( bot, MP_CONCEPT_PLAYER_SENTRYAHEAD, true );
			}
		}

		const CKnownEntity *threat = bot->GetVisionInterface()->GetPrimaryKnownThreat( true );
		CBaseEntity *target = threat ? threat->GetEntity() : NULL;
		if ( target && target->IsPlayer() )
		{
			int v = 0;
			for ( ; v < *numVotes; ++v )
			{
				if ( voteEnt[v] == target )
					break;
			}
			if ( v == *numVotes && v < MAX_PLAYERS )
			{
				voteEnt[v] = target;
				voteCount[v] = 0;
				++*numVotes;
			}
			if ( v < MAX_PLAYERS )
				++voteCount[v];
		}
	}

	// Teammate bots learn sentries the team knows about.
	void ShareSentries( int slot )
	{
		const int team = TeamOfSlot( slot );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
			if ( !bot || !bot->IsAlive() || bot->GetTeamNumber() != team || !TFBotBlackboard::IsActive( bot ) || bot->GetEnemySentry() )
				continue;

			CObjectSentrygun *nearest = NULL;
			float nearestDist = 3000.0f;
			for ( int s = 0; s < kMaxSentries; ++s )
			{
				CObjectSentrygun *sentry = s_team[ slot ].m_sentries[s].m_sentry;
				if ( !sentry )
					continue;
				float dist = bot->GetRangeTo( sentry );
				if ( dist < nearestDist )
				{
					nearestDist = dist;
					nearest = sentry;
				}
			}
			if ( nearest )
			{
				bot->RememberEnemySentry( nearest, nearest->GetAbsOrigin() );
				TFBotFeatures::CountEvent( "bb_sentry_shared" );
			}
		}
	}

	void UpdateHold( int slot, int teamSize, float maxCharge, bool releasing )
	{
		TFBotBBTeam_t &ts = s_team[ slot ];
		const float now = gpGlobals->curtime;
		const int team = TeamOfSlot( slot );
		bool wasHolding = HoldActive( slot );

		if ( maxCharge < 0.5f )
			ts.m_uberWaitArmed = true;

		if ( releasing && wasHolding )
		{
			ts.m_holdUntil = 0.0f;	// uber is out: push now
		}
		else if ( IsAttacking( team ) && !TimeIsShort() )
		{
			// wiped: half the team (at least 3) died in the last 12 s
			int recent = 0;
			for ( int i = 0; i < kDeathLog; ++i )
			{
				if ( ts.m_deaths[i] > 0.0f && now - ts.m_deaths[i] < 12.0f )
					++recent;
			}
			if ( recent >= MAX( 3, teamSize / 2 ) && !wasHolding )
			{
				ts.m_holdUntil = now + tf_bot_bb_regroup_time.GetFloat();
				ts.m_holdReason = 1;
				TFBotFeatures::CountEvent( "bb_hold_regroup" );
				V_memset( ts.m_deaths, 0, sizeof( ts.m_deaths ) );
			}
			else if ( maxCharge >= 0.85f && ts.m_uberWaitArmed && !wasHolding )
			{
				ts.m_holdUntil = now + tf_bot_bb_uber_wait_time.GetFloat();
				ts.m_holdReason = 2;
				TFBotFeatures::CountEvent( "bb_hold_uber" );
				ts.m_uberWaitArmed = false;
			}
		}

		bool holding = HoldActive( slot );
		if ( holding != wasHolding && tf_bot_bb_debug.GetBool() )
			DevMsg( "TFBot BB: team %d %s holding (%s)\n", team, holding ? "starts" : "stops", ts.m_holdReason == 2 ? "uber" : "regroup" );

		if ( wasHolding && !holding )
		{
			// Someone near the front calls the push.
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
				if ( bot && bot->IsAlive() && bot->GetTeamNumber() == team && Speak( bot, MP_CONCEPT_PLAYER_GO, true ) )
					break;
			}
		}
	}

	void UpdatePerception( CTFBot *bot )
	{
		int index = bot->entindex();
		if ( index < 1 || index > MAX_PLAYERS )
			return;
		TFBotBBBot_t &st = s_bot[ index ];
		const float now = gpGlobals->curtime;
		if ( now < st.m_nextPerception )
			return;
		st.m_nextPerception = now + RandomFloat( 0.4f, 0.6f );

		if ( !bot->IsAlive() )
			return;
		// Busy with a threat it already knows about, seen or not: the stock code
		// is watching it (or where it was last seen).
		const CKnownEntity *threat = bot->GetVisionInterface()->GetPrimaryKnownThreat( false );
		if ( threat )
			return;

		const float awareness = TFBotProfiles::GetEffective( bot ).Get( TFBOT_PROFILE_AWARENESS );
		const float hearRange = Lerp( awareness, 500.0f, 1100.0f );

		// Hearing: running enemies close by that we can't see.
		CTFPlayer *heard = NULL;
		float heardDist = hearRange;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFPlayer *enemy = ToTFPlayer( UTIL_PlayerByIndex( i ) );
			if ( !enemy || !enemy->IsAlive() || enemy->GetTeamNumber() == bot->GetTeamNumber() || enemy->GetTeamNumber() < FIRST_GAME_TEAM )
				continue;
			if ( enemy->GetAbsVelocity().Length2D() < 150.0f )
				continue;	// walking slowly or standing: no footsteps worth hearing
			float range = hearRange * ( enemy->m_Shared.IsStealthed() ? 0.5f : 1.0f );
			float dist = bot->GetRangeTo( enemy );
			if ( dist > range || dist > heardDist )
				continue;
			if ( bot->GetVisionInterface()->IsAbleToSee( enemy, IVision::USE_FOV ) )
				continue;
			// Only someone in the open outside our view (behind us). Turning
			// toward footsteps behind a wall just stares at the wall; watching
			// the approaches (pre-aim / stock glances) covers that case.
			if ( !bot->GetVisionInterface()->IsLineOfSightClear( enemy->WorldSpaceCenter() ) )
				continue;
			heard = enemy;
			heardDist = dist;
		}

		if ( heard )
		{
			// Only roughly where the sound came from.
			float spread = 0.25f * heardDist;
			Vector spot = heard->WorldSpaceCenter() + Vector( RandomFloat( -spread, spread ), RandomFloat( -spread, spread ), 0.0f );
			bot->GetBodyInterface()->AimHeadTowards( spot, IBody::INTERESTING, 0.8f, NULL, "Heard footsteps" );
			TFBotFeatures::CountEvent( "perception_heard" );
			return;
		}

		// Now and then, check an enemy approach we aren't watching (outside the
		// view cone, in the open). A random look over the shoulder mostly hit
		// walls and cost forward vision: the perception-only A/B lost.
		if ( RandomFloat() < 0.12f * awareness )
		{
			CTFNavArea *myArea = bot->GetLastKnownArea();
			if ( myArea )
			{
				const CUtlVector< CTFNavArea * > &areas = myArea->GetEnemyInvasionAreaVector( bot->GetTeamNumber() );
				Vector view = bot->GetBodyInterface()->GetViewVector();
				for ( int tries = 0; tries < 6 && areas.Count() > 0; ++tries )
				{
					Vector spot = areas[ RandomInt( 0, areas.Count() - 1 ) ]->GetRandomPoint() + Vector( 0, 0, HumanEyeHeight );
					Vector to = spot - bot->EyePosition();
					if ( to.NormalizeInPlace() < 150.0f || DotProduct( to, view ) > 0.5f )
						continue;	// too close, or already in view
					if ( !bot->GetVisionInterface()->IsLineOfSightClear( spot ) )
						continue;
					bot->GetBodyInterface()->AimHeadTowards( spot, IBody::INTERESTING, 0.6f, NULL, "Checking an approach" );
					TFBotFeatures::CountEvent( "perception_glance" );
					break;
				}
			}
		}
	}

	void UpdateRoutineCallouts( CTFBot *bot, int slot, float teamMaxCharge )
	{
		int index = bot->entindex();
		if ( index < 1 || index > MAX_PLAYERS || !bot->IsAlive() )
			return;
		const float now = gpGlobals->curtime;

		// "MEDIC!" when hurt and the team has a medic nearby.
		float health = (float)bot->GetHealth() / (float)bot->GetMaxHealth();
		if ( health < 0.5f && now - s_bot[ index ].m_lastMedicCall > 10.0f && !bot->IsPlayerClass( TF_CLASS_MEDIC ) )
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTFPlayer *medic = ToTFPlayer( UTIL_PlayerByIndex( i ) );
				if ( medic && medic != bot && medic->IsAlive() && medic->GetTeamNumber() == bot->GetTeamNumber() &&
					 medic->IsPlayerClass( TF_CLASS_MEDIC ) && !bot->IsRangeGreaterThan( medic, 1500.0f ) )
				{
					if ( Speak( bot, MP_CONCEPT_PLAYER_MEDIC, false ) )
						s_bot[ index ].m_lastMedicCall = now;
					break;
				}
			}
		}

		// Medic: "Ubercharge ready!" once per charge.
		if ( bot->IsPlayerClass( TF_CLASS_MEDIC ) )
		{
			CWeaponMedigun *medigun = dynamic_cast< CWeaponMedigun * >( bot->Weapon_OwnsThisID( TF_WEAPON_MEDIGUN ) );
			if ( medigun && medigun->GetChargeLevel() >= 1.0f && !s_team[ slot ].m_medicReadyCalled )
			{
				if ( Speak( bot, MP_CONCEPT_PLAYER_CHARGEREADY, true ) )
					s_team[ slot ].m_medicReadyCalled = true;
			}
			else if ( medigun && medigun->GetChargeLevel() < 0.5f )
			{
				s_team[ slot ].m_medicReadyCalled = false;
			}
		}
	}

	bool m_listening;
	float m_nextUpdate;
};

static CTFBotBlackboardSystem s_tfBotBlackboardSystem;
