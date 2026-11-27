//========= TFBot overhaul: unlocks (M6) ======================================//
//
// See tf_bot_unlocks.h and docs/tfbot/DESIGN.md M6.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_unlocks.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/profile/tf_bot_profile.h"
#include "tf_weapon_medigun.h"
#include "tf_weapon_wrench.h"
#include "tf_gamerules.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

struct TFBotWhitelistEntry_t
{
	const char *m_itemName;		// items_game.txt "name"
	int m_playerClass;
	const char *m_milestone;
	const char *m_handling;		// what the bot code does differently
};

static const TFBotWhitelistEntry_t s_whitelist[] =
{
	{ "The Direct Hit",	TF_CLASS_SOLDIER,	"M6", "aim model leads with its real (faster) speed and aims centre mass: little splash" },
	{ "The Kritzkrieg",	TF_CLASS_MEDIC,		"M6", "pops when an enemy is near the patient; crits don't save a dying patient" },
	{ "The Gunslinger",	TF_CLASS_ENGINEER,	"M6", "battle engineer: mini-sentries near the fight, fights with the shotgun" },
};


//-----------------------------------------------------------------------------
bool TFBotUnlocks::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_UNLOCKS ) &&
		   !( TFGameRules() && TFGameRules()->IsMannVsMachineMode() );	// popfiles own MvM loadouts
}

bool TFBotUnlocks::IsWhitelisted( const char *itemName, int playerClass )
{
	for ( int i = 0; i < ARRAYSIZE( s_whitelist ); ++i )
	{
		if ( !V_stricmp( s_whitelist[i].m_itemName, itemName ) &&
			 ( playerClass == TF_CLASS_UNDEFINED || s_whitelist[i].m_playerClass == playerClass ) )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
void TFBotUnlocks::ApplyLoadout( CTFBot *bot )
{
	if ( !IsActive( bot ) )
		return;

	int cls = bot->GetPlayerClass()->GetClassIndex();
	if ( cls <= TF_CLASS_UNDEFINED || cls >= TF_CLASS_COUNT_ALL )
		return;

	const char *itemName = TFBotProfiles::GetEffective( bot ).m_loadout[ cls ];
	if ( !itemName[0] )
		return;

	if ( !IsWhitelisted( itemName, cls ) )
	{
		static float s_lastWarning = -1.0f;
		if ( gpGlobals->curtime - s_lastWarning > 30.0f )
		{
			Warning( "TFBot unlocks: '%s' is not on the whitelist for this class (tf_bot_unlocks_list); using stock\n", itemName );
			s_lastWarning = gpGlobals->curtime;
		}
		return;
	}

	bot->AddItem( itemName );
	TFBotFeatures::CountEvent( "unlock_given" );
}


//-----------------------------------------------------------------------------
bool TFBotUnlocks::IsKritz( const CTFBot *medic, const CWeaponMedigun *medigun )
{
	return IsActive( medic ) && medigun && medigun->GetChargeType() == MEDIGUN_CHARGE_CRITICALBOOST;
}

bool TFBotUnlocks::ShouldDeployKritz( CTFBot *medic, CTFPlayer *patient )
{
	if ( !patient || !patient->IsAlive() )
		return false;

	// Use it rather than lose it.
	if ( medic->GetHealth() < 40 )
		return true;

	if ( patient->m_Shared.InCond( TF_COND_INVULNERABLE ) || patient->m_Shared.IsCritBoosted() )
		return false;

	// Crits are for killing: pop when there's an enemy near the patient.
	const CKnownEntity *threat = medic->GetVisionInterface()->GetPrimaryKnownThreat( true );
	return threat && threat->GetEntity() &&
		   ( threat->GetEntity()->GetAbsOrigin() - patient->GetAbsOrigin() ).IsLengthLessThan( 1000.0f );
}


//-----------------------------------------------------------------------------
bool TFBotUnlocks::HasGunslinger( const CTFBot *bot )
{
	CTFWrench *wrench = dynamic_cast< CTFWrench * >( const_cast< CTFBot * >( bot )->Weapon_GetSlot( TF_WPN_TYPE_MELEE ) );
	return wrench && wrench->IsPDQ();
}


//-----------------------------------------------------------------------------
CON_COMMAND( tf_bot_unlocks_list, "List the unlocks bots may use and what the bot code does with each" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	for ( int i = 0; i < ARRAYSIZE( s_whitelist ); ++i )
	{
		Msg( "  %-18s %-9s %s  %s\n", s_whitelist[i].m_itemName, g_aRawPlayerClassNamesShort[ s_whitelist[i].m_playerClass ],
			s_whitelist[i].m_milestone, s_whitelist[i].m_handling );
	}
}
