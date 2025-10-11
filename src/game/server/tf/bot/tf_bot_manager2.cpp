//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot Manager 2.0 implementation
//
//=============================================================================//

#include "cbase.h"
#include "tf_bot_manager2.h"
#include "tf_bot.h"
#include "tf_bot_attributes.h"
#include "tf_gamerules.h"
#include "tf_team.h"
#include "team.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
CTFBotManager2 *g_pBotManager2 = NULL;

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
ConVar tf_bot_pool_enabled( "tf_bot_pool_enabled", "1", FCVAR_NONE, "Enable bot pooling for performance" );
ConVar tf_bot_pool_max_size( "tf_bot_pool_max_size", "24", FCVAR_NONE, "Maximum number of bots to keep in pool" );
ConVar tf_bot_pool_cleanup_interval( "tf_bot_pool_cleanup_interval", "30.0", FCVAR_NONE, "How often to clean up stale pooled bots (seconds)" );
ConVar tf_bot_profile_dir( "tf_bot_profile_dir", "cfg/bots/", FCVAR_NONE, "Directory containing bot profile files" );
ConVar tf_bot_auto_difficulty( "tf_bot_auto_difficulty", "0", FCVAR_NONE, "Automatically adjust bot difficulty based on performance" );
ConVar tf_bot_diversity( "tf_bot_diversity", "1", FCVAR_NONE, "Randomize bot attributes for diversity" );
ConVar tf_bot_diversity_variance( "tf_bot_diversity_variance", "0.1", FCVAR_NONE, "Amount of randomization (0.0-1.0)" );

// External ConVars from old bot manager
extern ConVar tf_bot_difficulty;
extern ConVar tf_bot_quota;
extern ConVar tf_bot_quota_mode;
extern ConVar tf_bot_join_after_player;
extern ConVar tf_bot_auto_vacate;
extern ConVar tf_bot_offline_practice;

//-----------------------------------------------------------------------------
// Utility functions
//-----------------------------------------------------------------------------
static BotQuotaMode ParseQuotaMode( const char *mode )
{
	if ( FStrEq( mode, "fill" ) )
		return BOT_QUOTA_FILL;
	else if ( FStrEq( mode, "match" ) )
		return BOT_QUOTA_MATCH;
	else
		return BOT_QUOTA_NORMAL;
}

//-----------------------------------------------------------------------------
// CTFBotManager2 implementation
//-----------------------------------------------------------------------------
CTFBotManager2::CTFBotManager2()
	: NextBotManager()
{
	// Initialize member variables
	m_bPoolingEnabled = true;
	m_nMaxPoolSize = 24;
	m_flNextPoolCleanupTime = 0.0f;
	m_quotaMode = BOT_QUOTA_NORMAL;
	m_flNextQuotaCheckTime = 0.0f;
	m_nLastDesiredBotCount = 0;
	m_bOfflinePractice = false;
	m_bFirstUpdate = true;
	m_bInLevelShutdown = false;

	// Set global singleton
	g_pBotManager2 = this;

	Msg( "[BotManager2] Initialized with attribute system\n" );
}

CTFBotManager2::~CTFBotManager2()
{
	// Clean up all bots
	KickAllBots( false );
	ClearBotPool();
	ClearProfiles();
	m_eventListeners.Purge();

	g_pBotManager2 = NULL;
}

//-----------------------------------------------------------------------------
// NextBotManager overrides
//-----------------------------------------------------------------------------
void CTFBotManager2::Update()
{
	// Call base class first
	NextBotManager::Update();

	// First update initialization
	if ( m_bFirstUpdate )
	{
		m_bFirstUpdate = false;
		OnMapLoaded();
	}

	// Skip updates during level shutdown
	if ( m_bInLevelShutdown )
		return;

	// Periodic quota maintenance
	ThinkQuotaMaintenance();

	// Periodic bot pool cleanup
	ThinkBotPool();
}

void CTFBotManager2::OnMapLoaded( void )
{
	Msg( "[BotManager2] Map loaded, resetting bot system\n" );

	// Call base class
	NextBotManager::OnMapLoaded();

	// Load bot profiles from directory
	LoadBotProfiles( tf_bot_profile_dir.GetString() );

	// Reset timers
	m_flNextQuotaCheckTime = gpGlobals->curtime;
	m_flNextPoolCleanupTime = gpGlobals->curtime + tf_bot_pool_cleanup_interval.GetFloat();

	// Update quota mode
	m_quotaMode = ParseQuotaMode( tf_bot_quota_mode.GetString() );

	// Update pooling settings
	m_bPoolingEnabled = tf_bot_pool_enabled.GetBool();
	m_nMaxPoolSize = tf_bot_pool_max_size.GetInt();
}

void CTFBotManager2::OnRoundRestart( void )
{
	Msg( "[BotManager2] Round restarted\n" );

	// Call base class
	NextBotManager::OnRoundRestart();

	// Reset bot statistics for new round
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( pBot )
		{
			// Reset round-specific stats (but keep overall stats)
			// TODO: Add per-round vs overall stats tracking
		}
	}
}

//-----------------------------------------------------------------------------
// Bot lifecycle management
//-----------------------------------------------------------------------------
CTFBot* CTFBotManager2::CreateBot( const CTFBotAttributes &attribs )
{
	// Create bot entity
	CTFBot *pBot = (CTFBot *)CreateEntityByName( "tf_bot" );
	if ( !pBot )
	{
		Warning( "[BotManager2] Failed to create bot entity!\n" );
		return NULL;
	}

	// Apply attributes to bot
	// NOTE: CTFBot needs to be extended to store CTFBotAttributes
	// For now, we'll just set basic properties
	// TODO: Add CTFBot::SetAttributes( const CTFBotAttributes& ) method

	// Set bot name
	pBot->SetPlayerName( attribs.name );

	// Set difficulty (compatibility with old system)
	if ( attribs.IsEasy() )
		pBot->SetDifficulty( CTFBot::EASY );
	else if ( attribs.IsNormal() )
		pBot->SetDifficulty( CTFBot::NORMAL );
	else if ( attribs.IsHard() )
		pBot->SetDifficulty( CTFBot::HARD );
	else
		pBot->SetDifficulty( CTFBot::EXPERT );

	// Set attribute flags
	if ( attribs.HasAttribute( BOT_ATTR_QUOTA_MANAGED ) )
		pBot->SetAttribute( CTFBot::QUOTA_MANANGED );
	if ( attribs.HasAttribute( BOT_ATTR_IS_NPC ) )
		pBot->SetAttribute( CTFBot::IS_NPC );
	if ( attribs.HasAttribute( BOT_ATTR_ALWAYS_CRIT ) )
		pBot->SetAttribute( CTFBot::ALWAYS_CRIT );

	// Add to active bots list
	m_activeBots.AddToTail( pBot );

	// Fire event
	FireEvent_BotCreated( pBot );

	Msg( "[BotManager2] Created bot: %s (Skill: %.2f)\n", attribs.name, attribs.skill.aimAccuracy );

	return pBot;
}

void CTFBotManager2::DestroyBot( CTFBot *bot )
{
	if ( !bot )
		return;

	// Remove from active bots list
	m_activeBots.FindAndRemove( bot );

	// Fire event before destruction
	FireEvent_BotDestroyed( bot );

	// Remove from game
	UTIL_Remove( bot );

	Msg( "[BotManager2] Destroyed bot: %s\n", bot->GetPlayerName() );
}

void CTFBotManager2::KickBot( CTFBot *bot, const char *reason )
{
	if ( !bot )
		return;

	const char *kickReason = reason ? reason : "Kicked by bot manager";
	Msg( "[BotManager2] Kicking bot %s: %s\n", bot->GetPlayerName(), kickReason );

	// If pooling is enabled, return to pool instead of destroying
	if ( m_bPoolingEnabled && m_botPool.Count() < m_nMaxPoolSize )
	{
		ReturnBotToPool( bot );
	}
	else
	{
		// Kick from server using engine command
		engine->ServerCommand( UTIL_VarArgs( "kickid %d %s\n", bot->GetUserID(), kickReason ) );
	}
}

void CTFBotManager2::KickAllBots( bool quotaManagedOnly )
{
	Msg( "[BotManager2] Kicking all bots (quotaManaged=%d)\n", quotaManagedOnly );

	// Kick all active bots
	for ( int i = m_activeBots.Count() - 1; i >= 0; i-- )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( !pBot )
			continue;

		// Skip non-quota-managed bots if requested
		if ( quotaManagedOnly && !pBot->HasAttribute( CTFBot::QUOTA_MANANGED ) )
			continue;

		DestroyBot( pBot );
	}

	// Clear bot pool
	if ( !quotaManagedOnly )
	{
		ClearBotPool();
	}
}

void CTFBotManager2::KickBotFromTeam( int team )
{
	// Try to kick a dead bot first
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( !pBot || pBot->GetTeamNumber() != team )
			continue;

		if ( !pBot->IsAlive() && pBot->HasAttribute( CTFBot::QUOTA_MANANGED ) )
		{
			KickBot( pBot, "Making room for human player" );
			return;
		}
	}

	// No dead bots, kick any bot on team
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( !pBot || pBot->GetTeamNumber() != team )
			continue;

		if ( pBot->HasAttribute( CTFBot::QUOTA_MANANGED ) )
		{
			KickBot( pBot, "Making room for human player" );
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Bot pooling
//-----------------------------------------------------------------------------
CTFBot* CTFBotManager2::GetBotFromPool()
{
	if ( !m_bPoolingEnabled )
		return NULL;

	// Find available bot in pool
	FOR_EACH_VEC( m_botPool, i )
	{
		BotPoolEntry &entry = m_botPool[i];
		if ( !entry.isActive && entry.bot )
		{
			// Mark as active
			entry.isActive = true;
			m_activeBots.AddToTail( entry.bot );

			Msg( "[BotManager2] Reusing bot from pool: %s\n", entry.bot->GetPlayerName() );
			return entry.bot;
		}
	}

	return NULL; // No bots available in pool
}

void CTFBotManager2::ReturnBotToPool( CTFBot *bot )
{
	if ( !bot || !m_bPoolingEnabled )
		return;

	// Check if pool is full
	if ( GetPooledBotCount() >= m_nMaxPoolSize )
	{
		Msg( "[BotManager2] Pool full, destroying bot %s\n", bot->GetPlayerName() );
		DestroyBot( bot );
		return;
	}

	// Remove from active list
	m_activeBots.FindAndRemove( bot );

	// Add to pool or update existing entry
	bool found = false;
	FOR_EACH_VEC( m_botPool, i )
	{
		if ( m_botPool[i].bot == bot )
		{
			m_botPool[i].isActive = false;
			m_botPool[i].pooledTime = gpGlobals->curtime;
			found = true;
			break;
		}
	}

	if ( !found )
	{
		// Add new entry to pool
		BotPoolEntry entry;
		entry.bot = bot;
		entry.pooledTime = gpGlobals->curtime;
		entry.isActive = false;
		m_botPool.AddToTail( entry );
	}

	// Reset bot state (remove from team, clear objectives, etc.)
	bot->ChangeTeam( TEAM_SPECTATOR, false, true );

	Msg( "[BotManager2] Returned bot %s to pool\n", bot->GetPlayerName() );
}

void CTFBotManager2::ClearBotPool()
{
	Msg( "[BotManager2] Clearing bot pool (%d bots)\n", m_botPool.Count() );

	FOR_EACH_VEC( m_botPool, i )
	{
		BotPoolEntry &entry = m_botPool[i];
		if ( entry.bot && !entry.isActive )
		{
			UTIL_Remove( entry.bot );
		}
	}

	m_botPool.Purge();
}

int CTFBotManager2::GetPooledBotCount() const
{
	int count = 0;
	FOR_EACH_VEC( m_botPool, i )
	{
		if ( !m_botPool[i].isActive )
			count++;
	}
	return count;
}

//-----------------------------------------------------------------------------
// Quota management
//-----------------------------------------------------------------------------
void CTFBotManager2::MaintainBotQuota()
{
	// Don't maintain quota during these conditions
	if ( TheNavMesh->IsGenerating() )
		return;

	if ( g_fGameOver )
		return;

	if ( !TFGameRules() || TFGameRules()->IsInTraining() )
		return;

	// Wait for listen server host to join
	if ( !engine->IsDedicatedServer() )
	{
		CBasePlayer *pPlayer = UTIL_GetListenServerHost();
		if ( !pPlayer )
			return;
	}

	// Check if should join after first human player
	if ( ShouldJoinAfterPlayer() )
	{
		// Count human players
		int humanCount = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
			if ( pPlayer && !pPlayer->IsFakeClient() && pPlayer->IsConnected() )
			{
				humanCount++;
			}
		}

		if ( humanCount == 0 )
			return; // Wait for first human
	}

	// Auto-vacate for humans
	AutoVacateForHumans();

	// Calculate desired bot count
	int desiredBotCount = GetDesiredBotCount();
	int currentBotCount = GetBotCount();

	// Add or remove bots to match quota
	if ( currentBotCount < desiredBotCount )
	{
		// Need to add bots
		int toAdd = desiredBotCount - currentBotCount;
		Msg( "[BotManager2] Adding %d bots (current: %d, desired: %d)\n", toAdd, currentBotCount, desiredBotCount );

		for ( int i = 0; i < toAdd; i++ )
		{
			// Create bot with default attributes
			CTFBotAttributes attribs = CreateAttributesFromDifficulty( (BotDifficultyPreset)tf_bot_difficulty.GetInt() );

			// Randomize for diversity
			if ( tf_bot_diversity.GetBool() )
			{
				attribs.Randomize( tf_bot_diversity_variance.GetFloat() );
			}

			// Generate name
			V_sprintf_safe( attribs.name, "Bot%02d", currentBotCount + i + 1 );

			CTFBot *pBot = CreateBot( attribs );
			if ( pBot )
			{
				// Assign team and class
				int team = DetermineTeamAssignment( pBot );
				pBot->ChangeTeam( team, false, true );

				int playerClass = DetermineClassAssignment( pBot, team );
				pBot->HandleCommand_JoinClass( GetPlayerClassName( playerClass ) );

				// Spawn bot
				pBot->ForceRespawn();

				FireEvent_BotSpawned( pBot );
			}
		}
	}
	else if ( currentBotCount > desiredBotCount )
	{
		// Need to remove bots
		int toRemove = currentBotCount - desiredBotCount;
		Msg( "[BotManager2] Removing %d bots (current: %d, desired: %d)\n", toRemove, currentBotCount, desiredBotCount );

		for ( int i = 0; i < toRemove; i++ )
		{
			// Kick from team with most bots
			int redBots = CountBotsOnTeam( TF_TEAM_RED );
			int blueBots = CountBotsOnTeam( TF_TEAM_BLUE );

			int teamToKick = ( redBots > blueBots ) ? TF_TEAM_RED : TF_TEAM_BLUE;
			KickBotFromTeam( teamToKick );
		}
	}
}

void CTFBotManager2::SetQuotaMode( BotQuotaMode mode )
{
	if ( mode != m_quotaMode )
	{
		Msg( "[BotManager2] Quota mode changed: %d -> %d\n", m_quotaMode, mode );
		m_quotaMode = mode;
		m_flNextQuotaCheckTime = gpGlobals->curtime; // Force immediate check
	}
}

int CTFBotManager2::GetDesiredBotCount() const
{
	int quota = tf_bot_quota.GetInt();

	switch ( m_quotaMode )
	{
	case BOT_QUOTA_NORMAL:
		// Exact number specified by quota
		return quota;

	case BOT_QUOTA_FILL:
		{
			// Fill to quota total players (humans + bots)
			int humanCount = 0;
			for ( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
				if ( pPlayer && !pPlayer->IsFakeClient() && pPlayer->IsConnected() )
				{
					if ( pPlayer->GetTeamNumber() == TF_TEAM_RED || pPlayer->GetTeamNumber() == TF_TEAM_BLUE )
						humanCount++;
				}
			}

			int desiredBots = quota - humanCount;
			return MAX( 0, desiredBots );
		}

	case BOT_QUOTA_MATCH:
		{
			// Maintain ratio (e.g., 1 human = 2 bots if quota is 2)
			int humanCount = 0;
			for ( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
				if ( pPlayer && !pPlayer->IsFakeClient() && pPlayer->IsConnected() )
				{
					if ( pPlayer->GetTeamNumber() == TF_TEAM_RED || pPlayer->GetTeamNumber() == TF_TEAM_BLUE )
						humanCount++;
				}
			}

			return humanCount * quota;
		}
	}

	return quota;
}

bool CTFBotManager2::ShouldRespawnBot( CTFBot *bot ) const
{
	if ( !bot )
		return false;

	// Check if bot is still within quota
	int currentBotCount = GetBotCount();
	int desiredBotCount = GetDesiredBotCount();

	return ( currentBotCount <= desiredBotCount );
}

//-----------------------------------------------------------------------------
// Bot queries
//-----------------------------------------------------------------------------
CTFBot* CTFBotManager2::FindBotByName( const char *name ) const
{
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( pBot && FStrEq( pBot->GetPlayerName(), name ) )
			return pBot;
	}
	return NULL;
}

CTFBot* CTFBotManager2::FindBotByUserID( int userid ) const
{
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( pBot && pBot->GetUserID() == userid )
			return pBot;
	}
	return NULL;
}

void CTFBotManager2::GetAllBots( CUtlVector<CTFBot*> &bots ) const
{
	bots.RemoveAll();
	FOR_EACH_VEC( m_activeBots, i )
	{
		if ( m_activeBots[i] )
			bots.AddToTail( m_activeBots[i] );
	}
}

int CTFBotManager2::GetBotCount( int team ) const
{
	int count = 0;
	FOR_EACH_VEC( m_activeBots, i )
	{
		CTFBot *pBot = m_activeBots[i];
		if ( !pBot )
			continue;

		if ( team == TEAM_ANY || pBot->GetTeamNumber() == team )
			count++;
	}
	return count;
}

//-----------------------------------------------------------------------------
// Team/class assignment
//-----------------------------------------------------------------------------
int CTFBotManager2::DetermineTeamAssignment( CTFBot *bot ) const
{
	if ( !bot )
		return TEAM_UNASSIGNED;

	// TODO: Check bot's preferred team from attributes

	// Balance teams
	int redCount = CountBotsOnTeam( TF_TEAM_RED ) + CountHumansOnTeam( TF_TEAM_RED );
	int blueCount = CountBotsOnTeam( TF_TEAM_BLUE ) + CountHumansOnTeam( TF_TEAM_BLUE );

	if ( redCount < blueCount )
		return TF_TEAM_RED;
	else if ( blueCount < redCount )
		return TF_TEAM_BLUE;

	// Equal, pick random
	return RandomInt( 0, 1 ) ? TF_TEAM_RED : TF_TEAM_BLUE;
}

int CTFBotManager2::DetermineClassAssignment( CTFBot *bot, int team ) const
{
	if ( !bot )
		return TF_CLASS_SCOUT;

	// TODO: Check bot's preferred class from attributes
	// TODO: Auto-balance classes on team

	// For now, pick random class
	return RandomInt( TF_CLASS_SCOUT, TF_CLASS_ENGINEER );
}

bool CTFBotManager2::IsTeamFull( int team ) const
{
	// TODO: Implement team size limits
	return false;
}

//-----------------------------------------------------------------------------
// Event system
//-----------------------------------------------------------------------------
void CTFBotManager2::RegisterEventListener( IBotEventListener *listener )
{
	if ( !listener )
		return;

	if ( m_eventListeners.Find( listener ) == m_eventListeners.InvalidIndex() )
	{
		m_eventListeners.AddToTail( listener );
	}
}

void CTFBotManager2::UnregisterEventListener( IBotEventListener *listener )
{
	m_eventListeners.FindAndRemove( listener );
}

void CTFBotManager2::FireEvent_BotCreated( CTFBot *bot )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotCreated( bot );
	}
}

void CTFBotManager2::FireEvent_BotDestroyed( CTFBot *bot )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotDestroyed( bot );
	}
}

void CTFBotManager2::FireEvent_BotSpawned( CTFBot *bot )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotSpawned( bot );
	}
}

void CTFBotManager2::FireEvent_BotKilled( CTFBot *bot, const CTakeDamageInfo &info )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotKilled( bot, info );
	}
}

void CTFBotManager2::FireEvent_BotTeamChanged( CTFBot *bot, int oldTeam, int newTeam )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotTeamChanged( bot, oldTeam, newTeam );
	}
}

void CTFBotManager2::FireEvent_BotClassChanged( CTFBot *bot, int oldClass, int newClass )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotClassChanged( bot, oldClass, newClass );
	}
}

void CTFBotManager2::FireEvent_BotAttributesChanged( CTFBot *bot )
{
	FOR_EACH_VEC( m_eventListeners, i )
	{
		m_eventListeners[i]->OnBotAttributesChanged( bot );
	}
}

//-----------------------------------------------------------------------------
// Profile management
//-----------------------------------------------------------------------------
bool CTFBotManager2::LoadBotProfiles( const char *directory )
{
	// TODO: Scan directory for .txt files, load each as KeyValues
	Msg( "[BotManager2] LoadBotProfiles from '%s' - TODO\n", directory );
	return true;
}

bool CTFBotManager2::LoadBotProfile( const char *filename )
{
	CTFBotAttributes attribs;
	if ( LoadBotProfileFromFile( filename, attribs ) )
	{
		m_botProfiles.AddToTail( attribs );
		Msg( "[BotManager2] Loaded bot profile: %s\n", attribs.name );
		return true;
	}
	return false;
}

const CTFBotAttributes* CTFBotManager2::GetProfileByName( const char *name ) const
{
	FOR_EACH_VEC( m_botProfiles, i )
	{
		if ( FStrEq( m_botProfiles[i].name, name ) )
			return &m_botProfiles[i];
	}
	return NULL;
}

void CTFBotManager2::ClearProfiles()
{
	m_botProfiles.Purge();
}

//-----------------------------------------------------------------------------
// Internal helpers
//-----------------------------------------------------------------------------
void CTFBotManager2::ThinkQuotaMaintenance()
{
	if ( gpGlobals->curtime < m_flNextQuotaCheckTime )
		return;

	// Check quota every 0.25 seconds
	m_flNextQuotaCheckTime = gpGlobals->curtime + 0.25f;

	// Update quota mode from ConVar
	m_quotaMode = ParseQuotaMode( tf_bot_quota_mode.GetString() );

	// Maintain quota
	MaintainBotQuota();
}

void CTFBotManager2::ThinkBotPool()
{
	if ( !m_bPoolingEnabled )
		return;

	if ( gpGlobals->curtime < m_flNextPoolCleanupTime )
		return;

	m_flNextPoolCleanupTime = gpGlobals->curtime + tf_bot_pool_cleanup_interval.GetFloat();

	RemoveStaleBotsFromPool();
}

void CTFBotManager2::RemoveStaleBotsFromPool()
{
	// Remove bots that have been in pool too long (5 minutes)
	const float MAX_POOL_TIME = 300.0f;

	for ( int i = m_botPool.Count() - 1; i >= 0; i-- )
	{
		BotPoolEntry &entry = m_botPool[i];
		if ( !entry.isActive && entry.bot )
		{
			float timeInPool = gpGlobals->curtime - entry.pooledTime;
			if ( timeInPool > MAX_POOL_TIME )
			{
				Msg( "[BotManager2] Removing stale bot from pool: %s\n", entry.bot->GetPlayerName() );
				UTIL_Remove( entry.bot );
				m_botPool.Remove( i );
			}
		}
	}
}

int CTFBotManager2::CountHumansOnTeam( int team ) const
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer && !pPlayer->IsFakeClient() && pPlayer->IsConnected() )
		{
			if ( team == TEAM_ANY || pPlayer->GetTeamNumber() == team )
				count++;
		}
	}
	return count;
}

int CTFBotManager2::CountBotsOnTeam( int team ) const
{
	return GetBotCount( team );
}

bool CTFBotManager2::ShouldJoinAfterPlayer() const
{
	return tf_bot_join_after_player.GetBool();
}

void CTFBotManager2::AutoVacateForHumans()
{
	if ( !tf_bot_auto_vacate.GetBool() )
		return;

	// TODO: Implement auto-vacate logic
}

//-----------------------------------------------------------------------------
// Debug
//-----------------------------------------------------------------------------
void CTFBotManager2::DebugPrintBotInfo() const
{
	Msg( "=== Bot Manager 2 Info ===\n" );
	Msg( "  Active Bots: %d\n", m_activeBots.Count() );
	Msg( "  Pooled Bots: %d\n", GetPooledBotCount() );
	Msg( "  Quota Mode: %d\n", m_quotaMode );
	Msg( "  Desired Bot Count: %d\n", GetDesiredBotCount() );
	Msg( "  Profiles Loaded: %d\n", m_botProfiles.Count() );
	Msg( "  Event Listeners: %d\n", m_eventListeners.Count() );
}

void CTFBotManager2::DebugPrintPoolInfo() const
{
	Msg( "=== Bot Pool Info ===\n" );
	Msg( "  Pool Size: %d/%d\n", m_botPool.Count(), m_nMaxPoolSize );
	Msg( "  Pooling Enabled: %d\n", m_bPoolingEnabled );

	FOR_EACH_VEC( m_botPool, i )
	{
		const BotPoolEntry &entry = m_botPool[i];
		if ( entry.bot )
		{
			float timeInPool = gpGlobals->curtime - entry.pooledTime;
			Msg( "    [%d] %s - Active: %d, Time: %.1fs\n",
				i, entry.bot->GetPlayerName(), entry.isActive, timeInPool );
		}
	}
}

void CTFBotManager2::DebugPrintQuotaInfo() const
{
	Msg( "=== Quota Info ===\n" );
	Msg( "  Mode: %d (0=normal, 1=fill, 2=match)\n", m_quotaMode );
	Msg( "  Quota: %d\n", tf_bot_quota.GetInt() );
	Msg( "  Desired: %d\n", GetDesiredBotCount() );
	Msg( "  Current: %d\n", GetBotCount() );
	Msg( "  RED: %d bots, %d humans\n",
		CountBotsOnTeam( TF_TEAM_RED ), CountHumansOnTeam( TF_TEAM_RED ) );
	Msg( "  BLU: %d bots, %d humans\n",
		CountBotsOnTeam( TF_TEAM_BLUE ), CountHumansOnTeam( TF_TEAM_BLUE ) );
}

//-----------------------------------------------------------------------------
// Compatibility
//-----------------------------------------------------------------------------
bool CTFBotManager2::IsAllBotTeam( int team ) const
{
	return ( CountBotsOnTeam( team ) > 0 && CountHumansOnTeam( team ) == 0 );
}

//-----------------------------------------------------------------------------
// Console commands for debugging
//-----------------------------------------------------------------------------
CON_COMMAND( tf_bot_manager2_info, "Print bot manager 2 info" )
{
	if ( g_pBotManager2 )
	{
		g_pBotManager2->DebugPrintBotInfo();
		g_pBotManager2->DebugPrintQuotaInfo();
		g_pBotManager2->DebugPrintPoolInfo();
	}
	else
	{
		Msg( "Bot Manager 2 not initialized\n" );
	}
}

CON_COMMAND( tf_bot_manager2_test_pool, "Test bot pooling system" )
{
	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	// Create test bot
	CTFBotAttributes attribs = CreateAttributesFromDifficulty( BOT_DIFFICULTY_NORMAL );
	V_strncpy( attribs.name, "PoolTest", sizeof( attribs.name ) );

	CTFBot *bot = g_pBotManager2->CreateBot( attribs );
	if ( bot )
	{
		Msg( "Created test bot, returning to pool...\n" );
		g_pBotManager2->ReturnBotToPool( bot );

		Msg( "Retrieving from pool...\n" );
		CTFBot *bot2 = g_pBotManager2->GetBotFromPool();

		if ( bot2 == bot )
		{
			Msg( "SUCCESS: Bot pooling works! Same bot retrieved.\n" );
		}
		else
		{
			Msg( "FAIL: Different bot retrieved\n" );
		}
	}
}

CON_COMMAND_F( tf_bot_create2, "Create bot with new manager (usage: tf_bot_create2 <difficulty> [class] [team])", FCVAR_CHEAT )
{
	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	// Parse difficulty (0-3 or easy/normal/hard/expert)
	BotDifficultyPreset difficulty = BOT_DIFFICULTY_NORMAL;
	if ( args.ArgC() > 1 )
	{
		const char *diffStr = args.Arg( 1 );
		if ( FStrEq( diffStr, "easy" ) || FStrEq( diffStr, "0" ) )
			difficulty = BOT_DIFFICULTY_EASY;
		else if ( FStrEq( diffStr, "normal" ) || FStrEq( diffStr, "1" ) )
			difficulty = BOT_DIFFICULTY_NORMAL;
		else if ( FStrEq( diffStr, "hard" ) || FStrEq( diffStr, "2" ) )
			difficulty = BOT_DIFFICULTY_HARD;
		else if ( FStrEq( diffStr, "expert" ) || FStrEq( diffStr, "3" ) )
			difficulty = BOT_DIFFICULTY_EXPERT;
	}

	// Create attributes
	CTFBotAttributes attribs = CreateAttributesFromDifficulty( difficulty );

	// Parse optional class
	if ( args.ArgC() > 2 )
	{
		const char *classStr = args.Arg( 2 );
		int classIndex = GetClassIndexFromString( classStr );
		if ( classIndex != TF_CLASS_UNDEFINED )
			attribs.preferredClass = classIndex;
	}

	// Parse optional team
	if ( args.ArgC() > 3 )
	{
		const char *teamStr = args.Arg( 3 );
		if ( FStrEq( teamStr, "red" ) )
			attribs.preferredTeam = TF_TEAM_RED;
		else if ( FStrEq( teamStr, "blue" ) )
			attribs.preferredTeam = TF_TEAM_BLUE;
	}

	// Apply diversity if enabled
	if ( tf_bot_diversity.GetBool() )
		attribs.Randomize( tf_bot_diversity_variance.GetFloat() );

	// Create bot
	CTFBot *bot = g_pBotManager2->CreateBot( attribs );
	if ( bot )
	{
		Msg( "Created bot: %s (difficulty: %d, class: %s, team: %d)\n",
			bot->GetPlayerName(), difficulty,
			GetPlayerClassName( attribs.preferredClass ), attribs.preferredTeam );
	}
	else
	{
		Msg( "Failed to create bot\n" );
	}
}

CON_COMMAND( tf_bot_attributes, "Print attributes for a bot (usage: tf_bot_attributes <name>)" )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_attributes <name>\n" );
		return;
	}

	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	const char *botName = args.Arg( 1 );
	CTFBot *bot = g_pBotManager2->FindBotByName( botName );

	if ( !bot )
	{
		Msg( "Bot '%s' not found\n", botName );
		return;
	}

	// TODO: Add GetAttributes() method to CTFBot
	Msg( "Bot found: %s\n", bot->GetPlayerName() );
	Msg( "  Team: %d\n", bot->GetTeamNumber() );
	Msg( "  Class: %s\n", bot->GetPlayerClass()->GetName() );
	Msg( "  Health: %d/%d\n", bot->GetHealth(), bot->GetMaxHealth() );
	Msg( "  UserID: %d\n", bot->GetUserID() );
}

CON_COMMAND_F( tf_bot_clear_pool, "Clear the bot pool", FCVAR_CHEAT )
{
	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	int poolCount = g_pBotManager2->GetPooledBotCount();
	g_pBotManager2->ClearBotPool();
	Msg( "Cleared bot pool (%d bots removed)\n", poolCount );
}

CON_COMMAND_F( tf_bot_kick_all2, "Kick all bots managed by bot manager 2", FCVAR_CHEAT )
{
	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	int count = g_pBotManager2->GetActiveBotCount();
	g_pBotManager2->KickAllBots( true );
	Msg( "Kicked %d bots\n", count );
}

CON_COMMAND( tf_bot_load_profile, "Load bot profile and spawn (usage: tf_bot_load_profile <filename>)" )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_load_profile <filename>\n" );
		return;
	}

	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	const char *filename = args.Arg( 1 );

	// Load profile
	if ( g_pBotManager2->LoadBotProfile( filename ) )
	{
		const CTFBotAttributes *profile = g_pBotManager2->GetProfileByName( filename );
		if ( profile )
		{
			// Spawn bot with profile
			CTFBotAttributes attribs;
			attribs.CopyFrom( *profile );

			CTFBot *bot = g_pBotManager2->CreateBot( attribs );
			if ( bot )
			{
				Msg( "Spawned bot from profile: %s\n", profile->name );
			}
			else
			{
				Msg( "Failed to spawn bot\n" );
			}
		}
	}
	else
	{
		Msg( "Failed to load profile: %s\n", filename );
	}
}

CON_COMMAND( tf_bot_list_profiles, "List loaded bot profiles" )
{
	if ( !g_pBotManager2 )
	{
		Msg( "Bot Manager 2 not initialized\n" );
		return;
	}

	int count = g_pBotManager2->GetProfileCount();
	Msg( "Loaded profiles: %d\n", count );

	// TODO: Add GetProfileByIndex() to iterate and print profile names
}
