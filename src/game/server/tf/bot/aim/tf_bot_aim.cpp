//========= TFBot overhaul: human-like aim (M2) ===============================//
//
// See tf_bot_aim.h and docs/tfbot/DESIGN.md M2.
//
//=============================================================================//
#include "cbase.h"
#include "tf_bot_aim.h"
#include "bot/tf_bot.h"
#include "bot/features/tf_bot_features.h"
#include "bot/profile/tf_bot_profile.h"
#include "tf_weaponbase_gun.h"
#include "tf_obj_sentrygun.h"
#include "nav_mesh/tf_nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar tf_bot_ballistic_elevation_rate;

ConVar tf_bot_aim_overshoot_settle( "tf_bot_aim_overshoot_settle", "0.18", FCVAR_CHEAT,
	"Time constant (s) for a flick's overshoot to settle (M2 aim model)" );
ConVar tf_bot_aim_motion_scale( "tf_bot_aim_motion_scale", "0.05", FCVAR_CHEAT,
	"Degrees of extra aim error per degree/second of target angular speed, before the profile's aim_motion_penalty" );
ConVar tf_bot_aim_error_scale( "tf_bot_aim_error_scale", "1.0", FCVAR_CHEAT,
	"Scales the profile's aim_error (hand wobble) and aim_overshoot (M2 aim model)" );
ConVar tf_bot_aim_wobble_settle( "tf_bot_aim_wobble_settle", "0.5", FCVAR_CHEAT,
	"Time constant (s) for hand wobble to shrink while a bot stays on one target (M2 aim model)" );
ConVar tf_bot_aim_wobble_floor( "tf_bot_aim_wobble_floor", "0.25", FCVAR_CHEAT,
	"Fraction of the profile's aim_error left once a bot has settled on a target (M2 aim model)" );
ConVar tf_bot_aim_speed_scale( "tf_bot_aim_speed_scale", "1.0", FCVAR_CHEAT,
	"Scales the profile's aim_speed, the head turn speed (M2 aim model)" );
ConVar tf_bot_aim_preaim( "tf_bot_aim_preaim", "1", FCVAR_CHEAT,
	"Out of combat, hold the nearest likely enemy approach instead of the stock random glances (M2 aim model)" );
ConVar tf_bot_aim_preaim_hold_scale( "tf_bot_aim_preaim_hold_scale", "1.0", FCVAR_CHEAT,
	"Scales how long a pre-aim angle is held (1-2 s by awareness at 1.0) (M2 aim model)" );
ConVar tf_bot_aim_debug( "tf_bot_aim_debug", "0", FCVAR_CHEAT, "Draw the M2 aim model's ideal and erroneous aim points" );

// Projectile gravity in units/s^2 at gravity scale 1 (sv_gravity default).
static const float kGravity = 800.0f;

struct TFBotAimState_t
{
	EHANDLE m_target;
	float m_acquireTime;
	float m_lastUpdate;
	Vector m_overshootDir;		// unit, perpendicular to the line of sight at acquisition
	float m_overshootDeg;
	float m_wobblePhase;
	float m_wobbleMagPhase;
	float m_leadNoise;			// -1..1, how wrong this bot's lead is for this engagement
	CountdownTimer m_preAimTimer;
};

static TFBotAimState_t s_aim[ MAX_PLAYERS + 1 ];


//-----------------------------------------------------------------------------
bool TFBotAim::IsActive( const CTFBot *bot )
{
	return TFBotFeatures::IsEnabled( bot, TFBOT_FEATURE_AIM );
}

static TFBotAimState_t *StateFor( const CTFBot *bot )
{
	int index = bot->entindex();
	if ( index < 1 || index > MAX_PLAYERS )
		return NULL;
	return &s_aim[ index ];
}


//-----------------------------------------------------------------------------
float TFBotAim::GetMaxHeadAngularVelocity( const CTFBot *me )
{
	return TFBotProfiles::GetEffective( me ).Get( TFBOT_PROFILE_AIM_SPEED ) * tf_bot_aim_speed_scale.GetFloat();
}


//-----------------------------------------------------------------------------
// Projectile speed: the weapon's own value, else a per-weapon fallback.
//-----------------------------------------------------------------------------
static float ProjectileSpeed( CTFWeaponBaseGun *gun )
{
	float speed = gun->GetProjectileSpeed();
	if ( speed > 1.0f )
		return speed;

	switch ( gun->GetWeaponID() )
	{
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:	return 1980.0f;
	case TF_WEAPON_GRENADELAUNCHER:				return 1216.0f;
	case TF_WEAPON_PIPEBOMBLAUNCHER:			return 900.0f;
	case TF_WEAPON_CANNON:						return 1454.0f;
	case TF_WEAPON_FLAREGUN:					return 2000.0f;
	case TF_WEAPON_SYRINGEGUN_MEDIC:			return 1000.0f;
	case TF_WEAPON_CROSSBOW:					return 2400.0f;
	case TF_WEAPON_COMPOUND_BOW:				return 1800.0f;
	default:									return 1100.0f;
	}
}

static bool IsSplashRocket( int id )
{
	return id == TF_WEAPON_ROCKETLAUNCHER || id == TF_WEAPON_PARTICLE_CANNON;
}

static bool IsLobbed( int id )
{
	return id == TF_WEAPON_GRENADELAUNCHER || id == TF_WEAPON_PIPEBOMBLAUNCHER || id == TF_WEAPON_CANNON;
}

// Raise the aim for a lobbed projectile, as the stock code does.
static Vector AddLobElevation( const Vector &eye, const Vector &point )
{
	float range = ( point - eye ).Length2D();
	float elevationAngle = MIN( range * tf_bot_ballistic_elevation_rate.GetFloat(), 45.0f );
	float s, c;
	FastSinCos( DEG2RAD( elevationAngle ), &s, &c );
	return ( c > 0.0f ) ? point + Vector( 0, 0, range * s / c ) : point;
}


//-----------------------------------------------------------------------------
// Where a perfect aimer of this skill would aim, including projectile lead.
//-----------------------------------------------------------------------------
static Vector IdealPoint( CTFBot *me, const CBaseCombatCharacter *subject, const TFBotProfile_t &p, TFBotAimState_t &st )
{
	const Vector eye = me->EyePosition();
	const float aimSkill = TFBotProfileSkillGoodness( TFBOT_PROFILE_AIM_ERROR, p.Get( TFBOT_PROFILE_AIM_ERROR ) );
	CTFWeaponBase *weapon = me->m_Shared.GetActiveTFWeapon();
	CTFWeaponBaseGun *gun = dynamic_cast< CTFWeaponBaseGun * >( weapon );
	int id = weapon ? weapon->GetWeaponID() : TF_WEAPON_NONE;

	// Buildings: the sentry's body; lob grenades onto it.
	if ( subject->IsBaseObject() )
	{
		Vector point = subject->WorldSpaceCenter();
		const CObjectSentrygun *sentry = dynamic_cast< const CObjectSentrygun * >( subject );
		if ( sentry )
			point = sentry->GetAbsOrigin() + 0.5f * sentry->GetViewOffset();
		return ( gun && IsLobbed( id ) ) ? AddLobElevation( eye, point ) : point;
	}

	const Vector center = subject->WorldSpaceCenter();
	const Vector head = subject->EyePosition();
	const Vector feet = subject->GetAbsOrigin();

	if ( WeaponID_IsSniperRifle( id ) )
		return Lerp( 0.35f + 0.65f * aimSkill, center, head );

	int projectile = gun ? gun->GetWeaponProjectileType() : TF_PROJECTILE_NONE;
	if ( !gun || projectile == TF_PROJECTILE_BULLET || projectile == TF_PROJECTILE_NONE )
		return Lerp( 0.25f * aimSkill, center, head );	// centre mass; better players sit a bit higher

	// --- projectiles ---
	Vector base = center;
	bool groundAim = false;
	if ( IsSplashRocket( id ) )
	{
		const float aboveTolerance = 30.0f;
		if ( feet.z - aboveTolerance > me->GetAbsOrigin().z )
		{
			base = me->GetVisionInterface()->IsAbleToSee( feet, IVision::DISREGARD_FOV ) ? feet : center;
		}
		else if ( subject->GetGroundEntity() == NULL )
		{
			trace_t result;
			UTIL_TraceLine( feet, feet + Vector( 0, 0, -200 ), MASK_SOLID, subject, COLLISION_GROUP_NONE, &result );
			base = result.DidHit() ? result.endpos : feet;
			groundAim = true;
		}
		else
		{
			base = feet + Vector( 0, 0, 4 );
			groundAim = true;
		}
	}
	else if ( id == TF_WEAPON_COMPOUND_BOW || id == TF_WEAPON_CROSSBOW )
	{
		base = Lerp( 0.5f + 0.5f * aimSkill, center, head );
	}

	// Lead: iterate the intercept twice. Weaker players misjudge the lead.
	const float speed = ProjectileSpeed( gun );
	// Lead misjudgement grows with aim error: about +-10% for the casual tier.
	const float leadFactor = 1.0f + st.m_leadNoise * 0.6f * ( 1.0f - aimSkill ) * tf_bot_aim_error_scale.GetFloat();
	Vector velocity = subject->GetAbsVelocity();
	if ( groundAim )
		velocity.z = 0.0f;

	Vector point = base;
	float flightTime = 0.0f;
	for ( int i = 0; i < 2; ++i )
	{
		flightTime = ( point - eye ).Length() / speed;
		point = base + velocity * flightTime * leadFactor;
	}

	if ( IsLobbed( id ) )
		return AddLobElevation( eye, point );

	float gravityScale = gun->GetProjectileGravity();
	if ( gravityScale > 0.0f )
		point.z += 0.5f * kGravity * gravityScale * flightTime * flightTime;

	return point;
}


//-----------------------------------------------------------------------------
Vector TFBotAim::SelectTargetPoint( CTFBot *me, const CBaseCombatCharacter *subject )
{
	TFBotAimState_t *st = StateFor( me );
	if ( !st || !subject )
		return subject ? subject->WorldSpaceCenter() : vec3_origin;

	const TFBotProfile_t &p = TFBotProfiles::GetEffective( me );
	const float now = gpGlobals->curtime;
	const Vector eye = me->EyePosition();

	// New engagement: remember how far we flicked to get here.
	bool newTarget = ( st->m_target.Get() != subject ) || ( now - st->m_lastUpdate > 1.0f );
	if ( newTarget )
	{
		st->m_target = const_cast< CBaseCombatCharacter * >( subject );
		st->m_acquireTime = now;
		st->m_wobblePhase = RandomFloat( 0.0f, 2.0f * M_PI );
		st->m_wobbleMagPhase = RandomFloat( 0.0f, 2.0f * M_PI );
		st->m_leadNoise = RandomFloat( -1.0f, 1.0f );
		TFBotFeatures::CountEvent( "aim_acquire" );
	}

	Vector ideal = IdealPoint( me, subject, p, *st );

	Vector los = ideal - eye;
	float dist = los.NormalizeInPlace();
	if ( dist < 1.0f )
	{
		st->m_lastUpdate = now;
		return ideal;
	}

	if ( newTarget )
	{
		Vector view = me->GetBodyInterface()->GetViewVector();
		float flickDeg = RAD2DEG( acosf( clamp( DotProduct( view, los ), -1.0f, 1.0f ) ) );
		Vector flick = los - view;
		flick -= DotProduct( flick, los ) * los;
		if ( flick.NormalizeInPlace() < 0.001f )
			flick = Vector( los.y, -los.x, 0.0f ).Normalized();
		st->m_overshootDir = flick;
		st->m_overshootDeg = MIN( p.Get( TFBOT_PROFILE_AIM_OVERSHOOT ) * tf_bot_aim_error_scale.GetFloat() * flickDeg, 12.0f );
	}
	st->m_lastUpdate = now;
	const float t = now - st->m_acquireTime;

	// Basis perpendicular to the line of sight.
	Vector right;
	CrossProduct( los, Vector( 0, 0, 1 ), right );
	if ( right.NormalizeInPlace() < 0.001f )
		right = Vector( 1, 0, 0 );
	Vector up;
	CrossProduct( right, los, up );

	// Hand wobble: a slowly circling offset whose size breathes, and shrinks
	// toward a floor while the bot stays on the same target (people settle).
	float wobbleFloor = clamp( tf_bot_aim_wobble_floor.GetFloat(), 0.0f, 1.0f );
	float settled = wobbleFloor + ( 1.0f - wobbleFloor ) * expf( -t / MAX( tf_bot_aim_wobble_settle.GetFloat(), 0.01f ) );
	float wobbleMag = p.Get( TFBOT_PROFILE_AIM_ERROR ) * tf_bot_aim_error_scale.GetFloat() * settled *
					  ( 0.55f + 0.45f * sinf( 1.3f * t + st->m_wobbleMagPhase ) );
	float wobbleAng = st->m_wobblePhase + 0.9f * t;
	float errRight = wobbleMag * cosf( wobbleAng );
	float errUp = 0.6f * wobbleMag * sinf( wobbleAng );

	// Lag behind targets crossing the view.
	Vector relVel = subject->GetAbsVelocity() - me->GetAbsVelocity();
	float vRight = DotProduct( relVel, right );
	float vUp = DotProduct( relVel, up );
	float vCross = sqrtf( vRight * vRight + vUp * vUp );
	if ( vCross > 1.0f && !subject->IsBaseObject() )
	{
		float angSpeedDeg = RAD2DEG( vCross / dist );
		float motionDeg = p.Get( TFBOT_PROFILE_AIM_MOTION_PENALTY ) * tf_bot_aim_motion_scale.GetFloat() * angSpeedDeg;
		errRight -= motionDeg * vRight / vCross;
		errUp -= motionDeg * vUp / vCross;
	}

	// Flick overshoot settling.
	float settle = MAX( tf_bot_aim_overshoot_settle.GetFloat(), 0.01f );
	float over = st->m_overshootDeg * expf( -t / settle );
	errRight += over * DotProduct( st->m_overshootDir, right );
	errUp += over * DotProduct( st->m_overshootDir, up );

	Vector aim = ideal + right * ( tanf( DEG2RAD( errRight ) ) * dist ) + up * ( tanf( DEG2RAD( errUp ) ) * dist );

	if ( tf_bot_aim_debug.GetBool() )
	{
		NDebugOverlay::Cross3D( ideal, 4.0f, 0, 255, 0, true, 0.1f );
		NDebugOverlay::Cross3D( aim, 4.0f, 255, 0, 0, true, 0.1f );
	}

	return aim;
}


//-----------------------------------------------------------------------------
// Hold a likely enemy approach at head height, like a person would.
//-----------------------------------------------------------------------------
bool TFBotAim::UpdatePreAim( CTFBot *me, bool lookForEnemies )
{
	if ( !IsActive( me ) || !tf_bot_aim_preaim.GetBool() )
		return false;

	TFBotAimState_t *st = StateFor( me );
	if ( !st )
		return false;
	if ( st->m_preAimTimer.HasStarted() && !st->m_preAimTimer.IsElapsed() )
		return true;

	CTFNavArea *myArea = me->GetLastKnownArea();
	if ( !myArea )
		return true;

	int team = me->GetTeamNumber();
	if ( !lookForEnemies )
		team = GetEnemyTeam( team );

	const CUtlVector< CTFNavArea * > &areas = myArea->GetEnemyInvasionAreaVector( team );
	if ( areas.Count() == 0 )
		return true;

	const float minGazeRange = me->m_Shared.InCond( TF_COND_ZOOMED ) ? 750.0f : 150.0f;
	const float awareness = TFBotProfiles::GetEffective( me ).Get( TFBOT_PROFILE_AWARENESS );

	// Prefer the nearest visible approach: that's where an enemy shows up first.
	Vector best = vec3_origin;
	float bestScore = -1.0f;
	for ( int tries = 0; tries < 12; ++tries )
	{
		Vector spot = areas[ RandomInt( 0, areas.Count() - 1 ) ]->GetRandomPoint() + Vector( 0, 0, HumanEyeHeight );
		float range = me->GetRangeTo( spot );
		if ( range < minGazeRange || !me->GetVisionInterface()->IsLineOfSightClear( spot ) )
			continue;
		float score = 1000.0f / ( range + 100.0f ) + RandomFloat( 0.0f, 0.5f );
		if ( score > bestScore )
		{
			bestScore = score;
			best = spot;
		}
	}

	// Hold the angle; aware players check more angles, so they hold shorter.
	float hold = Lerp( awareness, 2.0f, 1.0f ) * RandomFloat( 0.8f, 1.2f ) * MAX( tf_bot_aim_preaim_hold_scale.GetFloat(), 0.05f );
	if ( bestScore > 0.0f )
	{
		me->GetBodyInterface()->AimHeadTowards( best, IBody::INTERESTING, hold, NULL, "Pre-aiming the likely enemy approach" );
		TFBotFeatures::CountEvent( "aim_preaim_hold" );
	}
	st->m_preAimTimer.Start( bestScore > 0.0f ? hold : 0.5f );
	return true;
}
