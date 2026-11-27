//========= TFBot overhaul: combat movement (M4) ==============================//
//
// Replaces the stock dodge (a random left/right/none choice every tick, which
// reads as jitter) with how people move in a fight:
//   - sustained strafes whose direction changes every 0.25-1 s, faster and
//     less predictable for bots with a higher 'strafe' profile value,
//   - occasional jumps for mobile classes; scouts double jump,
//   - medics strafe while healing (the stock dodge skips them).
// No pathfinding changes: these are button presses on top of path following.
//
// See docs/tfbot/DESIGN.md M4.
//
//=============================================================================//
#ifndef TF_BOT_MOVEMENT_H
#define TF_BOT_MOVEMENT_H
#ifdef _WIN32
#pragma once
#endif

class CTFBot;

namespace TFBotMovement
{
	bool IsActive( const CTFBot *bot );

	// Hook: CTFBotMainAction::Dodge. True if handled (skip the stock dodge).
	bool CombatMove( CTFBot *me );
}

#endif // TF_BOT_MOVEMENT_H
