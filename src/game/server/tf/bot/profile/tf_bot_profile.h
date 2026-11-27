//========= TFBot overhaul: bot profiles ======================================//
//
// Every bot name maps to a stable profile: skill values from its tier (plus a
// small variation seeded by the name) and style traits (aggression, flanking,
// class preferences, engineer style) that are also seeded by the name unless a
// profile file sets them. Profile files (scripts/tf_bot_profiles.txt and
// scripts/tf_bot_profiles_custom.txt) can define tiers, presets and named
// profiles with inheritance ("base") and presets ("presets").
//
// Active only for teams running the 'profiles' feature. In MvM a robot uses
// its tier's base profile with no variation (popfiles stay in control).
//
// See docs/tfbot/DESIGN.md section 3.2 and M1.
//
//=============================================================================//
#ifndef TF_BOT_PROFILE_H
#define TF_BOT_PROFILE_H
#ifdef _WIN32
#pragma once
#endif

#include "tf_bot_profile_schema.h"
#include "tf_shareddefs.h"

class CTFBot;

struct TFBotProfile_t
{
	char m_name[ 64 ];							// profile key = bot name
	int m_tier;									// 0..3, see g_szTFBotProfileTierNames
	float m_value[ TFBOT_PROFILE_KEY_COUNT ];
	float m_classWeight[ TF_CLASS_COUNT_ALL ];	// relative preference, 0 = never
	char m_loadout[ TF_CLASS_COUNT_ALL ][ 64 ];	// item name per class ("" = stock), used from M6

	float Get( ETFBotProfileKey key ) const { return m_value[ key ]; }
	float GetSkillScore() const;				// 0 (worst) .. 1 (best), average over skill keys
};

namespace TFBotProfiles
{
	// This bot's profile if it spawned with the 'profiles' feature, else NULL.
	const TFBotProfile_t *Get( const CTFBot *bot );

	// Its profile, or else the base profile of its difficulty tier. For
	// features (aim, perception, ...) that need values even without profiles.
	const TFBotProfile_t &GetEffective( const CTFBot *bot );

	// Hook: CTFBot::Spawn (after TFBotFeatures::OnBotSpawn).
	void OnBotSpawn( CTFBot *bot );

	// Hook: CTFBot::GetNextSpawnClassname. Returns a class name, or NULL to use
	// the stock choice.
	const char *ChooseClass( CTFBot *bot );
}

#endif // TF_BOT_PROFILE_H
