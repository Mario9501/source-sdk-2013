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
				Vector botPos = pBot->GetAbsOrigin();
				NDebugOverlay::Sphere( botPos, 16.0f, 255, 255, 0, true, flDuration );

				// Draw bot's velocity/facing direction as a line
				Vector velocity = pBot->GetAbsVelocity();
				if ( velocity.Length() > 1.0f )
				{
					Vector endVel = botPos + velocity.Normalized() * 50.0f;
					NDebugOverlay::Line( botPos, endVel, 255, 128, 0, true, flDuration );
					NDebugOverlay::Sphere( endVel, 6.0f, 255, 128, 0, true, flDuration );
				}

				// Draw start marker (green sphere)
				if ( selectedPath.waypoints.Count() > 0 )
				{
					NDebugOverlay::Sphere( selectedPath.waypoints[0], 12.0f, 0, 255, 0, true, flDuration );

					// Draw end marker (red sphere)
					Vector endPos = selectedPath.waypoints[selectedPath.waypoints.Count() - 1];
					NDebugOverlay::Sphere( endPos, 12.0f, 255, 0, 0, true, flDuration );

					// Draw text label at end showing path info
					char szLabel[256];
					Q_snprintf( szLabel, sizeof(szLabel), "=== %s ===\nPath Type: %s\nDistance: %.1f units\nWaypoints: %d\n(Demo: random goal)",
							   pBot->GetPlayerName(), pathTypeName, selectedPath.length, selectedPath.waypoints.Count() );
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

				// Draw direction indicator from bot to first waypoint
				if ( selectedPath.waypoints.Count() > 0 )
				{
					Vector botPos = pBot->GetAbsOrigin();
					Vector firstWaypoint = selectedPath.waypoints[0];

					// Draw thick yellow line from bot to first waypoint
					NDebugOverlay::Line( botPos, firstWaypoint, 255, 255, 0, true, flDuration );

					// Draw chevron/arrow head at the waypoint to show direction
					Vector toWaypoint = firstWaypoint - botPos;
					toWaypoint.NormalizeInPlace();
					Vector perpendicular( -toWaypoint.y, toWaypoint.x, 0 );
					perpendicular.NormalizeInPlace();

					// Create arrow head pointing toward waypoint
					Vector arrowBase = firstWaypoint - (toWaypoint * 10.0f);
					Vector arrowLeft = arrowBase + (perpendicular * 8.0f);
					Vector arrowRight = arrowBase - (perpendicular * 8.0f);

					NDebugOverlay::Line( firstWaypoint, arrowLeft, 255, 255, 0, true, flDuration );
					NDebugOverlay::Line( firstWaypoint, arrowRight, 255, 255, 0, true, flDuration );
					NDebugOverlay::Line( arrowLeft, arrowRight, 255, 255, 0, true, flDuration );

					// Add info text near bot showing what's happening
					char szBotInfo[256];
					Q_snprintf( szBotInfo, sizeof(szBotInfo),
						"Next Waypoint: %.1f units\n"
						"Total Path: %.1f units\n"
						"Yellow Sphere = Bot Position\n"
						"Orange Line = Movement Direction\n"
						"Yellow Arrow = Path Direction",
						(firstWaypoint - botPos).Length(),
						selectedPath.length );
					NDebugOverlay::Text( botPos + Vector(0, 0, 50), szBotInfo, true, flDuration );
				}

				// Draw color legend
				char szLegend[256];
				Q_snprintf( szLegend, sizeof(szLegend),
					"PATH COLORS:\n"
					"Green = Primary (shortest)\n"
					"Blue = Flanking\n"
					"Yellow = Alternative\n"
					"Cyan = Safe (Heavy)\n"
					"Orange = Fast (Scout)" );
				NDebugOverlay::Text( botPos + Vector(50, 50, 30), szLegend, true, flDuration );
			}
		}
	}

	return true;
}