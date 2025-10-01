//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_objectives.cpp
// Objective-based goal selection implementation
// Story 1.3: Objective-Based Goal Selection
// Created: 2025-09-30

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_bt_objectives.h"
#include "team_control_point.h"
#include "team_control_point_master.h"
#include "team_train_watcher.h"
#include "entity_capture_flag.h"
#include "tf_gamerules.h"
#include "debugoverlay_shared.h"

// ConVars for objective selection tuning
ConVar tf_bot_objective_eval_interval( "tf_bot_objective_eval_interval", "3.0", FCVAR_GAMEDLL,
									  "How often to re-evaluate objective goals (seconds)", true, 1.0f, true, 10.0f );

extern ConVar tf_bot_path_debug;
extern ConVar tf_bot_path_debug_duration;

//----------------------------------------------------------------------------
// CTFBotObjectiveGoals - Constructor
//----------------------------------------------------------------------------
CTFBotObjectiveGoals::CTFBotObjectiveGoals( CTFBot *pBot )
	: m_pBot( pBot )
{
	// Start with immediate evaluation
	m_evaluationTimer.Invalidate();
}

//----------------------------------------------------------------------------
// CTFBotObjectiveGoals - Destructor
//----------------------------------------------------------------------------
CTFBotObjectiveGoals::~CTFBotObjectiveGoals()
{
}

//----------------------------------------------------------------------------
// Update - Periodic update
//----------------------------------------------------------------------------
void CTFBotObjectiveGoals::Update( float deltaTime )
{
	// Nothing to update currently - evaluation happens on-demand
}

//----------------------------------------------------------------------------
// SelectObjectiveGoal - Main evaluation method
// Returns the highest priority objective for the bot
//----------------------------------------------------------------------------
bool CTFBotObjectiveGoals::SelectObjectiveGoal( ObjectiveGoal_t *outGoal )
{
	if ( !m_pBot || !outGoal )
		return false;

	// Throttle expensive evaluations
	float evalInterval = tf_bot_objective_eval_interval.GetFloat();
	if ( m_evaluationTimer.HasStarted() && !m_evaluationTimer.IsElapsed() )
	{
		// Use cached goal (will be filled by caller)
		return false;
	}

	m_evaluationTimer.Start( evalInterval );

	CUtlVector< ObjectiveGoal_t > potentialGoals;

	// Evaluate different objective types based on game mode
	EvaluateControlPoints( &potentialGoals );
	EvaluateCTFObjectives( &potentialGoals );
	EvaluatePayloadObjectives( &potentialGoals );

	// Find highest priority goal
	if ( potentialGoals.Count() == 0 )
	{
		DevMsg( "[Objectives] %s (%s): No objectives found!\n",
				m_pBot->GetPlayerName(),
				m_pBot->GetTeamNumber() == TF_TEAM_RED ? "RED" : "BLU" );
		return false;
	}

	ObjectiveGoal_t *pBestGoal = &potentialGoals[0];
	for ( int i = 1; i < potentialGoals.Count(); ++i )
	{
		if ( potentialGoals[i].priority > pBestGoal->priority )
		{
			pBestGoal = &potentialGoals[i];
		}
	}

	// Debug output
	const char *goalTypeNames[] = { "NONE", "CAPTURE", "DEFEND", "FETCH_FLAG", "DELIVER_FLAG", "PUSH_PAYLOAD", "BLOCK_PAYLOAD" };
	DevMsg( "[Objectives] %s (%s): Selected %s (priority %.1f) from %d options\n",
			m_pBot->GetPlayerName(),
			m_pBot->GetTeamNumber() == TF_TEAM_RED ? "RED" : "BLU",
			goalTypeNames[pBestGoal->type],
			pBestGoal->priority,
			potentialGoals.Count() );

	*outGoal = *pBestGoal;
	return true;
}

//----------------------------------------------------------------------------
// EvaluateControlPoints - Evaluate capture/defend point objectives
//----------------------------------------------------------------------------
void CTFBotObjectiveGoals::EvaluateControlPoints( CUtlVector< ObjectiveGoal_t > *goals )
{
	if ( !TFGameRules() )
		return;

	// Get points we can capture
	CUtlVector< CTeamControlPoint * > capturePoints;
	TFGameRules()->CollectCapturePoints( m_pBot, &capturePoints );

	for ( int i = 0; i < capturePoints.Count(); ++i )
	{
		CTeamControlPoint *pPoint = capturePoints[i];
		if ( !pPoint )
			continue;

		// Skip locked points - they can't be captured yet
		if ( pPoint->IsLocked() )
		{
			if ( tf_bot_path_debug.GetBool() )
			{
				DevMsg( "[Objectives] %s: Skipping LOCKED control point %d\n",
						m_pBot->GetPlayerName(), pPoint->GetPointIndex() );
			}
			continue;
		}

		ObjectiveGoal_t goal;
		goal.type = GOAL_CAPTURE_POINT;
		goal.position = pPoint->GetAbsOrigin();
		goal.pEntity = pPoint;
		goal.priority = CalculateControlPointPriority( pPoint, true );

		if ( IsObjectiveReachable( goal.position ) )
		{
			goals->AddToTail( goal );
		}
	}

	// Get points we should defend
	CUtlVector< CTeamControlPoint * > defendPoints;
	TFGameRules()->CollectDefendPoints( m_pBot, &defendPoints );

	for ( int i = 0; i < defendPoints.Count(); ++i )
	{
		CTeamControlPoint *pPoint = defendPoints[i];
		if ( !pPoint )
			continue;

		// Skip locked points
		if ( pPoint->IsLocked() )
		{
			if ( tf_bot_path_debug.GetBool() )
			{
				DevMsg( "[Objectives] %s: Skipping LOCKED defend point %d\n",
						m_pBot->GetPlayerName(), pPoint->GetPointIndex() );
			}
			continue;
		}

		ObjectiveGoal_t goal;
		goal.type = GOAL_DEFEND_POINT;
		goal.position = pPoint->GetAbsOrigin();
		goal.pEntity = pPoint;
		goal.priority = CalculateControlPointPriority( pPoint, false );

		if ( IsObjectiveReachable( goal.position ) )
		{
			goals->AddToTail( goal );
		}
	}
}

//----------------------------------------------------------------------------
// EvaluateCTFObjectives - Evaluate CTF flag objectives
//----------------------------------------------------------------------------
void CTFBotObjectiveGoals::EvaluateCTFObjectives( CUtlVector< ObjectiveGoal_t > *goals )
{
	if ( !TFGameRules() || TFGameRules()->GetGameType() != TF_GAMETYPE_CTF )
		return;

	// Look for flag to fetch
	CCaptureFlag *pFlag = m_pBot->GetFlagToFetch();
	if ( pFlag )
	{
		ObjectiveGoal_t goal;
		goal.type = GOAL_FETCH_FLAG;
		goal.position = pFlag->GetAbsOrigin();
		goal.pEntity = pFlag;
		goal.priority = CalculateFlagPriority( pFlag );

		if ( IsObjectiveReachable( goal.position ) )
		{
			goals->AddToTail( goal );
		}
	}

	// If we're carrying the flag, go to capture zone
	if ( m_pBot->HasTheFlag() )
	{
		CCaptureZone *pCaptureZone = m_pBot->GetFlagCaptureZone();
		if ( pCaptureZone )
		{
			ObjectiveGoal_t goal;
			goal.type = GOAL_DELIVER_FLAG;
			goal.position = pCaptureZone->GetAbsOrigin();
			goal.pEntity = pCaptureZone;
			goal.priority = 100.0f; // Highest priority when carrying flag

			goals->AddToTail( goal );
		}
	}
}

//----------------------------------------------------------------------------
// EvaluatePayloadObjectives - Evaluate payload cart objectives
//----------------------------------------------------------------------------
void CTFBotObjectiveGoals::EvaluatePayloadObjectives( CUtlVector< ObjectiveGoal_t > *goals )
{
	if ( !TFGameRules() )
		return;

	// Find payload cart
	CTeamTrainWatcher *pTrainWatcher = TFGameRules()->GetPayloadToPush( m_pBot->GetTeamNumber() );
	if ( pTrainWatcher )
	{
		CBaseEntity *pTrain = pTrainWatcher->GetTrainEntity();
		if ( pTrain )
		{
			ObjectiveGoal_t goal;
			goal.type = GOAL_PUSH_PAYLOAD;
			goal.position = pTrain->GetAbsOrigin();
			goal.pEntity = pTrain;
			goal.priority = 95.0f; // Very high priority - payload is main objective

			if ( IsObjectiveReachable( goal.position ) )
			{
				goals->AddToTail( goal );
			}
		}
	}

	// Find payload to block (enemy cart)
	pTrainWatcher = TFGameRules()->GetPayloadToBlock( m_pBot->GetTeamNumber() );
	if ( pTrainWatcher )
	{
		CBaseEntity *pTrain = pTrainWatcher->GetTrainEntity();
		if ( pTrain )
		{
			ObjectiveGoal_t goal;
			goal.type = GOAL_BLOCK_PAYLOAD;
			goal.position = pTrain->GetAbsOrigin();
			goal.pEntity = pTrain;
			goal.priority = 70.0f;

			if ( IsObjectiveReachable( goal.position ) )
			{
				goals->AddToTail( goal );
			}
		}
	}
}

//----------------------------------------------------------------------------
// CalculateControlPointPriority - Calculate priority for a control point
//----------------------------------------------------------------------------
float CTFBotObjectiveGoals::CalculateControlPointPriority( CTeamControlPoint *pPoint, bool bCapture )
{
	if ( !pPoint || !m_pBot )
		return 0.0f;

	// Make defense and capture equally important
	float priority = bCapture ? 75.0f : 75.0f;

	// Increase priority if point was recently contested
	if ( pPoint->HasBeenContested() && (gpGlobals->curtime - pPoint->LastContestedAt()) < 30.0f )
	{
		priority += 20.0f;
	}

	// Distance factor - closer points are more attractive
	float dist = (pPoint->GetAbsOrigin() - m_pBot->GetAbsOrigin()).Length();
	float distFactor = 1.0f - clamp( dist / 5000.0f, 0.0f, 0.8f );
	priority *= distFactor;

	return priority;
}

//----------------------------------------------------------------------------
// CalculateFlagPriority - Calculate priority for CTF flag
//----------------------------------------------------------------------------
float CTFBotObjectiveGoals::CalculateFlagPriority( CCaptureFlag *pFlag )
{
	if ( !pFlag )
		return 0.0f;

	float priority = 85.0f; // CTF is high priority

	// Distance factor
	float dist = (pFlag->GetAbsOrigin() - m_pBot->GetAbsOrigin()).Length();
	float distFactor = 1.0f - clamp( dist / 5000.0f, 0.0f, 0.7f );
	priority *= distFactor;

	return priority;
}

//----------------------------------------------------------------------------
// IsObjectiveReachable - Check if bot can reach this objective
//----------------------------------------------------------------------------
bool CTFBotObjectiveGoals::IsObjectiveReachable( const Vector &goalPos )
{
	if ( !m_pBot )
		return false;

	Vector botPos = m_pBot->GetAbsOrigin();

	// Check if there's a nav area at both bot and goal positions
	CTFNavArea *pStartArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( botPos );
	CTFNavArea *pGoalArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( goalPos );

	if ( !pStartArea || !pGoalArea )
		return false;

	// Verify that a path actually exists from bot to goal
	ShortestPathCost costFunc;
	if ( !NavAreaBuildPath( pStartArea, pGoalArea, &goalPos, costFunc, NULL, 0.0f, m_pBot->GetTeamNumber() ) )
	{
		if ( tf_bot_path_debug.GetBool() )
		{
			DevMsg( "[Objectives] %s: Objective at (%.0f, %.0f, %.0f) is UNREACHABLE - no path exists!\n",
					m_pBot->GetPlayerName(), goalPos.x, goalPos.y, goalPos.z );
		}
		return false;
	}

	return true;
}

//----------------------------------------------------------------------------
// BTObjectiveNavigationNode - Constructor
//----------------------------------------------------------------------------
BTObjectiveNavigationNode::BTObjectiveNavigationNode()
	: BTDecisionNode( "ObjectiveNavigation" )
	, m_pObjectiveGoals( NULL )
	, m_pPathSelector( NULL )
	, m_iCurrentWaypoint( 0 )
	, m_lastPosition( vec3_origin )
	, m_flStuckCheckTime( 0.0f )
	, m_flStuckDuration( 0.0f )
{
}

//----------------------------------------------------------------------------
// BTObjectiveNavigationNode - Destructor
//----------------------------------------------------------------------------
BTObjectiveNavigationNode::~BTObjectiveNavigationNode()
{
	if ( m_pObjectiveGoals )
	{
		delete m_pObjectiveGoals;
		m_pObjectiveGoals = NULL;
	}

	if ( m_pPathSelector )
	{
		delete m_pPathSelector;
		m_pPathSelector = NULL;
	}
}

//----------------------------------------------------------------------------
// CalculateWeight - Return weight for objective navigation
//----------------------------------------------------------------------------
float BTObjectiveNavigationNode::CalculateWeight( CTFBot *pBot )
{
	if ( !pBot || !pBot->IsAlive() )
		return 0.0f;

	// Objective navigation is the highest priority for now
	return 1.0f;
}

//----------------------------------------------------------------------------
// Execute - Perform objective-based navigation
//----------------------------------------------------------------------------
bool BTObjectiveNavigationNode::Execute( CTFBot *pBot )
{
	if ( !pBot )
		return false;

	// Create subsystems if needed
	if ( !m_pObjectiveGoals )
	{
		m_pObjectiveGoals = new CTFBotObjectiveGoals( pBot );
	}

	if ( !m_pPathSelector )
	{
		m_pPathSelector = new CTFBotPathSelector( pBot );
	}

	// Update subsystems
	m_pObjectiveGoals->Update( gpGlobals->frametime );
	m_pPathSelector->Update( gpGlobals->frametime );

	// Re-evaluate objective periodically
	if ( !m_goalTimer.HasStarted() || m_goalTimer.IsElapsed() )
	{
		ObjectiveGoal_t newGoal;
		if ( m_pObjectiveGoals->SelectObjectiveGoal( &newGoal ) )
		{
			m_currentGoal = newGoal;

			// Calculate path to new objective
			if ( m_pPathSelector->SelectPath( pBot->GetAbsOrigin(), m_currentGoal.position, &m_currentPath ) )
			{
				m_iCurrentWaypoint = 0;
				m_goalTimer.Start( tf_bot_objective_eval_interval.GetFloat() );
			}
			else
			{
				// Path failed - objective might be unreachable, retry sooner
				DevMsg( "[Objectives] %s: Failed to find path to objective, re-evaluating in 1 second\n",
						pBot->GetPlayerName() );
				m_goalTimer.Start( 1.0f );
			}
		}
		else
		{
			// No objective found, retry sooner
			DevMsg( "[Objectives] %s: No valid objectives found, re-evaluating in 1 second\n",
					pBot->GetPlayerName() );
			m_goalTimer.Start( 1.0f );
		}
	}

	// Safety check: if we have an objective but no valid path, force re-evaluation
	if ( m_currentPath.waypoints.Count() == 0 && m_goalTimer.HasStarted() )
	{
		DevMsg( "[Objectives] %s: Have objective but no path - forcing re-evaluation\n",
				pBot->GetPlayerName() );
		m_goalTimer.Invalidate();
	}

	// Follow current path
	if ( m_currentPath.waypoints.Count() > 0 && m_iCurrentWaypoint < m_currentPath.waypoints.Count() )
	{
		Vector currentWaypoint = m_currentPath.waypoints[m_iCurrentWaypoint];
		Vector botPos = pBot->GetAbsOrigin();

		// Debug visualization
		static float flLastDebugDraw = 0.0f;
		if ( tf_bot_path_debug.GetBool() && (gpGlobals->curtime - flLastDebugDraw) >= 0.5f )
		{
			flLastDebugDraw = gpGlobals->curtime;
			float flDuration = tf_bot_path_debug_duration.GetFloat();

			// Color code based on path type
			int r = 0, g = 255, b = 0; // Default: Green for Primary
			switch ( m_currentPath.type )
			{
				case PATH_FLANK:
					r = 0; g = 128; b = 255; break; // Blue
				case PATH_ALTERNATIVE:
					r = 255; g = 255; b = 0; break; // Yellow
				case PATH_SAFE:
					r = 0; g = 255; b = 128; break; // Cyan
				case PATH_FAST:
					r = 255; g = 128; b = 0; break; // Orange
			}

			// Draw bot marker
			NDebugOverlay::Sphere( botPos, 16.0f, 255, 255, 0, true, flDuration );

			// Draw objective goal
			NDebugOverlay::Sphere( m_currentGoal.position, 12.0f, 255, 0, 0, true, flDuration );

			// Draw path waypoints
			for ( int i = 1; i < m_currentPath.waypoints.Count(); ++i )
			{
				NDebugOverlay::Line( m_currentPath.waypoints[i-1], m_currentPath.waypoints[i],
									 r, g, b, true, flDuration );
			}

			// Highlight current target waypoint
			NDebugOverlay::Cross3D( currentWaypoint, 12.0f, 255, 255, 0, true, flDuration );
			NDebugOverlay::HorzArrow( botPos, currentWaypoint, 10.0f, 255, 255, 0, 255, true, flDuration );
		}

		// Move toward current waypoint
		pBot->GetLocomotionInterface()->Approach( currentWaypoint );

		// Face toward the waypoint we're moving to
		Vector toWaypoint = currentWaypoint - botPos;
		if ( toWaypoint.Length() > 10.0f )
		{
			QAngle desiredAngles;
			VectorAngles( toWaypoint, desiredAngles );
			pBot->GetLocomotionInterface()->FaceTowards( currentWaypoint );
		}

		// Advance waypoint when close
		float distToWaypoint = (currentWaypoint - botPos).Length();
		if ( distToWaypoint < 50.0f )
		{
			m_iCurrentWaypoint++;

			// Reset stuck detection when reaching waypoint
			m_flStuckDuration = 0.0f;
			m_lastPosition = botPos;
			m_flStuckCheckTime = gpGlobals->curtime;
		}
		else
		{
			// Stuck detection - check if bot hasn't moved in a while
			if ( m_flStuckCheckTime == 0.0f )
			{
				// Initialize stuck check
				m_lastPosition = botPos;
				m_flStuckCheckTime = gpGlobals->curtime;
				m_flStuckDuration = 0.0f;
			}
			else if ( gpGlobals->curtime - m_flStuckCheckTime >= 1.0f )
			{
				// Check if bot moved significantly in the last second
				float distMoved = (botPos - m_lastPosition).Length();

				if ( distMoved < 50.0f ) // Bot hasn't moved much
				{
					m_flStuckDuration += (gpGlobals->curtime - m_flStuckCheckTime);

					// If stuck for more than 3 seconds, try next waypoint or repath
					if ( m_flStuckDuration >= 3.0f )
					{
						DevMsg( "[Objectives] %s: STUCK for %.1f seconds! Trying recovery...\n",
								pBot->GetPlayerName(), m_flStuckDuration );

						// First try: skip to next waypoint
						if ( m_iCurrentWaypoint + 1 < m_currentPath.waypoints.Count() )
						{
							DevMsg( "[Objectives] %s: Skipping waypoint %d -> %d\n",
									pBot->GetPlayerName(), m_iCurrentWaypoint, m_iCurrentWaypoint + 1 );
							m_iCurrentWaypoint++;
							m_flStuckDuration = 0.0f;
						}
						else
						{
							// Can't skip waypoint, force immediate re-path
							DevMsg( "[Objectives] %s: No more waypoints to skip - forcing re-evaluation\n",
									pBot->GetPlayerName() );
							m_goalTimer.Invalidate();
							m_flStuckDuration = 0.0f;
						}
					}
				}
				else
				{
					// Bot is moving, reset stuck counter
					m_flStuckDuration = 0.0f;
				}

				// Update for next check
				m_lastPosition = botPos;
				m_flStuckCheckTime = gpGlobals->curtime;
			}
		}
	}

	return true;
}
