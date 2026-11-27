//========= TFBot overhaul: per-bot feature flags ============================//
//
// See tf_bot_features.h and docs/tfbot/DESIGN.md section 3.7.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_features.h"
#include "bot/tf_bot.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

struct TFBotFeatureInfo_t
{
	const char *m_name;
	bool m_isDefault;		// part of the "default" set (proven by A/B + playtests)
	const char *m_description;
};

static const TFBotFeatureInfo_t s_featureInfo[ TFBOT_FEATURE_COUNT ] =
{
	{ "test_noop", false, "M0 plumbing check: registered and recorded, changes no behaviour" },
	{ "profiles",  false, "M1 per-bot skill/style profiles (aim tracking, reaction), balanced assignment, team-needs class choice" },
	{ "aim",       false, "M2 human-like aim: wobble, lag on moving targets, flick overshoot, real projectile leading, pre-aim" },
	{ "blackboard", false, "M3 shared team knowledge (sentries, spies), group up after wipes / for uber, focus fire, medics answer bot calls" },
	{ "voice",     false, "M3 voice callouts: medic, spy, sentry ahead, uber ready, go go go (rate-limited)" },
	{ "perception", false, "M3 hearing nearby footsteps (approximate position) and glancing around when idle" },
	{ "medic",     false, "M3 medic fixes: IsGoodUberTarget no longer always false" },
	{ "engineer",  false, "M3b engineer styles (turtle/forward/battle), carry buildings instead of blowing them up, save dying sentries, help teammates' buildings" },
	{ "movement",  false, "M4 combat movement: sustained strafes with human-like direction changes, combat jumps, scout double jumps, medic strafing" },
	{ "unlocks",   false, "M6 whitelisted unlocks from profile loadouts: Direct Hit, Kritzkrieg (own deploy logic), Gunslinger (battle engineer)" },
	{ "mobility",  false, "M7 soldiers rocket jump and demos sticky jump up ledges the path planner can now use" },
};

COMPILE_TIME_ASSERT( TFBOT_FEATURE_COUNT <= 64 );

static TFBotFeatureMask s_botMask[ MAX_PLAYERS + 1 ];
static double s_featureCost[ TFBOT_FEATURE_COUNT ];

static void OnFeatureSettingChanged( IConVar *var, const char *pOldValue, float flOldValue );

ConVar tf_bot_features_red( "tf_bot_features_red", "default", FCVAR_NONE,
	"Overhaul features for RED bots: comma list of feature names, or 'stock', 'default', 'all'. Read when a bot spawns.",
	OnFeatureSettingChanged );
ConVar tf_bot_features_blu( "tf_bot_features_blu", "default", FCVAR_NONE,
	"Overhaul features for BLU bots: comma list of feature names, or 'stock', 'default', 'all'. Read when a bot spawns.",
	OnFeatureSettingChanged );


//-----------------------------------------------------------------------------
static int FindFeature( const char *name )
{
	for ( int i = 0; i < TFBOT_FEATURE_COUNT; ++i )
	{
		if ( !V_stricmp( s_featureInfo[i].m_name, name ) )
			return i;
	}
	return -1;
}


//-----------------------------------------------------------------------------
static TFBotFeatureMask ParseFeatureList( const char *list, bool warnUnknown )
{
	TFBotFeatureMask mask = 0;

	CUtlStringList tokens;
	V_SplitString( list, ",", tokens );

	FOR_EACH_VEC( tokens, i )
	{
		char token[ 64 ];
		V_strncpy( token, tokens[i], sizeof( token ) );
		Q_StripPrecedingAndTrailingWhitespace( token );

		if ( !token[0] || !V_stricmp( token, "stock" ) || !V_stricmp( token, "none" ) )
			continue;

		if ( !V_stricmp( token, "all" ) )
		{
			for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
				mask |= ( (TFBotFeatureMask)1 << f );
			continue;
		}

		if ( !V_stricmp( token, "default" ) )
		{
			for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
			{
				if ( s_featureInfo[f].m_isDefault )
					mask |= ( (TFBotFeatureMask)1 << f );
			}
			continue;
		}

		int f = FindFeature( token );
		if ( f < 0 )
		{
			if ( warnUnknown )
				Warning( "TFBot features: unknown feature '%s' (see tf_bot_features_list)\n", token );
			continue;
		}
		mask |= ( (TFBotFeatureMask)1 << f );
	}

	return mask;
}


//-----------------------------------------------------------------------------
static void OnFeatureSettingChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVarRef ref( var );
	ParseFeatureList( ref.GetString(), true );
}


//-----------------------------------------------------------------------------
const char *TFBotFeatures::GetName( ETFBotFeature feature )
{
	if ( feature < 0 || feature >= TFBOT_FEATURE_COUNT )
		return "invalid";
	return s_featureInfo[ feature ].m_name;
}


//-----------------------------------------------------------------------------
TFBotFeatureMask TFBotFeatures::GetBotMask( const CTFBot *bot )
{
	if ( !bot )
		return 0;

	int index = bot->entindex();
	if ( index < 1 || index > MAX_PLAYERS )
		return 0;

	return s_botMask[ index ];
}


//-----------------------------------------------------------------------------
bool TFBotFeatures::IsEnabled( const CTFBot *bot, ETFBotFeature feature )
{
	if ( feature < 0 || feature >= TFBOT_FEATURE_COUNT )
		return false;

	return ( GetBotMask( bot ) >> feature ) & 1;
}


//-----------------------------------------------------------------------------
const char *TFBotFeatures::GetTeamSetting( int team )
{
	switch ( team )
	{
	case TF_TEAM_RED:	return tf_bot_features_red.GetString();
	case TF_TEAM_BLUE:	return tf_bot_features_blu.GetString();
	default:			return "stock";
	}
}


//-----------------------------------------------------------------------------
TFBotFeatureMask TFBotFeatures::GetTeamMask( int team )
{
	if ( team != TF_TEAM_RED && team != TF_TEAM_BLUE )
		return 0;

	return ParseFeatureList( GetTeamSetting( team ), false );
}


//-----------------------------------------------------------------------------
void TFBotFeatures::OnBotSpawn( CTFBot *bot )
{
	if ( !bot )
		return;

	int index = bot->entindex();
	if ( index < 1 || index > MAX_PLAYERS )
		return;

	s_botMask[ index ] = GetTeamMask( bot->GetTeamNumber() );
}


//-----------------------------------------------------------------------------
void TFBotFeatures::MaskToString( TFBotFeatureMask mask, char *buf, int bufSize )
{
	if ( bufSize <= 0 )
		return;

	buf[0] = '\0';
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
	{
		if ( !( ( mask >> f ) & 1 ) )
			continue;

		if ( buf[0] )
			V_strncat( buf, ",", bufSize );
		V_strncat( buf, s_featureInfo[f].m_name, bufSize );
	}
}


//-----------------------------------------------------------------------------
void TFBotFeatures::AddCost( ETFBotFeature feature, double seconds )
{
	if ( feature >= 0 && feature < TFBOT_FEATURE_COUNT )
		s_featureCost[ feature ] += seconds;
}


//-----------------------------------------------------------------------------
double TFBotFeatures::GetCost( ETFBotFeature feature )
{
	if ( feature < 0 || feature >= TFBOT_FEATURE_COUNT )
		return 0.0;
	return s_featureCost[ feature ];
}


//-----------------------------------------------------------------------------
static const int kMaxEvents = 48;
static const char *s_eventName[ kMaxEvents ];
static int s_eventCount[ kMaxEvents ];
static int s_numEvents = 0;

void TFBotFeatures::CountEvent( const char *name )
{
	for ( int i = 0; i < s_numEvents; ++i )
	{
		if ( s_eventName[i] == name || !V_strcmp( s_eventName[i], name ) )
		{
			++s_eventCount[i];
			return;
		}
	}
	if ( s_numEvents < kMaxEvents )
	{
		s_eventName[ s_numEvents ] = name;
		s_eventCount[ s_numEvents ] = 1;
		++s_numEvents;
	}
}

int TFBotFeatures::GetEventCount( int index, const char **name )
{
	if ( index < 0 || index >= s_numEvents )
		return -1;
	*name = s_eventName[ index ];
	return s_eventCount[ index ];
}

void TFBotFeatures::ResetEventCounts()
{
	for ( int i = 0; i < s_numEvents; ++i )
		s_eventCount[i] = 0;
}


//-----------------------------------------------------------------------------
CTFBotFeatureCostScope::CTFBotFeatureCostScope( ETFBotFeature feature )
	: m_feature( feature ), m_start( Plat_FloatTime() )
{
}

CTFBotFeatureCostScope::~CTFBotFeatureCostScope()
{
	TFBotFeatures::AddCost( m_feature, Plat_FloatTime() - m_start );
}


//-----------------------------------------------------------------------------
CON_COMMAND( tf_bot_features_list, "List TFBot overhaul features and which teams currently enable them" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	TFBotFeatureMask red = TFBotFeatures::GetTeamMask( TF_TEAM_RED );
	TFBotFeatureMask blu = TFBotFeatures::GetTeamMask( TF_TEAM_BLUE );

	Msg( "TFBot features (RED = \"%s\", BLU = \"%s\"):\n", tf_bot_features_red.GetString(), tf_bot_features_blu.GetString() );
	Msg( "  %-24s %-8s %-4s %-4s %s\n", "name", "status", "RED", "BLU", "description" );
	for ( int f = 0; f < TFBOT_FEATURE_COUNT; ++f )
	{
		Msg( "  %-24s %-8s %-4s %-4s %s\n",
			s_featureInfo[f].m_name,
			s_featureInfo[f].m_isDefault ? "default" : "testing",
			( ( red >> f ) & 1 ) ? "on" : "-",
			( ( blu >> f ) & 1 ) ? "on" : "-",
			s_featureInfo[f].m_description );
	}
}


//-----------------------------------------------------------------------------
CON_COMMAND( tf_bot_features_dump, "Show the feature set each bot spawned with" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTFBot *bot = ToTFBot( UTIL_PlayerByIndex( i ) );
		if ( !bot )
			continue;

		char names[ 512 ];
		TFBotFeatures::MaskToString( TFBotFeatures::GetBotMask( bot ), names, sizeof( names ) );
		Msg( "  %2d %-20s team %d : %s\n", i, bot->GetPlayerName(), bot->GetTeamNumber(), names[0] ? names : "(stock)" );
	}
}
