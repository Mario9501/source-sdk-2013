//========= TFBot overhaul: playstyle cloning (M4b) ===========================//
//
// tf_bot_record_player <name> opts a human into recording. While they play,
// the server measures class choices, how far forward they play, how often
// they're away from teammates, engagement distances, time to first shot after
// an enemy appears, hitscan accuracy, health when they disengage, voice
// commands, and strafing/jumping in fights. Stats accumulate across sessions
// in cfg/tf_bot_records.txt.
//
// tf_bot_profile_from_record <name> [profile] fits those stats onto profile
// keys with plain statistics (no ML) and writes an ordinary custom profile,
// based on the closest tier, that can be edited like any other.
//
// See docs/tfbot/DESIGN.md M4b.
//
//=============================================================================//
#ifndef TF_BOT_CLONE_H
#define TF_BOT_CLONE_H
#ifdef _WIN32
#pragma once
#endif

class CTFPlayer;
class CTFWeaponBase;

namespace TFBotClone
{
	// Called from existing overhaul hooks (metrics weapon-fired, blackboard spoke).
	void OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon );
	void OnPlayerSpoke( CTFPlayer *player, int concept );
}

#endif // TF_BOT_CLONE_H
