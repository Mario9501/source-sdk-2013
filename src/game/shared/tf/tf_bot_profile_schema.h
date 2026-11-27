//========= TFBot overhaul: bot profile schema ================================//
//
// The single table of profile keys, their ranges and per-bot variation. The
// server validates profile files against it, and the in-game profile editor
// (client, M1b) builds its sliders from it, so a key added here shows up in
// both places.
//
// See docs/tfbot/DESIGN.md sections 3.2 and M1.
//
//=============================================================================//
#ifndef TF_BOT_PROFILE_SCHEMA_H
#define TF_BOT_PROFILE_SCHEMA_H
#ifdef _WIN32
#pragma once
#endif

// Append new keys at the end of a group; files refer to keys by name, so
// reordering is safe, but keep groups together for the editor.
enum ETFBotProfileKey
{
	// --- skill ---
	TFBOT_PROFILE_REACTION_TIME = 0,	// seconds before reacting to a newly seen enemy
	TFBOT_PROFILE_AIM_ERROR,			// degrees of base aim error
	TFBOT_PROFILE_AIM_TRACKING,			// seconds between aim target updates (tracking lag)
	TFBOT_PROFILE_AIM_SPEED,			// degrees/second head turn speed
	TFBOT_PROFILE_AIM_OVERSHOOT,		// fraction a flick overshoots before correcting
	TFBOT_PROFILE_AIM_MOTION_PENALTY,	// extra aim error per unit of target angular speed
	TFBOT_PROFILE_DECISION_QUALITY,		// 0..1, positioning / target choice / retreat judgement
	TFBOT_PROFILE_AWARENESS,			// 0..1, hearing range and how often it glances around

	// --- style ---
	TFBOT_PROFILE_AGGRESSION,			// 0 cautious .. 1 reckless
	TFBOT_PROFILE_FLANK,				// 0 front line .. 1 flanker
	TFBOT_PROFILE_CALLOUT_RATE,			// 0 silent .. 1 calls out everything
	TFBOT_PROFILE_RETREAT_HEALTH,		// health fraction at which it falls back
	TFBOT_PROFILE_STRAFE,				// 0..1, how much it strafes and dodges in fights
	TFBOT_PROFILE_ENGINEER_STYLE,		// turtle, forward, battle

	TFBOT_PROFILE_KEY_COUNT
};

enum ETFBotProfileGroup
{
	TFBOT_PROFILE_GROUP_SKILL,
	TFBOT_PROFILE_GROUP_STYLE,
};

enum ETFBotEngineerStyle
{
	TFBOT_ENGINEER_TURTLE = 0,
	TFBOT_ENGINEER_FORWARD,
	TFBOT_ENGINEER_BATTLE,
};

struct TFBotProfileKeyInfo_t
{
	const char *m_name;
	ETFBotProfileGroup m_group;
	float m_min;
	float m_max;
	float m_variance;			// per-bot variation as a fraction of (max - min)
	bool m_higherIsBetter;		// skill keys: direction used for the skill score
	const char *m_enumNames;	// comma list for choice keys, NULL for numbers
	const char *m_milestone;	// first milestone that uses the key
	const char *m_description;
};

const TFBotProfileKeyInfo_t &TFBotProfileKeyInfo( int key );

// -1 if unknown. Case-insensitive.
int TFBotProfileFindKey( const char *name );

// Number or (for choice keys) a choice name. False if it can't be parsed.
bool TFBotProfileParseValue( int key, const char *text, float *outValue );

// Clamp into the key's range; reports whether clamping happened.
float TFBotProfileClampValue( int key, float value, bool *wasClamped = NULL );

void TFBotProfileFormatValue( int key, float value, char *buf, int bufSize );

// 0 (worst) .. 1 (best) for one skill key's value.
float TFBotProfileSkillGoodness( int key, float value );

// Profile file names (MOD search path).
#define TFBOT_PROFILES_STOCK_FILE	"scripts/tf_bot_profiles.txt"
#define TFBOT_PROFILES_CUSTOM_FILE	"scripts/tf_bot_profiles_custom.txt"

// Skill tier base profiles, indexed by tf_bot_difficulty (0..3).
#define TFBOT_PROFILE_TIER_COUNT 4
extern const char *g_szTFBotProfileTierNames[ TFBOT_PROFILE_TIER_COUNT ];

#endif // TF_BOT_PROFILE_SCHEMA_H
