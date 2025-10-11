//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot attribute system implementation
//
//=============================================================================//

#include "cbase.h"
#include "tf_bot_attributes.h"
#include "filesystem.h"
#include "KeyValues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Difficulty preset definitions
//-----------------------------------------------------------------------------
static const struct
{
	float aimAccuracy;
	float reactionTime;
	float decisionSpeed;
	float movementSkill;
	float awarenessRadius;
	float trackingAbility;
	float situationalAwareness;
} s_DifficultyPresets[BOT_DIFFICULTY_COUNT] =
{
	// EASY
	{ 0.3f, 0.7f, 0.3f, 0.2f, 0.4f, 0.3f, 0.2f },
	// NORMAL
	{ 0.5f, 0.5f, 0.5f, 0.5f, 0.6f, 0.5f, 0.5f },
	// HARD
	{ 0.7f, 0.3f, 0.7f, 0.7f, 0.8f, 0.7f, 0.7f },
	// EXPERT
	{ 0.9f, 0.1f, 0.9f, 0.9f, 1.0f, 0.9f, 0.9f }
};

//-----------------------------------------------------------------------------
// Personality archetype definitions
//-----------------------------------------------------------------------------
static const struct
{
	float aggression;
	float teamwork;
	float selfPreservation;
	float objectiveFocus;
	float curiosity;
	float patience;
} s_PersonalityArchetypes[BOT_PERSONALITY_COUNT] =
{
	// BALANCED
	{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },
	// AGGRESSIVE
	{ 0.9f, 0.3f, 0.2f, 0.6f, 0.4f, 0.2f },
	// DEFENSIVE
	{ 0.3f, 0.7f, 0.8f, 0.7f, 0.3f, 0.8f },
	// SUPPORTIVE
	{ 0.4f, 0.9f, 0.7f, 0.5f, 0.4f, 0.6f },
	// FLANKER
	{ 0.7f, 0.4f, 0.6f, 0.5f, 0.8f, 0.5f },
	// SNIPER
	{ 0.6f, 0.5f, 0.8f, 0.6f, 0.5f, 0.9f },
	// ENGINEER
	{ 0.3f, 0.8f, 0.7f, 0.8f, 0.3f, 0.7f }
};

//-----------------------------------------------------------------------------
// BotSkillParams implementation
//-----------------------------------------------------------------------------
void BotSkillParams::ApplyDifficultyPreset(BotDifficultyPreset difficulty)
{
	if (difficulty < 0 || difficulty >= BOT_DIFFICULTY_COUNT)
	{
		Warning("Invalid difficulty preset %d, using NORMAL\n", difficulty);
		difficulty = BOT_DIFFICULTY_NORMAL;
	}

	aimAccuracy = s_DifficultyPresets[difficulty].aimAccuracy;
	reactionTime = s_DifficultyPresets[difficulty].reactionTime;
	decisionSpeed = s_DifficultyPresets[difficulty].decisionSpeed;
	movementSkill = s_DifficultyPresets[difficulty].movementSkill;
	awarenessRadius = s_DifficultyPresets[difficulty].awarenessRadius;
	trackingAbility = s_DifficultyPresets[difficulty].trackingAbility;
	situationalAwareness = s_DifficultyPresets[difficulty].situationalAwareness;
}

void BotSkillParams::Randomize(float variance)
{
	// Add random variance to each skill parameter (within [0.0, 1.0] bounds)
	aimAccuracy = clamp(aimAccuracy + RandomFloat(-variance, variance), 0.0f, 1.0f);
	reactionTime = clamp(reactionTime + RandomFloat(-variance, variance), 0.0f, 1.0f);
	decisionSpeed = clamp(decisionSpeed + RandomFloat(-variance, variance), 0.0f, 1.0f);
	movementSkill = clamp(movementSkill + RandomFloat(-variance, variance), 0.0f, 1.0f);
	awarenessRadius = clamp(awarenessRadius + RandomFloat(-variance, variance), 0.0f, 1.0f);
	trackingAbility = clamp(trackingAbility + RandomFloat(-variance, variance), 0.0f, 1.0f);
	situationalAwareness = clamp(situationalAwareness + RandomFloat(-variance, variance), 0.0f, 1.0f);
}

//-----------------------------------------------------------------------------
// BotPersonalityParams implementation
//-----------------------------------------------------------------------------
void BotPersonalityParams::ApplyArchetype(BotPersonality archetype)
{
	if (archetype < 0 || archetype >= BOT_PERSONALITY_COUNT)
	{
		Warning("Invalid personality archetype %d, using BALANCED\n", archetype);
		archetype = BOT_PERSONALITY_BALANCED;
	}

	aggression = s_PersonalityArchetypes[archetype].aggression;
	teamwork = s_PersonalityArchetypes[archetype].teamwork;
	selfPreservation = s_PersonalityArchetypes[archetype].selfPreservation;
	objectiveFocus = s_PersonalityArchetypes[archetype].objectiveFocus;
	curiosity = s_PersonalityArchetypes[archetype].curiosity;
	patience = s_PersonalityArchetypes[archetype].patience;
}

//-----------------------------------------------------------------------------
// CTFBotAttributes implementation
//-----------------------------------------------------------------------------
CTFBotAttributes::CTFBotAttributes()
{
	Reset();
}

CTFBotAttributes::~CTFBotAttributes()
{
	// Nothing to clean up
}

void CTFBotAttributes::Reset()
{
	// Reset skill to default (medium)
	skill = BotSkillParams();

	// Reset personality to balanced
	personality = BotPersonalityParams();

	// Reset identity
	V_strncpy(name, "Bot", sizeof(name));
	preferredClass = TF_CLASS_UNDEFINED;
	preferredTeam = TEAM_UNASSIGNED;

	// Clear flags
	flags = BOT_ATTR_QUOTA_MANAGED;

	// Reset loadout to defaults
	for (int i = 0; i < BOT_LOADOUT_WEAPON_COUNT; i++)
	{
		weaponLoadout[i] = -1;	// -1 means use class default weapon
	}
	for (int i = 0; i < 8; i++)
	{
		cosmeticLoadout[i] = -1;	// -1 means no cosmetic
	}

	// Reset stats
	stats.Reset();
}

void CTFBotAttributes::ApplyDifficultyPreset(BotDifficultyPreset difficulty)
{
	skill.ApplyDifficultyPreset(difficulty);
}

void CTFBotAttributes::ApplyPersonality(BotPersonality personality_type)
{
	personality.ApplyArchetype(personality_type);
}

void CTFBotAttributes::LoadFromKeyValues(KeyValues *pKV)
{
	if (!pKV)
		return;

	// Load identity
	const char *botName = pKV->GetString("name", "Bot");
	V_strncpy(name, botName, sizeof(name));

	const char *classStr = pKV->GetString("preferredClass", "undefined");
	preferredClass = GetClassIndexFromString(classStr);

	const char *teamStr = pKV->GetString("preferredTeam", "auto");
	if (FStrEq(teamStr, "red"))
		preferredTeam = TF_TEAM_RED;
	else if (FStrEq(teamStr, "blue"))
		preferredTeam = TF_TEAM_BLUE;
	else
		preferredTeam = TEAM_UNASSIGNED;

	// Load skill parameters
	KeyValues *pSkill = pKV->FindKey("skill");
	if (pSkill)
	{
		skill.aimAccuracy = pSkill->GetFloat("aimAccuracy", 0.5f);
		skill.reactionTime = pSkill->GetFloat("reactionTime", 0.5f);
		skill.decisionSpeed = pSkill->GetFloat("decisionSpeed", 0.5f);
		skill.movementSkill = pSkill->GetFloat("movementSkill", 0.5f);
		skill.awarenessRadius = pSkill->GetFloat("awarenessRadius", 0.5f);
		skill.trackingAbility = pSkill->GetFloat("trackingAbility", 0.5f);
		skill.situationalAwareness = pSkill->GetFloat("situationalAwareness", 0.5f);
	}

	// Load personality parameters
	KeyValues *pPersonality = pKV->FindKey("personality");
	if (pPersonality)
	{
		personality.aggression = pPersonality->GetFloat("aggression", 0.5f);
		personality.teamwork = pPersonality->GetFloat("teamwork", 0.5f);
		personality.selfPreservation = pPersonality->GetFloat("selfPreservation", 0.5f);
		personality.objectiveFocus = pPersonality->GetFloat("objectiveFocus", 0.5f);
		personality.curiosity = pPersonality->GetFloat("curiosity", 0.5f);
		personality.patience = pPersonality->GetFloat("patience", 0.5f);
	}

	// Load loadout
	KeyValues *pLoadout = pKV->FindKey("loadout");
	if (pLoadout)
	{
		weaponLoadout[BOT_LOADOUT_PRIMARY] = pLoadout->GetInt("primary", -1);
		weaponLoadout[BOT_LOADOUT_SECONDARY] = pLoadout->GetInt("secondary", -1);
		weaponLoadout[BOT_LOADOUT_MELEE] = pLoadout->GetInt("melee", -1);
	}

	// Load flags
	flags = 0;
	if (pKV->GetBool("quotaManaged", true))
		flags |= BOT_ATTR_QUOTA_MANAGED;
	if (pKV->GetBool("isNPC", false))
		flags |= BOT_ATTR_IS_NPC;
	if (pKV->GetBool("meleeOnly", false))
		flags |= BOT_ATTR_MELEE_ONLY;
	if (pKV->GetBool("alwaysCrit", false))
		flags |= BOT_ATTR_ALWAYS_CRIT;
	if (pKV->GetBool("invulnerable", false))
		flags |= BOT_ATTR_INVULNERABLE;
}

void CTFBotAttributes::SaveToKeyValues(KeyValues *pKV) const
{
	if (!pKV)
		return;

	// Save identity
	pKV->SetString("name", name);
	pKV->SetString("preferredClass", GetPlayerClassName(preferredClass));

	if (preferredTeam == TF_TEAM_RED)
		pKV->SetString("preferredTeam", "red");
	else if (preferredTeam == TF_TEAM_BLUE)
		pKV->SetString("preferredTeam", "blue");
	else
		pKV->SetString("preferredTeam", "auto");

	// Save skill parameters
	KeyValues *pSkill = pKV->FindKey("skill", true);
	pSkill->SetFloat("aimAccuracy", skill.aimAccuracy);
	pSkill->SetFloat("reactionTime", skill.reactionTime);
	pSkill->SetFloat("decisionSpeed", skill.decisionSpeed);
	pSkill->SetFloat("movementSkill", skill.movementSkill);
	pSkill->SetFloat("awarenessRadius", skill.awarenessRadius);
	pSkill->SetFloat("trackingAbility", skill.trackingAbility);
	pSkill->SetFloat("situationalAwareness", skill.situationalAwareness);

	// Save personality parameters
	KeyValues *pPersonality = pKV->FindKey("personality", true);
	pPersonality->SetFloat("aggression", personality.aggression);
	pPersonality->SetFloat("teamwork", personality.teamwork);
	pPersonality->SetFloat("selfPreservation", personality.selfPreservation);
	pPersonality->SetFloat("objectiveFocus", personality.objectiveFocus);
	pPersonality->SetFloat("curiosity", personality.curiosity);
	pPersonality->SetFloat("patience", personality.patience);

	// Save loadout
	KeyValues *pLoadout = pKV->FindKey("loadout", true);
	if (weaponLoadout[BOT_LOADOUT_PRIMARY] != -1)
		pLoadout->SetInt("primary", weaponLoadout[BOT_LOADOUT_PRIMARY]);
	if (weaponLoadout[BOT_LOADOUT_SECONDARY] != -1)
		pLoadout->SetInt("secondary", weaponLoadout[BOT_LOADOUT_SECONDARY]);
	if (weaponLoadout[BOT_LOADOUT_MELEE] != -1)
		pLoadout->SetInt("melee", weaponLoadout[BOT_LOADOUT_MELEE]);

	// Save flags
	pKV->SetBool("quotaManaged", HasAttribute(BOT_ATTR_QUOTA_MANAGED));
	pKV->SetBool("isNPC", HasAttribute(BOT_ATTR_IS_NPC));
	pKV->SetBool("meleeOnly", HasAttribute(BOT_ATTR_MELEE_ONLY));
	pKV->SetBool("alwaysCrit", HasAttribute(BOT_ATTR_ALWAYS_CRIT));
	pKV->SetBool("invulnerable", HasAttribute(BOT_ATTR_INVULNERABLE));
}

void CTFBotAttributes::Randomize(float skillVariance)
{
	skill.Randomize(skillVariance);

	// Also randomize personality slightly
	personality.aggression = clamp(personality.aggression + RandomFloat(-0.1f, 0.1f), 0.0f, 1.0f);
	personality.teamwork = clamp(personality.teamwork + RandomFloat(-0.1f, 0.1f), 0.0f, 1.0f);
	personality.selfPreservation = clamp(personality.selfPreservation + RandomFloat(-0.1f, 0.1f), 0.0f, 1.0f);
}

void CTFBotAttributes::CopyFrom(const CTFBotAttributes &other)
{
	// Deep copy all attributes
	skill = other.skill;
	personality = other.personality;
	V_strncpy(name, other.name, sizeof(name));
	preferredClass = other.preferredClass;
	preferredTeam = other.preferredTeam;
	flags = other.flags;

	for (int i = 0; i < BOT_LOADOUT_WEAPON_COUNT; i++)
		weaponLoadout[i] = other.weaponLoadout[i];
	for (int i = 0; i < 8; i++)
		cosmeticLoadout[i] = other.cosmeticLoadout[i];

	stats = other.stats;
}

void CTFBotAttributes::Print() const
{
	Msg("=== Bot Attributes: %s ===\n", name);
	Msg("  Class: %s | Team: %d | Flags: 0x%X\n",
		GetPlayerClassName(preferredClass), preferredTeam, flags);

	Msg("  Skill: Aim=%.2f React=%.2f Movement=%.2f Awareness=%.2f\n",
		skill.aimAccuracy, skill.reactionTime, skill.movementSkill, skill.awarenessRadius);

	Msg("  Personality: Agg=%.2f Team=%.2f Preserve=%.2f Objective=%.2f\n",
		personality.aggression, personality.teamwork,
		personality.selfPreservation, personality.objectiveFocus);

	Msg("  Stats: K/D=%d/%d Damage=%d Score=%.2f\n",
		stats.kills, stats.deaths, stats.damage, stats.performanceScore);
}

//-----------------------------------------------------------------------------
// Global helper functions
//-----------------------------------------------------------------------------
CTFBotAttributes CreateAttributesFromDifficulty(BotDifficultyPreset difficulty)
{
	CTFBotAttributes attribs;
	attribs.ApplyDifficultyPreset(difficulty);
	return attribs;
}

CTFBotAttributes CreateAttributesFromPersonality(BotPersonality personality)
{
	CTFBotAttributes attribs;
	attribs.ApplyPersonality(personality);
	return attribs;
}

bool LoadBotProfileFromFile(const char *filename, CTFBotAttributes &attribs)
{
	KeyValues *pKV = new KeyValues("BotProfile");
	if (!pKV->LoadFromFile(filesystem, filename, "MOD"))
	{
		Warning("Failed to load bot profile from '%s'\n", filename);
		pKV->deleteThis();
		return false;
	}

	attribs.LoadFromKeyValues(pKV);
	pKV->deleteThis();
	return true;
}

bool SaveBotProfileToFile(const char *filename, const CTFBotAttributes &attribs)
{
	KeyValues *pKV = new KeyValues("BotProfile");
	attribs.SaveToKeyValues(pKV);

	bool success = pKV->SaveToFile(filesystem, filename, "MOD");
	if (!success)
	{
		Warning("Failed to save bot profile to '%s'\n", filename);
	}

	pKV->deleteThis();
	return success;
}
