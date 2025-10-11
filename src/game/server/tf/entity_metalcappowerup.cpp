//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF Metal Cap Powerup.
//
//=============================================================================//
#include "cbase.h"
#include "items.h"
#include "tf_gamerules.h"
#include "tf_shareddefs.h"
#include "tf_player.h"
#include "tf_team.h"
#include "engine/IEngineSound.h"
#include "entity_metalcappowerup.h"

//=============================================================================
//
// CTF Metal Cap Powerup defines.
//
#define TF_METAL_CAP_PICKUP_SOUND	"MetalCap.Pickup"

// Register the entity
LINK_ENTITY_TO_CLASS( item_metalcappowerup, CTFMetalCapPowerup );

//=============================================================================
//
// CTF Metal Cap Powerup functions.
//

//-----------------------------------------------------------------------------
// Purpose: Spawn function for the Metal Cap powerup
//-----------------------------------------------------------------------------
void CTFMetalCapPowerup::Spawn( void )
{
	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
// Purpose: Precache function for the Metal Cap powerup
//-----------------------------------------------------------------------------
void CTFMetalCapPowerup::Precache( void )
{
	PrecacheScriptSound( TF_METAL_CAP_PICKUP_SOUND );
	PrecacheModel( TF_METAL_CAP_MODEL );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: MyTouch function for the Metal Cap powerup (T011, T012)
//-----------------------------------------------------------------------------
bool CTFMetalCapPowerup::MyTouch( CBasePlayer *pPlayer )
{
	bool bSuccess = false;

	if ( ValidTouch( pPlayer ) )
	{
		CTFPlayer *pTFPlayer = ToTFPlayer( pPlayer );
		if ( !pTFPlayer )
			return false;

		// T012: Mutual exclusivity checks - prevent pickup if player has other powerups
		// Metal Cap is mutually exclusive with other invulnerability/crit powerups
		if ( pTFPlayer->m_Shared.InCond( TF_COND_INVULNERABLE_USER_BUFF ) ||
		     pTFPlayer->m_Shared.InCond( TF_COND_CRITBOOSTED_USER_BUFF ) ||
		     pTFPlayer->m_Shared.InCond( TF_COND_INVULNERABLE ) ||
		     pTFPlayer->m_Shared.InCond( TF_COND_CRITBOOSTED ) )
		{
			// Player already has a conflicting powerup - deny pickup
			return false;
		}

		// T011: Apply Metal Cap condition with 20 second duration (FR-004)
		pTFPlayer->m_Shared.AddCond( TF_COND_METAL_CAP, TF_METAL_CAP_DURATION );

		// Play pickup sound (handled by OnAddMetalCap in tf_player_shared.cpp)
		CSingleUserRecipientFilter user( pPlayer );
		EmitSound( user, entindex(), TF_METAL_CAP_PICKUP_SOUND );

		bSuccess = true;
	}

	return bSuccess;
}
