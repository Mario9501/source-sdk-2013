//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot attribute system - defines skill levels, personality, loadout
//
// $NoKeywords: $
//=============================================================================//

#ifndef TF_BOT_ATTRIBUTES_H
#define TF_BOT_ATTRIBUTES_H
#ifdef _WIN32
#pragma once
#endif

#include "tf_shareddefs.h"

//-----------------------------------------------------------------------------
// Bot difficulty presets (compatible with existing tf_bot_difficulty)
//-----------------------------------------------------------------------------
enum BotDifficultyPreset
{
	BOT_DIFFICULTY_EASY = 0,
	BOT_DIFFICULTY_NORMAL = 1,
	BOT_DIFFICULTY_HARD = 2,
	BOT_DIFFICULTY_EXPERT = 3,

	BOT_DIFFICULTY_COUNT
};

//-----------------------------------------------------------------------------
// Bot personality archetypes
//-----------------------------------------------------------------------------
enum BotPersonality
{
	BOT_PERSONALITY_BALANCED = 0,		// Standard balanced bot
	BOT_PERSONALITY_AGGRESSIVE,			// Rushes enemies, takes risks
	BOT_PERSONALITY_DEFENSIVE,			// Plays safe, holds positions
	BOT_PERSONALITY_SUPPORTIVE,			// Focuses on helping teammates
	BOT_PERSONALITY_FLANKER,			// Uses alternate routes, sneaky
	BOT_PERSONALITY_SNIPER,				// Prefers long range, cautious
	BOT_PERSONALITY_ENGINEER,			// Focuses on building/defense

	BOT_PERSONALITY_COUNT
};

//-----------------------------------------------------------------------------
// Bot attribute flags (replaces old CTFBot attributes)
//-----------------------------------------------------------------------------
enum BotAttributeFlags
{
	BOT_ATTR_NONE = 0,
	BOT_ATTR_QUOTA_MANAGED = (1 << 0),			// Managed by bot quota system
	BOT_ATTR_IS_NPC = (1 << 1),					// Is an NPC (not player-like)
	BOT_ATTR_IGNORE_PLAYERS = (1 << 2),			// Ignores all players
	BOT_ATTR_IGNORE_OBJECTIVES = (1 << 3),		// Ignores scenario objectives
	BOT_ATTR_IGNORE_FLAG = (1 << 4),			// Ignores CTF flag
	BOT_ATTR_IGNORE_SENTRIES = (1 << 5),		// Ignores enemy sentries
	BOT_ATTR_MELEE_ONLY = (1 << 6),				// Uses melee weapons only
	BOT_ATTR_ALWAYS_CRIT = (1 << 7),			// Always has critical hits
	BOT_ATTR_INVULNERABLE = (1 << 8),			// Cannot take damage
	BOT_ATTR_DEBUG = (1 << 9),					// Shows debug overlays
	BOT_ATTR_RETAIN_BUILDINGS = (1 << 10),		// Engineer keeps buildings on class change
	BOT_ATTR_SUPPRESS_FIRE = (1 << 11),			// Never fires weapons (testing)
	BOT_ATTR_DISABLE_DODGE = (1 << 12),			// Never dodges
	BOT_ATTR_HOLD_FIRE_UNTIL_FULL_RELOAD = (1 << 13),	// Waits for full reload before firing
};

//-----------------------------------------------------------------------------
// Bot loadout slot indices
//-----------------------------------------------------------------------------
enum BotLoadoutSlot
{
	BOT_LOADOUT_PRIMARY = 0,
	BOT_LOADOUT_SECONDARY = 1,
	BOT_LOADOUT_MELEE = 2,
	BOT_LOADOUT_PDA = 3,
	BOT_LOADOUT_PDA2 = 4,
	BOT_LOADOUT_BUILDING = 5,

	BOT_LOADOUT_WEAPON_COUNT = 6
};

//-----------------------------------------------------------------------------
// Bot skill parameters (all 0.0 - 1.0 normalized)
//-----------------------------------------------------------------------------
struct BotSkillParams
{
	float aimAccuracy;			// 0.0 = misses constantly, 1.0 = perfect aim
	float reactionTime;			// 0.0 = instant reaction, 1.0 = slow/delayed (in seconds when scaled)
	float decisionSpeed;		// 0.0 = slow decisions, 1.0 = instant tactical choices
	float movementSkill;		// 0.0 = basic movement, 1.0 = rocket jumps, strafing, dodging
	float awarenessRadius;		// 0.0 = blind, 1.0 = sees everything in range (scales actual radius)
	float trackingAbility;		// 0.0 = loses targets easily, 1.0 = perfect tracking
	float situationalAwareness;	// 0.0 = oblivious, 1.0 = sees flanks, predicts threats

	BotSkillParams()
	{
		// Default to medium skill
		aimAccuracy = 0.5f;
		reactionTime = 0.5f;
		decisionSpeed = 0.5f;
		movementSkill = 0.5f;
		awarenessRadius = 0.5f;
		trackingAbility = 0.5f;
		situationalAwareness = 0.5f;
	}

	// Apply difficulty preset
	void ApplyDifficultyPreset(BotDifficultyPreset difficulty);

	// Randomize with variance (for bot diversity)
	void Randomize(float variance = 0.1f);
};

//-----------------------------------------------------------------------------
// Bot personality parameters (0.0 - 1.0 normalized)
//-----------------------------------------------------------------------------
struct BotPersonalityParams
{
	float aggression;			// 0.0 = passive, 1.0 = always attacking
	float teamwork;				// 0.0 = lone wolf, 1.0 = always near team
	float selfPreservation;		// 0.0 = suicidal rushes, 1.0 = very cautious
	float objectiveFocus;		// 0.0 = ignores objectives, 1.0 = tunnel vision on objectives
	float curiosity;			// 0.0 = never explores, 1.0 = investigates everything
	float patience;				// 0.0 = impulsive, 1.0 = waits for perfect moment

	BotPersonalityParams()
	{
		// Default balanced personality
		aggression = 0.5f;
		teamwork = 0.5f;
		selfPreservation = 0.5f;
		objectiveFocus = 0.5f;
		curiosity = 0.5f;
		patience = 0.5f;
	}

	// Apply personality archetype
	void ApplyArchetype(BotPersonality archetype);
};

//-----------------------------------------------------------------------------
// Complete bot attribute structure
//-----------------------------------------------------------------------------
class CTFBotAttributes
{
public:
	CTFBotAttributes();
	~CTFBotAttributes();

	// Initialization
	void Reset();									// Reset to default values
	void ApplyDifficultyPreset(BotDifficultyPreset difficulty);
	void ApplyPersonality(BotPersonality personality);
	void LoadFromKeyValues(KeyValues *pKV);			// Load from JSON/KeyValues
	void SaveToKeyValues(KeyValues *pKV) const;		// Save to JSON/KeyValues

	// Skill parameters
	BotSkillParams skill;

	// Personality parameters
	BotPersonalityParams personality;

	// Bot identity
	char name[MAX_PLAYER_NAME_LENGTH];
	int preferredClass;								// TF_CLASS_SCOUT, etc. (TF_CLASS_UNDEFINED = auto)
	int preferredTeam;								// TF_TEAM_RED, TF_TEAM_BLUE, TEAM_UNASSIGNED = auto

	// Attribute flags
	unsigned int flags;								// BotAttributeFlags bitmask

	// Loadout
	int weaponLoadout[BOT_LOADOUT_WEAPON_COUNT];	// Item definition IDs (-1 = default weapon)
	int cosmeticLoadout[8];							// Cosmetic item IDs (-1 = none)

	// Statistics (for adaptive difficulty / ML)
	struct BotStats
	{
		int kills;
		int deaths;
		int assists;
		int damage;
		int healing;
		int objectivesCompleted;
		float avgLifetime;							// Average time alive per life
		float performanceScore;						// 0.0-1.0 performance metric

		BotStats()
		{
			kills = 0;
			deaths = 0;
			assists = 0;
			damage = 0;
			healing = 0;
			objectivesCompleted = 0;
			avgLifetime = 0.0f;
			performanceScore = 0.5f;
		}

		void Reset()
		{
			kills = deaths = assists = damage = healing = objectivesCompleted = 0;
			avgLifetime = 0.0f;
			performanceScore = 0.5f;
		}
	} stats;

	// Attribute flag helpers
	void SetAttribute(BotAttributeFlags attr) { flags |= attr; }
	void ClearAttribute(BotAttributeFlags attr) { flags &= ~attr; }
	bool HasAttribute(BotAttributeFlags attr) const { return (flags & attr) != 0; }

	// Difficulty helpers
	bool IsEasy() const { return skill.aimAccuracy < 0.4f; }
	bool IsNormal() const { return skill.aimAccuracy >= 0.4f && skill.aimAccuracy < 0.6f; }
	bool IsHard() const { return skill.aimAccuracy >= 0.6f && skill.aimAccuracy < 0.8f; }
	bool IsExpert() const { return skill.aimAccuracy >= 0.8f; }

	// Utility
	void Randomize(float skillVariance = 0.1f);		// Add variance for bot diversity
	void CopyFrom(const CTFBotAttributes &other);
	void Print() const;								// Debug print all attributes
};

//-----------------------------------------------------------------------------
// Global helper functions
//-----------------------------------------------------------------------------

// Create attributes from difficulty preset
CTFBotAttributes CreateAttributesFromDifficulty(BotDifficultyPreset difficulty);

// Create attributes from personality archetype
CTFBotAttributes CreateAttributesFromPersonality(BotPersonality personality);

// Load bot attributes from file
bool LoadBotProfileFromFile(const char *filename, CTFBotAttributes &attribs);

// Save bot attributes to file
bool SaveBotProfileToFile(const char *filename, const CTFBotAttributes &attribs);

#endif // TF_BOT_ATTRIBUTES_H
