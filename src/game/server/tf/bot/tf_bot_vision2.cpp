//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: TF Bot Vision 2.0 implementation
//
//=============================================================================//

#include "cbase.h"
#include "tf_bot_vision2.h"
#include "tf_bot.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_obj_sentrygun.h"
#include "tf_obj_dispenser.h"
#include "tf_obj_teleporter.h"
#include "tf_weapon_medigun.h"
#include "nav_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// ConVars
//-----------------------------------------------------------------------------
ConVar tf_bot_vision2_enabled( "tf_bot_vision2_enabled", "1", FCVAR_CHEAT, "Enable enhanced vision system" );
ConVar tf_bot_vision_range_multiplier( "tf_bot_vision_range_multiplier", "1.0", FCVAR_CHEAT, "Global vision range multiplier" );
ConVar tf_bot_vision_fov_center( "tf_bot_vision_fov_center", "60", FCVAR_CHEAT, "Center FOV override (0 = use class default)" );
ConVar tf_bot_vision_fov_peripheral( "tf_bot_vision_fov_peripheral", "120", FCVAR_CHEAT, "Peripheral FOV override (0 = use class default)" );

ConVar tf_bot_recognition_gradual( "tf_bot_recognition_gradual", "1", FCVAR_CHEAT, "Enable gradual recognition system" );
ConVar tf_bot_recognition_min_time( "tf_bot_recognition_min_time", "0.05", FCVAR_CHEAT, "Minimum recognition time" );

ConVar tf_bot_spy_suspicion_enabled( "tf_bot_spy_suspicion_enabled", "1", FCVAR_CHEAT, "Enable spy suspicion system" );
ConVar tf_bot_spy_suspicion_threshold( "tf_bot_spy_suspicion_threshold", "0.7", FCVAR_CHEAT, "Suspicion threshold to reveal spy" );

ConVar tf_bot_occlusion_tracking( "tf_bot_occlusion_tracking", "1", FCVAR_CHEAT, "Enable occlusion/cover tracking" );
ConVar tf_bot_occlusion_sensitivity( "tf_bot_occlusion_sensitivity", "0.5", FCVAR_CHEAT, "How much occlusion affects recognition" );

ConVar tf_bot_vision_memory_time( "tf_bot_vision_memory_time", "5.0", FCVAR_CHEAT, "How long to remember lost entities (seconds)" );
ConVar tf_bot_vision_predict_movement( "tf_bot_vision_predict_movement", "1", FCVAR_CHEAT, "Predict positions of lost entities" );

ConVar tf_bot_vision_debug( "tf_bot_vision_debug", "0", FCVAR_CHEAT, "Debug draw vision system" );

//-----------------------------------------------------------------------------
// Class vision profiles (static data)
//-----------------------------------------------------------------------------
static ClassVisionProfile CreateScoutProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.0f;
	profile.visionCone.centerFOV = 90.0f;
	profile.visionCone.peripheralFOV = 180.0f;
	profile.visionCone.centerAccuracy = 0.8f;
	profile.visionCone.peripheralAccuracy = 0.6f;
	profile.occlusionSensitivity = 0.7f;
	return profile;
}

static ClassVisionProfile CreateSniperProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.5f;		// Long range vision
	profile.visionCone.centerFOV = 30.0f;		// Narrow focus
	profile.visionCone.peripheralFOV = 120.0f;
	profile.visionCone.centerAccuracy = 1.0f;	// Perfect in center
	profile.visionCone.peripheralAccuracy = 0.4f;
	profile.occlusionSensitivity = 0.3f;		// Can see through some cover
	return profile;
}

static ClassVisionProfile CreateSoldierProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.0f;
	profile.visionCone.centerFOV = 70.0f;
	profile.visionCone.peripheralFOV = 140.0f;
	profile.visionCone.centerAccuracy = 0.9f;
	profile.visionCone.peripheralAccuracy = 0.5f;
	profile.occlusionSensitivity = 0.6f;
	return profile;
}

static ClassVisionProfile CreatePyroProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 0.8f;		// Shorter vision (mask)
	profile.visionCone.centerFOV = 80.0f;
	profile.visionCone.peripheralFOV = 150.0f;
	profile.visionCone.centerAccuracy = 0.7f;
	profile.visionCone.peripheralAccuracy = 0.6f;
	profile.occlusionSensitivity = 0.8f;
	return profile;
}

static ClassVisionProfile CreateDemomanProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 0.9f;		// Eye patch
	profile.visionCone.centerFOV = 60.0f;
	profile.visionCone.peripheralFOV = 120.0f;
	profile.visionCone.centerAccuracy = 0.8f;
	profile.visionCone.peripheralAccuracy = 0.4f;
	profile.occlusionSensitivity = 0.7f;
	return profile;
}

static ClassVisionProfile CreateHeavyProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 0.8f;		// Short range focus
	profile.visionCone.centerFOV = 60.0f;
	profile.visionCone.peripheralFOV = 140.0f;
	profile.visionCone.centerAccuracy = 0.9f;
	profile.visionCone.peripheralAccuracy = 0.5f;
	profile.occlusionSensitivity = 0.8f;
	return profile;
}

static ClassVisionProfile CreateEngineerProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.0f;
	profile.visionCone.centerFOV = 75.0f;
	profile.visionCone.peripheralFOV = 160.0f;	// Good peripheral awareness
	profile.visionCone.centerAccuracy = 0.8f;
	profile.visionCone.peripheralAccuracy = 0.6f;
	profile.occlusionSensitivity = 0.5f;
	return profile;
}

static ClassVisionProfile CreateMedicProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.1f;		// Good awareness
	profile.visionCone.centerFOV = 80.0f;
	profile.visionCone.peripheralFOV = 170.0f;	// Excellent peripheral
	profile.visionCone.centerAccuracy = 0.7f;
	profile.visionCone.peripheralAccuracy = 0.7f;	// Good all-around
	profile.occlusionSensitivity = 0.6f;
	return profile;
}

static ClassVisionProfile CreateSpyProfile()
{
	ClassVisionProfile profile;
	profile.visionRangeMultiplier = 1.0f;
	profile.visionCone.centerFOV = 75.0f;
	profile.visionCone.peripheralFOV = 170.0f;
	profile.visionCone.centerAccuracy = 0.9f;
	profile.visionCone.peripheralAccuracy = 0.7f;
	profile.occlusionSensitivity = 0.4f;		// Good at spotting through cover
	return profile;
}

//-----------------------------------------------------------------------------
// Get class vision profile
//-----------------------------------------------------------------------------
ClassVisionProfile CTFBotVision2::GetProfileForClass( int classIndex )
{
	switch ( classIndex )
	{
	case TF_CLASS_SCOUT:		return CreateScoutProfile();
	case TF_CLASS_SNIPER:		return CreateSniperProfile();
	case TF_CLASS_SOLDIER:		return CreateSoldierProfile();
	case TF_CLASS_DEMOMAN:		return CreateDemomanProfile();
	case TF_CLASS_MEDIC:		return CreateMedicProfile();
	case TF_CLASS_HEAVYWEAPONS:	return CreateHeavyProfile();
	case TF_CLASS_PYRO:			return CreatePyroProfile();
	case TF_CLASS_SPY:			return CreateSpyProfile();
	case TF_CLASS_ENGINEER:		return CreateEngineerProfile();
	default:
		ClassVisionProfile defaultProfile;
		return defaultProfile;
	}
}

//-----------------------------------------------------------------------------
// CTFBotVision2 implementation
//-----------------------------------------------------------------------------
CTFBotVision2::CTFBotVision2( INextBot *bot ) : CTFBotVision( bot )
{
	m_attributes = NULL;

	// Get class profile
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( me )
	{
		m_visionProfile = GetProfileForClass( me->GetPlayerClass()->GetClassIndex() );
	}

	m_recognitionUpdateTimer.Start( 0.1f );
	m_occlusionUpdateTimer.Start( 0.2f );
	m_memoryUpdateTimer.Start( 0.5f );
}

CTFBotVision2::~CTFBotVision2()
{
	m_visibleEntities.Purge();
}

//-----------------------------------------------------------------------------
// Set bot attributes
//-----------------------------------------------------------------------------
void CTFBotVision2::SetAttributes( const CTFBotAttributes *attributes )
{
	m_attributes = attributes;
}

//-----------------------------------------------------------------------------
// Update vision system
//-----------------------------------------------------------------------------
void CTFBotVision2::Update( void )
{
	if ( !tf_bot_vision2_enabled.GetBool() )
	{
		// Fall back to old system
		CTFBotVision::Update();
		return;
	}

	// Call base update
	CTFBotVision::Update();

	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return;

	float deltaTime = gpGlobals->frametime;

	// Update visible entities
	if ( m_recognitionUpdateTimer.IsElapsed() )
	{
		m_recognitionUpdateTimer.Start( 0.1f );
		UpdateVisibleEntities( deltaTime );
	}

	// Update occlusion tracking
	if ( tf_bot_occlusion_tracking.GetBool() && m_occlusionUpdateTimer.IsElapsed() )
	{
		m_occlusionUpdateTimer.Start( 0.2f );
		FOR_EACH_VEC( m_visibleEntities, i )
		{
			UpdateOcclusion( m_visibleEntities[i] );
		}
	}

	// Update spy suspicion
	if ( tf_bot_spy_suspicion_enabled.GetBool() )
	{
		m_spyTracker.Update( me, deltaTime );
	}

	// Update vision memory
	if ( m_memoryUpdateTimer.IsElapsed() )
	{
		m_memoryUpdateTimer.Start( 0.5f );
		m_memory.Update( deltaTime );
	}

	// Debug visualization
	if ( tf_bot_vision_debug.GetBool() )
	{
		DebugDrawVision();
	}
}

//-----------------------------------------------------------------------------
// Update visible entities and recognition
//-----------------------------------------------------------------------------
void CTFBotVision2::UpdateVisibleEntities( float deltaTime )
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return;

	// Update recognition progress for visible entities
	FOR_EACH_VEC( m_visibleEntities, i )
	{
		VisibleEntity &visible = m_visibleEntities[i];

		if ( !visible.entity.Get() )
		{
			continue;
		}

		const CKnownEntity *known = GetKnown( visible.entity );
		if ( known && known->IsVisibleRecently() )
		{
			// Still visible, update recognition
			UpdateRecognition( visible, deltaTime );

			// Update last known info
			visible.lastSeenTime = gpGlobals->curtime;
			visible.lastKnownPosition = visible.entity->GetAbsOrigin();

			if ( visible.entity->IsPlayer() )
			{
				CTFPlayer *player = ToTFPlayer( visible.entity );
				visible.lastKnownVelocity = player->GetAbsVelocity();
			}
		}
		else
		{
			// Lost sight, add to memory
			if ( tf_bot_vision_predict_movement.GetBool() )
			{
				m_memory.RememberEntity( visible.entity, visible.lastKnownPosition, visible.lastKnownVelocity );
			}

			// Decay recognition
			visible.recognitionProgress -= deltaTime * 0.5f;
			if ( visible.recognitionProgress < 0.0f )
			{
				visible.recognitionProgress = 0.0f;
				visible.isFullyRecognized = false;
			}
		}
	}

	// Prune invalid/forgotten entities
	PruneInvalidEntities();
}

//-----------------------------------------------------------------------------
// Update recognition progress
//-----------------------------------------------------------------------------
void CTFBotVision2::UpdateRecognition( VisibleEntity &visible, float deltaTime )
{
	if ( !tf_bot_recognition_gradual.GetBool() )
	{
		// Instant recognition
		visible.recognitionProgress = 1.0f;
		visible.isFullyRecognized = true;
		return;
	}

	if ( visible.isFullyRecognized )
		return;

	// Calculate recognition speed
	float recognitionTime = GetEffectiveRecognitionTime();
	float recognitionSpeed = 1.0f / Max( recognitionTime, tf_bot_recognition_min_time.GetFloat() );

	// Apply FOV multiplier
	float fovMultiplier = GetRecognitionSpeedMultiplier( visible.entity );
	recognitionSpeed *= fovMultiplier;

	// Apply occlusion penalty
	if ( tf_bot_occlusion_tracking.GetBool() && visible.occlusion.isPartiallyOccluded )
	{
		float occlusionPenalty = 1.0f - (visible.occlusion.occlusionPercentage * tf_bot_occlusion_sensitivity.GetFloat());
		recognitionSpeed *= occlusionPenalty;
	}

	// Update progress
	visible.recognitionProgress += recognitionSpeed * deltaTime;
	visible.recognitionProgress = clamp( visible.recognitionProgress, 0.0f, 1.0f );

	if ( visible.recognitionProgress >= 1.0f )
	{
		visible.isFullyRecognized = true;
	}

	// Update threat level
	visible.threatLevel = CalculateThreatLevel( visible.entity );
}

//-----------------------------------------------------------------------------
// Update occlusion info
//-----------------------------------------------------------------------------
void CTFBotVision2::UpdateOcclusion( VisibleEntity &visible )
{
	visible.occlusion = CalculateOcclusion( visible.entity );
}

//-----------------------------------------------------------------------------
// Prune invalid entities
//-----------------------------------------------------------------------------
void CTFBotVision2::PruneInvalidEntities()
{
	for ( int i = m_visibleEntities.Count() - 1; i >= 0; i-- )
	{
		if ( !m_visibleEntities[i].entity.Get() )
		{
			m_visibleEntities.Remove( i );
			continue;
		}

		// Remove if not seen for a while and not fully recognized
		float timeSinceSeen = gpGlobals->curtime - m_visibleEntities[i].lastSeenTime;
		if ( timeSinceSeen > 2.0f && !m_visibleEntities[i].isFullyRecognized )
		{
			m_visibleEntities.Remove( i );
		}
	}
}

//-----------------------------------------------------------------------------
// Find or create visible entity
//-----------------------------------------------------------------------------
VisibleEntity* CTFBotVision2::FindOrCreateVisibleEntity( CBaseEntity *entity )
{
	VisibleEntity *existing = FindVisibleEntity( entity );
	if ( existing )
		return existing;

	VisibleEntity newEntry;
	newEntry.entity = entity;
	newEntry.lastSeenTime = gpGlobals->curtime;
	newEntry.recognitionProgress = 0.0f;
	newEntry.isFullyRecognized = false;
	newEntry.lastKnownPosition = entity->GetAbsOrigin();
	newEntry.threatLevel = THREAT_NONE;

	return &m_visibleEntities[m_visibleEntities.AddToTail( newEntry )];
}

VisibleEntity* CTFBotVision2::FindVisibleEntity( CBaseEntity *entity )
{
	FOR_EACH_VEC( m_visibleEntities, i )
	{
		if ( m_visibleEntities[i].entity == entity )
			return &m_visibleEntities[i];
	}
	return NULL;
}

const VisibleEntity* CTFBotVision2::FindVisibleEntity( CBaseEntity *entity ) const
{
	FOR_EACH_VEC( m_visibleEntities, i )
	{
		if ( m_visibleEntities[i].entity == entity )
			return &m_visibleEntities[i];
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Effective vision parameters (attribute-driven)
//-----------------------------------------------------------------------------
float CTFBotVision2::GetEffectiveVisionRange() const
{
	float baseRange = 6000.0f;

	// Apply class multiplier
	baseRange *= m_visionProfile.visionRangeMultiplier;

	// Apply global multiplier
	baseRange *= tf_bot_vision_range_multiplier.GetFloat();

	// Apply attribute (awarenessRadius)
	if ( m_attributes )
	{
		baseRange *= m_attributes->skill.awarenessRadius;
	}

	return baseRange;
}

float CTFBotVision2::GetEffectiveRecognitionTime() const
{
	float baseTime = 0.5f;

	// Apply attribute (reactionTime)
	if ( m_attributes )
	{
		baseTime = m_attributes->skill.reactionTime;
	}

	return Max( baseTime, tf_bot_recognition_min_time.GetFloat() );
}

float CTFBotVision2::GetEffectiveAwareness() const
{
	if ( m_attributes )
	{
		return m_attributes->skill.situationalAwareness;
	}
	return 0.5f;
}

//-----------------------------------------------------------------------------
// Vision range override
//-----------------------------------------------------------------------------
float CTFBotVision2::GetMaxVisionRange( void ) const
{
	if ( !tf_bot_vision2_enabled.GetBool() )
	{
		return CTFBotVision::GetMaxVisionRange();
	}

	return GetEffectiveVisionRange();
}

//-----------------------------------------------------------------------------
// Recognition time override
//-----------------------------------------------------------------------------
float CTFBotVision2::GetMinRecognizeTime( void ) const
{
	if ( !tf_bot_vision2_enabled.GetBool() )
	{
		return CTFBotVision::GetMinRecognizeTime();
	}

	return GetEffectiveRecognitionTime();
}

//-----------------------------------------------------------------------------
// Check if entity is noticed (override)
//-----------------------------------------------------------------------------
bool CTFBotVision2::IsVisibleEntityNoticed( CBaseEntity *subject ) const
{
	if ( !tf_bot_vision2_enabled.GetBool() )
	{
		return CTFBotVision::IsVisibleEntityNoticed( subject );
	}

	// Check if fully recognized
	const VisibleEntity *visible = FindVisibleEntity( subject );
	if ( !visible )
	{
		// Not in our tracking list, check if we should track it
		const_cast<CTFBotVision2*>(this)->FindOrCreateVisibleEntity( subject );
		return false;
	}

	return visible->isFullyRecognized;
}

//-----------------------------------------------------------------------------
// Check if entity should be ignored (override)
//-----------------------------------------------------------------------------
bool CTFBotVision2::IsIgnored( CBaseEntity *subject ) const
{
	// Use base class ignore logic (spy detection, etc.)
	return CTFBotVision::IsIgnored( subject );
}

//-----------------------------------------------------------------------------
// FOV system
//-----------------------------------------------------------------------------
VisionCone CTFBotVision2::GetVisionCone() const
{
	VisionCone cone = m_visionProfile.visionCone;

	// ConVar overrides
	if ( tf_bot_vision_fov_center.GetFloat() > 0.0f )
		cone.centerFOV = tf_bot_vision_fov_center.GetFloat();

	if ( tf_bot_vision_fov_peripheral.GetFloat() > 0.0f )
		cone.peripheralFOV = tf_bot_vision_fov_peripheral.GetFloat();

	return cone;
}

bool CTFBotVision2::IsInCenterVision( CBaseEntity *entity ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return false;

	VisionCone cone = GetVisionCone();
	float angle = CalculateAngleToEntity( entity );

	return IsAngleInFOV( angle, cone.centerFOV );
}

bool CTFBotVision2::IsInPeripheralVision( CBaseEntity *entity ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return false;

	VisionCone cone = GetVisionCone();
	float angle = CalculateAngleToEntity( entity );

	return IsAngleInFOV( angle, cone.peripheralFOV );
}

float CTFBotVision2::GetRecognitionSpeedMultiplier( CBaseEntity *entity ) const
{
	if ( IsInCenterVision( entity ) )
	{
		return GetVisionCone().centerAccuracy;
	}
	else if ( IsInPeripheralVision( entity ) )
	{
		return GetVisionCone().peripheralAccuracy;
	}

	return 0.1f;  // Very slow recognition outside FOV
}

//-----------------------------------------------------------------------------
// Calculate angle to entity
//-----------------------------------------------------------------------------
float CTFBotVision2::CalculateAngleToEntity( CBaseEntity *entity ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me || !entity )
		return 180.0f;

	Vector toEntity = entity->WorldSpaceCenter() - me->EyePosition();
	toEntity.NormalizeInPlace();

	Vector forward;
	me->EyeVectors( &forward );

	float dotProduct = DotProduct( forward, toEntity );
	float angle = RAD2DEG( acos( dotProduct ) );

	return angle;
}

float CTFBotVision2::CalculateDistanceToEntity( CBaseEntity *entity ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me || !entity )
		return 9999.0f;

	return ( entity->GetAbsOrigin() - me->GetAbsOrigin() ).Length();
}

//-----------------------------------------------------------------------------
// Check if angle is in FOV
//-----------------------------------------------------------------------------
bool IsAngleInFOV( float angle, float fov )
{
	return angle <= (fov * 0.5f);
}

//-----------------------------------------------------------------------------
// Threat assessment
//-----------------------------------------------------------------------------
ThreatLevel CTFBotVision2::CalculateThreatLevel( CBaseEntity *entity ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me || !entity )
		return THREAT_NONE;

	if ( !me->IsEnemy( entity ) )
		return THREAT_NONE;

	ThreatLevel threat = THREAT_LOW;
	float distance = CalculateDistanceToEntity( entity );

	// Check for critical threats
	if ( entity->IsPlayer() )
	{
		CTFPlayer *player = ToTFPlayer( entity );

		// Ubered enemy = critical threat
		if ( player->m_Shared.InCond( TF_COND_INVULNERABLE ) ||
			 player->m_Shared.InCond( TF_COND_INVULNERABLE_WEARINGOFF ) )
		{
			return THREAT_CRITICAL;
		}

		// Spy behind us = critical
		if ( player->IsPlayerClass( TF_CLASS_SPY ) )
		{
			Vector toSpy = player->GetAbsOrigin() - me->GetAbsOrigin();
			Vector forward;
			me->GetVectors( &forward, NULL, NULL );

			if ( DotProduct( toSpy.Normalized(), forward ) < -0.5f )  // Behind us
			{
				return THREAT_CRITICAL;
			}
		}

		// Close range dangerous classes
		if ( distance < 500.0f )
		{
			switch ( player->GetPlayerClass()->GetClassIndex() )
			{
			case TF_CLASS_PYRO:
			case TF_CLASS_HEAVYWEAPONS:
			case TF_CLASS_SOLDIER:
				return THREAT_HIGH;
			}
		}

		// Medium range
		if ( distance < 1500.0f )
		{
			threat = THREAT_MEDIUM;
		}

		// Wounded enemies are lower threat
		if ( player->GetHealth() < player->GetMaxHealth() * 0.3f )
		{
			if ( threat > THREAT_LOW )
				threat = (ThreatLevel)(threat - 1);
		}
	}
	else
	{
		// Buildings
		CObjectSentrygun *sentry = dynamic_cast<CObjectSentrygun*>( entity );
		if ( sentry && !sentry->IsBuilding() && !sentry->IsPlacing() )
		{
			if ( distance < 1100.0f )  // Sentry range
			{
				return THREAT_CRITICAL;
			}
			return THREAT_HIGH;
		}
	}

	return threat;
}

//-----------------------------------------------------------------------------
// Get visible enemies sorted by threat
//-----------------------------------------------------------------------------
void CTFBotVision2::GetVisibleEnemiesByThreat( CUtlVector<VisibleEntity*> &threats )
{
	threats.RemoveAll();

	// Collect fully recognized threats
	FOR_EACH_VEC( m_visibleEntities, i )
	{
		if ( m_visibleEntities[i].isFullyRecognized )
		{
			threats.AddToTail( &m_visibleEntities[i] );
		}
	}

	// Sort by threat level (bubble sort for small lists)
	for ( int i = 0; i < threats.Count() - 1; i++ )
	{
		for ( int j = 0; j < threats.Count() - i - 1; j++ )
		{
			if ( threats[j]->threatLevel < threats[j+1]->threatLevel )
			{
				VisibleEntity *temp = threats[j];
				threats[j] = threats[j+1];
				threats[j+1] = temp;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Get most dangerous visible enemy
//-----------------------------------------------------------------------------
CBaseEntity* CTFBotVision2::GetMostDangerousVisibleEnemy() const
{
	ThreatLevel highestThreat = THREAT_NONE;
	CBaseEntity *mostDangerous = NULL;

	FOR_EACH_VEC( m_visibleEntities, i )
	{
		const VisibleEntity &visible = m_visibleEntities[i];

		if ( !visible.isFullyRecognized )
			continue;

		if ( visible.threatLevel > highestThreat )
		{
			highestThreat = visible.threatLevel;
			mostDangerous = visible.entity;
		}
	}

	return mostDangerous;
}

//-----------------------------------------------------------------------------
// Occlusion calculation
//-----------------------------------------------------------------------------
OcclusionInfo CTFBotVision2::CalculateOcclusion( CBaseEntity *entity ) const
{
	OcclusionInfo info;

	if ( !entity )
		return info;

	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return info;

	// Test 5 points on entity
	Vector testPoints[5];
	testPoints[0] = entity->EyePosition();							// Head
	testPoints[1] = entity->WorldSpaceCenter();					// Torso
	testPoints[2] = entity->WorldSpaceCenter() + Vector(0,0,-24);	// Lower torso
	testPoints[3] = entity->GetAbsOrigin() + Vector(0,0,32);		// Legs
	testPoints[4] = entity->GetAbsOrigin() + Vector(0,0,8);			// Feet

	int visiblePoints = 0;
	for ( int i = 0; i < 5; i++ )
	{
		if ( IsPointVisible( testPoints[i] ) )
		{
			visiblePoints++;
			info.lastFullyVisiblePos = testPoints[i];
			info.timeSinceFullyVisible = 0.0f;
		}
	}

	info.occlusionPercentage = 1.0f - (visiblePoints / 5.0f);
	info.isPartiallyOccluded = (visiblePoints > 0 && visiblePoints < 5);

	return info;
}

bool CTFBotVision2::IsPartiallyVisible( CBaseEntity *entity ) const
{
	OcclusionInfo info = CalculateOcclusion( entity );
	return info.isPartiallyOccluded;
}

bool CTFBotVision2::IsPointVisible( const Vector &point ) const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return false;

	trace_t result;
	UTIL_TraceLine( me->EyePosition(), point, MASK_VISIBLE, me, COLLISION_GROUP_NONE, &result );

	return (result.fraction > 0.99f);
}

int CTFBotVision2::CountVisiblePoints( CBaseEntity *entity ) const
{
	if ( !entity )
		return 0;

	Vector testPoints[5];
	testPoints[0] = entity->EyePosition();
	testPoints[1] = entity->WorldSpaceCenter();
	testPoints[2] = entity->WorldSpaceCenter() + Vector(0,0,-24);
	testPoints[3] = entity->GetAbsOrigin() + Vector(0,0,32);
	testPoints[4] = entity->GetAbsOrigin() + Vector(0,0,8);

	int count = 0;
	for ( int i = 0; i < 5; i++ )
	{
		if ( IsPointVisible( testPoints[i] ) )
			count++;
	}

	return count;
}

//-----------------------------------------------------------------------------
// Recognition queries
//-----------------------------------------------------------------------------
float CTFBotVision2::GetRecognitionProgress( CBaseEntity *entity ) const
{
	const VisibleEntity *visible = FindVisibleEntity( entity );
	return visible ? visible->recognitionProgress : 0.0f;
}

bool CTFBotVision2::IsFullyRecognized( CBaseEntity *entity ) const
{
	const VisibleEntity *visible = FindVisibleEntity( entity );
	return visible ? visible->isFullyRecognized : false;
}

void CTFBotVision2::ForceRecognize( CBaseEntity *entity )
{
	VisibleEntity *visible = FindOrCreateVisibleEntity( entity );
	if ( visible )
	{
		visible->recognitionProgress = 1.0f;
		visible->isFullyRecognized = true;
	}
}

//-----------------------------------------------------------------------------
// Spy detection
//-----------------------------------------------------------------------------
float CTFBotVision2::GetSpySuspicion( CTFPlayer *player ) const
{
	return m_spyTracker.GetSuspicionLevel( player );
}

//-----------------------------------------------------------------------------
// Vision memory
//-----------------------------------------------------------------------------
bool CTFBotVision2::HasMemoryOf( CBaseEntity *entity ) const
{
	return m_memory.HasMemoryOf( entity );
}

Vector CTFBotVision2::GetLastKnownPosition( CBaseEntity *entity ) const
{
	return m_memory.GetLastKnownPosition( entity );
}

//-----------------------------------------------------------------------------
// Debug visualization
//-----------------------------------------------------------------------------
void CTFBotVision2::DebugDrawVision() const
{
	// TODO: Draw vision cone, visible entities, threat levels
}

void CTFBotVision2::DebugPrintVisibleEntities() const
{
	CTFBot *me = static_cast<CTFBot*>( GetBot()->GetEntity() );
	if ( !me )
		return;

	Msg( "=== %s Vision ===\n", me->GetPlayerName() );
	Msg( "  Visible entities: %d\n", m_visibleEntities.Count() );

	FOR_EACH_VEC( m_visibleEntities, i )
	{
		const VisibleEntity &v = m_visibleEntities[i];
		if ( v.entity.Get() )
		{
			Msg( "  [%d] %s - Recognition: %.2f, Threat: %d\n",
				i, v.entity->GetClassname(), v.recognitionProgress, v.threatLevel );
		}
	}
}

//=============================================================================
// SpySuspicionTracker implementation
//=============================================================================

SpySuspicionTracker::SpySuspicionTracker()
{
}

void SpySuspicionTracker::Update( CTFBot *me, float deltaTime )
{
	if ( !me )
		return;

	// Update all tracked players
	for ( int i = m_suspicionList.Count() - 1; i >= 0; i-- )
	{
		SuspicionEntry &entry = m_suspicionList[i];

		CTFPlayer *player = ToTFPlayer( entry.player.Get() );
		if ( !player || !player->IsAlive() )
		{
			m_suspicionList.Remove( i );
			continue;
		}

		// Calculate suspicion
		entry.suspicionLevel = CalculateSuspicion( me, player, entry );

		// Decay suspicion over time
		entry.suspicionLevel -= deltaTime * 0.1f;
		entry.suspicionLevel = clamp( entry.suspicionLevel, 0.0f, 1.0f );

		// Check if we should reveal spy
		if ( entry.suspicionLevel >= tf_bot_spy_suspicion_threshold.GetFloat() )
		{
			if ( player->m_Shared.InCond( TF_COND_DISGUISED ) && !player->m_Shared.IsStealthed() )
			{
				me->RealizeSpy( player );
			}
		}

		// Remove low suspicion entries
		if ( entry.suspicionLevel <= 0.0f )
		{
			m_suspicionList.Remove( i );
		}
	}
}

float SpySuspicionTracker::GetSuspicionLevel( CTFPlayer *player ) const
{
	FOR_EACH_VEC( m_suspicionList, i )
	{
		if ( m_suspicionList[i].player == player )
			return m_suspicionList[i].suspicionLevel;
	}
	return 0.0f;
}

void SpySuspicionTracker::OnPlayerBumped( CTFPlayer *player )
{
	SuspicionEntry *entry = FindOrCreateEntry( player );
	entry->lastBumpTime = gpGlobals->curtime;
	entry->suspicionLevel += 0.5f;
}

void SpySuspicionTracker::OnPlayerNearObjective( CTFPlayer *player )
{
	SuspicionEntry *entry = FindOrCreateEntry( player );
	entry->lastNearObjectiveTime = gpGlobals->curtime;
	entry->suspicionLevel += 0.3f;
}

void SpySuspicionTracker::Reset()
{
	m_suspicionList.Purge();
}

SpySuspicionTracker::SuspicionEntry* SpySuspicionTracker::FindOrCreateEntry( CTFPlayer *player )
{
	FOR_EACH_VEC( m_suspicionList, i )
	{
		if ( m_suspicionList[i].player == player )
			return &m_suspicionList[i];
	}

	SuspicionEntry newEntry;
	newEntry.player = player;
	return &m_suspicionList[m_suspicionList.AddToTail( newEntry )];
}

float SpySuspicionTracker::CalculateSuspicion( CTFBot *me, CTFPlayer *player, SuspicionEntry &entry )
{
	if ( !player->m_Shared.InCond( TF_COND_DISGUISED ) )
		return 0.0f;

	float suspicion = entry.suspicionLevel;

	// Recent bump
	if ( gpGlobals->curtime - entry.lastBumpTime < 2.0f )
	{
		suspicion += 0.4f;
	}

	// Moving against team flow
	if ( IsMovingAgainstTeam( me, player ) )
	{
		suspicion += 0.2f;
	}

	// Lurking near objective
	if ( IsLurkingNearObjective( player ) )
	{
		suspicion += 0.3f;
	}

	return clamp( suspicion, 0.0f, 1.0f );
}

bool SpySuspicionTracker::IsMovingAgainstTeam( CTFBot *me, CTFPlayer *player )
{
	// TODO: Check if player is moving opposite direction of team
	return false;
}

bool SpySuspicionTracker::IsLurkingNearObjective( CTFPlayer *player )
{
	// TODO: Check if near control points, intel, etc.
	return false;
}

//=============================================================================
// VisionMemory implementation
//=============================================================================

VisionMemory::VisionMemory()
{
	m_memoryDuration = tf_bot_vision_memory_time.GetFloat();
}

void VisionMemory::RememberEntity( CBaseEntity *entity, Vector lastPos, Vector lastVel )
{
	EntityMemory *existing = FindMemory( entity );
	if ( existing )
	{
		existing->lastPosition = lastPos;
		existing->lastVelocity = lastVel;
		existing->lostSightTime = gpGlobals->curtime;
		existing->confidence = 1.0f;
		return;
	}

	EntityMemory memory;
	memory.entity = entity;
	memory.lastPosition = lastPos;
	memory.lastVelocity = lastVel;
	memory.lostSightTime = gpGlobals->curtime;
	memory.confidence = 1.0f;

	m_memories.AddToTail( memory );
}

void VisionMemory::ForgetEntity( CBaseEntity *entity )
{
	FOR_EACH_VEC( m_memories, i )
	{
		if ( m_memories[i].entity == entity )
		{
			m_memories.Remove( i );
			return;
		}
	}
}

Vector VisionMemory::PredictPosition( CBaseEntity *entity, float timeSinceLost ) const
{
	const EntityMemory *memory = FindMemory( entity );
	if ( !memory )
		return vec3_origin;

	// Simple linear prediction
	Vector predicted = memory->lastPosition + memory->lastVelocity * timeSinceLost;

	// TODO: Clamp to nav mesh walkable areas

	return predicted;
}

void VisionMemory::Update( float deltaTime )
{
	m_memoryDuration = tf_bot_vision_memory_time.GetFloat();

	UpdatePredictions( deltaTime );
	PruneOldMemories();
}

void VisionMemory::Clear()
{
	m_memories.Purge();
}

bool VisionMemory::HasMemoryOf( CBaseEntity *entity ) const
{
	return FindMemory( entity ) != NULL;
}

Vector VisionMemory::GetLastKnownPosition( CBaseEntity *entity ) const
{
	const EntityMemory *memory = FindMemory( entity );
	return memory ? memory->lastPosition : vec3_origin;
}

float VisionMemory::GetTimeSinceSeen( CBaseEntity *entity ) const
{
	const EntityMemory *memory = FindMemory( entity );
	if ( !memory )
		return 9999.0f;

	return gpGlobals->curtime - memory->lostSightTime;
}

VisionMemory::EntityMemory* VisionMemory::FindMemory( CBaseEntity *entity )
{
	FOR_EACH_VEC( m_memories, i )
	{
		if ( m_memories[i].entity == entity )
			return &m_memories[i];
	}
	return NULL;
}

const VisionMemory::EntityMemory* VisionMemory::FindMemory( CBaseEntity *entity ) const
{
	FOR_EACH_VEC( m_memories, i )
	{
		if ( m_memories[i].entity == entity )
			return &m_memories[i];
	}
	return NULL;
}

void VisionMemory::UpdatePredictions( float deltaTime )
{
	FOR_EACH_VEC( m_memories, i )
	{
		EntityMemory &memory = m_memories[i];

		float timeLost = gpGlobals->curtime - memory.lostSightTime;
		memory.predictedPosition = memory.lastPosition + memory.lastVelocity * timeLost;

		// Decay confidence over time
		memory.confidence = 1.0f - (timeLost / m_memoryDuration);
		memory.confidence = clamp( memory.confidence, 0.0f, 1.0f );
	}
}

void VisionMemory::PruneOldMemories()
{
	for ( int i = m_memories.Count() - 1; i >= 0; i-- )
	{
		if ( !m_memories[i].entity.Get() )
		{
			m_memories.Remove( i );
			continue;
		}

		float timeLost = gpGlobals->curtime - m_memories[i].lostSightTime;
		if ( timeLost > m_memoryDuration )
		{
			m_memories.Remove( i );
		}
	}
}
