//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot Manager 2.0 - Improved bot lifecycle management with pooling
//
// $NoKeywords: $
//=============================================================================//

#ifndef TF_BOT_MANAGER2_H
#define TF_BOT_MANAGER2_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotManager.h"
#include "tf_bot_attributes.h"
#include "utlvector.h"

class CTFBot;
class CTFPlayer;

//-----------------------------------------------------------------------------
// Bot quota modes
//-----------------------------------------------------------------------------
enum BotQuotaMode
{
	BOT_QUOTA_NORMAL = 0,		// Maintain exact number of bots specified by tf_bot_quota
	BOT_QUOTA_FILL,				// Fill server to tf_bot_quota total players (bots + humans)
	BOT_QUOTA_MATCH,			// Maintain ratio of bots:humans (e.g., 1:2 = 1 human, 2 bots)
};

//-----------------------------------------------------------------------------
// Bot event listener interface - allows other systems to subscribe to bot events
//-----------------------------------------------------------------------------
class IBotEventListener
{
public:
	virtual ~IBotEventListener() {}

	// Lifecycle events
	virtual void OnBotCreated( CTFBot *bot ) {}
	virtual void OnBotDestroyed( CTFBot *bot ) {}
	virtual void OnBotSpawned( CTFBot *bot ) {}
	virtual void OnBotKilled( CTFBot *bot, const CTakeDamageInfo &info ) {}

	// State change events
	virtual void OnBotTeamChanged( CTFBot *bot, int oldTeam, int newTeam ) {}
	virtual void OnBotClassChanged( CTFBot *bot, int oldClass, int newClass ) {}
	virtual void OnBotAttributesChanged( CTFBot *bot ) {}

	// Gameplay events
	virtual void OnBotDamagedPlayer( CTFBot *bot, CBaseEntity *victim, const CTakeDamageInfo &info ) {}
	virtual void OnBotHealedPlayer( CTFBot *bot, CBaseEntity *patient, float amount ) {}
	virtual void OnBotCapturedPoint( CTFBot *bot, int pointIndex ) {}
	virtual void OnBotCapturedFlag( CTFBot *bot ) {}
};

//-----------------------------------------------------------------------------
// Bot pool entry - tracks pooled bots
//-----------------------------------------------------------------------------
struct BotPoolEntry
{
	CTFBot *bot;
	float pooledTime;		// When bot was returned to pool
	bool isActive;			// Currently in use

	BotPoolEntry()
	{
		bot = NULL;
		pooledTime = 0.0f;
		isActive = false;
	}
};

//-----------------------------------------------------------------------------
// CTFBotManager2 - Improved bot manager with pooling and attribute system
//-----------------------------------------------------------------------------
class CTFBotManager2 : public NextBotManager
{
public:
	CTFBotManager2();
	virtual ~CTFBotManager2();

	//-------------------------------------------------------------------------
	// NextBotManager overrides
	//-------------------------------------------------------------------------
	virtual void Update() OVERRIDE;
	virtual void OnMapLoaded( void ) OVERRIDE;
	virtual void OnRoundRestart( void ) OVERRIDE;

	//-------------------------------------------------------------------------
	// Bot lifecycle management
	//-------------------------------------------------------------------------
	CTFBot* CreateBot( const CTFBotAttributes &attribs );		// Create new bot with attributes
	void DestroyBot( CTFBot *bot );								// Permanently destroy bot
	void KickBot( CTFBot *bot, const char *reason = NULL );		// Kick bot from server
	void KickAllBots( bool quotaManagedOnly = true );			// Kick all bots
	void KickBotFromTeam( int team );							// Kick one bot from specified team

	//-------------------------------------------------------------------------
	// Bot pooling (performance optimization)
	//-------------------------------------------------------------------------
	void EnableBotPooling( bool enable ) { m_bPoolingEnabled = enable; }
	bool IsPoolingEnabled() const { return m_bPoolingEnabled; }
	void SetMaxPoolSize( int size ) { m_nMaxPoolSize = size; }
	int GetMaxPoolSize() const { return m_nMaxPoolSize; }

	CTFBot* GetBotFromPool();									// Get bot from pool (or create new)
	void ReturnBotToPool( CTFBot *bot );						// Return bot to pool for reuse
	void ClearBotPool();										// Destroy all pooled bots

	//-------------------------------------------------------------------------
	// Quota management
	//-------------------------------------------------------------------------
	void MaintainBotQuota();									// Update bot count based on quota settings
	void SetQuotaMode( BotQuotaMode mode );						// Set quota mode
	BotQuotaMode GetQuotaMode() const { return m_quotaMode; }
	int GetDesiredBotCount() const;								// Calculate how many bots should exist
	bool ShouldRespawnBot( CTFBot *bot ) const;					// Check if bot should respawn

	//-------------------------------------------------------------------------
	// Bot queries
	//-------------------------------------------------------------------------
	CTFBot* FindBotByName( const char *name ) const;
	CTFBot* FindBotByUserID( int userid ) const;
	void GetAllBots( CUtlVector<CTFBot*> &bots ) const;		// Get all active bots
	int GetBotCount( int team = TEAM_ANY ) const;				// Count bots on team
	int GetActiveBotCount() const { return m_activeBots.Count(); }
	int GetPooledBotCount() const;

	//-------------------------------------------------------------------------
	// Team/class assignment
	//-------------------------------------------------------------------------
	int DetermineTeamAssignment( CTFBot *bot ) const;			// Auto-assign bot to team
	int DetermineClassAssignment( CTFBot *bot, int team ) const;// Auto-assign bot class
	bool IsTeamFull( int team ) const;							// Check if team is full

	//-------------------------------------------------------------------------
	// Event system
	//-------------------------------------------------------------------------
	void RegisterEventListener( IBotEventListener *listener );
	void UnregisterEventListener( IBotEventListener *listener );

	// Event firing functions (called by bots or game events)
	void FireEvent_BotCreated( CTFBot *bot );
	void FireEvent_BotDestroyed( CTFBot *bot );
	void FireEvent_BotSpawned( CTFBot *bot );
	void FireEvent_BotKilled( CTFBot *bot, const CTakeDamageInfo &info );
	void FireEvent_BotTeamChanged( CTFBot *bot, int oldTeam, int newTeam );
	void FireEvent_BotClassChanged( CTFBot *bot, int oldClass, int newClass );
	void FireEvent_BotAttributesChanged( CTFBot *bot );

	//-------------------------------------------------------------------------
	// Profile management
	//-------------------------------------------------------------------------
	bool LoadBotProfiles( const char *directory );				// Load all bot profiles from directory
	bool LoadBotProfile( const char *filename );				// Load single bot profile
	const CTFBotAttributes* GetProfileByName( const char *name ) const;
	int GetProfileCount() const { return m_botProfiles.Count(); }
	void ClearProfiles();

	//-------------------------------------------------------------------------
	// Statistics
	//-------------------------------------------------------------------------
	void UpdateBotStatistics( CTFBot *bot );					// Update bot performance stats
	void SaveBotStatistics( const char *filename );				// Save all bot stats to file
	void ResetBotStatistics();									// Reset all bot stats

	//-------------------------------------------------------------------------
	// Debug/utility
	//-------------------------------------------------------------------------
	void DebugPrintBotInfo() const;								// Print all bot info to console
	void DebugPrintPoolInfo() const;							// Print bot pool status
	void DebugPrintQuotaInfo() const;							// Print quota status

	//-------------------------------------------------------------------------
	// Compatibility with old CTFBotManager
	//-------------------------------------------------------------------------
	bool IsAllBotTeam( int team ) const;						// Check if team is all bots
	bool IsInOfflinePractice() const { return m_bOfflinePractice; }
	void SetOfflinePractice( bool value ) { m_bOfflinePractice = value; }

private:
	//-------------------------------------------------------------------------
	// Internal helpers
	//-------------------------------------------------------------------------
	void ThinkQuotaMaintenance();								// Periodic quota check
	void ThinkBotPool();										// Periodic pool cleanup
	void RemoveStaleBotsFromPool();								// Remove old pooled bots
	int CountHumansOnTeam( int team ) const;					// Count human players on team
	int CountBotsOnTeam( int team ) const;						// Count bots on team
	bool ShouldJoinAfterPlayer() const;							// Check if bots should wait for human
	void AutoVacateForHumans();									// Kick bots to make room for humans

	//-------------------------------------------------------------------------
	// Member variables
	//-------------------------------------------------------------------------

	// Bot pooling
	CUtlVector<BotPoolEntry> m_botPool;							// Pool of reusable bots
	bool m_bPoolingEnabled;										// Is pooling enabled?
	int m_nMaxPoolSize;											// Max bots in pool
	float m_flNextPoolCleanupTime;								// Next time to clean up pool

	// Active bots
	CUtlVector<CTFBot*> m_activeBots;							// Currently active bots

	// Quota management
	BotQuotaMode m_quotaMode;									// Current quota mode
	float m_flNextQuotaCheckTime;								// Next time to check quota
	int m_nLastDesiredBotCount;									// Cache last desired count

	// Bot profiles
	CUtlVector<CTFBotAttributes> m_botProfiles;					// Loaded bot profiles

	// Event listeners
	CUtlVector<IBotEventListener*> m_eventListeners;			// Registered event listeners

	// Flags
	bool m_bOfflinePractice;									// Offline practice mode
	bool m_bFirstUpdate;										// First update after map load
	bool m_bInLevelShutdown;									// Currently shutting down
};

//-----------------------------------------------------------------------------
// Singleton accessor
//-----------------------------------------------------------------------------
extern CTFBotManager2 *g_pBotManager2;

inline CTFBotManager2& TheTFBots2( void )
{
	return *g_pBotManager2;
}

//-----------------------------------------------------------------------------
// ConVars for new bot manager
//-----------------------------------------------------------------------------

// These will be defined in tf_bot_manager2.cpp:
// tf_bot_quota                 // Number of bots (existing)
// tf_bot_quota_mode            // "normal", "fill", "match" (existing)
// tf_bot_difficulty            // 0-3: easy/normal/hard/expert (existing)
//
// NEW ConVars:
// tf_bot_pool_enabled          // Enable bot pooling (default: 1)
// tf_bot_pool_max_size         // Max pool size (default: 24)
// tf_bot_pool_cleanup_interval // Pool cleanup interval in seconds (default: 30.0)
// tf_bot_profile_dir           // Directory for bot profiles (default: "cfg/bots/")
// tf_bot_auto_difficulty       // Auto-adjust difficulty based on performance (default: 0)
// tf_bot_diversity             // Randomize bot attributes (default: 1)
// tf_bot_diversity_variance    // Variance amount for randomization (default: 0.1)

#endif // TF_BOT_MANAGER2_H
