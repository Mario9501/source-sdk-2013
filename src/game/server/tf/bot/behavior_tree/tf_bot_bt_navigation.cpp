//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_navigation.cpp
// Navigation decision node implementation
// Created: 2025-09-29

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_bt_navigation.h"

extern ConVar tf_bot_path_debug;
extern ConVar tf_bot_path_debug_duration;

//----------------------------------------------------------------------------
// BTNavigationNode - Constructor
//----------------------------------------------------------------------------
BTNavigationNode::BTNavigationNode()
	: BTDecisionNode( "Navigation" )
	, m_pPathSelector( NULL )
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

	// For Story 1.2, we're just setting up the infrastructure
	// Actual path application to bot navigation will happen in existing behaviors
	// For now, just select a path to demonstrate the system works

	// Get bot's current goal (if any)
	// In a real implementation, this would come from the bot's tactical decision
	// For demonstration, use a nearby nav area as goal
	CTFNavArea *pCurrentArea = pBot->GetLastKnownArea();
	if ( !pCurrentArea )
		return true; // No nav area, skip

	// Find a random nearby area as demonstration goal
	CUtlVector< CNavArea * > nearbyAreas;
	pCurrentArea->CollectAdjacentAreas( &nearbyAreas );

	if ( nearbyAreas.Count() > 0 )
	{
		int randomIdx = RandomInt( 0, nearbyAreas.Count() - 1 );
		CTFNavArea *pGoalArea = (CTFNavArea *)nearbyAreas[randomIdx];

		// Select path using path selector
		PathInfo_t selectedPath;
		if ( m_pPathSelector->SelectPath( pBot->GetAbsOrigin(), pGoalArea->GetCenter(), &selectedPath ) )
		{
			if ( tf_bot_path_debug.GetBool() )
			{
				float flDuration = tf_bot_path_debug_duration.GetFloat();

				// Color code based on path type
				int r = 0, g = 255, b = 0; // Default: Green for Primary
				const char *pathTypeName = "PRIMARY";

				switch ( selectedPath.type )
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

				// Draw bot's current position marker (large yellow sphere)
				NDebugOverlay::Sphere( pBot->GetAbsOrigin(), 16.0f, 255, 255, 0, true, flDuration );

				// Draw start marker (green sphere)
				if ( selectedPath.waypoints.Count() > 0 )
				{
					NDebugOverlay::Sphere( selectedPath.waypoints[0], 12.0f, 0, 255, 0, true, flDuration );

					// Draw end marker (red sphere)
					Vector endPos = selectedPath.waypoints[selectedPath.waypoints.Count() - 1];
					NDebugOverlay::Sphere( endPos, 12.0f, 255, 0, 0, true, flDuration );

					// Draw text label at end showing path info
					char szLabel[128];
					Q_snprintf( szLabel, sizeof(szLabel), "%s\n%.1f units\n%s",
							   pathTypeName, selectedPath.length, pBot->GetPlayerName() );
					NDebugOverlay::Text( endPos + Vector(0, 0, 20), szLabel, true, flDuration );
				}

				// Draw path waypoints with color-coded lines
				for ( int i = 1; i < selectedPath.waypoints.Count(); ++i )
				{
					// Main path line (thicker)
					NDebugOverlay::Line( selectedPath.waypoints[i-1], selectedPath.waypoints[i],
										 r, g, b, true, flDuration );

					// Draw small waypoint markers (white dots)
					NDebugOverlay::Sphere( selectedPath.waypoints[i], 4.0f, 255, 255, 255, true, flDuration );
				}

				// Draw arrow from bot to first waypoint
				if ( selectedPath.waypoints.Count() > 0 )
				{
					NDebugOverlay::HorzArrow( pBot->GetAbsOrigin(), selectedPath.waypoints[0],
											  8.0f, 255, 255, 0, 200, true, flDuration );
				}
			}
		}
	}

	return true;
}