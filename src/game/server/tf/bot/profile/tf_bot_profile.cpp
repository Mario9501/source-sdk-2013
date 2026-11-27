//========= TFBot overhaul: bot profiles ======================================//
//
// See tf_bot_profile.h and docs/tfbot/DESIGN.md section 3.2 / M1.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_profile.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "tf_gamerules.h"
#include "tf_player.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar tf_bot_difficulty;
extern const char *GetRandomBotName( void );

ConVar tf_bot_human_tier( "tf_bot_human_tier", "2", FCVAR_NONE,
	"Skill tier each human counts as when bot profiles balance the teams (0 new, 1 casual, 2 experienced, 3 competitive).",
	true, 0.0f, true, (float)( TFBOT_PROFILE_TIER_COUNT - 1 ) );


//-----------------------------------------------------------------------------
// Fallback tier values, used only for keys the profile files don't set.
// scripts/tf_bot_profiles.txt is the place to tune these.
//-----------------------------------------------------------------------------
static const float s_fallbackTier[ TFBOT_PROFILE_TIER_COUNT ][ TFBOT_PROFILE_KEY_COUNT ] =
{
	//  react  aimErr track  speed  over   motion decis  aware  | aggr  flank call  retreat strafe eng
	{   0.70f, 4.5f,  0.35f, 700.f, 0.30f, 1.00f, 0.15f, 0.20f,   0.5f, 0.3f, 0.5f, 0.35f,  0.3f,  0.f },	// new
	{   0.45f, 2.5f,  0.20f, 1000.f,0.20f, 0.70f, 0.40f, 0.40f,   0.5f, 0.3f, 0.5f, 0.35f,  0.4f,  0.f },	// casual
	{   0.30f, 1.5f,  0.10f, 1300.f,0.12f, 0.45f, 0.65f, 0.65f,   0.5f, 0.3f, 0.5f, 0.35f,  0.5f,  0.f },	// experienced
	{   0.22f, 0.8f,  0.06f, 1700.f,0.06f, 0.25f, 0.88f, 0.85f,   0.5f, 0.3f, 0.5f, 0.35f,  0.6f,  0.f },	// competitive
};


//-----------------------------------------------------------------------------
static KeyValues *s_stockFile = NULL;
static KeyValues *s_customFile = NULL;

struct TFBotProfileSlot_t
{
	bool m_assigned;
	int m_userId;
	TFBotProfile_t m_profile;
};

static TFBotProfileSlot_t s_slot[ MAX_PLAYERS + 1 ];

// Tier base profiles for GetEffective(); invalidated whenever files reload.
static TFBotProfile_t s_tierCache[ TFBOT_PROFILE_TIER_COUNT ];
static bool s_tierCacheValid[ TFBOT_PROFILE_TIER_COUNT ];


//-----------------------------------------------------------------------------
float TFBotProfile_t::GetSkillScore() const
{
	float sum = 0.0f;
	int n = 0;
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		if ( TFBotProfileKeyInfo( k ).m_group != TFBOT_PROFILE_GROUP_SKILL )
			continue;
		sum += TFBotProfileSkillGoodness( k, m_value[k] );
		++n;
	}
	return n ? sum / n : 0.5f;
}


//-----------------------------------------------------------------------------
// Name-seeded randomness: the same name always gets the same numbers.
//-----------------------------------------------------------------------------
static uint32 HashName( const char *name, uint32 salt )
{
	uint32 h = 2166136261u ^ ( salt * 16777619u );
	for ( const char *c = name; *c; ++c )
	{
		h ^= (uint32)(unsigned char)tolower( (unsigned char)*c );
		h *= 16777619u;
	}
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return h;
}

static float HashUnit( const char *name, uint32 salt )
{
	return ( HashName( name, salt ) & 0xFFFFFF ) / (float)0x1000000;
}


//-----------------------------------------------------------------------------
static int ParseClassName( const char *name )
{
	for ( int i = TF_FIRST_NORMAL_CLASS; i < TF_LAST_NORMAL_CLASS; ++i )
	{
		if ( !V_stricmp( name, g_aRawPlayerClassNamesShort[i] ) || !V_stricmp( name, g_aRawPlayerClassNames[i] ) )
			return i;
	}
	return TF_CLASS_UNDEFINED;
}

static int TierIndex( const char *name )
{
	for ( int t = 0; t < TFBOT_PROFILE_TIER_COUNT; ++t )
	{
		if ( !V_stricmp( name, g_szTFBotProfileTierNames[t] ) )
			return t;
	}
	return -1;
}

static bool IsPresetName( const char *name )
{
	return !V_strnicmp( name, "preset_", 7 );
}


//-----------------------------------------------------------------------------
// Files
//-----------------------------------------------------------------------------
static KeyValues *FindEntry( const char *name )
{
	if ( !name || !name[0] )
		return NULL;
	KeyValues *entry = s_customFile ? s_customFile->FindKey( name ) : NULL;
	if ( !entry && s_stockFile )
		entry = s_stockFile->FindKey( name );
	return entry;
}

static void ValidateFile( KeyValues *file, const char *path )
{
	if ( !file )
		return;

	for ( KeyValues *entry = file->GetFirstTrueSubKey(); entry; entry = entry->GetNextTrueSubKey() )
	{
		for ( KeyValues *item = entry->GetFirstSubKey(); item; item = item->GetNextKey() )
		{
			const char *key = item->GetName();
			if ( !V_stricmp( key, "base" ) || !V_stricmp( key, "presets" ) )
			{
				CUtlStringList names;
				V_SplitString( item->GetString(), ",", names );
				FOR_EACH_VEC( names, i )
				{
					Q_StripPrecedingAndTrailingWhitespace( names[i] );
					if ( names[i][0] && !FindEntry( names[i] ) )
						Warning( "TFBot profiles (%s): '%s' refers to unknown profile '%s'\n", path, entry->GetName(), names[i] );
				}
				continue;
			}
			if ( !V_stricmp( key, "classes" ) || !V_stricmp( key, "loadout" ) )
			{
				for ( KeyValues *c = item->GetFirstSubKey(); c; c = c->GetNextKey() )
				{
					if ( ParseClassName( c->GetName() ) == TF_CLASS_UNDEFINED )
						Warning( "TFBot profiles (%s): '%s' has unknown class '%s' in '%s'\n", path, entry->GetName(), c->GetName(), key );
				}
				continue;
			}

			int k = TFBotProfileFindKey( key );
			float value;
			if ( k < 0 )
			{
				Warning( "TFBot profiles (%s): '%s' has unknown key '%s' (see tf_bot_profile_keys)\n", path, entry->GetName(), key );
			}
			else if ( !TFBotProfileParseValue( k, item->GetString(), &value ) )
			{
				Warning( "TFBot profiles (%s): '%s' key '%s' has unreadable value '%s'\n", path, entry->GetName(), key, item->GetString() );
			}
			else
			{
				bool clamped = false;
				TFBotProfileClampValue( k, value, &clamped );
				if ( clamped )
				{
					const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
					Warning( "TFBot profiles (%s): '%s' key '%s' = %s is outside %g..%g, clamping\n",
						path, entry->GetName(), key, item->GetString(), info.m_min, info.m_max );
				}
			}
		}
	}
}

static void LoadFiles()
{
	if ( s_stockFile )
		s_stockFile->deleteThis();
	if ( s_customFile )
		s_customFile->deleteThis();

	s_stockFile = new KeyValues( "TFBotProfiles" );
	if ( !s_stockFile->LoadFromFile( filesystem, TFBOT_PROFILES_STOCK_FILE, "MOD" ) )
		Warning( "TFBot profiles: %s not found, using built-in tier values\n", TFBOT_PROFILES_STOCK_FILE );

	s_customFile = new KeyValues( "TFBotProfiles" );
	s_customFile->LoadFromFile( filesystem, TFBOT_PROFILES_CUSTOM_FILE, "MOD" );	// optional

	ValidateFile( s_stockFile, TFBOT_PROFILES_STOCK_FILE );
	ValidateFile( s_customFile, TFBOT_PROFILES_CUSTOM_FILE );

	for ( int t = 0; t < TFBOT_PROFILE_TIER_COUNT; ++t )
		s_tierCacheValid[t] = false;
}

static void EnsureLoaded()
{
	if ( !s_stockFile )
		LoadFiles();
}


//-----------------------------------------------------------------------------
// Resolution
//-----------------------------------------------------------------------------
struct TFBotProfileResolve_t
{
	TFBotProfile_t m_profile;
	bool m_set[ TFBOT_PROFILE_KEY_COUNT ];	// set by a non-tier entry
	bool m_classesSet;
};

static void ApplyEntry( KeyValues *entry, TFBotProfileResolve_t &st, int depth )
{
	if ( !entry || depth > 8 )
		return;

	const char *entryName = entry->GetName();
	int tier = TierIndex( entryName );
	bool explicitEntry = ( tier < 0 );

	// bases first, then presets, then own values
	const char *lists[2] = { entry->GetString( "base", "" ), entry->GetString( "presets", "" ) };
	for ( int l = 0; l < 2; ++l )
	{
		CUtlStringList names;
		V_SplitString( lists[l], ",", names );
		FOR_EACH_VEC( names, i )
		{
			Q_StripPrecedingAndTrailingWhitespace( names[i] );
			if ( !names[i][0] )
				continue;
			if ( l == 0 && TierIndex( names[i] ) >= 0 )
				st.m_profile.m_tier = TierIndex( names[i] );
			ApplyEntry( FindEntry( names[i] ), st, depth + 1 );
		}
	}

	if ( tier >= 0 )
		st.m_profile.m_tier = tier;

	for ( KeyValues *item = entry->GetFirstSubKey(); item; item = item->GetNextKey() )
	{
		const char *key = item->GetName();
		if ( !V_stricmp( key, "base" ) || !V_stricmp( key, "presets" ) )
			continue;

		if ( !V_stricmp( key, "classes" ) )
		{
			for ( KeyValues *c = item->GetFirstSubKey(); c; c = c->GetNextKey() )
			{
				int cls = ParseClassName( c->GetName() );
				if ( cls != TF_CLASS_UNDEFINED )
				{
					st.m_profile.m_classWeight[ cls ] = clamp( c->GetFloat(), 0.0f, 10.0f );
					st.m_classesSet = st.m_classesSet || explicitEntry;
				}
			}
			continue;
		}

		if ( !V_stricmp( key, "loadout" ) )
		{
			for ( KeyValues *c = item->GetFirstSubKey(); c; c = c->GetNextKey() )
			{
				int cls = ParseClassName( c->GetName() );
				if ( cls != TF_CLASS_UNDEFINED )
					V_strncpy( st.m_profile.m_loadout[ cls ], c->GetString(), sizeof( st.m_profile.m_loadout[ cls ] ) );
			}
			continue;
		}

		int k = TFBotProfileFindKey( key );
		float value;
		if ( k >= 0 && TFBotProfileParseValue( k, item->GetString(), &value ) )
		{
			st.m_profile.m_value[k] = TFBotProfileClampValue( k, value );
			st.m_set[k] = st.m_set[k] || explicitEntry;
		}
	}
}

// Build the profile for a bot name. 'anonymousTraits' fills style keys and
// class preferences that no profile sets from the name; 'variation' adds the
// per-bot skill variation.
static void BuildProfile( const char *name, int tier, bool anonymousTraits, bool variation, TFBotProfile_t *out )
{
	EnsureLoaded();
	tier = clamp( tier, 0, TFBOT_PROFILE_TIER_COUNT - 1 );

	TFBotProfileResolve_t st;
	V_memset( &st, 0, sizeof( st ) );
	V_strncpy( st.m_profile.m_name, name, sizeof( st.m_profile.m_name ) );
	st.m_profile.m_tier = tier;
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
		st.m_profile.m_value[k] = s_fallbackTier[ tier ][ k ];
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
		st.m_profile.m_classWeight[c] = 1.0f;

	ApplyEntry( FindEntry( g_szTFBotProfileTierNames[ tier ] ), st, 0 );

	KeyValues *own = FindEntry( name );
	if ( own && TierIndex( name ) < 0 )
	{
		// A named profile's own base may name another tier: start from that.
		const char *base = own->GetString( "base", "" );
		int ownTier = TierIndex( base );
		if ( ownTier >= 0 && ownTier != tier )
		{
			for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
				st.m_profile.m_value[k] = s_fallbackTier[ ownTier ][ k ];
		}
		ApplyEntry( own, st, 0 );
	}

	TFBotProfile_t &p = st.m_profile;

	if ( anonymousTraits )
	{
		struct { int key; float lo; float hi; uint32 salt; } traits[] =
		{
			{ TFBOT_PROFILE_AGGRESSION,     0.15f, 0.85f, 1 },
			{ TFBOT_PROFILE_FLANK,          0.0f,  1.0f,  2 },
			{ TFBOT_PROFILE_CALLOUT_RATE,   0.2f,  0.9f,  3 },
			{ TFBOT_PROFILE_RETREAT_HEALTH, 0.25f, 0.50f, 4 },
		};
		for ( int i = 0; i < ARRAYSIZE( traits ); ++i )
		{
			if ( !st.m_set[ traits[i].key ] )
				p.m_value[ traits[i].key ] = traits[i].lo + ( traits[i].hi - traits[i].lo ) * HashUnit( name, traits[i].salt );
		}

		if ( !st.m_set[ TFBOT_PROFILE_STRAFE ] )
			p.m_value[ TFBOT_PROFILE_STRAFE ] = clamp( p.m_value[ TFBOT_PROFILE_STRAFE ] + 0.3f * ( HashUnit( name, 5 ) - 0.5f ), 0.0f, 1.0f );

		if ( !st.m_set[ TFBOT_PROFILE_ENGINEER_STYLE ] )
		{
			// Aggression picks the engineer style (design doc M3b).
			float aggression = p.m_value[ TFBOT_PROFILE_AGGRESSION ];
			p.m_value[ TFBOT_PROFILE_ENGINEER_STYLE ] = (float)( aggression < 0.4f ? TFBOT_ENGINEER_TURTLE :
															  aggression < 0.7f ? TFBOT_ENGINEER_FORWARD : TFBOT_ENGINEER_BATTLE );
		}

		if ( !st.m_classesSet )
		{
			// Two favourite classes and one the bot avoids.
			const int numClasses = TF_LAST_NORMAL_CLASS - TF_FIRST_NORMAL_CLASS;
			int fav1 = TF_FIRST_NORMAL_CLASS + (int)( HashUnit( name, 10 ) * numClasses ) % numClasses;
			int fav2 = TF_FIRST_NORMAL_CLASS + (int)( HashUnit( name, 11 ) * numClasses ) % numClasses;
			int avoid = TF_FIRST_NORMAL_CLASS + (int)( HashUnit( name, 12 ) * numClasses ) % numClasses;
			p.m_classWeight[ avoid ] = 0.3f;
			p.m_classWeight[ fav2 ] = 2.5f;
			p.m_classWeight[ fav1 ] = 4.0f;
		}

		// Loadouts no profile set (used only with the 'unlocks' feature): some
		// soldiers and medics run the whitelisted unlocks; battle engineers
		// need the Gunslinger.
		if ( !p.m_loadout[ TF_CLASS_SOLDIER ][0] && HashUnit( name, 30 ) < 0.3f )
			V_strncpy( p.m_loadout[ TF_CLASS_SOLDIER ], "The Direct Hit", sizeof( p.m_loadout[0] ) );
		if ( !p.m_loadout[ TF_CLASS_MEDIC ][0] && HashUnit( name, 31 ) < 0.3f )
			V_strncpy( p.m_loadout[ TF_CLASS_MEDIC ], "The Kritzkrieg", sizeof( p.m_loadout[0] ) );
		if ( !p.m_loadout[ TF_CLASS_ENGINEER ][0] && (int)( p.m_value[ TFBOT_PROFILE_ENGINEER_STYLE ] + 0.5f ) == TFBOT_ENGINEER_BATTLE )
			V_strncpy( p.m_loadout[ TF_CLASS_ENGINEER ], "The Gunslinger", sizeof( p.m_loadout[0] ) );
	}

	if ( variation )
	{
		for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
		{
			const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
			if ( info.m_variance <= 0.0f || st.m_set[k] )
				continue;
			float delta = ( HashUnit( name, 100 + k ) * 2.0f - 1.0f ) * info.m_variance * ( info.m_max - info.m_min );
			p.m_value[k] = TFBotProfileClampValue( k, p.m_value[k] + delta );
		}
	}

	*out = p;
}

static float TierSkillScore( int tier )
{
	TFBotProfile_t p;
	BuildProfile( g_szTFBotProfileTierNames[ clamp( tier, 0, TFBOT_PROFILE_TIER_COUNT - 1 ) ], tier, false, false, &p );
	return p.GetSkillScore();
}


//-----------------------------------------------------------------------------
// Assignment
//-----------------------------------------------------------------------------
static bool TeamUsesProfiles( int team )
{
	return ( TFBotFeatures::GetTeamMask( team ) >> TFBOT_FEATURE_PROFILES ) & 1;
}

static int ConvarTier()
{
	return clamp( tf_bot_difficulty.GetInt(), 0, TFBOT_PROFILE_TIER_COUNT - 1 );
}

static TFBotProfileSlot_t *SlotFor( const CTFBot *bot )
{
	int index = bot ? bot->entindex() : 0;
	if ( index < 1 || index > MAX_PLAYERS )
		return NULL;
	return &s_slot[ index ];
}

static bool NameInUse( const char *name, CTFPlayer *except )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player != except && !V_stricmp( player->GetPlayerName(), name ) )
			return true;
	}
	return false;
}

// Average skill of a team, counting humans as tf_bot_human_tier.
static float TeamAverageSkill( int team, CTFPlayer *except, int *count )
{
	float sum = 0.0f;
	int n = 0;
	float humanScore = TierSkillScore( tf_bot_human_tier.GetInt() );
	float defaultBotScore = TierSkillScore( ConvarTier() );

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player == except || player->GetTeamNumber() != team )
			continue;

		CTFBot *bot = ToTFBot( player );
		if ( bot )
		{
			TFBotProfileSlot_t *slot = SlotFor( bot );
			sum += ( slot && slot->m_assigned && slot->m_userId == bot->GetUserID() ) ? slot->m_profile.GetSkillScore() : defaultBotScore;
		}
		else if ( !player->IsFakeClient() && !player->IsHLTV() && !player->IsReplay() )
		{
			sum += humanScore;
		}
		else
		{
			continue;
		}
		++n;
	}

	if ( count )
		*count = n;
	return n ? sum / n : -1.0f;
}

static CTFBot::DifficultyType DifficultyFromProfile( const TFBotProfile_t &p )
{
	float q = p.Get( TFBOT_PROFILE_DECISION_QUALITY );
	if ( q < 0.30f ) return CTFBot::EASY;
	if ( q < 0.55f ) return CTFBot::NORMAL;
	if ( q < 0.80f ) return CTFBot::HARD;
	return CTFBot::EXPERT;
}

static void AssignProfile( CTFBot *bot )
{
	TFBotProfileSlot_t *slot = SlotFor( bot );
	if ( !slot )
		return;

	if ( TFGameRules() && TFGameRules()->IsMannVsMachineMode() )
	{
		// Robots: base profile of their popfile skill, no variation or traits.
		int tier = clamp( (int)bot->GetDifficulty(), 0, TFBOT_PROFILE_TIER_COUNT - 1 );
		BuildProfile( g_szTFBotProfileTierNames[ tier ], tier, false, false, &slot->m_profile );
		slot->m_assigned = true;
		slot->m_userId = bot->GetUserID();
		return;
	}

	int team = bot->GetTeamNumber();
	int otherTeam = ( team == TF_TEAM_RED ) ? TF_TEAM_BLUE : TF_TEAM_RED;
	int tier = ConvarTier();
	const char *currentName = bot->GetPlayerName();

	// A bot added for a named profile keeps it.
	KeyValues *named = FindEntry( currentName );
	if ( named && TierIndex( currentName ) < 0 && !IsPresetName( currentName ) )
	{
		BuildProfile( currentName, tier, true, true, &slot->m_profile );
	}
	else
	{
		// Pick the candidate name that best balances average team skill.
		CUtlVector< CUtlString > candidates;
		candidates.AddToTail( currentName );
		for ( int tries = 0; tries < 24 && candidates.Count() < 6; ++tries )
		{
			const char *name = GetRandomBotName();
			bool dup = NameInUse( name, bot );
			FOR_EACH_VEC( candidates, c )
				dup = dup || !V_stricmp( candidates[c].Get(), name );
			if ( !dup )
				candidates.AddToTail( name );
		}

		int teamCount = 0;
		float teamAvg = TeamAverageSkill( team, bot, &teamCount );
		float otherAvg = TeamAverageSkill( otherTeam, NULL, NULL );

		float bestDiff = FLT_MAX;
		TFBotProfile_t best;
		V_memset( &best, 0, sizeof( best ) );
		FOR_EACH_VEC( candidates, c )
		{
			TFBotProfile_t p;
			BuildProfile( candidates[c].Get(), tier, true, true, &p );
			float newAvg = ( teamAvg < 0.0f ) ? p.GetSkillScore()
							: ( teamAvg * teamCount + p.GetSkillScore() ) / ( teamCount + 1 );
			float diff = ( otherAvg < 0.0f ) ? 0.0f : fabsf( newAvg - otherAvg );
			if ( diff < bestDiff )
			{
				bestDiff = diff;
				best = p;
			}
		}
		slot->m_profile = best;
	}

	slot->m_assigned = true;
	slot->m_userId = bot->GetUserID();
	bot->SetDifficulty( DifficultyFromProfile( slot->m_profile ) );
	TFBotFeatures::CountEvent( "profile_assigned" );
}

static void EnsureAssigned( CTFBot *bot )
{
	TFBotProfileSlot_t *slot = SlotFor( bot );
	if ( !slot )
		return;

	if ( slot->m_assigned && slot->m_userId == bot->GetUserID() )
		return;

	slot->m_assigned = false;
	if ( TeamUsesProfiles( bot->GetTeamNumber() ) )
		AssignProfile( bot );
}

static void EnforceName( CTFBot *bot, const TFBotProfileSlot_t *slot )
{
	if ( TFGameRules() && TFGameRules()->IsMannVsMachineMode() )
		return;
	if ( V_stricmp( bot->GetPlayerName(), slot->m_profile.m_name ) && !NameInUse( slot->m_profile.m_name, bot ) )
		engine->SetFakeClientConVarValue( bot->edict(), "name", slot->m_profile.m_name );
}


//-----------------------------------------------------------------------------
const TFBotProfile_t *TFBotProfiles::Get( const CTFBot *bot )
{
	if ( !TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_PROFILES ) )
		return NULL;

	TFBotProfileSlot_t *slot = SlotFor( bot );
	if ( !slot || !slot->m_assigned || slot->m_userId != bot->GetUserID() )
		return NULL;

	return &slot->m_profile;
}


//-----------------------------------------------------------------------------
const TFBotProfile_t &TFBotProfiles::GetEffective( const CTFBot *bot )
{
	const TFBotProfile_t *profile = Get( bot );
	if ( profile )
		return *profile;

	int tier = bot ? clamp( (int)bot->GetDifficulty(), 0, TFBOT_PROFILE_TIER_COUNT - 1 ) : 1;
	if ( !s_tierCacheValid[ tier ] )
	{
		BuildProfile( g_szTFBotProfileTierNames[ tier ], tier, false, false, &s_tierCache[ tier ] );
		s_tierCacheValid[ tier ] = true;
	}
	return s_tierCache[ tier ];
}


//-----------------------------------------------------------------------------
void TFBotProfiles::OnBotSpawn( CTFBot *bot )
{
	TFBotProfileSlot_t *slot = SlotFor( bot );
	if ( !slot )
		return;

	if ( !TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_PROFILES ) )
		return;

	EnsureAssigned( bot );
	if ( slot->m_assigned )
		EnforceName( bot, slot );
}


//-----------------------------------------------------------------------------
const char *TFBotProfiles::ChooseClass( CTFBot *bot )
{
	if ( !TFGameRules() || TFGameRules()->IsMannVsMachineMode() || TFGameRules()->IsInTraining() )
		return NULL;
	if ( !TeamUsesProfiles( bot->GetTeamNumber() ) || !bot->CanChangeClass() )
		return NULL;

	EnsureAssigned( bot );
	TFBotProfileSlot_t *slot = SlotFor( bot );
	if ( !slot || !slot->m_assigned )
		return NULL;

	const TFBotProfile_t &p = slot->m_profile;
	int team = bot->GetTeamNumber();

	// What the team already has (excluding this bot).
	int count[ TF_CLASS_COUNT_ALL ] = { 0 };
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFPlayer *player = ToTFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player == bot || player->GetTeamNumber() != team )
			continue;
		int cls = player->GetPlayerClass()->GetClassIndex();
		if ( cls == TF_CLASS_UNDEFINED )
			cls = player->GetDesiredPlayerClassIndex();
		if ( cls > TF_CLASS_UNDEFINED && cls < TF_CLASS_COUNT_ALL )
			++count[ cls ];
	}

	bool defending = TFGameRules()->IsAttackDefenseMode() && team == TF_TEAM_RED;
	bool facingSentry = bot->GetEnemySentry() != NULL;
	int currentClass = bot->GetPlayerClass()->GetClassIndex();
	bool haveCurrent = currentClass >= TF_FIRST_NORMAL_CLASS && currentClass < TF_LAST_NORMAL_CLASS;

	// Switch only on respawn (DESIGN M1). The stock in-spawn re-evaluation
	// (CTFBotMainAction::Update) asks again every second or two while the bot
	// is alive in its spawn room and kills the bot to switch whenever the
	// answer differs. A fresh weighted-random answer there meant a suicide loop,
	// so while alive the answer is the class it has (unless limits now forbid it).
	if ( bot->IsAlive() && haveCurrent && TFGameRules()->CanBotChooseClass( bot, currentClass ) )
	{
		TFPlayerClassData_t *data = GetPlayerClassData( currentClass );
		return data ? data->m_szClassName : NULL;
	}

	// An engineer with buildings standing keeps them (switching destroys them).
	if ( currentClass == TF_CLASS_ENGINEER && bot->GetObjectCount() > 0 )
	{
		TFPlayerClassData_t *data = GetPlayerClassData( currentClass );
		return data ? data->m_szClassName : NULL;
	}

	float weight[ TF_CLASS_COUNT_ALL ] = { 0.0f };
	float total = 0.0f;
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
	{
		float w = p.m_classWeight[c];
		if ( w <= 0.0f || !TFGameRules()->CanBotChooseClass( bot, c ) )
			continue;

		// Team needs.
		if ( c == TF_CLASS_MEDIC && count[ TF_CLASS_MEDIC ] == 0 )
			w *= 3.0f;
		if ( defending && ( c == TF_CLASS_ENGINEER || c == TF_CLASS_HEAVYWEAPONS ) )
			w *= 1.5f;
		if ( defending && c == TF_CLASS_ENGINEER && count[ TF_CLASS_ENGINEER ] == 0 )
			w *= 1.5f;
		if ( facingSentry )
		{
			if ( c == TF_CLASS_DEMOMAN ) w *= 2.5f;
			else if ( c == TF_CLASS_SPY ) w *= 1.5f;
			else if ( c == TF_CLASS_SOLDIER ) w *= 1.3f;
		}

		// Don't stack classes.
		if ( count[c] >= 3 )
			w *= 0.1f;
		else if ( count[c] >= 2 )
			w *= 0.3f;
		if ( ( c == TF_CLASS_SNIPER || c == TF_CLASS_SPY ) && count[c] >= 1 )
			w *= 0.4f;

		weight[c] = w;
		total += w;
	}

	if ( total <= 0.0f )
		return NULL;

	// People mostly keep their class between lives: 75% plus the class's own
	// share of the weights (so a class the team needs is kept more), up to 95%.
	if ( haveCurrent && weight[ currentClass ] > 0.0f )
	{
		float keepChance = clamp( 0.75f + weight[ currentClass ] / total, 0.75f, 0.95f );
		if ( RandomFloat( 0.0f, 1.0f ) < keepChance )
		{
			TFPlayerClassData_t *data = GetPlayerClassData( currentClass );
			return data ? data->m_szClassName : NULL;
		}
	}

	float pick = RandomFloat( 0.0f, total );
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
	{
		pick -= weight[c];
		if ( pick <= 0.0f && weight[c] > 0.0f )
		{
			TFPlayerClassData_t *data = GetPlayerClassData( c );
			if ( haveCurrent && c != currentClass )
				TFBotFeatures::CountEvent( "profile_class_switch" );
			return data ? data->m_szClassName : NULL;
		}
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Rebuild live bots' profiles after the files changed.
//-----------------------------------------------------------------------------
static void RebuildAssigned()
{
	for ( int i = 1; i <= MAX_PLAYERS; ++i )
	{
		TFBotProfileSlot_t &slot = s_slot[i];
		if ( !slot.m_assigned )
			continue;

		CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
		if ( !bot || bot->GetUserID() != slot.m_userId )
		{
			slot.m_assigned = false;
			continue;
		}

		bool mvm = TFGameRules() && TFGameRules()->IsMannVsMachineMode();
		char name[ 64 ];
		V_strncpy( name, slot.m_profile.m_name, sizeof( name ) );
		int tier = slot.m_profile.m_tier;
		BuildProfile( name, tier, !mvm, !mvm, &slot.m_profile );
		bot->SetDifficulty( DifficultyFromProfile( slot.m_profile ) );
	}
}

class CTFBotProfileSystem : public CAutoGameSystem
{
public:
	CTFBotProfileSystem() : CAutoGameSystem( "CTFBotProfileSystem" ) {}

	virtual void LevelInitPreEntity() OVERRIDE
	{
		// Pick up edits to the profile files on every map load.
		LoadFiles();
		RebuildAssigned();
	}

	virtual void Shutdown() OVERRIDE
	{
		if ( s_stockFile )
			s_stockFile->deleteThis();
		if ( s_customFile )
			s_customFile->deleteThis();
		s_stockFile = s_customFile = NULL;
	}
};

static CTFBotProfileSystem s_tfBotProfileSystem;


//-----------------------------------------------------------------------------
// Console tooling
//-----------------------------------------------------------------------------
static CTFBot *FindBotByName( const char *name )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
		if ( bot && !V_stricmp( bot->GetPlayerName(), name ) )
			return bot;
	}
	return NULL;
}

static void PrintProfile( const TFBotProfile_t &p, const char *heading )
{
	Msg( "%s '%s' (tier %s, skill score %.2f)\n", heading, p.m_name, g_szTFBotProfileTierNames[ p.m_tier ], p.GetSkillScore() );
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
		char value[ 32 ];
		TFBotProfileFormatValue( k, p.m_value[k], value, sizeof( value ) );
		Msg( "  %-20s %-10s (%s, %s)\n", info.m_name, value, info.m_group == TFBOT_PROFILE_GROUP_SKILL ? "skill" : "style", info.m_milestone );
	}
	Msg( "  classes:" );
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
		Msg( " %s=%.1f", g_aRawPlayerClassNamesShort[c], p.m_classWeight[c] );
	Msg( "\n" );
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS; ++c )
	{
		if ( p.m_loadout[c][0] )
			Msg( "  loadout %s: %s\n", g_aRawPlayerClassNamesShort[c], p.m_loadout[c] );
	}
}

// Completes profile names (stock, custom and live bots) and, after a name, keys.
static int ProfileCompletion( const char *partial, char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
	EnsureLoaded();

	char command[ 64 ];
	const char *space = V_strstr( partial, " " );
	if ( !space )
		return 0;
	V_strncpy( command, partial, MIN( (int)sizeof( command ), (int)( space - partial ) + 1 ) );
	const char *rest = space + 1;

	int n = 0;
	const char *secondSpace = V_strstr( rest, " " );
	if ( !secondSpace )
	{
		CUtlVector< CUtlString > names;
		KeyValues *files[2] = { s_stockFile, s_customFile };
		for ( int f = 0; f < 2; ++f )
			for ( KeyValues *e = files[f] ? files[f]->GetFirstTrueSubKey() : NULL; e; e = e->GetNextTrueSubKey() )
				names.AddToTail( e->GetName() );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
			if ( bot )
				names.AddToTail( bot->GetPlayerName() );
		}
		FOR_EACH_VEC( names, i )
		{
			if ( n < COMMAND_COMPLETION_MAXITEMS && !V_strnicmp( names[i].Get(), rest, V_strlen( rest ) ) )
				V_snprintf( commands[ n++ ], COMMAND_COMPLETION_ITEM_LENGTH, "%s \"%s\"", command, names[i].Get() );
		}
		return n;
	}

	if ( V_stricmp( command, "tf_bot_profile_set" ) )
		return 0;

	char name[ 64 ];
	V_strncpy( name, rest, MIN( (int)sizeof( name ), (int)( secondSpace - rest ) + 1 ) );
	const char *keyPartial = secondSpace + 1;
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT && n < COMMAND_COMPLETION_MAXITEMS; ++k )
	{
		const char *key = TFBotProfileKeyInfo( k ).m_name;
		if ( !V_strnicmp( key, keyPartial, V_strlen( keyPartial ) ) )
			V_snprintf( commands[ n++ ], COMMAND_COMPLETION_ITEM_LENGTH, "%s %s %s", command, name, key );
	}
	for ( int c = TF_FIRST_NORMAL_CLASS; c < TF_LAST_NORMAL_CLASS && n < COMMAND_COMPLETION_MAXITEMS; ++c )
	{
		char key[ 48 ];
		V_snprintf( key, sizeof( key ), "class_%s", g_aRawPlayerClassNamesShort[c] );
		if ( !V_strnicmp( key, keyPartial, V_strlen( keyPartial ) ) )
			V_snprintf( commands[ n++ ], COMMAND_COMPLETION_ITEM_LENGTH, "%s %s %s", command, name, key );
	}
	return n;
}

CON_COMMAND( tf_bot_profile_keys, "List the bot profile keys, their ranges and what they do" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
		Msg( "  %-20s %-5s %4s  %g..%g%s%s  %s\n", info.m_name,
			info.m_group == TFBOT_PROFILE_GROUP_SKILL ? "skill" : "style", info.m_milestone,
			info.m_min, info.m_max, info.m_enumNames ? " " : "", info.m_enumNames ? info.m_enumNames : "", info.m_description );
	}
	Msg( "  class_<class> <weight>     class preference (0 never .. 10)\n" );
	Msg( "  loadout_<class> <item>     item name for that class (whitelisted items only, M6)\n" );
}

CON_COMMAND( tf_bot_profile_list, "List profiles in the profile files and the profiles live bots are using" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	EnsureLoaded();

	KeyValues *files[2] = { s_stockFile, s_customFile };
	const char *labels[2] = { TFBOT_PROFILES_STOCK_FILE, TFBOT_PROFILES_CUSTOM_FILE };
	for ( int f = 0; f < 2; ++f )
	{
		Msg( "%s:\n", labels[f] );
		for ( KeyValues *e = files[f] ? files[f]->GetFirstTrueSubKey() : NULL; e; e = e->GetNextTrueSubKey() )
		{
			const char *base = e->GetString( "base", "" );
			Msg( "  %-28s%s%s\n", e->GetName(), base[0] ? " base " : "", base );
		}
	}

	Msg( "live bots:\n" );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
		if ( !bot )
			continue;
		const TFBotProfile_t *p = TFBotProfiles::Get( bot );
		if ( p )
			Msg( "  %-24s team %d  %s  skill %.2f\n", bot->GetPlayerName(), bot->GetTeamNumber(), g_szTFBotProfileTierNames[ p->m_tier ], p->GetSkillScore() );
		else
			Msg( "  %-24s team %d  (stock)\n", bot->GetPlayerName(), bot->GetTeamNumber() );
	}
}

CON_COMMAND_F_COMPLETION( tf_bot_profile_show, "Show a profile: tf_bot_profile_show <name>. For a live bot, shows the values it is using.", FCVAR_NONE, ProfileCompletion )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_profile_show <name>\n" );
		return;
	}

	CTFBot *bot = FindBotByName( args.Arg( 1 ) );
	const TFBotProfile_t *live = bot ? TFBotProfiles::Get( bot ) : NULL;
	if ( live )
	{
		PrintProfile( *live, "Live bot" );
		return;
	}

	TFBotProfile_t p;
	BuildProfile( args.Arg( 1 ), ConvarTier(), true, true, &p );
	PrintProfile( p, FindEntry( args.Arg( 1 ) ) ? "Profile" : "Generated profile" );
}

CON_COMMAND_F_COMPLETION( tf_bot_profile_new, "Create a custom profile: tf_bot_profile_new <name> [base]", FCVAR_NONE, ProfileCompletion )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_profile_new <name> [base]\n" );
		return;
	}
	EnsureLoaded();

	const char *name = args.Arg( 1 );
	const char *base = args.ArgC() >= 3 ? args.Arg( 2 ) : g_szTFBotProfileTierNames[ ConvarTier() ];
	if ( !FindEntry( base ) && TierIndex( base ) < 0 )
	{
		Warning( "Unknown base profile '%s'\n", base );
		return;
	}

	KeyValues *entry = s_customFile->FindKey( name, true );
	entry->SetString( "base", base );
	Msg( "Created profile '%s' (base %s). Save with tf_bot_profile_save.\n", name, base );
	RebuildAssigned();
}

CON_COMMAND_F_COMPLETION( tf_bot_profile_set, "Set a profile value: tf_bot_profile_set <name> <key> <value> (keys: tf_bot_profile_keys)", FCVAR_NONE, ProfileCompletion )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( args.ArgC() < 4 )
	{
		Msg( "Usage: tf_bot_profile_set <name> <key> <value>\n" );
		return;
	}
	EnsureLoaded();

	const char *name = args.Arg( 1 );
	const char *key = args.Arg( 2 );
	const char *text = args.Arg( 3 );

	KeyValues *entry = s_customFile->FindKey( name );
	if ( !entry )
	{
		// Editing a live bot or a stock profile: start a custom override.
		entry = s_customFile->FindKey( name, true );
		KeyValues *stock = s_stockFile ? s_stockFile->FindKey( name ) : NULL;
		if ( stock )
		{
			stock->CopySubkeys( entry );
		}
		else
		{
			CTFBot *bot = FindBotByName( name );
			const TFBotProfile_t *live = bot ? TFBotProfiles::Get( bot ) : NULL;
			entry->SetString( "base", g_szTFBotProfileTierNames[ live ? live->m_tier : ConvarTier() ] );
		}
	}

	if ( !V_strnicmp( key, "class_", 6 ) || !V_strnicmp( key, "loadout_", 8 ) )
	{
		bool isClass = !V_strnicmp( key, "class_", 6 );
		int cls = ParseClassName( key + ( isClass ? 6 : 8 ) );
		if ( cls == TF_CLASS_UNDEFINED )
		{
			Warning( "Unknown class in '%s'\n", key );
			return;
		}
		KeyValues *section = entry->FindKey( isClass ? "classes" : "loadout", true );
		if ( isClass )
			section->SetFloat( g_aRawPlayerClassNamesShort[ cls ], clamp( (float)atof( text ), 0.0f, 10.0f ) );
		else
			section->SetString( g_aRawPlayerClassNamesShort[ cls ], text );
	}
	else
	{
		int k = TFBotProfileFindKey( key );
		float value;
		if ( k < 0 )
		{
			Warning( "Unknown key '%s' (see tf_bot_profile_keys)\n", key );
			return;
		}
		if ( !TFBotProfileParseValue( k, text, &value ) )
		{
			Warning( "Can't read '%s' as a value for '%s'\n", text, key );
			return;
		}
		bool clamped = false;
		value = TFBotProfileClampValue( k, value, &clamped );
		if ( clamped )
			Warning( "Clamped to %g (range %g..%g)\n", value, TFBotProfileKeyInfo( k ).m_min, TFBotProfileKeyInfo( k ).m_max );
		char formatted[ 32 ];
		TFBotProfileFormatValue( k, value, formatted, sizeof( formatted ) );
		entry->SetString( TFBotProfileKeyInfo( k ).m_name, formatted );
	}

	RebuildAssigned();
	Msg( "Set %s.%s = %s (live bots updated; tf_bot_profile_save to keep it)\n", name, key, text );
}

CON_COMMAND( tf_bot_profile_save, "Save custom profiles to " TFBOT_PROFILES_CUSTOM_FILE )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	EnsureLoaded();
	filesystem->CreateDirHierarchy( "scripts", "MOD" );
	if ( s_customFile->SaveToFile( filesystem, TFBOT_PROFILES_CUSTOM_FILE, "MOD" ) )
		Msg( "Saved %s\n", TFBOT_PROFILES_CUSTOM_FILE );
	else
		Warning( "Could not save %s\n", TFBOT_PROFILES_CUSTOM_FILE );
}

CON_COMMAND( tf_bot_profile_reload, "Reload the profile files; live bots pick up the changes immediately" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	LoadFiles();
	RebuildAssigned();
	Msg( "Reloaded bot profiles\n" );
}

CON_COMMAND_F_COMPLETION( tf_bot_add_profile, "Add a bot that uses a profile: tf_bot_add_profile <name> [red|blue]", FCVAR_NONE, ProfileCompletion )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_bot_add_profile <name> [red|blue]\n" );
		return;
	}
	// tf_bot_add only knows "red" and "blue"; anything else would be read as a name.
	const char *team = ( args.ArgC() >= 3 && ( !V_stricmp( args.Arg( 2 ), "red" ) || !V_stricmp( args.Arg( 2 ), "blue" ) ) ) ? args.Arg( 2 ) : "";
	engine->ServerCommand( UTIL_VarArgs( "tf_bot_add %s noquota \"%s\"\n", team, args.Arg( 1 ) ) );
}
