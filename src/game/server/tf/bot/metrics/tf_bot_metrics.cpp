//========= TFBot overhaul: match metrics =====================================//
//
// One JSON Lines file per match. Record types (field "type"):
//   match_start  map, run id, timescale, tickrate, team feature settings
//   round_start  round number, bots/humans and features per team
//   kill         attacker/victim team, class, bot flag, weapon
//   capture      control point or flag capture
//   stuck        one bot stuck episode: start, duration, position, how it ended
//   round_end    winner, reason, duration, per-team stats, tick cost, feature cost
//   match_end    totals; written before an automatic quit or at level shutdown
//
// Hitscan accuracy: shots come from the TFBotMetrics_OnWeaponFired hook, which
// runs at the start of CTFWeaponBaseGun::PrimaryAttack, just before the bullets
// are traced. A hit is a player_hurt from an attacker whose hitscan shot the
// hook recorded in this same tick (hitscan damage lands in the firing tick),
// which keeps sentry damage out. (m_flLastFireTime can't be used: the gun sets
// it only after the damage has been dealt.) Projectile accuracy is deferred to M2.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_metrics.h"
#include "GameEventListener.h"
#include "filesystem.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_weaponbase_gun.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/clone/tf_bot_clone.h"
#include "nav_mesh/autogen/tf_nav_autogen.h"
#include "nav_mesh.h"
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_metrics( "tf_bot_metrics", "0", FCVAR_NONE, "Write TFBot overhaul metrics (JSON Lines) for each match." );
ConVar tf_bot_metrics_dir( "tf_bot_metrics_dir", "tfbot_metrics", FCVAR_NONE, "Folder (MOD search path) that receives metrics files." );
ConVar tf_bot_metrics_run_id( "tf_bot_metrics_run_id", "manual", FCVAR_NONE, "Run identifier: subfolder under tf_bot_metrics_dir, also written into match_start." );
ConVar tf_bot_ab_rounds( "tf_bot_ab_rounds", "0", FCVAR_NONE, "If > 0, write metrics and quit the server after this many rounds have ended (A/B harness)." );


//-----------------------------------------------------------------------------
// Minimal JSON object writer for one line.
//-----------------------------------------------------------------------------
class CTFBotJsonLine
{
public:
	explicit CTFBotJsonLine( const char *type ) : m_len( 0 ), m_needComma( false )
	{
		m_buf[0] = '\0';
		Raw( "{" );
		Str( "type", type );
	}

	void Str( const char *key, const char *value )	{ Key( key ); Quoted( value ); }
	void Int( const char *key, int value )			{ Key( key ); Rawf( "%d", value ); }
	void Float( const char *key, double value )		{ Key( key ); Rawf( "%.4f", value ); }
	void Bool( const char *key, bool value )		{ Key( key ); Raw( value ? "true" : "false" ); }

	void BeginObject( const char *key )				{ Key( key ); Raw( "{" ); m_needComma = false; }
	void EndObject()								{ Raw( "}" ); m_needComma = true; }
	void BeginArray( const char *key )				{ Key( key ); Raw( "[" ); m_needComma = false; }
	void EndArray()									{ Raw( "]" ); m_needComma = true; }
	void ArrayStr( const char *value )				{ Comma(); Quoted( value ); m_needComma = true; }
	void ArrayFloat( double value )					{ Comma(); Rawf( "%.1f", value ); m_needComma = true; }

	const char *Finish( int *length )
	{
		Raw( "}\n" );
		*length = m_len;
		return m_buf;
	}

private:
	void Comma()						{ if ( m_needComma ) Raw( "," ); }
	void Key( const char *key )			{ Comma(); Quoted( key ); Raw( ":" ); m_needComma = true; }

	void Raw( const char *text )
	{
		int n = V_strlen( text );
		if ( m_len + n >= (int)sizeof( m_buf ) - 1 )
			return;
		V_memcpy( m_buf + m_len, text, n );
		m_len += n;
		m_buf[ m_len ] = '\0';
	}

	void Rawf( const char *fmt, ... )
	{
		char tmp[ 64 ];
		va_list args;
		va_start( args, fmt );
		V_vsnprintf( tmp, sizeof( tmp ), fmt, args );
		va_end( args );
		Raw( tmp );
	}

	void Quoted( const char *text )
	{
		Raw( "\"" );
		char esc[ 8 ];
		for ( const char *c = text ? text : ""; *c; ++c )
		{
			unsigned char ch = (unsigned char)*c;
			if ( ch == '"' || ch == '\\' )
			{
				esc[0] = '\\'; esc[1] = ch; esc[2] = '\0';
				Raw( esc );
			}
			else if ( ch < 0x20 )
			{
				V_snprintf( esc, sizeof( esc ), "\\u%04x", ch );
				Raw( esc );
			}
			else
			{
				esc[0] = ch; esc[1] = '\0';
				Raw( esc );
			}
		}
		Raw( "\"" );
	}

	char m_buf[ 8192 ];
	int m_len;
	bool m_needComma;
};


//-----------------------------------------------------------------------------
enum { SLOT_RED = 0, SLOT_BLU, SLOT_COUNT };

static int TeamSlot( int team )
{
	if ( team == TF_TEAM_RED )
		return SLOT_RED;
	if ( team == TF_TEAM_BLUE )
		return SLOT_BLU;
	return -1;
}

static const char *TeamName( int team )
{
	if ( team == TF_TEAM_RED )
		return "red";
	if ( team == TF_TEAM_BLUE )
		return "blu";
	return "none";
}

static const char *SlotTeamName( int slot )
{
	return slot == SLOT_RED ? "red" : "blu";
}

static const char *ClassName( CTFPlayer *player )
{
	int iClass = player->GetPlayerClass()->GetClassIndex();
	if ( iClass < 0 || iClass >= TF_CLASS_MENU_BUTTONS )
		return "unknown";
	return g_aPlayerClassNames_NonLocalized[ iClass ];
}

static bool IsHitscanGun( CTFWeaponBase *weapon )
{
	CTFWeaponBaseGun *gun = dynamic_cast< CTFWeaponBaseGun * >( weapon );
	return gun && gun->GetWeaponProjectileType() == TF_PROJECTILE_BULLET;
}

// Keep run ids / folder names to a safe character set, no "..", no leading '/'.
static void SanitizePath( const char *in, char *out, int outSize )
{
	int n = 0;
	for ( const char *c = in; *c && n < outSize - 1; ++c )
	{
		char ch = *c;
		bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) || ( ch >= '0' && ch <= '9' ) ||
				  ch == '_' || ch == '-' || ch == '.' || ch == '/';
		if ( !ok )
			ch = '_';
		if ( ch == '/' && n == 0 )
			continue;
		if ( ch == '.' && n > 0 && out[ n - 1 ] == '.' )
			ch = '_';
		out[ n++ ] = ch;
	}
	out[ n ] = '\0';
	if ( !out[0] )
		V_strncpy( out, "unnamed", outSize );
}


//-----------------------------------------------------------------------------
struct TFBotTeamStats_t
{
	int kills;
	int deaths;
	int suicides;
	int damage;
	int shots;
	int hitscanShots;
	int hitscanHits;
	int captures;
	float objectiveSeconds;
	float stuckSeconds;

	void Reset() { V_memset( this, 0, sizeof( *this ) ); }
};

struct TFBotTickCost_t
{
	int ticks;
	double sumMs;
	double maxMs;
	int overBudget;

	void Reset() { ticks = 0; sumMs = 0.0; maxMs = 0.0; overBudget = 0; }

	void Add( double ms, double budgetMs )
	{
		++ticks;
		sumMs += ms;
		if ( ms > maxMs )
			maxMs = ms;
		if ( ms > budgetMs )
			++overBudget;
	}

	void Write( CTFBotJsonLine &line, const char *key ) const
	{
		line.BeginObject( key );
		line.Int( "ticks", ticks );
		line.Float( "mean", ticks ? sumMs / ticks : 0.0 );
		line.Float( "max", maxMs );
		line.Int( "over_budget", overBudget );
		line.EndObject();
	}
};

struct TFBotStuckEpisode_t
{
	bool active;
	float start;
	Vector pos;
	int team;
	char className[ 32 ];
};


//-----------------------------------------------------------------------------
class CTFBotMetrics : public CAutoGameSystemPerFrame, public CGameEventListener
{
public:
	CTFBotMetrics() : CAutoGameSystemPerFrame( "CTFBotMetrics" )
	{
		m_file = FILESYSTEM_INVALID_HANDLE;
		m_listening = false;
	}

	virtual void LevelInitPostEntity() OVERRIDE;
	virtual void LevelShutdownPreEntity() OVERRIDE;
	virtual void Shutdown() OVERRIDE
	{
		// Unregister while the engine's event manager still exists; the static
		// destructor runs too late for that.
		StopListeningForAllEvents();
		m_listening = false;
	}
	virtual void FrameUpdatePreEntityThink() OVERRIDE;
	virtual void FrameUpdatePostEntityThink() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;

	void OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon );

private:
	bool IsActive() const { return m_file != FILESYSTEM_INVALID_HANDLE; }
	bool IsRoundRunning() const { return TFGameRules() && TFGameRules()->State_Get() == GR_STATE_RND_RUNNING; }

	void OpenFile();
	void CloseFile();
	void Emit( CTFBotJsonLine &line );

	void WriteMatchStart();
	void WriteRoundStart();
	void WriteRoundEnd( int winnerTeam, int reason, bool stalemate, bool fullRound, bool suddenDeath, float duration );
	void WriteMatchEnd( const char *reason );
	void WriteFeatureCosts( CTFBotJsonLine &line, const double *since );
	void WriteTeamFeatures( CTFBotJsonLine &line, int team );

	void SampleSecond( float dt );
	void UpdateStuck( int index, CTFBot *bot, bool stuck );
	void EndStuck( int index, const char *how );
	void EndAllStuck( const char *how );

	void OnPlayerDeath( IGameEvent *event );
	void OnPlayerHurt( IGameEvent *event );
	void OnRoundEnded();

	FileHandle_t m_file;
	bool m_listening;
	bool m_quitting;
	char m_path[ MAX_PATH ];

	int m_round;				// rounds started
	int m_roundsEnded;			// rounds counted toward tf_bot_ab_rounds
	float m_roundStart;
	float m_matchStartGameTime;
	double m_matchStartWallTime;
	float m_nextSample;
	float m_lastSample;
	double m_frameStart;

	TFBotTeamStats_t m_team[ SLOT_COUNT ];
	TFBotTickCost_t m_roundTicks;
	TFBotTickCost_t m_matchTicks;
	double m_roundCostStart[ TFBOT_FEATURE_COUNT ];
	double m_matchCostStart[ TFBOT_FEATURE_COUNT ];
	TFBotStuckEpisode_t m_stuck[ MAX_PLAYERS + 1 ];
	int m_lastHitscanTick[ MAX_PLAYERS + 1 ];	// tick of each player's last hitscan shot
};

static CTFBotMetrics s_tfBotMetrics;


//-----------------------------------------------------------------------------
void TFBotMetrics_OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon )
{
	s_tfBotMetrics.OnWeaponFired( player, weapon );
	TFBotClone::OnWeaponFired( player, weapon );	// M4b playstyle recording shares this hook
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::LevelInitPostEntity()
{
	if ( !m_listening )
	{
		ListenForGameEvent( "teamplay_round_start" );
		ListenForGameEvent( "teamplay_round_win" );
		ListenForGameEvent( "teamplay_round_stalemate" );
		ListenForGameEvent( "player_death" );
		ListenForGameEvent( "player_hurt" );
		ListenForGameEvent( "teamplay_point_captured" );
		ListenForGameEvent( "teamplay_flag_event" );
		m_listening = true;
	}

	m_quitting = false;
	m_round = 0;
	m_roundsEnded = 0;
	m_roundStart = gpGlobals->curtime;
	m_matchStartGameTime = gpGlobals->curtime;
	m_matchStartWallTime = Plat_FloatTime();
	m_nextSample = 0.0f;
	m_lastSample = gpGlobals->curtime;
	m_frameStart = 0.0;
	for ( int s = 0; s < SLOT_COUNT; ++s )
		m_team[s].Reset();
	m_roundTicks.Reset();
	m_matchTicks.Reset();
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
	{
		m_roundCostStart[f] = TFBotFeatures::GetCost( (ETFBotFeature)f );
		m_matchCostStart[f] = m_roundCostStart[f];
	}
	V_memset( m_stuck, 0, sizeof( m_stuck ) );
	for ( int i = 0; i <= MAX_PLAYERS; ++i )
		m_lastHitscanTick[i] = -1;

	if ( tf_bot_metrics.GetBool() )
	{
		OpenFile();
		if ( IsActive() )
			WriteMatchStart();
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::LevelShutdownPreEntity()
{
	if ( IsActive() )
	{
		EndAllStuck( "level_shutdown" );
		WriteMatchEnd( "level_shutdown" );
		CloseFile();
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::OpenFile()
{
	char runId[ 128 ];
	char baseDir[ 128 ];
	SanitizePath( tf_bot_metrics_run_id.GetString(), runId, sizeof( runId ) );
	SanitizePath( tf_bot_metrics_dir.GetString(), baseDir, sizeof( baseDir ) );

	char dir[ MAX_PATH ];
	V_snprintf( dir, sizeof( dir ), "%s/%s", baseDir, runId );
	filesystem->CreateDirHierarchy( dir, "MOD" );

	time_t now = time( NULL );
	struct tm t;
	Plat_localtime( &now, &t );

	ConVarRef hostport( "hostport" );
	V_snprintf( m_path, sizeof( m_path ), "%s/%s_%04d%02d%02d-%02d%02d%02d_%d.jsonl",
		dir, STRING( gpGlobals->mapname ),
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
		hostport.IsValid() ? hostport.GetInt() : 0 );

	m_file = filesystem->Open( m_path, "w", "MOD" );
	if ( m_file == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "TFBot metrics: could not open '%s' for writing\n", m_path );
		return;
	}
	Msg( "TFBot metrics: writing %s\n", m_path );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::CloseFile()
{
	if ( !IsActive() )
		return;

	filesystem->Close( m_file );
	m_file = FILESYSTEM_INVALID_HANDLE;
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::Emit( CTFBotJsonLine &line )
{
	if ( !IsActive() )
		return;

	int length = 0;
	const char *text = line.Finish( &length );
	filesystem->Write( text, length, m_file );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteTeamFeatures( CTFBotJsonLine &line, int team )
{
	char names[ 512 ];
	TFBotFeatures::MaskToString( TFBotFeatures::GetTeamMask( team ), names, sizeof( names ) );
	line.BeginObject( TeamName( team ) );
	line.Str( "setting", TFBotFeatures::GetTeamSetting( team ) );
	line.Str( "enabled", names );
	line.EndObject();
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteFeatureCosts( CTFBotJsonLine &line, const double *since )
{
	line.BeginObject( "feature_cost_ms" );
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
	{
		double cost = TFBotFeatures::GetCost( (ETFBotFeature)f ) - since[f];
		line.Float( TFBotFeatures::GetName( (ETFBotFeature)f ), cost * 1000.0 );
	}
	line.EndObject();
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteMatchStart()
{
	ConVarRef timescale( "host_timescale" );
	ConVarRef difficulty( "tf_bot_difficulty" );
	ConVarRef quota( "tf_bot_quota" );

	time_t now = time( NULL );
	struct tm t;
	Plat_localtime( &now, &t );
	char wall[ 32 ];
	V_snprintf( wall, sizeof( wall ), "%04d-%02d-%02dT%02d:%02d:%02d",
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec );

	CTFBotJsonLine line( "match_start" );
	line.Int( "version", 1 );
	line.Str( "map", STRING( gpGlobals->mapname ) );
	line.Str( "run", tf_bot_metrics_run_id.GetString() );
	line.Str( "wall_time", wall );
	line.Float( "timescale", timescale.IsValid() ? timescale.GetFloat() : 1.0f );
	line.Float( "tickrate", 1.0f / gpGlobals->interval_per_tick );
	line.Int( "maxplayers", gpGlobals->maxClients );
	line.Int( "difficulty", difficulty.IsValid() ? difficulty.GetInt() : -1 );
	line.Int( "quota", quota.IsValid() ? quota.GetInt() : -1 );
	if ( TFGameRules() )
	{
		line.Int( "game_type", TFGameRules()->GetGameType() );
		line.Bool( "attack_defense", TFGameRules()->IsAttackDefenseMode() );
		line.Bool( "koth", TFGameRules()->IsInKothMode() );
	}
	line.BeginArray( "feature_registry" );
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
		line.ArrayStr( TFBotFeatures::GetName( (ETFBotFeature)f ) );
	line.EndArray();
	line.BeginObject( "features" );
	WriteTeamFeatures( line, TF_TEAM_RED );
	WriteTeamFeatures( line, TF_TEAM_BLUE );
	line.EndObject();
	Emit( line );
	filesystem->Flush( m_file );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteRoundStart()
{
	int bots[ SLOT_COUNT ] = { 0, 0 };
	int humans[ SLOT_COUNT ] = { 0, 0 };
	int mismatched[ SLOT_COUNT ] = { 0, 0 };

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player )
			continue;

		int slot = TeamSlot( player->GetTeamNumber() );
		if ( slot < 0 )
			continue;

		CTFBot *bot = ToTFBot( player );
		if ( bot )
		{
			++bots[ slot ];
			if ( TFBotFeatures::GetBotMask( bot ) != TFBotFeatures::GetTeamMask( player->GetTeamNumber() ) )
				++mismatched[ slot ];
		}
		else if ( !player->IsFakeClient() )
		{
			++humans[ slot ];
		}
	}

	CTFBotJsonLine line( "round_start" );
	line.Int( "round", m_round );
	line.Float( "t", gpGlobals->curtime - m_matchStartGameTime );
	const TFNavStatus_t &nav = TFNavAutogen::GetStatus();
	line.BeginObject( "nav" );
	line.Bool( "loaded", TheNavMesh->IsLoaded() );
	line.Bool( "generated", nav.m_generated );
	line.Bool( "verified", nav.m_validated && nav.m_verified );
	line.Int( "areas", nav.m_areaCount );
	line.EndObject();
	line.BeginObject( "teams" );
	for ( int s = 0; s < SLOT_COUNT; ++s )
	{
		int team = ( s == SLOT_RED ) ? TF_TEAM_RED : TF_TEAM_BLUE;
		char names[ 512 ];
		TFBotFeatures::MaskToString( TFBotFeatures::GetTeamMask( team ), names, sizeof( names ) );

		line.BeginObject( SlotTeamName( s ) );
		line.Int( "bots", bots[s] );
		line.Int( "humans", humans[s] );
		line.Str( "setting", TFBotFeatures::GetTeamSetting( team ) );
		line.Str( "enabled", names );
		line.Int( "bots_with_other_features", mismatched[s] );
		line.EndObject();
	}
	line.EndObject();
	Emit( line );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteRoundEnd( int winnerTeam, int reason, bool stalemate, bool fullRound, bool suddenDeath, float duration )
{
	CTFBotJsonLine line( "round_end" );
	line.Int( "round", m_round );
	line.Float( "t", gpGlobals->curtime - m_matchStartGameTime );
	line.Str( "winner", TeamName( winnerTeam ) );
	line.Int( "reason", reason );
	line.Bool( "stalemate", stalemate );
	line.Bool( "full_round", fullRound );
	line.Bool( "sudden_death", suddenDeath );
	line.Float( "duration", duration );

	line.BeginObject( "teams" );
	for ( int s = 0; s < SLOT_COUNT; ++s )
	{
		const TFBotTeamStats_t &st = m_team[s];
		line.BeginObject( SlotTeamName( s ) );
		line.Int( "kills", st.kills );
		line.Int( "deaths", st.deaths );
		line.Int( "suicides", st.suicides );
		line.Int( "damage", st.damage );
		line.Int( "shots", st.shots );
		line.Int( "hitscan_shots", st.hitscanShots );
		line.Int( "hitscan_hits", st.hitscanHits );
		line.Int( "captures", st.captures );
		line.Float( "objective_seconds", st.objectiveSeconds );
		line.Float( "stuck_seconds", st.stuckSeconds );
		line.EndObject();
	}
	line.EndObject();

	m_roundTicks.Write( line, "tick_ms" );
	WriteFeatureCosts( line, m_roundCostStart );

	line.BeginObject( "feature_events" );
	const char *eventName = NULL;
	for ( int i = 0; ; ++i )
	{
		int count = TFBotFeatures::GetEventCount( i, &eventName );
		if ( count < 0 )
			break;
		if ( count > 0 )
			line.Int( eventName, count );
	}
	line.EndObject();
	TFBotFeatures::ResetEventCounts();

	Emit( line );
	filesystem->Flush( m_file );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::WriteMatchEnd( const char *reason )
{
	CTFBotJsonLine line( "match_end" );
	line.Str( "reason", reason );
	line.Int( "rounds_started", m_round );
	line.Int( "rounds_counted", m_roundsEnded );
	line.Float( "game_seconds", gpGlobals->curtime - m_matchStartGameTime );
	line.Float( "wall_seconds", Plat_FloatTime() - m_matchStartWallTime );
	m_matchTicks.Write( line, "tick_ms" );
	WriteFeatureCosts( line, m_matchCostStart );
	Emit( line );
	filesystem->Flush( m_file );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::FrameUpdatePreEntityThink()
{
	m_frameStart = Plat_FloatTime();
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::FrameUpdatePostEntityThink()
{
	if ( !IsActive() || m_quitting )
		return;

	// Cost of the game-side tick: nav mesh, NextBot updates, entity thinks.
	double ms = ( Plat_FloatTime() - m_frameStart ) * 1000.0;
	double budgetMs = gpGlobals->interval_per_tick * 1000.0;
	m_roundTicks.Add( ms, budgetMs );
	m_matchTicks.Add( ms, budgetMs );

	if ( !IsRoundRunning() )
		return;

	if ( gpGlobals->curtime >= m_nextSample )
	{
		float dt = gpGlobals->curtime - m_lastSample;
		if ( dt <= 0.0f || dt > 2.0f )
			dt = 1.0f;
		SampleSecond( dt );
		m_lastSample = gpGlobals->curtime;
		m_nextSample = gpGlobals->curtime + 1.0f;
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::SampleSecond( float dt )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsAlive() )
		{
			if ( m_stuck[i].active )
				EndStuck( i, "death" );
			continue;
		}

		int slot = TeamSlot( player->GetTeamNumber() );
		if ( slot < 0 )
			continue;

		if ( player->HasTheFlag() || player->GetControlPointStandingOn() )
			m_team[ slot ].objectiveSeconds += dt;

		CTFBot *bot = ToTFBot( player );
		if ( bot )
		{
			ILocomotion *loco = bot->GetLocomotionInterface();
			bool stuck = loco && loco->IsStuck();
			if ( stuck )
				m_team[ slot ].stuckSeconds += dt;
			UpdateStuck( i, bot, stuck );
		}
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::UpdateStuck( int index, CTFBot *bot, bool stuck )
{
	TFBotStuckEpisode_t &ep = m_stuck[ index ];
	if ( stuck && !ep.active )
	{
		ep.active = true;
		ep.start = gpGlobals->curtime;
		ep.pos = bot->GetAbsOrigin();
		ep.team = bot->GetTeamNumber();
		V_strncpy( ep.className, ClassName( bot ), sizeof( ep.className ) );
	}
	else if ( !stuck && ep.active )
	{
		EndStuck( index, "unstuck" );
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::EndStuck( int index, const char *how )
{
	TFBotStuckEpisode_t &ep = m_stuck[ index ];
	if ( !ep.active )
		return;
	ep.active = false;

	CTFBotJsonLine line( "stuck" );
	line.Int( "round", m_round );
	line.Float( "t_start", ep.start - m_matchStartGameTime );
	line.Float( "duration", gpGlobals->curtime - ep.start );
	line.Str( "team", TeamName( ep.team ) );
	line.Str( "class", ep.className );
	line.BeginArray( "pos" );
	line.ArrayFloat( ep.pos.x );
	line.ArrayFloat( ep.pos.y );
	line.ArrayFloat( ep.pos.z );
	line.EndArray();
	line.Str( "ended", how );
	Emit( line );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::EndAllStuck( const char *how )
{
	for ( int i = 1; i <= MAX_PLAYERS; ++i )
		EndStuck( i, how );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon )
{
	if ( !IsActive() || !player || !weapon || !IsRoundRunning() )
		return;

	int slot = TeamSlot( player->GetTeamNumber() );
	if ( slot < 0 )
		return;

	++m_team[ slot ].shots;
	if ( IsHitscanGun( weapon ) )
	{
		++m_team[ slot ].hitscanShots;
		int index = player->entindex();
		if ( index >= 1 && index <= MAX_PLAYERS )
			m_lastHitscanTick[ index ] = gpGlobals->tickcount;
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::OnPlayerHurt( IGameEvent *event )
{
	CTFPlayer *victim = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
	CTFPlayer *attacker = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
	if ( !victim || !attacker || victim == attacker )
		return;
	if ( victim->GetTeamNumber() == attacker->GetTeamNumber() )
		return;

	int slot = TeamSlot( attacker->GetTeamNumber() );
	if ( slot < 0 )
		return;

	m_team[ slot ].damage += event->GetInt( "damageamount" );

	int index = attacker->entindex();
	CTFWeaponBase *weapon = attacker->GetActiveTFWeapon();
	if ( index >= 1 && index <= MAX_PLAYERS &&
		 m_lastHitscanTick[ index ] == gpGlobals->tickcount &&
		 weapon && IsHitscanGun( weapon ) &&
		 weapon->GetWeaponID() == event->GetInt( "weaponid" ) )
	{
		++m_team[ slot ].hitscanHits;
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::OnPlayerDeath( IGameEvent *event )
{
	if ( event->GetInt( "death_flags" ) & TF_DEATH_FEIGN_DEATH )
		return;

	CTFPlayer *victim = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) );
	if ( !victim )
		return;

	if ( m_stuck[ victim->entindex() ].active )
		EndStuck( victim->entindex(), "death" );

	CTFPlayer *attacker = ToTFPlayer( UTIL_PlayerByUserId( event->GetInt( "attacker" ) ) );
	int victimSlot = TeamSlot( victim->GetTeamNumber() );
	if ( victimSlot >= 0 )
		++m_team[ victimSlot ].deaths;

	bool suicide = !attacker || attacker == victim;
	if ( suicide )
	{
		if ( victimSlot >= 0 )
			++m_team[ victimSlot ].suicides;
	}
	else
	{
		int attackerSlot = TeamSlot( attacker->GetTeamNumber() );
		if ( attackerSlot >= 0 && attacker->GetTeamNumber() != victim->GetTeamNumber() )
			++m_team[ attackerSlot ].kills;
	}

	CTFBotJsonLine line( "kill" );
	line.Int( "round", m_round );
	line.Float( "t", gpGlobals->curtime - m_matchStartGameTime );
	line.BeginObject( "victim" );
	line.Str( "team", TeamName( victim->GetTeamNumber() ) );
	line.Str( "class", ClassName( victim ) );
	line.Bool( "bot", victim->IsBot() );
	line.EndObject();
	if ( !suicide )
	{
		line.BeginObject( "attacker" );
		line.Str( "team", TeamName( attacker->GetTeamNumber() ) );
		line.Str( "class", ClassName( attacker ) );
		line.Bool( "bot", attacker->IsBot() );
		line.EndObject();
	}
	line.Bool( "suicide", suicide );
	line.Str( "weapon", event->GetString( "weapon" ) );
	line.Int( "weaponid", event->GetInt( "weaponid" ) );
	line.Int( "custom", event->GetInt( "customkill" ) );
	line.Bool( "assisted", event->GetInt( "assister" ) > 0 );
	Emit( line );
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::OnRoundEnded()
{
	EndAllStuck( "round_end" );

	for ( int s = 0; s < SLOT_COUNT; ++s )
		m_team[s].Reset();
	m_roundTicks.Reset();
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
		m_roundCostStart[f] = TFBotFeatures::GetCost( (ETFBotFeature)f );

	int target = tf_bot_ab_rounds.GetInt();
	if ( target > 0 && m_roundsEnded >= target && !m_quitting )
	{
		m_quitting = true;
		WriteMatchEnd( "ab_rounds" );
		CloseFile();
		Msg( "TFBot metrics: %d rounds done, quitting\n", m_roundsEnded );
		engine->ServerCommand( "quit\n" );
	}
}


//-----------------------------------------------------------------------------
void CTFBotMetrics::FireGameEvent( IGameEvent *event )
{
	if ( !IsActive() || m_quitting )
		return;

	const char *name = event->GetName();

	if ( FStrEq( name, "teamplay_round_start" ) )
	{
		++m_round;
		m_roundStart = gpGlobals->curtime;
		m_lastSample = gpGlobals->curtime;
		m_nextSample = gpGlobals->curtime + 1.0f;
		for ( int s = 0; s < SLOT_COUNT; ++s )
			m_team[s].Reset();
		m_roundTicks.Reset();
		for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
			m_roundCostStart[f] = TFBotFeatures::GetCost( (ETFBotFeature)f );
		TFBotFeatures::ResetEventCounts();
		WriteRoundStart();
	}
	else if ( FStrEq( name, "teamplay_round_win" ) )
	{
		WriteRoundEnd( event->GetInt( "team" ), event->GetInt( "winreason" ), false,
			event->GetBool( "full_round" ), event->GetBool( "was_sudden_death" ), event->GetFloat( "round_time" ) );
		++m_roundsEnded;
		OnRoundEnded();
	}
	else if ( FStrEq( name, "teamplay_round_stalemate" ) )
	{
		int reason = event->GetInt( "reason" );
		WriteRoundEnd( TEAM_UNASSIGNED, reason, true, true, false, gpGlobals->curtime - m_roundStart );
		// Only a real round-timer stalemate counts toward tf_bot_ab_rounds.
		if ( reason == STALEMATE_TIMER )
			++m_roundsEnded;
		OnRoundEnded();
	}
	else if ( !IsRoundRunning() )
	{
		// Ignore combat and objective events outside of a running round
		// (pre-round, humiliation).
		return;
	}
	else if ( FStrEq( name, "player_death" ) )
	{
		OnPlayerDeath( event );
	}
	else if ( FStrEq( name, "player_hurt" ) )
	{
		OnPlayerHurt( event );
	}
	else if ( FStrEq( name, "teamplay_point_captured" ) )
	{
		int team = event->GetInt( "team" );
		int slot = TeamSlot( team );
		if ( slot >= 0 )
			++m_team[ slot ].captures;

		CTFBotJsonLine line( "capture" );
		line.Int( "round", m_round );
		line.Float( "t", gpGlobals->curtime - m_matchStartGameTime );
		line.Str( "team", TeamName( team ) );
		line.Str( "what", "point" );
		line.Int( "cp", event->GetInt( "cp" ) );
		line.Str( "name", event->GetString( "cpname" ) );
		Emit( line );
	}
	else if ( FStrEq( name, "teamplay_flag_event" ) && event->GetInt( "eventtype" ) == TF_FLAGEVENT_CAPTURE )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( event->GetInt( "player" ) ) );
		int team = player ? player->GetTeamNumber() : TEAM_UNASSIGNED;
		int slot = TeamSlot( team );
		if ( slot >= 0 )
			++m_team[ slot ].captures;

		CTFBotJsonLine line( "capture" );
		line.Int( "round", m_round );
		line.Float( "t", gpGlobals->curtime - m_matchStartGameTime );
		line.Str( "team", TeamName( team ) );
		line.Str( "what", "flag" );
		Emit( line );
	}
}
