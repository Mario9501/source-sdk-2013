//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_navigation.cpp
// Navigation decision node implementation
// Created: 2025-09-29

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_bt_navigation.h"

extern ConVar tf_bot_path_debug;
extern ConVar tf_bot_path_debug_duration;

ConVar tf_bot_path_debug_text( "tf_bot_path_debug_text", "0", FCVAR_GAMEDLL,
							   "Show text labels in path debug (0=visual only, 1=show text)" );

//----------------------------------------------------------------------------
// BTNavigationNode - Constructor
//----------------------------------------------------------------------------
BTNavigationNode::BTNavigationNode()
	: BTDecisionNode( "Navigation" )
	, m_pPathSelector( NULL )
	, m_iCurrentWaypoint( 0 )
	, m_goalPosition( vec3_origin )
{
}

//----------------------------------------------------------------------------
// BTNavigationNode - Destructor
//----------------------------------------------------------------------------
BTNavigationNode::~BTNavigationNode()
{
	if ( m_pPathSelector )
	{
		delete m_pPathSelector;
		m_pPathSelector = NULL;
	}
}

//----------------------------------------------------------------------------
// CalculateWeight - Return weight for navigation action
//----------------------------------------------------------------------------
float BTNavigationNode::CalculateWeight( CTFBot *pBot )
{
	if ( !pBot || !pBot->IsAlive() )
		return 0.0f;

	// Navigation is important but not critical
	// This weight will compete with combat, objectives, etc. in future stories
	return 0.5f;
}

//----------------------------------------------------------------------------
// Execute - Perform navigation with dynamic path selection
// Now with actual bot movement along calculated paths!
//----------------------------------------------------------------------------
bool BTNavigationNode::Execute( CTFBot *pBot )
{
	if ( !pBot )
		return false;

	// Create path selector if needed
	if ( !m_pPathSelector )
	{
		m_pPathSelector = new CTFBotPathSelector( pBot );
	}

	// Update path selector
	m_pPathSelector->Update( gpGlobals->frametime );

	// Get bot's current nav area
	CTFNavArea *pCurrentArea = pBot->GetLastKnownArea();
	if ( !pCurrentArea )
		return true; // No nav area, skip

	// Pick a new goal every 10 seconds (or if we don't have one)
	if ( !m_goalTimer.HasStarted() || m_goalTimer.IsElapsed() )
	{
		// Find areas within a reasonable distance (500-2000 units)
		const float minDist = 500.0f;
		const float maxDist = 2000.0f;

		CUtlVector< CNavArea * > nearbyAreas;
		Vector botPos = pBot->GetAbsOrigin();

		// Collect areas in a radius around the bot
		Extent searchExtent;
		searchExtent.lo = botPos - Vector( maxDist, maxDist, 500.0f );
		searchExtent.hi = botPos + Vector( maxDist, maxDist, 500.0f );

		TheNavMesh->CollectAreasOverlappingExtent( searchExtent, &nearbyAreas );

		// Filter to areas within distance range
		CUtlVector< CNavArea * > validAreas;
		for ( int i = 0; i < nearbyAreas.Count(); ++i )
		{
			float dist = (nearbyAreas[i]->GetCenter() - botPos).Length();
			if ( dist >= minDist && dist <= maxDist )
			{
				validAreas.AddToTail( nearbyAreas[i] );
			}
		}

		if ( validAreas.Count() > 0 )
		{
			int randomIdx = RandomInt( 0, validAreas.Count() - 1 );
			CTFNavArea *pGoalArea = (CTFNavArea *)validAreas[randomIdx];
			m_goalPosition = pGoalArea->GetCenter();

			// Calculate path to new goal
			if ( m_pPathSelector->SelectPath( pBot->GetAbsOrigin(), m_goalPosition, &m_currentPath ) )
			{
				m_iCurrentWaypoint = 0; // Start at first waypoint
				m_goalTimer.Start( 10.0f ); // Pick new goal in 10 seconds
			}
		}
	}

	// Follow the current path if we have one
	if ( m_currentPath.waypoints.Count() > 0 && m_iCurrentWaypoint < m_currentPath.waypoints.Count() )
	{
		Vector currentWaypoint = m_currentPath.waypoints[m_iCurrentWaypoint];
		Vector botPos = pBot->GetAbsOrigin();

		// Move toward current waypoint
		pBot->GetLocomotionInterface()->Approach( currentWaypoint );

		// Check if we're close enough to advance to next waypoint
		float distToWaypoint = (currentWaypoint - botPos).Length();
		if ( distToWaypoint < 50.0f ) // Within 50 units
		{
			m_iCurrentWaypoint++;

			// If we reached the end, the goal timer will pick a new goal
		}

		// Draw debug visualization (throttled to avoid overwhelming renderer)
		// Only draw every 0.5 seconds to match BT evaluation interval
		static float flLastDebugDraw = 0.0f;
		if ( tf_bot_path_debug.GetBool() && (gpGlobals->curtime - flLastDebugDraw) >= 0.5f )
		{
			flLastDebugDraw = gpGlobals->curtime;
			float flDuration = tf_bot_path_debug_duration.GetFloat();

			// Color code based on path type
			int r = 0, g = 255, b = 0; // Default: Green for Primary
			const char *pathTypeName = "PRIMARY";

			switch ( m_currentPath.type )
			{
				case PATH_FLANK:
					r = 0; g = 128; b = 255; // Blue
					pathTypeName = "FLANK";
					break;
				case PATH_ALTERNATIVE:
					r = 255; g = 255; b = 0; // Yellow
					pathTypeName = "ALTERNATIVE";
					break;
				case PATH_SAFE:
					r = 0; g = 255; b = 128; // Cyan
					pathTypeName = "SAFE";
					break;
				case PATH_FAST:
					r = 255; g = 128; b = 0; // Orange
					pathTypeName = "FAST";
					break;
				default:
					r = 0; g = 255; b = 0; // Green for Primary
					pathTypeName = "PRIMARY";
					break;
			}

			// Draw bot's current position marker
			NDebugOverlay::Sphere( botPos, 16.0f, 255, 255, 0, true, flDuration );
			NDebugOverlay::Circle( botPos, 24.0f, 255, 255, 0, 100, true, flDuration );

			// Draw bot's facing direction
			QAngle botAngles = pBot->EyeAngles();
			NDebugOverlay::Axis( botPos, botAngles, 40.0f, true, flDuration );

			// Draw velocity
			Vector velocity = pBot->GetAbsVelocity();
			if ( velocity.Length() > 1.0f )
			{
				Vector endVel = botPos + velocity.Normalized() * 50.0f;
				NDebugOverlay::VertArrow( botPos, endVel, 8.0f, 255, 128, 0, 200, true, flDuration );
			}

			// Draw goal marker
			NDebugOverlay::Sphere( m_goalPosition, 12.0f, 255, 0, 0, true, flDuration );
			NDebugOverlay::Circle( m_goalPosition, 20.0f, 255, 0, 0, 200, true, flDuration );

			// Draw path waypoints with color-coded lines
			for ( int i = 1; i < m_currentPath.waypoints.Count(); ++i )
			{
				NDebugOverlay::Line( m_currentPath.waypoints[i-1], m_currentPath.waypoints[i],
									 r, g, b, true, flDuration );
				NDebugOverlay::Cross3D( m_currentPath.waypoints[i], 6.0f, 255, 255, 255, true, flDuration );
			}

			// Highlight current target waypoint with larger cross
			if ( m_iCurrentWaypoint < m_currentPath.waypoints.Count() )
			{
				NDebugOverlay::Cross3D( currentWaypoint, 12.0f, 255, 255, 0, true, flDuration );
				NDebugOverlay::HorzArrow( botPos, currentWaypoint, 10.0f, 255, 255, 0, 255, true, flDuration );
			}

			// Info text (only if enabled to prevent clutter)
			if ( tf_bot_path_debug_text.GetBool() )
			{
				char szInfo[256];
				Q_snprintf( szInfo, sizeof(szInfo),
					"%s: %s Path\nWP %d/%d (%.0fu)",
					pBot->GetPlayerName(),
					pathTypeName,
					m_iCurrentWaypoint + 1, m_currentPath.waypoints.Count(),
					distToWaypoint );
				NDebugOverlay::Text( botPos + Vector(0, 0, 55), szInfo, true, flDuration );
			}
		}
	}

	return true;
}