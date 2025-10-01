//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_path_selector.h
// Dynamic Path Selection and Navigation Enhancement
// Created: 2025-09-29

#ifndef TF_BOT_PATH_SELECTOR_H
#define TF_BOT_PATH_SELECTOR_H

#ifdef _WIN32
#pragma once
#endif

#include "Path/NextBotPathFollow.h"
#include "../nav_mesh/tf_nav_mesh.h"

class CTFBot;

//----------------------------------------------------------------------------
// Path type enumeration
//----------------------------------------------------------------------------
enum EPathType
{
	PATH_PRIMARY = 0,		// Shortest/most direct route
	PATH_FLANK,				// Flanking route (approaches from side/rear)
	PATH_ALTERNATIVE,		// Longer but valid alternative
	PATH_SAFE,				// Covered/protected route (for Heavy, etc.)
	PATH_FAST,				// Fast/direct route (for Scout, etc.)
};

//----------------------------------------------------------------------------
// Path information structure
//----------------------------------------------------------------------------
struct PathInfo_t
{
	PathInfo_t() : type( PATH_PRIMARY ), length( 0.0f ), cost( 0.0f ) {}

	// Copy constructor - manually copy waypoints
	PathInfo_t( const PathInfo_t &other )
		: type( other.type )
		, length( other.length )
		, cost( other.cost )
	{
		waypoints.RemoveAll();
		for ( int i = 0; i < other.waypoints.Count(); ++i )
		{
			waypoints.AddToTail( other.waypoints[i] );
		}
	}

	// Assignment operator - manually copy waypoints
	PathInfo_t &operator=( const PathInfo_t &other )
	{
		if ( this != &other )
		{
			waypoints.RemoveAll();
			for ( int i = 0; i < other.waypoints.Count(); ++i )
			{
				waypoints.AddToTail( other.waypoints[i] );
			}
			type = other.type;
			length = other.length;
			cost = other.cost;
		}
		return *this;
	}

	// Copy helper method for explicit copying
	void CopyFrom( const PathInfo_t &other )
	{
		*this = other;
	}

	CUtlVector< Vector > waypoints;		// Waypoint positions
	EPathType type;						// Type of path
	float length;						// Total path length
	float cost;							// Cost for this path type
};

//----------------------------------------------------------------------------
// Dynamic path selector for bots
// Provides route variation and class-based path preferences
//----------------------------------------------------------------------------
class CTFBotPathSelector
{
public:
	CTFBotPathSelector( CTFBot *pBot );
	~CTFBotPathSelector();

	// Main path selection interface
	bool SelectPath( const Vector &start, const Vector &goal, PathInfo_t *outPath );

	// Update path recalculation timer
	void Update( float deltaTime );

	// Get current path type
	EPathType GetCurrentPathType() const { return m_currentPathType; }

	// Force path recalculation on next update
	void InvalidatePath() { m_pathRecalcTimer.Invalidate(); }

private:
	// Path computation methods
	bool ComputePrimaryPath( const Vector &start, const Vector &goal, PathInfo_t *outPath );
	bool ComputeFlankingPath( const Vector &start, const Vector &goal, PathInfo_t *outPath );

	// Path selection and validation
	PathInfo_t *SelectBestPath( const Vector &start, const Vector &goal );
	bool ValidatePath( const PathInfo_t &path );
	float CalculatePathCost( const PathInfo_t &path, CTFBot *pBot );

	// Class-based path preferences
	float GetClassPathPreference( EPathType pathType, CTFBot *pBot );

	// Helper methods
	float CalculatePathLength( const CUtlVector< Vector > &waypoints );
	bool IsPathReasonable( const PathInfo_t &primaryPath, const PathInfo_t &alternatePath );

private:
	CTFBot *m_pBot;								// Owner bot
	EPathType m_currentPathType;				// Currently selected path type
	CountdownTimer m_pathRecalcTimer;			// Timer for path recalculation
	Vector m_lastGoal;							// Last goal position (for change detection)
	float m_flPathVariationChance;				// Chance to take alternative path
};

#endif // TF_BOT_PATH_SELECTOR_H