//========= TFBot overhaul: engineers (M3b) ===================================//
//
// With the 'engineer' feature:
//   - Style comes from the profile (engineer_style): turtle holds the
//     objective, forward builds just behind the team's front line and moves
//     the nest up as the team advances, battle (Gunslinger minis) needs the
//     M6 unlock whitelist and plays as forward until then.
//   - Buildings are carried instead of detonated: when the nest must move,
//     the engineer picks up its sentry, carries it and places it again.
//   - A sentry about to die is picked up and carried back to safety.
//   - Turtle improvement: an engineer whose nest is done helps teammates'
//     buildings (finish construction, repair, upgrade, remove sappers).
//
// See docs/tfbot/DESIGN.md M3b.
//
//=============================================================================//
#ifndef TF_BOT_ENGINEER_OVERHAUL_H
#define TF_BOT_ENGINEER_OVERHAUL_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"

class CTFBot;
class CBaseObject;
class CObjectSentrygun;

namespace TFBotEngineer
{
	bool IsActive( const CTFBot *bot );
	int GetStyle( const CTFBot *bot );		// ETFBotEngineerStyle (battle -> forward until M6)

	// Hook: CTFBotEngineerMoveToBuild::SelectBuildLocation, after candidates
	// are collected. True if the style chose a location.
	bool PickNestLocation( CTFBot *me, Vector *location );

	// Forward style: the team has advanced well past the nest.
	bool ShouldAdvanceNest( CTFBot *me, CObjectSentrygun *sentry, bool isUnderAttack );

	// The sentry is about to die and can still be saved.
	bool ShouldSaveSentry( CTFBot *me, CObjectSentrygun *sentry );
	bool FindRetreatSpot( CTFBot *me, Vector *spot );

	// A teammate's building near the nest that needs a wrench.
	CBaseObject *FindBuildingToHelp( CTFBot *me, CObjectSentrygun *mySentry, bool isUnderAttack );

	// Battle style (Gunslinger, M6): no dispenser; fight beside the mini.
	bool SkipDispenser( const CTFBot *bot );
	bool ShouldFight( CTFBot *me, CObjectSentrygun *sentry );
}


//-----------------------------------------------------------------------------
// Pick up one of my buildings, then either carry it to a new nest location
// (MoveToBuild picks it) or to a given spot.
//-----------------------------------------------------------------------------
class CTFBotEngineerPickupAndMove : public Action< CTFBot >
{
public:
	explicit CTFBotEngineerPickupAndMove( CBaseObject *building );
	CTFBotEngineerPickupAndMove( CBaseObject *building, const Vector &placeAt );

	virtual ActionResult< CTFBot > OnStart( CTFBot *me, Action< CTFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CTFBot > Update( CTFBot *me, float interval ) OVERRIDE;
	virtual const char *GetName( void ) const OVERRIDE { return "EngineerPickupAndMove"; }

private:
	CHandle< CBaseObject > m_building;
	bool m_hasSpot;
	Vector m_spot;
	CountdownTimer m_giveUpTimer;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};


//-----------------------------------------------------------------------------
// Put down the building I'm carrying (optionally after walking to a spot).
//-----------------------------------------------------------------------------
class CTFBotEngineerPlaceCarried : public Action< CTFBot >
{
public:
	CTFBotEngineerPlaceCarried();
	explicit CTFBotEngineerPlaceCarried( const Vector &placeAt );

	virtual ActionResult< CTFBot > OnStart( CTFBot *me, Action< CTFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CTFBot > Update( CTFBot *me, float interval ) OVERRIDE;
	virtual const char *GetName( void ) const OVERRIDE { return "EngineerPlaceCarried"; }

private:
	bool m_hasSpot;
	Vector m_spot;
	bool m_needToAim;
	int m_wanderWay;
	CountdownTimer m_searchTimer;
	CountdownTimer m_giveUpTimer;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};


//-----------------------------------------------------------------------------
// Hit a teammate's building with the wrench until it is built, repaired,
// upgraded and free of sappers.
//-----------------------------------------------------------------------------
class CTFBotEngineerHelpBuilding : public Action< CTFBot >
{
public:
	explicit CTFBotEngineerHelpBuilding( CBaseObject *building );

	virtual ActionResult< CTFBot > OnStart( CTFBot *me, Action< CTFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CTFBot > Update( CTFBot *me, float interval ) OVERRIDE;
	virtual const char *GetName( void ) const OVERRIDE { return "EngineerHelpBuilding"; }

private:
	CHandle< CBaseObject > m_building;
	CountdownTimer m_giveUpTimer;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};

#endif // TF_BOT_ENGINEER_OVERHAUL_H
