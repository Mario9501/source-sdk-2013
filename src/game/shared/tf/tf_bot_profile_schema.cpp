//========= TFBot overhaul: bot profile schema ================================//
//
// See tf_bot_profile_schema.h.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_profile_schema.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const char *g_szTFBotProfileTierNames[ TFBOT_PROFILE_TIER_COUNT ] =
{
	"tier_new",
	"tier_casual",
	"tier_experienced",
	"tier_competitive",
};

static const TFBotProfileKeyInfo_t s_keyInfo[ TFBOT_PROFILE_KEY_COUNT ] =
{
	// name                  group                      min    max     var    higher? enum                       ms    description
	{ "reaction_time",       TFBOT_PROFILE_GROUP_SKILL, 0.10f, 1.50f,  0.04f, false, NULL,                      "M1", "Seconds before reacting to a newly seen enemy" },
	{ "aim_error",           TFBOT_PROFILE_GROUP_SKILL, 0.0f,  15.0f,  0.04f, false, NULL,                      "M2", "Base aim error in degrees" },
	{ "aim_tracking",        TFBOT_PROFILE_GROUP_SKILL, 0.02f, 1.00f,  0.03f, false, NULL,                      "M1", "Seconds between aim target updates (tracking lag)" },
	{ "aim_speed",           TFBOT_PROFILE_GROUP_SKILL, 150.f, 2000.f, 0.04f, true,  NULL,                      "M2", "Head turn speed in degrees per second" },
	{ "aim_overshoot",       TFBOT_PROFILE_GROUP_SKILL, 0.0f,  0.60f,  0.05f, false, NULL,                      "M2", "Fraction a flick overshoots before correcting" },
	{ "aim_motion_penalty",  TFBOT_PROFILE_GROUP_SKILL, 0.0f,  2.0f,   0.05f, false, NULL,                      "M2", "Extra aim error per unit of target angular speed" },
	{ "decision_quality",    TFBOT_PROFILE_GROUP_SKILL, 0.0f,  1.0f,   0.05f, true,  NULL,                      "M1", "Positioning, target choice and retreat judgement" },
	{ "awareness",           TFBOT_PROFILE_GROUP_SKILL, 0.0f,  1.0f,   0.06f, true,  NULL,                      "M3", "Hearing range and how often it glances around" },

	{ "aggression",          TFBOT_PROFILE_GROUP_STYLE, 0.0f,  1.0f,   0.0f,  true,  NULL,                      "M1", "0 cautious .. 1 reckless" },
	{ "flank",               TFBOT_PROFILE_GROUP_STYLE, 0.0f,  1.0f,   0.0f,  true,  NULL,                      "M3", "0 holds the front line .. 1 flanks" },
	{ "callout_rate",        TFBOT_PROFILE_GROUP_STYLE, 0.0f,  1.0f,   0.0f,  true,  NULL,                      "M3", "0 silent .. 1 calls out everything" },
	{ "retreat_health",      TFBOT_PROFILE_GROUP_STYLE, 0.10f, 0.90f,  0.0f,  true,  NULL,                      "M1", "Health fraction at which it falls back for health" },
	{ "strafe",              TFBOT_PROFILE_GROUP_STYLE, 0.0f,  1.0f,   0.0f,  true,  NULL,                      "M4", "How much it strafes and dodges in fights" },
	{ "engineer_style",      TFBOT_PROFILE_GROUP_STYLE, 0.0f,  2.0f,   0.0f,  true,  "turtle,forward,battle",   "M3b", "Engineer play style" },
};


//-----------------------------------------------------------------------------
const TFBotProfileKeyInfo_t &TFBotProfileKeyInfo( int key )
{
	Assert( key >= 0 && key < TFBOT_PROFILE_KEY_COUNT );
	if ( key < 0 || key >= TFBOT_PROFILE_KEY_COUNT )
		key = 0;
	return s_keyInfo[ key ];
}


//-----------------------------------------------------------------------------
int TFBotProfileFindKey( const char *name )
{
	for ( int i = 0; i < TFBOT_PROFILE_KEY_COUNT; ++i )
	{
		if ( !V_stricmp( s_keyInfo[i].m_name, name ) )
			return i;
	}
	return -1;
}


//-----------------------------------------------------------------------------
// Index of 'name' in a comma list, or -1.
static int FindChoice( const char *list, const char *name )
{
	int index = 0;
	const char *start = list;
	while ( *start )
	{
		const char *end = V_strstr( start, "," );
		int len = end ? (int)( end - start ) : V_strlen( start );
		if ( len == V_strlen( name ) && !V_strnicmp( start, name, len ) )
			return index;
		if ( !end )
			break;
		start = end + 1;
		++index;
	}
	return -1;
}


//-----------------------------------------------------------------------------
bool TFBotProfileParseValue( int key, const char *text, float *outValue )
{
	if ( key < 0 || key >= TFBOT_PROFILE_KEY_COUNT || !text || !text[0] )
		return false;

	const TFBotProfileKeyInfo_t &info = s_keyInfo[ key ];
	if ( info.m_enumNames )
	{
		int choice = FindChoice( info.m_enumNames, text );
		if ( choice >= 0 )
		{
			*outValue = (float)choice;
			return true;
		}
	}

	char *end = NULL;
	float value = strtof( text, &end );
	if ( end == text )
		return false;

	*outValue = info.m_enumNames ? (float)(int)( value + 0.5f ) : value;
	return true;
}


//-----------------------------------------------------------------------------
float TFBotProfileClampValue( int key, float value, bool *wasClamped )
{
	const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( key );
	float clamped = clamp( value, info.m_min, info.m_max );
	if ( wasClamped )
		*wasClamped = ( clamped != value );
	return clamped;
}


//-----------------------------------------------------------------------------
void TFBotProfileFormatValue( int key, float value, char *buf, int bufSize )
{
	const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( key );
	if ( info.m_enumNames )
	{
		int choice = (int)( value + 0.5f );
		const char *start = info.m_enumNames;
		for ( int i = 0; i < choice && start; ++i )
		{
			start = V_strstr( start, "," );
			if ( start )
				++start;
		}
		if ( start )
		{
			const char *end = V_strstr( start, "," );
			int len = end ? (int)( end - start ) : V_strlen( start );
			V_strncpy( buf, start, MIN( bufSize, len + 1 ) );
			return;
		}
	}
	V_snprintf( buf, bufSize, "%.4g", value );
}


//-----------------------------------------------------------------------------
float TFBotProfileSkillGoodness( int key, float value )
{
	const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( key );
	float range = info.m_max - info.m_min;
	if ( range <= 0.0f )
		return 0.5f;

	float t = clamp( ( value - info.m_min ) / range, 0.0f, 1.0f );
	return info.m_higherIsBetter ? t : 1.0f - t;
}
