//========= TFBot overhaul: human-like aim (M2) ===============================//
//
// Replaces "sample the target every N seconds and match its velocity exactly"
// with a model of a person aiming:
//   - where to aim depends on weapon and skill (head vs chest, feet for splash),
//   - projectiles are led using each weapon's real projectile speed,
//   - hand wobble scaled by aim_error,
//   - lag behind targets that move across the view (aim_motion_penalty), so
//     strafing works against bots,
//   - overshoot after a flick that settles over ~0.2 s (aim_overshoot),
//   - head turn speed from aim_speed; tracking lag from aim_tracking (M1),
//   - pre-aim: hold a likely enemy approach at head height.
// Reaction time comes from the profile through CTFBotVision (M1).
//
// Active for bots with the 'aim' feature. Values come from the bot's profile,
// or its tier's base profile when the 'profiles' feature is off.
//
// See docs/tfbot/DESIGN.md M2.
//
//=============================================================================//
#ifndef TF_BOT_AIM_H
#define TF_BOT_AIM_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;
class CBaseCombatCharacter;

namespace TFBotAim
{
	bool IsActive( const CTFBot *bot );

	// Hook: CTFBotMainAction::SelectTargetPoint.
	Vector SelectTargetPoint( CTFBot *me, const CBaseCombatCharacter *subject );

	// Hook: CTFBotBody::GetMaxHeadAngularVelocity. Degrees per second.
	float GetMaxHeadAngularVelocity( const CTFBot *me );

	// Hook: CTFBot::UpdateLookingAroundForIncomingPlayers. True if handled.
	bool UpdatePreAim( CTFBot *me, bool lookForEnemies );
}

#endif // TF_BOT_AIM_H
