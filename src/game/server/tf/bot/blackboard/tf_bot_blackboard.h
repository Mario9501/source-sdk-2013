//========= TFBot overhaul: team blackboard, voice, perception (M3) ===========//
//
// A per-team store of what the team actually knows: sentries and spies a
// teammate (bot or human) saw or called out, the enemy teammates are shooting
// at, and the team's push state. Nothing is posted that a teammate didn't
// perceive, and entries expire (design doc 3.3).
//
// Uses:
//   blackboard  share sentries/spies, focus fire, hold the push after a wipe or
//               while an uber is nearly ready, medics answer bot "MEDIC!" calls
//   voice       bots say the matching voice command (rate-limited)
//   perception  hear nearby running enemies (turn toward an approximate
//               position) and glance around when idle
// Human voice commands feed the blackboard through the SpeakConceptIfAllowed
// hook: "Spy!", "Sentry ahead!", "Incoming!", "Go go go!" / "Move up!".
//
// See docs/tfbot/DESIGN.md M3.
//
//=============================================================================//
#ifndef TF_BOT_BLACKBOARD_H
#define TF_BOT_BLACKBOARD_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;
class CTFPlayer;
class CKnownEntity;

namespace TFBotBlackboard
{
	bool IsActive( const CTFBot *bot );

	// Hook: CTFPlayer::SpeakConceptIfAllowed, after the line actually played.
	void OnPlayerSpoke( CTFPlayer *player, int concept );

	// Hook: CTFBot::RealizeSpy.
	void OnBotRealizedSpy( CTFBot *bot, CTFPlayer *spy );

	// Hook: scenario actions, before advancing along the path. True if this bot
	// should wait for the team instead of moving toward 'objective'.
	bool ShouldHoldAdvance( CTFBot *me, const Vector &objective );

	// Hook: CTFBotMainAction::SelectMoreDangerousThreat. Returns the team's
	// focus target if it is one of the two threats, else 'chosen'.
	const CKnownEntity *PreferFocusTarget( CTFBot *me, const CKnownEntity *threat1, const CKnownEntity *threat2, const CKnownEntity *chosen );

	// Hook: medic patient choice. True if this medic answers bots' calls too.
	bool MedicAnswersBotCalls( const CTFBot *medic );
}

#endif // TF_BOT_BLACKBOARD_H
