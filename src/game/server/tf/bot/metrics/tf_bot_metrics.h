//========= TFBot overhaul: match metrics =====================================//
//
// Writes one JSON Lines file per match (tf_bot_metrics 1) with round results,
// per-team combat/objective/stuck statistics, kills, captures and server tick
// cost. tools/tfbot/ab.py collects these files for A/B reports. With
// tf_bot_ab_rounds N the server quits by itself after N rounds.
//
// See docs/tfbot/DESIGN.md sections 3.5, 3.8 and the M0 milestone.
//
//=============================================================================//
#ifndef TF_BOT_METRICS_H
#define TF_BOT_METRICS_H
#ifdef _WIN32
#pragma once
#endif

class CTFPlayer;
class CTFWeaponBase;

// Hook: called from CTFGameStats::Event_PlayerFiredWeapon while a round is running.
void TFBotMetrics_OnWeaponFired( CTFPlayer *player, CTFWeaponBase *weapon );

#endif // TF_BOT_METRICS_H
