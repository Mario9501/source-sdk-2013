//========= TFBot overhaul: nav mesh pipeline (M5) ============================//
//
// See tf_nav_autogen.h and docs/tfbot/DESIGN.md M5.
//
//=============================================================================//
#include "cbase.h"
#include "tf_nav_autogen.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "nav_mesh/tf_nav_area.h"
#include "nav_pathfind.h"
#include "team_train_watcher.h"
#include "tf_gamerules.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar tf_bot_nav_autogen( "tf_bot_nav_autogen", "1", FCVAR_NONE,
	"Generate a nav mesh when a map loads without one (bots need it). The map reloads once when done." );
ConVar tf_bot_nav_autogen_force( "tf_bot_nav_autogen_force", "0", FCVAR_NONE,
	"Generate a nav even if the map ships one (for comparing a generated nav with Valve's). Writes maps/<map>.nav to the mod folder, which then shadows the shipped one." );
ConVar tf_bot_nav_autogen_quit( "tf_bot_nav_autogen_quit", "0", FCVAR_NONE,
	"Batch mode: quit once a generated nav has loaded and been validated." );

static TFNavStatus_t s_status;

const TFNavStatus_t &TFNavAutogen::GetStatus()
{
	return s_status;
}


//-----------------------------------------------------------------------------
// Seeds: stock generation only walks from info_player_start.
//-----------------------------------------------------------------------------
static void AddGroundSeed( const Vector &origin )
{
	Vector pos = origin;
	pos.x = TheNavMesh->SnapToGrid( pos.x );
	pos.y = TheNavMesh->SnapToGrid( pos.y );

	trace_t tr;
	UTIL_TraceLine( pos + Vector( 0, 0, 32 ), pos - Vector( 0, 0, 300 ), MASK_PLAYERSOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.z > 0.7f )
		TheNavMesh->AddWalkableSeed( tr.endpos, tr.plane.normal );
}

void CTFNavMesh::AddWalkableSeeds( void )
{
	CNavMesh::AddWalkableSeeds();

	static const char *seedClasses[] =
	{
		"info_player_teamspawn",
		"team_control_point",
		"item_teamflag",
		"path_track",
	};

	int count = 0;
	for ( int c = 0; c < ARRAYSIZE( seedClasses ); ++c )
	{
		for ( CBaseEntity *ent = gEntList.FindEntityByClassname( NULL, seedClasses[c] ); ent; ent = gEntList.FindEntityByClassname( ent, seedClasses[c] ) )
		{
			AddGroundSeed( ent->GetAbsOrigin() );
			++count;
		}
	}
	Msg( "TFBot nav: seeded generation from %d spawn/objective positions\n", count );
}


//-----------------------------------------------------------------------------
// Validation
//-----------------------------------------------------------------------------
static CNavArea *AreaAt( const Vector &pos )
{
	return TheNavMesh->GetNearestNavArea( pos, true, 300.0f, false, false );
}

static void CollectObjectives( CUtlVector< Vector > *objectives )
{
	// Payload checkpoints and the final point are control points too.
	static const char *objectiveClasses[] = { "team_control_point", "item_teamflag" };
	for ( int c = 0; c < ARRAYSIZE( objectiveClasses ); ++c )
	{
		for ( CBaseEntity *ent = gEntList.FindEntityByClassname( NULL, objectiveClasses[c] ); ent; ent = gEntList.FindEntityByClassname( ent, objectiveClasses[c] ) )
			objectives->AddToTail( ent->GetAbsOrigin() );
	}

	// The payload cart: only trains a train watcher drives (maps like cp_well
	// have decorative trains that nobody can or should reach).
	for ( CBaseEntity *ent = gEntList.FindEntityByClassname( NULL, "team_train_watcher" ); ent; ent = gEntList.FindEntityByClassname( ent, "team_train_watcher" ) )
	{
		CBaseEntity *train = static_cast< CTeamTrainWatcher * >( ent )->GetTrainEntity();
		if ( train )
			objectives->AddToTail( train->GetAbsOrigin() );
	}
}

static void Validate()
{
	s_status.m_validated = true;
	s_status.m_areaCount = TheNavAreas.Count();
	s_status.m_unreachableAreas = 0;
	s_status.m_missingRoutes = 0;

	CUtlVector< Vector > objectives;
	CollectObjectives( &objectives );

	// spawn areas per team
	CUtlVector< CNavArea * > spawnAreas[ 2 ];
	for ( CBaseEntity *spawn = gEntList.FindEntityByClassname( NULL, "info_player_teamspawn" ); spawn; spawn = gEntList.FindEntityByClassname( spawn, "info_player_teamspawn" ) )
	{
		int team = spawn->GetTeamNumber();
		if ( team != TF_TEAM_RED && team != TF_TEAM_BLUE )
			continue;
		CNavArea *area = AreaAt( spawn->GetAbsOrigin() );
		if ( area && spawnAreas[ team - TF_TEAM_RED ].Find( area ) < 0 )
			spawnAreas[ team - TF_TEAM_RED ].AddToTail( area );
	}

	// each team must reach each objective from at least one spawn
	for ( int t = 0; t < 2; ++t )
	{
		if ( spawnAreas[t].Count() == 0 )
		{
			++s_status.m_missingRoutes;
			continue;
		}
		FOR_EACH_VEC( objectives, o )
		{
			CNavArea *goal = AreaAt( objectives[o] );
			bool reached = false;
			for ( int s = 0; s < spawnAreas[t].Count() && !reached && goal; ++s )
			{
				ShortestPathCost cost;
				reached = NavAreaBuildPath( spawnAreas[t][s], goal, &objectives[o], cost );
			}
			if ( !reached )
				++s_status.m_missingRoutes;
		}
	}

	// how much of the mesh no spawn can reach (flood fill over connections)
	CUtlVector< CNavArea * > open;
	CNavArea::MakeNewMarker();
	for ( int t = 0; t < 2; ++t )
	{
		FOR_EACH_VEC( spawnAreas[t], s )
		{
			if ( !spawnAreas[t][s]->IsMarked() )
			{
				spawnAreas[t][s]->Mark();
				open.AddToTail( spawnAreas[t][s] );
			}
		}
	}
	int reachable = 0;
	while ( open.Count() > 0 )
	{
		CNavArea *area = open.Tail();
		open.RemoveMultipleFromTail( 1 );
		++reachable;
		for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
		{
			for ( int i = 0; i < area->GetAdjacentCount( (NavDirType)dir ); ++i )
			{
				CNavArea *adj = area->GetAdjacentArea( (NavDirType)dir, i );
				if ( adj && !adj->IsMarked() )
				{
					adj->Mark();
					open.AddToTail( adj );
				}
			}
		}
	}
	s_status.m_unreachableAreas = s_status.m_areaCount - reachable;

	float cutOff = s_status.m_areaCount ? (float)s_status.m_unreachableAreas / s_status.m_areaCount : 1.0f;
	// Areas no spawn reaches (rooftops, ledges only a jump reaches) are harmless as long as
	// every objective is reachable; a large share still hints at missing connections.
	s_status.m_verified = ( s_status.m_missingRoutes == 0 ) && ( cutOff < 0.25f );

	Msg( "TFBot nav: %s %s nav, %d areas, %d (%.0f%%) cut off from spawns, %d missing spawn-to-objective routes: %s\n",
		STRING( gpGlobals->mapname ), s_status.m_generated ? "generated" : "shipped",
		s_status.m_areaCount, s_status.m_unreachableAreas, 100.0f * cutOff, s_status.m_missingRoutes,
		s_status.m_verified ? "VERIFIED" : "UNVERIFIED" );
	if ( !s_status.m_verified )
		Warning( "TFBot nav: bots may get stuck or fail to reach objectives on this map\n" );
}


//-----------------------------------------------------------------------------
class CTFNavAutogenSystem : public CAutoGameSystemPerFrame
{
public:
	CTFNavAutogenSystem() : CAutoGameSystemPerFrame( "CTFNavAutogenSystem" ), m_checked( false ) {}

	virtual void LevelInitPostEntity() OVERRIDE
	{
		m_checked = false;
		V_memset( &s_status, 0, sizeof( s_status ) );
	}

	virtual void FrameUpdatePostEntityThink() OVERRIDE
	{
		if ( m_checked || TheNavMesh->IsGenerating() )
			return;
		m_checked = true;

		char navPath[ MAX_PATH ];
		V_snprintf( navPath, sizeof( navPath ), "maps/%s.nav", STRING( gpGlobals->mapname ) );
		s_status.m_loaded = TheNavMesh->IsLoaded();
		s_status.m_generated = filesystem->FileExists( navPath, "MOD_WRITE" );

		bool wantGenerate = tf_bot_nav_autogen.GetBool() &&
							( !s_status.m_loaded || ( tf_bot_nav_autogen_force.GetBool() && !s_status.m_generated ) );
		if ( wantGenerate )
		{
			Msg( "TFBot nav: %s has no nav mesh%s - generating one (the map reloads when done)\n",
				STRING( gpGlobals->mapname ), s_status.m_loaded ? " of our own" : "" );
			UTIL_ClientPrintAll( HUD_PRINTTALK, "Generating a navigation mesh for the bots on this map. The map will reload when it's done." );
			engine->ServerCommand( "tf_bot_kick all\n" );
			TheNavMesh->BeginGeneration();
			return;
		}

		if ( s_status.m_loaded )
			Validate();

		if ( tf_bot_nav_autogen_quit.GetBool() && s_status.m_loaded && s_status.m_generated )
		{
			Msg( "TFBot nav: batch mode, done with %s\n", STRING( gpGlobals->mapname ) );
			engine->ServerCommand( "quit\n" );
		}
	}

private:
	bool m_checked;
};

static CTFNavAutogenSystem s_tfNavAutogenSystem;


CON_COMMAND( tf_bot_nav_validate, "Check the loaded nav mesh: objectives reachable from spawns, areas cut off" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	if ( !TheNavMesh->IsLoaded() )
	{
		Msg( "No nav mesh loaded\n" );
		return;
	}
	Validate();
}
