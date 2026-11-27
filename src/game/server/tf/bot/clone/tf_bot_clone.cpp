//========= TFBot overhaul: playstyle cloning (M4b) ===========================//
//
// See tf_bot_clone.h and docs/tfbot/DESIGN.md M4b.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_clone.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_weaponbase_gun.h"
#include "nav_mesh/tf_nav_area.h"
#include "tf_bot_profile_schema.h"
#include "GameEventListener.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TFBOT_RECORDS_FILE "cfg/tf_bot_records.txt"

// Accumulated stats for one recorded player (persisted).
struct TFBotRecord_t
{
	char m_name[ 64 ];
	float m_aliveSeconds;
	float m_classSeconds[ TF_CLASS_COUNT_ALL ];
	float m_frontnessSum;		// incursion distance minus the team average, summed per second
	int m_frontnessCount;
	float m_aloneSeconds;		// > 800 units from the nearest teammate
	float m_engageDistSum;
	int m_engageCount;
	float m_reactionSum;
	int m_reactionCount;
	int m_hitscanShots;
	int m_hitscanHits;
	float m_retreatHealthSum;
	int m_retreatCount;
	int m_voiceCount;
	float m_combatSeconds;
	int m_strafeChanges;
	int m_combatJumps;
};

// Per-session tracking for a recorded player (not persisted).
struct TFBotRecordLive_t
{
	bool m_active;
	int m_userId;
	int m_recordIndex;
	float m_lastCombat;			// last time they dealt or took damage
	bool m_wasInCombat;
	int m_lastLateralSign;
	bool m_wasOnGround;
	int m_lastHitscanTick;
	float m_nextSecond;
	float m_nextVisCheck;
	float m_visibleSince[ MAX_PLAYERS + 1 ];	// enemy index -> time first seen in this sighting, 0 if not visible
	bool m_reacted[ MAX_PLAYERS + 1 ];
};

static CUtlVector< TFBotRecord_t > s_records;
static TFBotRecordLive_t s_live[ MAX_PLAYERS + 1 ];
static bool s_recordsLoaded = false;


//-----------------------------------------------------------------------------
// Persistence
//-----------------------------------------------------------------------------
static int FindRecord( const char *name, bool create )
{
	FOR_EACH_VEC( s_records, i )
	{
		if ( !V_stricmp( s_records[i].m_name, name ) )
			return i;
	}
	if ( !create )
		return -1;
	int i = s_records.AddToTail();
	V_memset( &s_records[i], 0, sizeof( TFBotRecord_t ) );
	V_strncpy( s_records[i].m_name, name, sizeof( s_records[i].m_name ) );
	return i;
}

static void LoadRecords()
{
	s_recordsLoaded = true;
	s_records.RemoveAll();

	KeyValues *file = new KeyValues( "TFBotRecords" );
	if ( file->LoadFromFile( filesystem, TFBOT_RECORDS_FILE, "MOD" ) )
	{
		for ( KeyValues *e = file->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
		{
			TFBotRecord_t &r = s_records[ FindRecord( e->GetName(), true ) ];
			r.m_aliveSeconds = e->GetFloat( "alive_seconds" );
			for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
				r.m_classSeconds[c] = e->GetFloat( CFmtStr( "class_%s", g_aRawPlayerClassNamesShort[c] ) );
			r.m_frontnessSum = e->GetFloat( "frontness_sum" );
			r.m_frontnessCount = e->GetInt( "frontness_count" );
			r.m_aloneSeconds = e->GetFloat( "alone_seconds" );
			r.m_engageDistSum = e->GetFloat( "engage_dist_sum" );
			r.m_engageCount = e->GetInt( "engage_count" );
			r.m_reactionSum = e->GetFloat( "reaction_sum" );
			r.m_reactionCount = e->GetInt( "reaction_count" );
			r.m_hitscanShots = e->GetInt( "hitscan_shots" );
			r.m_hitscanHits = e->GetInt( "hitscan_hits" );
			r.m_retreatHealthSum = e->GetFloat( "retreat_health_sum" );
			r.m_retreatCount = e->GetInt( "retreat_count" );
			r.m_voiceCount = e->GetInt( "voice_count" );
			r.m_combatSeconds = e->GetFloat( "combat_seconds" );
			r.m_strafeChanges = e->GetInt( "strafe_changes" );
			r.m_combatJumps = e->GetInt( "combat_jumps" );
		}
	}
	file->deleteThis();
}

static void SaveRecords()
{
	if ( !s_recordsLoaded || s_records.Count() == 0 )
		return;

	KeyValues *file = new KeyValues( "TFBotRecords" );
	FOR_EACH_VEC( s_records, i )
	{
		const TFBotRecord_t &r = s_records[i];
		KeyValues *e = file->FindKey( r.m_name, true );
		e->SetFloat( "alive_seconds", r.m_aliveSeconds );
		for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
			e->SetFloat( CFmtStr( "class_%s", g_aRawPlayerClassNamesShort[c] ), r.m_classSeconds[c] );
		e->SetFloat( "frontness_sum", r.m_frontnessSum );
		e->SetInt( "frontness_count", r.m_frontnessCount );
		e->SetFloat( "alone_seconds", r.m_aloneSeconds );
		e->SetFloat( "engage_dist_sum", r.m_engageDistSum );
		e->SetInt( "engage_count", r.m_engageCount );
		e->SetFloat( "reaction_sum", r.m_reactionSum );
		e->SetInt( "reaction_count", r.m_reactionCount );
		e->SetInt( "hitscan_shots", r.m_hitscanShots );
		e->SetInt( "hitscan_hits", r.m_hitscanHits );
		e->SetFloat( "retreat_health_sum", r.m_retreatHealthSum );
		e->SetInt( "retreat_count", r.m_retreatCount );
		e->SetInt( "voice_count", r.m_voiceCount );
		e->SetFloat( "combat_seconds", r.m_combatSeconds );
		e->SetInt( "strafe_changes", r.m_strafeChanges );
		e->SetInt( "combat_jumps", r.m_combatJumps );
	}
	filesystem->CreateDirHierarchy( "cfg", "MOD" );
	file->SaveToFile( filesystem, TFBOT_RECORDS_FILE, "MOD" );
	file->deleteThis();
}


//-----------------------------------------------------------------------------
static TFBotRecordLive_t *LiveFor( CTFPlayer *player )
{
	if ( !player )
		return NULL;
	int index = player->entindex();
	if ( index < 1 || index > MAX_PLAYERS )
		return NULL;
	TFBotRecordLive_t &live = s_live[ index ];
	if ( !live.m_active || live.m_userId != player->GetUserID() )
		return NULL;
	return &live;
}

static TFBotRecord_t &RecordFor( const TFBotRecordLive_t &live )
{
	return s_records[ live.m_recordIndex ];
}


//-----------------------------------------------------------------------------
void TFBotClone::OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon )
{
	TFBotRecordLive_t *live = LiveFor( player );
	if ( !live || !weapon )
		return;
	TFBotRecord_t &r = RecordFor( *live );

	CTFWeaponBaseGun *gun = dynamic_cast< CTFWeaponBaseGun * >( weapon );
	if ( gun && gun->GetWeaponProjectileType() == TF_PROJECTILE_BULLET )
	{
		++r.m_hitscanShots;
		live->m_lastHitscanTick = gpGlobals->tickcount;
	}

	// Reaction: first shot after an enemy came into view.
	const float now = gpGlobals->curtime;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		if ( live->m_visibleSince[i] > 0.0f && !live->m_reacted[i] && now - live->m_visibleSince[i] < 2.0f )
		{
			// visibility is sampled every 0.1 s: take half a sample off
			r.m_reactionSum += MAX( now - live->m_visibleSince[i] - 0.05f, 0.05f );
			++r.m_reactionCount;
			live->m_reacted[i] = true;
		}
	}
}

void TFBotClone::OnPlayerSpoke( CTFPlayer *player, int concept )
{
	TFBotRecordLive_t *live = LiveFor( player );
	if ( live )
		++RecordFor( *live ).m_voiceCount;
}


//-----------------------------------------------------------------------------
class CTFBotCloneSystem : public CAutoGameSystemPerFrame, public CGameEventListener
{
public:
	CTFBotCloneSystem() : CAutoGameSystemPerFrame( "CTFBotCloneSystem" ), m_listening( false ) {}

	virtual void LevelInitPostEntity() OVERRIDE
	{
		if ( !s_recordsLoaded )
			LoadRecords();
		if ( !m_listening )
		{
			ListenForGameEvent( "player_hurt" );
			ListenForGameEvent( "teamplay_round_win" );
			m_listening = true;
		}
	}

	virtual void LevelShutdownPreEntity() OVERRIDE
	{
		SaveRecords();
	}

	virtual void Shutdown() OVERRIDE
	{
		SaveRecords();
		StopListeningForAllEvents();
		m_listening = false;
	}

	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE
	{
		if ( FStrEq( event->GetName(), "teamplay_round_win" ) )
		{
			SaveRecords();
			return;
		}

		CTFPlayer *victim = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
		CTFPlayer *attacker = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
		if ( !victim || !attacker || victim == attacker )
			return;

		TFBotRecordLive_t *liveAttacker = LiveFor( attacker );
		if ( liveAttacker )
		{
			TFBotRecord_t &r = RecordFor( *liveAttacker );
			liveAttacker->m_lastCombat = gpGlobals->curtime;
			r.m_engageDistSum += ( attacker->GetAbsOrigin() - victim->GetAbsOrigin() ).Length();
			++r.m_engageCount;
			if ( liveAttacker->m_lastHitscanTick == gpGlobals->tickcount )
				++r.m_hitscanHits;
		}

		TFBotRecordLive_t *liveVictim = LiveFor( victim );
		if ( liveVictim )
			liveVictim->m_lastCombat = gpGlobals->curtime;
	}

	virtual void FrameUpdatePostEntityThink() OVERRIDE
	{
		for ( int i = 1; i <= MAX_PLAYERS; ++i )
		{
			if ( !s_live[i].m_active )
				continue;
			CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || player->GetUserID() != s_live[i].m_userId )
			{
				s_live[i].m_active = false;
				continue;
			}
			if ( player->IsAlive() && TFGameRules() && TFGameRules()->State_Get() == GR_STATE_RND_RUNNING )
				Sample( player, s_live[i] );
		}
	}

private:
	void Sample( CTFPlayer *player, TFBotRecordLive_t &live )
	{
		TFBotRecord_t &r = RecordFor( live );
		const float now = gpGlobals->curtime;
		const float dt = gpGlobals->frametime;
		const int team = player->GetTeamNumber();

		bool inCombat = now - live.m_lastCombat < 2.0f;

		// combat movement: strafe direction changes and jumps
		if ( inCombat )
		{
			r.m_combatSeconds += dt;

			Vector forward;
			player->EyeVectors( &forward );
			Vector right( forward.y, -forward.x, 0.0f );
			right.NormalizeInPlace();
			float lateral = DotProduct( player->GetAbsVelocity(), right );
			int sign = ( lateral > 100.0f ) ? 1 : ( lateral < -100.0f ? -1 : 0 );
			if ( sign != 0 && live.m_lastLateralSign != 0 && sign != live.m_lastLateralSign )
				++r.m_strafeChanges;
			if ( sign != 0 )
				live.m_lastLateralSign = sign;

			bool onGround = ( player->GetFlags() & FL_ONGROUND ) != 0;
			if ( live.m_wasOnGround && !onGround && player->GetAbsVelocity().z > 150.0f )
				++r.m_combatJumps;
			live.m_wasOnGround = onGround;
		}
		else if ( live.m_wasInCombat )
		{
			// a fight just ended: health when they disengaged
			float health = (float)player->GetHealth() / (float)player->GetMaxHealth();
			if ( health < 0.8f )
			{
				r.m_retreatHealthSum += health;
				++r.m_retreatCount;
			}
		}
		live.m_wasInCombat = inCombat;

		// enemy visibility for reaction time, every 0.1 s
		if ( now >= live.m_nextVisCheck )
		{
			live.m_nextVisCheck = now + 0.1f;
			Vector forward;
			player->EyeVectors( &forward );
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTFPlayer *enemy = ToTFPlayer( UTIL_PlayerByIndex( i ) );
				bool visible = false;
				if ( enemy && enemy->IsAlive() && enemy->GetTeamNumber() != team && enemy->GetTeamNumber() >= FIRST_GAME_TEAM &&
					 !enemy->m_Shared.IsStealthed() )
				{
					Vector to = enemy->WorldSpaceCenter() - player->EyePosition();
					float dist = to.NormalizeInPlace();
					if ( dist < 2500.0f && DotProduct( to, forward ) > 0.707f )
					{
						trace_t tr;
						UTIL_TraceLine( player->EyePosition(), enemy->WorldSpaceCenter(), MASK_BLOCKLOS, player, COLLISION_GROUP_NONE, &tr );
						visible = ( tr.fraction >= 1.0f || tr.m_pEnt == enemy );
					}
				}
				if ( visible && live.m_visibleSince[i] <= 0.0f )
				{
					live.m_visibleSince[i] = now;
					live.m_reacted[i] = false;
				}
				else if ( !visible )
				{
					live.m_visibleSince[i] = 0.0f;
				}
			}
		}

		// once a second: class, how far forward, how alone
		if ( now >= live.m_nextSecond )
		{
			live.m_nextSecond = now + 1.0f;
			r.m_aliveSeconds += 1.0f;
			int cls = player->GetPlayerClass()->GetClassIndex();
			if ( cls > TF_CLASS_UNDEFINED && cls < TF_CLASS_COUNT_ALL )
				r.m_classSeconds[ cls ] += 1.0f;

			CTFNavArea *myArea = static_cast< CTFNavArea * >( player->GetLastKnownArea() );
			float sum = 0.0f;
			int n = 0;
			float nearestMate = FLT_MAX;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTFPlayer *mate = ToTFPlayer( UTIL_PlayerByIndex( i ) );
				if ( !mate || mate == player || !mate->IsAlive() || mate->GetTeamNumber() != team )
					continue;
				nearestMate = MIN( nearestMate, ( mate->GetAbsOrigin() - player->GetAbsOrigin() ).Length() );
				CTFNavArea *area = static_cast< CTFNavArea * >( mate->GetLastKnownArea() );
				if ( area )
				{
					sum += area->GetIncursionDistance( team );
					++n;
				}
			}
			if ( myArea && n > 0 )
			{
				r.m_frontnessSum += myArea->GetIncursionDistance( team ) - sum / n;
				++r.m_frontnessCount;
			}
			if ( nearestMate > 800.0f && nearestMate < FLT_MAX )
				r.m_aloneSeconds += 1.0f;
		}
	}

	bool m_listening;
};

static CTFBotCloneSystem s_tfBotCloneSystem;


//-----------------------------------------------------------------------------
// Console
//-----------------------------------------------------------------------------
static CTFPlayer *FindHumanByName( const char *name )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && !player->IsBot() && !V_stricmp( player->GetPlayerName(), name ) )
			return player;
	}
	return NULL;
}

CON_COMMAND( tf_bot_record_player, "Record a human's playstyle for cloning: tf_bot_record_player <name> | stop <name> | list" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( !s_recordsLoaded )
		LoadRecords();

	if ( args.ArgC() < 2 || !V_stricmp( args.Arg( 1 ), "list" ) )
	{
		Msg( "Recording:" );
		for ( int i = 1; i <= MAX_PLAYERS; ++i )
		{
			CTFPlayer *p = s_live[i].m_active ? ToTFPlayer( UTIL_PlayerByIndex( i ) ) : NULL;
			if ( p )
				Msg( " %s", p->GetPlayerName() );
		}
		Msg( "\nRecords:" );
		FOR_EACH_VEC( s_records, r )
			Msg( " %s (%.0f min)", s_records[r].m_name, s_records[r].m_aliveSeconds / 60.0f );
		Msg( "\n" );
		return;
	}

	bool stop = !V_stricmp( args.Arg( 1 ), "stop" );
	const char *name = stop ? ( args.ArgC() >= 3 ? args.Arg( 2 ) : "" ) : args.Arg( 1 );
	CTFPlayer *player = FindHumanByName( name );
	if ( !player )
	{
		Warning( "No human player named '%s'\n", name );
		return;
	}

	TFBotRecordLive_t &live = s_live[ player->entindex() ];
	if ( stop )
	{
		live.m_active = false;
		SaveRecords();
		Msg( "Stopped recording %s\n", name );
		return;
	}

	V_memset( &live, 0, sizeof( live ) );
	live.m_active = true;
	live.m_userId = player->GetUserID();
	live.m_recordIndex = FindRecord( player->GetPlayerName(), true );
	live.m_lastHitscanTick = -1;
	Msg( "Recording %s's playstyle (adds to %.0f minutes on record)\n", name, s_records[ live.m_recordIndex ].m_aliveSeconds / 60.0f );
}

static void AppendSet( CUtlString &cmds, const char *profileName, const char *key, float value )
{
	cmds.Append( CFmtStr( "tf_bot_profile_set \"%s\" %s %.3f\n", profileName, key, value ) );
}

CON_COMMAND( tf_bot_profile_from_record, "Make a custom bot profile from a recorded player: tf_bot_profile_from_record <player> [profile name]" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( !s_recordsLoaded )
		LoadRecords();
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_profile_from_record <player> [profile name]\n" );
		return;
	}

	int index = FindRecord( args.Arg( 1 ), false );
	if ( index < 0 )
	{
		Warning( "No record for '%s' (tf_bot_record_player list)\n", args.Arg( 1 ) );
		return;
	}
	const TFBotRecord_t &r = s_records[ index ];
	if ( r.m_aliveSeconds < 300.0f )
		Warning( "Only %.0f minutes recorded; the profile will be rough\n", r.m_aliveSeconds / 60.0f );

	char profileName[ 64 ];
	V_snprintf( profileName, sizeof( profileName ), "%s", args.ArgC() >= 3 ? args.Arg( 2 ) : CFmtStr( "%s (clone)", r.m_name ).Access() );

	// Aim error from hitscan accuracy: 10% -> 12 deg, 60% -> 1 deg.
	float aimError = -1.0f;
	if ( r.m_hitscanShots >= 50 )
	{
		float accuracy = (float)r.m_hitscanHits / (float)r.m_hitscanShots;
		aimError = RemapValClamped( accuracy, 0.10f, 0.60f, 12.0f, 1.0f );
	}

	// Closest tier by aim error (or casual without enough shots).
	int tier = 1;
	if ( aimError >= 0.0f )
	{
		static const float tierAim[ TFBOT_PROFILE_TIER_COUNT ] = { 8.0f, 5.0f, 3.0f, 1.5f };
		float best = FLT_MAX;
		for ( int t = 0; t < TFBOT_PROFILE_TIER_COUNT; ++t )
		{
			if ( fabsf( tierAim[t] - aimError ) < best )
			{
				best = fabsf( tierAim[t] - aimError );
				tier = t;
			}
		}
	}

	CUtlString cmds;
	cmds.Append( CFmtStr( "tf_bot_profile_new \"%s\" %s\n", profileName, g_szTFBotProfileTierNames[ tier ] ) );

	if ( aimError >= 0.0f )
		AppendSet( cmds, profileName, "aim_error", aimError );
	if ( r.m_reactionCount >= 10 )
		AppendSet( cmds, profileName, "reaction_time", clamp( r.m_reactionSum / r.m_reactionCount, 0.10f, 1.50f ) );
	if ( r.m_frontnessCount >= 60 )
		AppendSet( cmds, profileName, "aggression", clamp( 0.5f + ( r.m_frontnessSum / r.m_frontnessCount ) / 1600.0f, 0.0f, 1.0f ) );
	if ( r.m_aliveSeconds >= 60.0f )
	{
		AppendSet( cmds, profileName, "flank", clamp( 1.5f * r.m_aloneSeconds / r.m_aliveSeconds, 0.0f, 1.0f ) );
		AppendSet( cmds, profileName, "callout_rate", clamp( ( r.m_voiceCount / ( r.m_aliveSeconds / 60.0f ) ) / 3.0f, 0.0f, 1.0f ) );
	}
	if ( r.m_retreatCount >= 5 )
		AppendSet( cmds, profileName, "retreat_health", clamp( r.m_retreatHealthSum / r.m_retreatCount, 0.10f, 0.90f ) );
	if ( r.m_combatSeconds >= 30.0f )
		AppendSet( cmds, profileName, "strafe", clamp( ( r.m_strafeChanges / r.m_combatSeconds ) / 2.5f + ( r.m_combatJumps / r.m_combatSeconds ) * 2.0f, 0.0f, 1.0f ) );

	// Class preferences from time played, scaled so the main class is 10.
	float most = 0.0f;
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
		most = MAX( most, r.m_classSeconds[c] );
	if ( most > 0.0f )
	{
		for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
		{
			float weight = 0.3f + 9.7f * r.m_classSeconds[c] / most;
			cmds.Append( CFmtStr( "tf_bot_profile_set \"%s\" class_%s %.2f\n", profileName, g_aRawPlayerClassNamesShort[c], weight ) );
		}
	}

	engine->ServerCommand( cmds.Get() );
	engine->ServerCommand( "tf_bot_profile_save\n" );
	Msg( "Building profile '%s' from %.0f recorded minutes (tier %s). Spawn it with tf_bot_add_profile \"%s\"\n",
		profileName, r.m_aliveSeconds / 60.0f, g_szTFBotProfileTierNames[ tier ], profileName );
	if ( r.m_engageCount > 0 )
		Msg( "  (typical engagement distance %.0f units)\n", r.m_engageDistSum / r.m_engageCount );
}
