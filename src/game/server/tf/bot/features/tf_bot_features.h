//========= TFBot overhaul: per-bot feature flags ============================//
//
// Every overhaul improvement is registered as a feature. Each team picks the
// features its bots spawn with (tf_bot_features_red / tf_bot_features_blu), and
// a bot snapshots its team's set when it spawns. This lets new and stock
// behaviour run side by side in one match for A/B testing, and lets a single
// feature be switched off to measure what it is worth.
//
// See docs/tfbot/DESIGN.md section 3.7.
//
//=============================================================================//
#ifndef TF_BOT_FEATURES_H
#define TF_BOT_FEATURES_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;

// Bit positions are part of the A/B record format: append new features at the
// end and never renumber.
enum ETFBotFeature
{
	TFBOT_FEATURE_TEST_NOOP = 0,	// M0 plumbing check, changes no behaviour
	TFBOT_FEATURE_PROFILES,			// M1 per-bot skill/style profiles, team-needs class choice
	TFBOT_FEATURE_AIM,				// M2 human-like aim model, real projectile leading, pre-aim
	TFBOT_FEATURE_BLACKBOARD,		// M3 shared team knowledge, group up / wait for uber, focus fire, medics answer bots
	TFBOT_FEATURE_VOICE,			// M3 bots make voice callouts
	TFBOT_FEATURE_PERCEPTION,		// M3 hearing footsteps (approximate), glancing around
	TFBOT_FEATURE_MEDIC,			// M3 medic fixes (IsGoodUberTarget)
	TFBOT_FEATURE_ENGINEER,			// M3b engineer styles, carrying buildings, helping teammates' buildings
	TFBOT_FEATURE_MOVEMENT,			// M4 sustained strafing, combat jumps, scout double jumps, medic strafing
	TFBOT_FEATURE_UNLOCKS,			// M6 whitelisted unlocks from profile loadouts (Direct Hit, Kritzkrieg, Gunslinger)
	TFBOT_FEATURE_MOBILITY,			// M7 rocket / sticky jumps as path links

	TFBOT_FEATURE_COUNT
};

typedef uint64 TFBotFeatureMask;

namespace TFBotFeatures
{
	const char *GetName( ETFBotFeature feature );

	// True if this bot spawned with the feature enabled.
	bool IsEnabled( const CTFBot *bot, ETFBotFeature feature );
	TFBotFeatureMask GetBotMask( const CTFBot *bot );

	// Mask a bot on this team would get if it spawned now.
	TFBotFeatureMask GetTeamMask( int team );
	const char *GetTeamSetting( int team );		// raw convar text

	// Called from CTFBot::Spawn().
	void OnBotSpawn( CTFBot *bot );

	// Comma-separated feature names ("" for none).
	void MaskToString( TFBotFeatureMask mask, char *buf, int bufSize );

	// Wall-clock cost per feature, accumulated through CTFBotFeatureCostScope.
	void AddCost( ETFBotFeature feature, double seconds );
	double GetCost( ETFBotFeature feature );

	// Named activity counters ("blast_jump", "building_pickup", ...) so A/B
	// runs show each behaviour actually fires. Name must be a string literal.
	void CountEvent( const char *name );
	int GetEventCount( int index, const char **name );	// index from 0; returns -1 past the end
	void ResetEventCounts();
}

// Times a block of feature code: TFBOT_FEATURE_COST_SCOPE( TFBOT_FEATURE_X );
class CTFBotFeatureCostScope
{
public:
	explicit CTFBotFeatureCostScope( ETFBotFeature feature );
	~CTFBotFeatureCostScope();

private:
	ETFBotFeature m_feature;
	double m_start;
};

#define TFBOT_FEATURE_COST_SCOPE_JOIN2( a, b ) a##b
#define TFBOT_FEATURE_COST_SCOPE_JOIN( a, b ) TFBOT_FEATURE_COST_SCOPE_JOIN2( a, b )
#define TFBOT_FEATURE_COST_SCOPE( feature ) \
	CTFBotFeatureCostScope TFBOT_FEATURE_COST_SCOPE_JOIN( tfbotFeatureCost_, __LINE__ )( feature )

#endif // TF_BOT_FEATURES_H
