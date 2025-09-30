//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_navigation.h
// Navigation decision node for behavior tree
// Created: 2025-09-29

#ifndef TF_BOT_BT_NAVIGATION_H
#define TF_BOT_BT_NAVIGATION_H

#ifdef _WIN32
#pragma once
#endif

#include "tf_bot_bt_node.h"
#include "../tf_bot_path_selector.h"

//----------------------------------------------------------------------------
// Navigation decision node - uses CTFBotPathSelector for dynamic paths
//----------------------------------------------------------------------------
class BTNavigationNode : public BTDecisionNode
{
public:
	BTNavigationNode();
	virtual ~BTNavigationNode();

	virtual float CalculateWeight( CTFBot *pBot ) OVERRIDE;
	virtual bool Execute( CTFBot *pBot ) OVERRIDE;

private:
	// Path selector instance (one per node for now, could be shared)
	CTFBotPathSelector *m_pPathSelector;

	// Current path state for waypoint following
	PathInfo_t m_currentPath;
	int m_iCurrentWaypoint;
	CountdownTimer m_goalTimer;			// Timer to pick new random goal
	Vector m_goalPosition;				// Current goal position
};

#endif // TF_BOT_BT_NAVIGATION_H