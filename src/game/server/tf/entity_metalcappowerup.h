//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF Metal Cap Powerup.
//
//=============================================================================//
#ifndef ENTITY_METALCAPPOWERUP_H
#define ENTITY_METALCAPPOWERUP_H

#ifdef _WIN32
#pragma once
#endif

#include "tf_powerup.h"

//=============================================================================
//
// CTF Metal Cap Powerup class (based on SM64 Metal Mario powerup).
//
//=============================================================================

#define TF_METAL_CAP_MODEL			"models/pickups/pickup_powerup_metalcap.mdl"
#define TF_METAL_CAP_DURATION		20.0f		// Duration in seconds (matches SM64's 600 frames at 30fps)
#define TF_METAL_CAP_RESPAWN_TIME	30.0f		// Respawn delay in seconds
#define TF_METAL_CAP_DESPAWN_TIME	10.0f		// Time before despawning if not picked up

class CTFMetalCapPowerup : public CTFPowerup
{
public:
	DECLARE_CLASS( CTFMetalCapPowerup, CTFPowerup );

	void			Spawn( void ) OVERRIDE;
	void			Precache( void ) OVERRIDE;
	bool			MyTouch( CBasePlayer *pPlayer ) OVERRIDE;

	virtual const char *GetDefaultPowerupModel( void ) { return TF_METAL_CAP_MODEL; }
	virtual float	GetRespawnDelay( void ) OVERRIDE { return TF_METAL_CAP_RESPAWN_TIME; }

	powerupsize_t	GetPowerupSize( void ) OVERRIDE { return POWERUP_FULL; }
};

#endif // ENTITY_METALCAPPOWERUP_H
