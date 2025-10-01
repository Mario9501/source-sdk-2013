//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_path_selector.cpp
// Dynamic Path Selection and Navigation Enhancement Implementation
// Created: 2025-09-29

#include "cbase.h"
#include "tf_bot.h"
#include "tf_bot_path_selector.h"
#include "../nav_mesh/tf_nav_mesh.h"
#include "nav_pathfind.h"
#include "tf_shareddefs.h"

// ConVars for path selection
ConVar tf_bot_path_variation_chance( "tf_bot_path_variation_chance", "0.35", FCVAR_NOTIFY | FCVAR_GAMEDLL,
									 "Probability that bot will take alternative/flanking path (0.0-1.0, default: 0.35)",
									 true, 0.0f, true, 1.0f );

ConVar tf_bot_path_recalc_interval( "tf_bot_path_recalc_interval", "5.0", FCVAR_GAMEDLL,
								   "Seconds before bot reconsiders current path (default: 5.0)",
								   true, 1.0f, true, 30.0f );

ConVar tf_bot_path_debug( "tf_bot_path_debug", "0", FCVAR_GAMEDLL,
						  "Debug path selection (draws paths, prints selection info)" );

ConVar tf_bot_path_debug_duration( "tf_bot_path_debug_duration", "2.0", FCVAR_GAMEDLL,
								   "How long debug path visualizations stay on screen (seconds)",
								   true, 0.1f, true, 10.0f );

//----------------------------------------------------------------------------
// CTFBotPathSelector - Constructor
//----------------------------------------------------------------------------
CTFBotPathSelector::CTFBotPathSelector( CTFBot *pBot )
	: m_pBot( pBot )
	, m_currentPathType( PATH_PRIMARY )
	, m_lastGoal( vec3_origin )
	, m_flPathVariationChance( 0.35f )
{
	m_pathRecalcTimer.Invalidate();
}

//----------------------------------------------------------------------------
// CTFBotPathSelector - Destructor
//----------------------------------------------------------------------------
CTFBotPathSelector::~CTFBotPathSelector()
{
}

//----------------------------------------------------------------------------
// Update - Check path recalculation timer
//----------------------------------------------------------------------------
void CTFBotPathSelector::Update( float deltaTime )
{
	// Update variation chance from ConVar
	m_flPathVariationChance = tf_bot_path_variation_chance.GetFloat();
}

//----------------------------------------------------------------------------
// SelectPath - Main path selection interface
//----------------------------------------------------------------------------
bool CTFBotPathSelector::SelectPath( const Vector &start, const Vector &goal, PathInfo_t *outPath )
{
	if ( !outPath || !m_pBot )
		return false;

	// Check if goal has changed significantly
	float flGoalChange = ( goal - m_lastGoal ).Length();
	if ( flGoalChange > 500.0f )
	{
		// Goal changed significantly, invalidate timer
		m_pathRecalcTimer.Invalidate();
		m_lastGoal = goal;
	}

	// Check if we need to recalculate path
	float flRecalcInterval = tf_bot_path_recalc_interval.GetFloat();
	if ( !m_pathRecalcTimer.HasStarted() || m_pathRecalcTimer.IsElapsed() )
	{
		// Time to recalculate - select best path
		PathInfo_t *pSelectedPath = SelectBestPath( start, goal );
		if ( pSelectedPath )
		{
			outPath->CopyFrom( *pSelectedPath );
			m_currentPathType = pSelectedPath->type;

			// Restart timer
			m_pathRecalcTimer.Start( flRecalcInterval );

			if ( tf_bot_path_debug.GetBool() )
			{
				const char *pathTypeName = "Primary";
				switch ( m_currentPathType )
				{
					case PATH_FLANK: pathTypeName = "Flank"; break;
					case PATH_ALTERNATIVE: pathTypeName = "Alternative"; break;
					case PATH_SAFE: pathTypeName = "Safe"; break;
					case PATH_FAST: pathTypeName = "Fast"; break;
				}

				DevMsg( "[TF Bot Path] %s: Selected %s path (%.1f units)\n",
						m_pBot->GetPlayerName(), pathTypeName, pSelectedPath->length );
			}

			return true;
		}
	}

	// No recalculation needed or failed, return primary path
	return ComputePrimaryPath( start, goal, outPath );
}

//----------------------------------------------------------------------------
// SelectBestPath - Evaluate and select best path based on preferences
//----------------------------------------------------------------------------
PathInfo_t *CTFBotPathSelector::SelectBestPath( const Vector &start, const Vector &goal )
{
	static PathInfo_t s_primaryPath;
	static PathInfo_t s_selectedPath;
	static PathInfo_t s_flankPath;  // Single static flank path to avoid copy issues

	// Always compute primary path as fallback
	if ( !ComputePrimaryPath( start, goal, &s_primaryPath ) )
	{
		return NULL;
	}

	// Decide whether to consider alternative paths
	float flRandom = RandomFloat( 0.0f, 1.0f );
	if ( flRandom > m_flPathVariationChance )
	{
		// Stick with primary path (80-90% of the time)
		s_selectedPath.CopyFrom( s_primaryPath );
		return &s_selectedPath;
	}

	// Try to find a flanking path
	bool bHasFlankPath = ComputeFlankingPath( start, goal, &s_flankPath );

	if ( !bHasFlankPath )
	{
		// No alternatives found, use primary
		s_selectedPath.CopyFrom( s_primaryPath );
		return &s_selectedPath;
	}

	// Calculate costs for primary and flank paths including class preferences
	float flPrimaryCost = CalculatePathCost( s_primaryPath, m_pBot );
	float flFlankCost = CalculatePathCost( s_flankPath, m_pBot );

	if ( tf_bot_path_debug.GetBool() )
	{
		DevMsg( "[Path] %s: Primary=%.1f units (cost %.1f), Flank=%.1f units (cost %.1f)\n",
				m_pBot->GetPlayerName(),
				s_primaryPath.length, flPrimaryCost,
				s_flankPath.length, flFlankCost );
	}

	PathInfo_t *pBestPath = ( flFlankCost < flPrimaryCost ) ? &s_flankPath : &s_primaryPath;

	s_selectedPath.CopyFrom( *pBestPath );
	return &s_selectedPath;
}

//----------------------------------------------------------------------------
// ComputePrimaryPath - Calculate shortest/direct route
//----------------------------------------------------------------------------
bool CTFBotPathSelector::ComputePrimaryPath( const Vector &start, const Vector &goal, PathInfo_t *outPath )
{
	if ( !outPath )
		return false;

	// Use existing nav mesh pathfinding
	CTFNavArea *pStartArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( start );
	CTFNavArea *pGoalArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( goal );

	if ( !pStartArea || !pGoalArea )
		return false;

	// Build path using nav mesh with ShortestPathCost functor
	ShortestPathCost costFunc;
	if ( !NavAreaBuildPath( pStartArea, pGoalArea, &goal, costFunc, NULL, 0.0f, TEAM_ANY, false ) )
		return false;

	// Convert area path to waypoints
	CTFNavArea *pArea = pGoalArea;
	outPath->waypoints.RemoveAll();
	outPath->waypoints.AddToTail( goal );

	while ( pArea && pArea != pStartArea )
	{
		outPath->waypoints.AddToTail( pArea->GetCenter() );
		pArea = (CTFNavArea *)pArea->GetParent();
	}

	if ( pArea )
	{
		outPath->waypoints.AddToTail( start );
	}

	// Reverse to get start->goal order
	for ( int i = 0; i < outPath->waypoints.Count() / 2; ++i )
	{
		Vector temp = outPath->waypoints[i];
		int j = outPath->waypoints.Count() - 1 - i;
		outPath->waypoints[i] = outPath->waypoints[j];
		outPath->waypoints[j] = temp;
	}

	outPath->type = PATH_PRIMARY;
	outPath->length = CalculatePathLength( outPath->waypoints );
	outPath->cost = outPath->length;

	return ValidatePath( *outPath );
}

//----------------------------------------------------------------------------
// ComputeFlankingPath - Calculate route that approaches from different angle
//----------------------------------------------------------------------------
bool CTFBotPathSelector::ComputeFlankingPath( const Vector &start, const Vector &goal, PathInfo_t *outPath )
{
	if ( !outPath )
		return false;

	CTFNavArea *pStartArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( start );
	CTFNavArea *pGoalArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( goal );
	if ( !pStartArea || !pGoalArea )
		return false;

	// Collect areas around the goal within a reasonable radius
	// Use 2-hop neighbors to get areas further away for better flanking
	CUtlVector< CNavArea * > nearbyAreas;
	CUtlVector< CNavArea * > visitedAreas;

	// First, get direct neighbors
	((CNavArea *)pGoalArea)->CollectAdjacentAreas( &nearbyAreas );
	visitedAreas.AddToTail( (CNavArea *)pGoalArea );

	// Then get neighbors of neighbors for more flank options
	CUtlVector< CNavArea * > firstHop;
	firstHop.AddVectorToTail( nearbyAreas );
	for ( int i = 0; i < firstHop.Count() && i < 10; ++i ) // Limit to prevent explosion
	{
		CUtlVector< CNavArea * > tempAdjacent;
		firstHop[i]->CollectAdjacentAreas( &tempAdjacent );

		for ( int j = 0; j < tempAdjacent.Count(); ++j )
		{
			// Only add if not already in our list
			if ( !visitedAreas.HasElement( tempAdjacent[j] ) && !nearbyAreas.HasElement( tempAdjacent[j] ) )
			{
				nearbyAreas.AddToTail( tempAdjacent[j] );
				visitedAreas.AddToTail( tempAdjacent[j] );
			}
		}
	}

	if ( nearbyAreas.Count() < 2 )
		return false; // Not enough areas to flank

	// Try multiple random flank candidates to find a valid one
	int maxAttempts = Min( 5, nearbyAreas.Count() );
	CUtlVector< int > triedIndices;

	for ( int attempt = 0; attempt < maxAttempts; ++attempt )
	{
		// Pick a random area we haven't tried yet
		int randomIdx;
		do {
			randomIdx = RandomInt( 0, nearbyAreas.Count() - 1 );
		} while ( triedIndices.HasElement( randomIdx ) && triedIndices.Count() < nearbyAreas.Count() );

		triedIndices.AddToTail( randomIdx );
		CTFNavArea *pFlankArea = (CTFNavArea *)nearbyAreas[randomIdx];

		// Build path: start -> flank area -> goal
		PathInfo_t pathToFlank;
		if ( !ComputePrimaryPath( start, pFlankArea->GetCenter(), &pathToFlank ) )
			continue;

		PathInfo_t pathFromFlank;
		if ( !ComputePrimaryPath( pFlankArea->GetCenter(), goal, &pathFromFlank ) )
			continue;

		// Combine paths
		outPath->waypoints.RemoveAll();
		outPath->waypoints.AddVectorToTail( pathToFlank.waypoints );
		outPath->waypoints.AddVectorToTail( pathFromFlank.waypoints );

		outPath->type = PATH_FLANK;
		outPath->length = CalculatePathLength( outPath->waypoints );

		// Check if flanking path is reasonable
		PathInfo_t primaryPath;
		if ( !ComputePrimaryPath( start, goal, &primaryPath ) )
			continue;

		if ( !IsPathReasonable( primaryPath, *outPath ) )
		{
			if ( tf_bot_path_debug.GetBool() && attempt == maxAttempts - 1 )
			{
				DevMsg( "[Path] All flank paths rejected (last: %.1f vs primary %.1f, ratio %.2f)\n",
						outPath->length, primaryPath.length, outPath->length / primaryPath.length );
			}
			continue;
		}

		// Found a valid flank path!
		if ( ValidatePath( *outPath ) )
		{
			if ( tf_bot_path_debug.GetBool() )
			{
				DevMsg( "[Path] Found valid flank path on attempt %d/%d\n", attempt + 1, maxAttempts );
			}
			return true;
		}
	}

	return false; // No valid flank path found after all attempts
}

//----------------------------------------------------------------------------
// ValidatePath - Check if path is valid and safe
//----------------------------------------------------------------------------
bool CTFBotPathSelector::ValidatePath( const PathInfo_t &path )
{
	if ( path.waypoints.Count() < 2 )
		return false;

	// Check each waypoint is on valid nav area
	for ( int i = 0; i < path.waypoints.Count(); ++i )
	{
		CTFNavArea *pArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( path.waypoints[i] );
		if ( !pArea || pArea->IsBlocked( TEAM_ANY ) )
			return false;
	}

	return true;
}

//----------------------------------------------------------------------------
// CalculatePathCost - Compute cost with class preferences and randomization
//----------------------------------------------------------------------------
float CTFBotPathSelector::CalculatePathCost( const PathInfo_t &path, CTFBot *pBot )
{
	if ( !pBot )
		return path.length;

	// Base cost is path length
	float flCost = path.length;

	// Apply class-based preference modifier
	float flPreference = GetClassPathPreference( path.type, pBot );
	flCost *= flPreference;

	// Add per-bot randomization (±15%) to create path diversity
	// This ensures different bots pick different paths even when going to same objective
	float flRandomFactor = RandomFloat( 0.85f, 1.15f );
	flCost *= flRandomFactor;

	return flCost;
}

//----------------------------------------------------------------------------
// GetClassPathPreference - Class-based path type preferences
//----------------------------------------------------------------------------
float CTFBotPathSelector::GetClassPathPreference( EPathType pathType, CTFBot *pBot )
{
	if ( !pBot )
		return 1.0f;

	int playerClass = pBot->GetPlayerClass()->GetClassIndex();

	switch ( playerClass )
	{
		case TF_CLASS_SCOUT:
			// Scout prefers fast/direct routes
			if ( pathType == PATH_FAST || pathType == PATH_PRIMARY )
				return 0.8f;  // 20% cost reduction
			return 1.0f;

		case TF_CLASS_HEAVYWEAPONS:
			// Heavy prefers safe/covered routes
			if ( pathType == PATH_SAFE )
				return 0.7f;  // 30% cost reduction
			return 1.2f;  // Penalize risky routes

		case TF_CLASS_SPY:
			// Spy prefers flanking routes
			if ( pathType == PATH_FLANK )
				return 0.6f;  // 40% cost reduction
			return 1.0f;

		case TF_CLASS_ENGINEER:
			// Engineer prefers safe routes near build spots
			if ( pathType == PATH_SAFE || pathType == PATH_PRIMARY )
				return 0.9f;
			return 1.0f;

		default:
			return 1.0f;
	}
}

//----------------------------------------------------------------------------
// CalculatePathLength - Sum distance between waypoints
//----------------------------------------------------------------------------
float CTFBotPathSelector::CalculatePathLength( const CUtlVector< Vector > &waypoints )
{
	float flLength = 0.0f;

	for ( int i = 1; i < waypoints.Count(); ++i )
	{
		flLength += ( waypoints[i] - waypoints[i-1] ).Length();
	}

	return flLength;
}

//----------------------------------------------------------------------------
// IsPathReasonable - Check if alternative path is not excessively long
//----------------------------------------------------------------------------
bool CTFBotPathSelector::IsPathReasonable( const PathInfo_t &primaryPath, const PathInfo_t &alternatePath )
{
	// Alternative path shouldn't be more than 2.0x the length of primary
	// Increased from 1.5x to allow more path variation
	const float MAX_LENGTH_RATIO = 2.0f;

	return ( alternatePath.length <= primaryPath.length * MAX_LENGTH_RATIO );
}