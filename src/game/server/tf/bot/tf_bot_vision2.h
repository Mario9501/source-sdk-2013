//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot Vision 2.0 - Enhanced vision system with attribute integration
//
// $NoKeywords: $
//=============================================================================//

#ifndef TF_BOT_VISION2_H
#define TF_BOT_VISION2_H
#ifdef _WIN32
#pragma once
#endif

#include "tf_bot_vision.h"
#include "tf_bot_attributes.h"

class CTFBot;
class CTFPlayer;

//-----------------------------------------------------------------------------
// Threat level classification
//-----------------------------------------------------------------------------
enum ThreatLevel
{
	THREAT_NONE = 0,		// No threat (friendly, dead, etc.)
	THREAT_LOW,				// Wounded enemy, far away, low-tier class
	THREAT_MEDIUM,			// Normal enemy, medium range
	THREAT_HIGH,			// Close enemy, dangerous class (Heavy, Soldier)
	THREAT_CRITICAL			// Spy behind, sentry, ubered enemy, immediate danger
};

//-----------------------------------------------------------------------------
// Field of view configuration
//-----------------------------------------------------------------------------
struct VisionCone
{
	float centerFOV;			// Center vision angle (degrees)
	float peripheralFOV;		// Peripheral vision angle (degrees)
	float centerAccuracy;		// Recognition speed multiplier in center (1.0 = normal)
	float peripheralAccuracy;	// Recognition speed multiplier in periphery (0.3 = slow)

	VisionCone()
	{
		centerFOV = 60.0f;
		peripheralFOV = 120.0f;
		centerAccuracy = 1.0f;
		peripheralAccuracy = 0.5f;
	}
};

//-----------------------------------------------------------------------------
// Class-specific vision profiles
//-----------------------------------------------------------------------------
struct ClassVisionProfile
{
	float visionRangeMultiplier;	// Multiplier for base vision range
	VisionCone visionCone;			// FOV configuration
	float occlusionSensitivity;		// 0.0 = ignores cover, 1.0 = very sensitive

	ClassVisionProfile()
	{
		visionRangeMultiplier = 1.0f;
		occlusionSensitivity = 0.5f;
	}
};

//-----------------------------------------------------------------------------
// Occlusion/cover tracking
//-----------------------------------------------------------------------------
struct OcclusionInfo
{
	bool isPartiallyOccluded;		// Is entity behind partial cover?
	float occlusionPercentage;		// 0.0 = fully visible, 1.0 = fully hidden
	Vector lastFullyVisiblePos;		// Last position where fully visible
	float timeSinceFullyVisible;	// Time since we had clear LOS
	Vector predictedPosition;		// Predicted position behind cover
	float predictionConfidence;		// 0.0-1.0 confidence in prediction

	OcclusionInfo()
	{
		isPartiallyOccluded = false;
		occlusionPercentage = 0.0f;
		lastFullyVisiblePos = vec3_origin;
		timeSinceFullyVisible = 0.0f;
		predictedPosition = vec3_origin;
		predictionConfidence = 0.0f;
	}
};

//-----------------------------------------------------------------------------
// Enhanced visible entity tracking
//-----------------------------------------------------------------------------
struct VisibleEntity
{
	EHANDLE entity;					// Entity handle
	float lastSeenTime;				// Last time we had LOS
	float recognitionProgress;		// 0.0-1.0, gradual recognition
	ThreatLevel threatLevel;		// Calculated threat level
	Vector lastKnownPosition;		// Last known position
	Vector lastKnownVelocity;		// Last known velocity
	OcclusionInfo occlusion;		// Occlusion/cover info
	bool isFullyRecognized;			// Have we fully recognized this entity?

	VisibleEntity()
	{
		lastSeenTime = 0.0f;
		recognitionProgress = 0.0f;
		threatLevel = THREAT_NONE;
		lastKnownPosition = vec3_origin;
		lastKnownVelocity = vec3_origin;
		isFullyRecognized = false;
	}
};

//-----------------------------------------------------------------------------
// Spy suspicion tracking
//-----------------------------------------------------------------------------
class SpySuspicionTracker
{
public:
	SpySuspicionTracker();

	void Update( CTFBot *me, float deltaTime );
	float GetSuspicionLevel( CTFPlayer *player ) const;
	void OnPlayerBumped( CTFPlayer *player );			// Called when bot collides with player
	void OnPlayerNearObjective( CTFPlayer *player );	// Called when player near objective
	void Reset();

private:
	struct SuspicionEntry
	{
		EHANDLE player;
		float suspicionLevel;		// 0.0-1.0
		float lastBumpTime;
		float lastNearObjectiveTime;
		Vector lastPosition;
		float timeAtSamePosition;	// How long they've been still

		SuspicionEntry()
		{
			suspicionLevel = 0.0f;
			lastBumpTime = 0.0f;
			lastNearObjectiveTime = 0.0f;
			lastPosition = vec3_origin;
			timeAtSamePosition = 0.0f;
		}
	};

	CUtlVector<SuspicionEntry> m_suspicionList;

	SuspicionEntry* FindOrCreateEntry( CTFPlayer *player );
	float CalculateSuspicion( CTFBot *me, CTFPlayer *player, SuspicionEntry &entry );
	bool IsMovingAgainstTeam( CTFBot *me, CTFPlayer *player );
	bool IsLurkingNearObjective( CTFPlayer *player );
};

//-----------------------------------------------------------------------------
// Vision memory - remember entities we've lost sight of
//-----------------------------------------------------------------------------
class VisionMemory
{
public:
	VisionMemory();

	void RememberEntity( CBaseEntity *entity, Vector lastPos, Vector lastVel );
	void ForgetEntity( CBaseEntity *entity );
	Vector PredictPosition( CBaseEntity *entity, float timeSinceLost ) const;
	void Update( float deltaTime );
	void Clear();

	// Query remembered entities
	bool HasMemoryOf( CBaseEntity *entity ) const;
	Vector GetLastKnownPosition( CBaseEntity *entity ) const;
	float GetTimeSinceSeen( CBaseEntity *entity ) const;

private:
	struct EntityMemory
	{
		EHANDLE entity;
		Vector lastPosition;
		Vector lastVelocity;
		Vector predictedPosition;
		float lostSightTime;
		float confidence;		// Prediction confidence (decays over time)

		EntityMemory()
		{
			lastPosition = vec3_origin;
			lastVelocity = vec3_origin;
			predictedPosition = vec3_origin;
			lostSightTime = 0.0f;
			confidence = 1.0f;
		}
	};

	CUtlVector<EntityMemory> m_memories;
	float m_memoryDuration;		// How long to remember (seconds)

	EntityMemory* FindMemory( CBaseEntity *entity );
	const EntityMemory* FindMemory( CBaseEntity *entity ) const;
	void UpdatePredictions( float deltaTime );
	void PruneOldMemories();
};

//-----------------------------------------------------------------------------
// CTFBotVision2 - Enhanced vision system
//-----------------------------------------------------------------------------
class CTFBotVision2 : public CTFBotVision
{
public:
	CTFBotVision2( INextBot *bot );
	virtual ~CTFBotVision2();

	//-------------------------------------------------------------------------
	// IVision overrides
	//-------------------------------------------------------------------------
	virtual void Update( void ) OVERRIDE;
	virtual bool IsIgnored( CBaseEntity *subject ) const OVERRIDE;
	virtual bool IsVisibleEntityNoticed( CBaseEntity *subject ) const OVERRIDE;
	virtual float GetMaxVisionRange( void ) const OVERRIDE;
	virtual float GetMinRecognizeTime( void ) const OVERRIDE;

	//-------------------------------------------------------------------------
	// Enhanced vision features
	//-------------------------------------------------------------------------

	// Attribute integration
	void SetAttributes( const CTFBotAttributes *attributes );
	const CTFBotAttributes* GetAttributes() const { return m_attributes; }

	// Effective vision parameters (attribute-driven)
	float GetEffectiveVisionRange() const;
	float GetEffectiveRecognitionTime() const;
	float GetEffectiveAwareness() const;

	// FOV system
	VisionCone GetVisionCone() const;
	bool IsInCenterVision( CBaseEntity *entity ) const;
	bool IsInPeripheralVision( CBaseEntity *entity ) const;
	float GetRecognitionSpeedMultiplier( CBaseEntity *entity ) const;

	// Threat assessment
	ThreatLevel CalculateThreatLevel( CBaseEntity *entity ) const;
	void GetVisibleEnemiesByThreat( CUtlVector<VisibleEntity*> &threats );
	CBaseEntity* GetMostDangerousVisibleEnemy() const;

	// Occlusion tracking
	OcclusionInfo CalculateOcclusion( CBaseEntity *entity ) const;
	bool IsPartiallyVisible( CBaseEntity *entity ) const;

	// Recognition system
	float GetRecognitionProgress( CBaseEntity *entity ) const;
	bool IsFullyRecognized( CBaseEntity *entity ) const;
	void ForceRecognize( CBaseEntity *entity );		// Instantly recognize

	// Spy detection
	SpySuspicionTracker& GetSpyTracker() { return m_spyTracker; }
	float GetSpySuspicion( CTFPlayer *player ) const;

	// Vision memory
	VisionMemory& GetMemory() { return m_memory; }
	bool HasMemoryOf( CBaseEntity *entity ) const;
	Vector GetLastKnownPosition( CBaseEntity *entity ) const;

	// Class-specific profiles
	static ClassVisionProfile GetProfileForClass( int classIndex );

	// Debug
	void DebugDrawVision() const;
	void DebugPrintVisibleEntities() const;

private:
	//-------------------------------------------------------------------------
	// Internal helpers
	//-------------------------------------------------------------------------
	void UpdateVisibleEntities( float deltaTime );
	void UpdateRecognition( VisibleEntity &visible, float deltaTime );
	void UpdateOcclusion( VisibleEntity &visible );
	void PruneInvalidEntities();

	VisibleEntity* FindOrCreateVisibleEntity( CBaseEntity *entity );
	VisibleEntity* FindVisibleEntity( CBaseEntity *entity );
	const VisibleEntity* FindVisibleEntity( CBaseEntity *entity ) const;

	float CalculateDistanceToEntity( CBaseEntity *entity ) const;
	float CalculateAngleToEntity( CBaseEntity *entity ) const;

	bool IsPointVisible( const Vector &point ) const;
	int CountVisiblePoints( CBaseEntity *entity ) const;

	//-------------------------------------------------------------------------
	// Member variables
	//-------------------------------------------------------------------------
	const CTFBotAttributes *m_attributes;		// Bot attributes (from Agent 1)

	// Visible entities tracking
	CUtlVector<VisibleEntity> m_visibleEntities;

	// Enhanced systems
	SpySuspicionTracker m_spyTracker;			// Spy detection
	VisionMemory m_memory;						// Vision memory

	// Class vision profile
	ClassVisionProfile m_visionProfile;

	// Update throttling
	CountdownTimer m_recognitionUpdateTimer;
	CountdownTimer m_occlusionUpdateTimer;
	CountdownTimer m_memoryUpdateTimer;
};

//-----------------------------------------------------------------------------
// Global helper functions
//-----------------------------------------------------------------------------

// Get class-appropriate vision profile
ClassVisionProfile CreateVisionProfileForClass( int classIndex );

// Calculate threat level for entity
ThreatLevel CalculateEntityThreat( CTFBot *observer, CBaseEntity *entity );

// Check if angle is within FOV
bool IsAngleInFOV( float angle, float fov );

#endif // TF_BOT_VISION2_H
