//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_objectives.h
// Objective-based goal selection for behavior tree
// Story 1.3: Objective-Based Goal Selection
// Created: 2025-09-30

#ifndef TF_BOT_BT_OBJECTIVES_H
#define TF_BOT_BT_OBJECTIVES_H

#ifdef _WIN32
#pragma once
#endif

#include "tf_bot_bt_node.h"
#include "../tf_bot_path_selector.h"
#include "../../nav_mesh/tf_nav_mesh.h"

class CTFBot;
class CTeamControlPoint;
class CCaptureFlag;
class CBaseEntity;

//----------------------------------------------------------------------------
// Goal types for objective-based navigation
//----------------------------------------------------------------------------
enum EObjectiveGoalType
{
	GOAL_NONE = 0,
	GOAL_CAPTURE_POINT,		// Control point to capture
	GOAL_DEFEND_POINT,		// Control point to defend
	GOAL_FETCH_FLAG,		// CTF flag to fetch
	GOAL_DELIVER_FLAG,		// CTF flag delivery zone
	GOAL_PUSH_PAYLOAD,		// Payload cart to push
	GOAL_BLOCK_PAYLOAD,		// Payload cart to block
};

//----------------------------------------------------------------------------
// Objective goal information
//----------------------------------------------------------------------------
struct ObjectiveGoal_t
{
	ObjectiveGoal_t()
		: type( GOAL_NONE )
		, position( vec3_origin )
		, pEntity( NULL )
		, priority( 0.0f )
	{}

	EObjectiveGoalType type;
	Vector position;
	CBaseEntity *pEntity;		// Associated entity (control point, flag, etc.)
	float priority;				// Priority score for this goal
};

//----------------------------------------------------------------------------
// Objective-based goal selector
// Evaluates game state and selects appropriate objectives for the bot
//----------------------------------------------------------------------------
class CTFBotObjectiveGoals
{
public:
	CTFBotObjectiveGoals( CTFBot *pBot );
	~CTFBotObjectiveGoals();

	// Main evaluation - returns the highest priority objective goal
	bool SelectObjectiveGoal( ObjectiveGoal_t *outGoal );

	// Update internal state (call periodically)
	void Update( float deltaTime );

private:
	// Objective evaluation methods
	void EvaluateControlPoints( CUtlVector< ObjectiveGoal_t > *goals );
	void EvaluateCTFObjectives( CUtlVector< ObjectiveGoal_t > *goals );
	void EvaluatePayloadObjectives( CUtlVector< ObjectiveGoal_t > *goals );

	// Priority calculation
	float CalculateControlPointPriority( CTeamControlPoint *pPoint, bool bCapture );
	float CalculateFlagPriority( CCaptureFlag *pFlag );

	// Helper methods
	bool IsObjectiveReachable( const Vector &goalPos );

private:
	CTFBot *m_pBot;
	CountdownTimer m_evaluationTimer;	// Throttle expensive evaluations
};

//----------------------------------------------------------------------------
// Objective-based navigation decision node
// Replaces random wandering with objective-driven movement
//----------------------------------------------------------------------------
class BTObjectiveNavigationNode : public BTDecisionNode
{
public:
	BTObjectiveNavigationNode();
	virtual ~BTObjectiveNavigationNode();

	// Calculate weight for objective navigation
	virtual float CalculateWeight( CTFBot *pBot );

	// Execute objective-based navigation
	virtual bool Execute( CTFBot *pBot );

private:
	CTFBotObjectiveGoals *m_pObjectiveGoals;	// Objective goal selector
	CTFBotPathSelector *m_pPathSelector;		// Path selector (from Story 1.2)

	// Current objective state
	ObjectiveGoal_t m_currentGoal;
	PathInfo_t m_currentPath;
	int m_iCurrentWaypoint;
	CountdownTimer m_goalTimer;				// When to re-evaluate objective

	// Stuck detection
	Vector m_lastPosition;
	float m_flStuckCheckTime;
	float m_flStuckDuration;
};

#endif // TF_BOT_BT_OBJECTIVES_H
