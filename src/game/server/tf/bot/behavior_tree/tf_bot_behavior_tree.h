//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_behavior_tree.h
// Core Behavior Tree Evaluation Engine
// Created: 2025-09-29

#ifndef TF_BOT_BEHAVIOR_TREE_H
#define TF_BOT_BEHAVIOR_TREE_H

#ifdef _WIN32
#pragma once
#endif

#include "tf_bot_bt_node.h"
#include "utlvector.h"
#include "tier0/memdbgon.h"

class CTFBot;

//----------------------------------------------------------------------------
// Core behavior tree evaluation engine
// Integrates with CTFBot to provide dynamic decision-making
//----------------------------------------------------------------------------
class CTFBotBehaviorTree
{
public:
	CTFBotBehaviorTree( CTFBot *pBot );
	~CTFBotBehaviorTree();

	// Main update loop - called from CTFBot::PhysicsSimulate()
	void Update( float deltaTime );

	// Enable/disable behavior tree (controlled by ConVar)
	void SetEnabled( bool bEnabled ) { m_bEnabled = bEnabled; }
	bool IsEnabled() const { return m_bEnabled; }

	// Build the behavior tree structure
	void BuildTree();

	// Select the highest-weight action from the tree
	BehaviorTreeNode *SelectAction();

	// Get the root node
	BehaviorTreeNode *GetRootNode() const { return m_pRootNode; }

private:
	CTFBot *m_pBot;						// Owner bot
	BehaviorTreeNode *m_pRootNode;		// Root of the behavior tree
	BehaviorTreeNode *m_pSelectedAction;// Currently selected action
	CountdownTimer m_evaluationTimer;	// Timer for throttled evaluation
	float m_flEvaluationInterval;		// Evaluation interval in seconds
	bool m_bEnabled;					// Is behavior tree enabled?
	float m_flLastEvalTime;				// Last evaluation time (for debug)
};

#endif // TF_BOT_BEHAVIOR_TREE_H