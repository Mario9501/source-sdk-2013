//========= TFBot overhaul: mobility jumps (M7) ===============================//
//
// Soldiers rocket jump and demomen sticky jump up ledges that a normal jump
// can't reach. Two parts:
//   - path planning: a capable bot's GetMaxJumpHeight rises from 72 to the
//     blast-jump height, so the planner (CTFBotPathCost) may route up high
//     ledges between nav areas;
//   - execution: when the path follower asks to climb such a ledge
//     (ClimbUpToLedge), the bot runs a short state machine: equip, look down,
//     jump and fire (soldier), or plant a sticky, wait for it to arm, jump and
//     detonate (demoman).
// Capable = has the launcher with ammo and enough health to pay for the jump.
//
// See docs/tfbot/DESIGN.md M7.
//
//=============================================================================//
#ifndef TF_BOT_MOBILITY_H
#define TF_BOT_MOBILITY_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;

namespace TFBotMobility
{
	bool IsActive( const CTFBot *bot );

	// Hook: CTFBotLocomotion::GetMaxJumpHeight.
	float GetMaxJumpHeight( const CTFBot *bot );

	// Hook: CTFBotLocomotion::ClimbUpToLedge. True if this climb needs a blast
	// jump, which is then started (or already running).
	bool HandleClimb( CTFBot *bot, const Vector &landingGoal, const Vector &landingForward );

	// Hook: CTFBotLocomotion::Update, every tick.
	void Update( CTFBot *bot );

	bool IsBlastJumping( const CTFBot *bot );
}

#endif // TF_BOT_MOBILITY_H
