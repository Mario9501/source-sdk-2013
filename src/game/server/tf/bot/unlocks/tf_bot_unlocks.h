//========= TFBot overhaul: unlocks (M6) ======================================//
//
// Bots get unlocks only from a curated whitelist; an item joins the list once
// the bot code handles it:
//   The Direct Hit   aim model (M2) leads with its real speed, aims centre mass
//   The Kritzkrieg   pops when an enemy is near the patient, not when low
//   The Gunslinger   enables the battle engineer style (minis, fights)
// Which item a bot carries comes from its profile ("loadout" section, or
// derived from the bot's name for anonymous bots). Stock otherwise.
//
// See docs/tfbot/DESIGN.md M6.
//
//=============================================================================//
#ifndef TF_BOT_UNLOCKS_H
#define TF_BOT_UNLOCKS_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;
class CTFPlayer;
class CWeaponMedigun;

namespace TFBotUnlocks
{
	bool IsActive( const CTFBot *bot );

	// Schema item name, for the given class (TF_CLASS_UNDEFINED: any class).
	bool IsWhitelisted( const char *itemName, int playerClass );

	// Hook: CTFBot::Spawn, after the profile is applied.
	void ApplyLoadout( CTFBot *bot );

	// Hook: medic uber decision.
	bool IsKritz( const CTFBot *medic, const CWeaponMedigun *medigun );
	bool ShouldDeployKritz( CTFBot *medic, CTFPlayer *patient );

	bool HasGunslinger( const CTFBot *bot );
}

#endif // TF_BOT_UNLOCKS_H
