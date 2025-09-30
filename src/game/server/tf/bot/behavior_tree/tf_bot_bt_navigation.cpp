//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_navigation.cpp
// Navigation decision node implementation
// Created: 2025-09-29

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_bt_navigation.h"

extern ConVar tf_bot_path_debug;

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
				// Draw path waypoints for debugging
				for ( int i = 1; i < selectedPath.waypoints.Count(); ++i )
				{
					NDebugOverlay::Line( selectedPath.waypoints[i-1], selectedPath.waypoints[i],
										 0, 255, 0, true, 1.0f );
				}
			}
		}
	}

	return true;
}