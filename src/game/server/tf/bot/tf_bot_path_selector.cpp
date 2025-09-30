//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_path_selector.cpp
// Dynamic Path Selection and Navigation Enhancement Implementation
// Created: 2025-09-29

#include "cbase.h"
#include "tf_bot.h"
#include "tf_bot_path_selector.h"
#include "../nav_mesh/tf_nav_mesh.h"

// ConVars for path selection
ConVar tf_bot_path_variation_chance( "tf_bot_path_variation_chance", "0.15", FCVAR_NOTIFY | FCVAR_GAMEDLL,
									 "Probability that bot will take alternative/flanking path (0.0-1.0, default: 0.15)",
									 true, 0.0f, true, 1.0f );

ConVar tf_bot_path_recalc_interval( "tf_bot_path_recalc_interval", "5.0", FCVAR_GAMEDLL,
								   "Seconds before bot reconsiders current path (default: 5.0)",
								   true, 1.0f, true, 30.0f );

ConVar tf_bot_path_debug( "tf_bot_path_debug", "0", FCVAR_GAMEDLL,
						  "Debug path selection (draws paths, prints selection info)" );

//----------------------------------------------------------------------------
// CTFBotPathSelector - Constructor
//----------------------------------------------------------------------------
CTFBotPathSelector::CTFBotPathSelector( CTFBot *pBot )
	: m_pBot( pBot )
	, m_currentPathType( PATH_PRIMARY )
	, m_lastGoal( vec3_origin )
	, m_flPathVariationChance( 0.15f )
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
			*outPath = *pSelectedPath;
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
	static CUtlVector< PathInfo_t > s_alternativePaths;

	s_alternativePaths.RemoveAll();

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
		s_selectedPath = s_primaryPath;
		return &s_selectedPath;
	}

	// Find alternative paths
	FindAlternativePaths( start, goal, s_alternativePaths );

	if ( s_alternativePaths.Count() == 0 )
	{
		// No alternatives found, use primary
		s_selectedPath = s_primaryPath;
		return &s_selectedPath;
	}

	// Calculate costs for all paths including class preferences
	float flBestCost = CalculatePathCost( s_primaryPath, m_pBot );
	PathInfo_t *pBestPath = &s_primaryPath;

	for ( int i = 0; i < s_alternativePaths.Count(); ++i )
	{
		float flCost = CalculatePathCost( s_alternativePaths[i], m_pBot );
		if ( flCost < flBestCost )
		{
			flBestCost = flCost;
			pBestPath = &s_alternativePaths[i];
		}
	}

	s_selectedPath = *pBestPath;
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

	// Build path using nav mesh
	CUtlVector< CTFNavArea * > areaPath;
	if ( !NavAreaBuildPath( pStartArea, pGoalArea, &goal, 0.0f, TEAM_ANY, false ) )
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
// FindAlternativePaths - Identify flanking and alternative routes
//----------------------------------------------------------------------------
bool CTFBotPathSelector::FindAlternativePaths( const Vector &start, const Vector &goal, CUtlVector< PathInfo_t > &outPaths )
{
	// Try to find a flanking path
	PathInfo_t flankPath;
	if ( ComputeFlankingPath( start, goal, &flankPath ) )
	{
		outPaths.AddToTail( flankPath );
	}

	// Could add more alternative path types here in future stories

	return outPaths.Count() > 0;
}

//----------------------------------------------------------------------------
// ComputeFlankingPath - Calculate route that approaches from different angle
//----------------------------------------------------------------------------
bool CTFBotPathSelector::ComputeFlankingPath( const Vector &start, const Vector &goal, PathInfo_t *outPath )
{
	if ( !outPath )
		return false;

	CTFNavArea *pGoalArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( goal );
	if ( !pGoalArea )
		return false;

	// Find areas near goal that approach from a different direction
	CUtlVector< CTFNavArea * > nearbyAreas;
	pGoalArea->CollectAdjacentAreas( &nearbyAreas );

	if ( nearbyAreas.Count() < 2 )
		return false; // Not enough areas to flank

	// Pick a random adjacent area as intermediate waypoint for flanking
	int randomIdx = RandomInt( 0, nearbyAreas.Count() - 1 );
	CTFNavArea *pFlankArea = nearbyAreas[randomIdx];

	// Build path: start -> flank area -> goal
	PathInfo_t pathToFlank;
	if ( !ComputePrimaryPath( start, pFlankArea->GetCenter(), &pathToFlank ) )
		return false;

	PathInfo_t pathFromFlank;
	if ( !ComputePrimaryPath( pFlankArea->GetCenter(), goal, &pathFromFlank ) )
		return false;

	// Combine paths
	outPath->waypoints.RemoveAll();
	outPath->waypoints.AddVectorToTail( pathToFlank.waypoints );
	outPath->waypoints.AddVectorToTail( pathFromFlank.waypoints );

	outPath->type = PATH_FLANK;
	outPath->length = CalculatePathLength( outPath->waypoints );

	// Check if flanking path is reasonable (not too much longer)
	PathInfo_t primaryPath;
	if ( ComputePrimaryPath( start, goal, &primaryPath ) )
	{
		if ( !IsPathReasonable( primaryPath, *outPath ) )
			return false;
	}

	return ValidatePath( *outPath );
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
// CalculatePathCost - Compute cost with class preferences
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

		case TF_CLASS_HEAVY:
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
	// Alternative path shouldn't be more than 1.5x the length of primary
	const float MAX_LENGTH_RATIO = 1.5f;

	return ( alternatePath.length <= primaryPath.length * MAX_LENGTH_RATIO );
}