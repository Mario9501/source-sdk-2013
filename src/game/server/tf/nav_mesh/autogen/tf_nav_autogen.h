//========= TFBot overhaul: nav mesh pipeline (M5) ============================//
//
// Bots need a .nav, and only some official maps ship one. This module:
//   - seeds generation from every team spawn, control point, flag and
//     payload path node (stock generation only uses info_player_start, which
//     most TF maps lack), via CTFNavMesh::AddWalkableSeeds,
//   - generates a nav when a map loads without one (tf_bot_nav_autogen 1):
//     notice to players, bots kicked, generate + analyse, the engine saves
//     maps/<map>.nav to the mod folder and reloads the map,
//   - never shadows an existing nav: generation only starts when the loader
//     found none (mod folder, VPKs, or embedded in the BSP), unless forced,
//   - validates the loaded mesh: every objective reachable from each team's
//     spawns, and how much of the mesh is cut off from all spawns,
//   - with tf_bot_nav_autogen_quit 1 quits once a generated nav is validated
//     (batch generation, tools/tfbot/nav_batch.py).
// Doors and the payload cart need no special handling: generation traces
// with a brush-only mask and treats doors as passable.
//
// See docs/tfbot/DESIGN.md M5.
//
//=============================================================================//
#ifndef TF_NAV_AUTOGEN_H
#define TF_NAV_AUTOGEN_H
#ifdef _WIN32
#pragma once
#endif

struct TFNavStatus_t
{
	bool m_loaded;
	bool m_generated;			// the loaded nav is one we generated (mod folder)
	bool m_validated;
	bool m_verified;			// all objectives reachable from both teams' spawns, little cut off
	int m_areaCount;
	int m_unreachableAreas;
	int m_missingRoutes;		// (team spawn group -> objective) pairs with no path
};

namespace TFNavAutogen
{
	const TFNavStatus_t &GetStatus();
}

#endif // TF_NAV_AUTOGEN_H
