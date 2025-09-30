//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_behavior_tree.cpp
// Core Behavior Tree Evaluation Engine Implementation
// Created: 2025-09-29

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_behavior_tree.h"

// ConVars for behavior tree control
ConVar tf_bot_behavior_tree_enabled( "tf_bot_behavior_tree_enabled", "0", FCVAR_NOTIFY | FCVAR_GAMEDLL,
									"Enable dynamic behavior tree AI system for bots (0=disabled, 1=enabled)" );

ConVar tf_bot_bt_debug_nodes( "tf_bot_bt_debug_nodes", "0", FCVAR_NOTIFY | FCVAR_GAMEDLL,
							  "Print behavior tree node selection to console for debugging" );

ConVar tf_bot_bt_eval_interval( "tf_bot_bt_eval_interval", "0.5", FCVAR_GAMEDLL,
							   "Behavior tree evaluation interval in seconds (default: 0.5)",
							   true, 0.1f, true, 2.0f );

//----------------------------------------------------------------------------
// CTFBotBehaviorTree - Constructor
//----------------------------------------------------------------------------
CTFBotBehaviorTree::CTFBotBehaviorTree( CTFBot *pBot )
	: m_pBot( pBot )
	, m_pRootNode( NULL )
	, m_pSelectedAction( NULL )
	, m_flEvaluationInterval( 0.5f )
	, m_bEnabled( false )
	, m_flLastEvalTime( 0.0f )
{
	// Initialize evaluation timer
	m_evaluationTimer.Start( m_flEvaluationInterval );

	// Build initial tree
	BuildTree();

	if ( tf_bot_bt_debug_nodes.GetBool() )
	{
		DevMsg( "[TF Bot BT] Behavior tree created for bot: %s\n",
				pBot ? pBot->GetPlayerName() : "NULL" );
	}
}

//----------------------------------------------------------------------------
// CTFBotBehaviorTree - Destructor
//----------------------------------------------------------------------------
CTFBotBehaviorTree::~CTFBotBehaviorTree()
{
	// Clean up tree nodes
	if ( m_pRootNode )
	{
		delete m_pRootNode;
		m_pRootNode = NULL;
	}

	m_pSelectedAction = NULL;
}

//----------------------------------------------------------------------------
// Update - Main evaluation loop (timer-throttled)
//----------------------------------------------------------------------------
void CTFBotBehaviorTree::Update( float deltaTime )
{
	// Check if behavior tree is enabled
	if ( !m_bEnabled || !tf_bot_behavior_tree_enabled.GetBool() )
	{
		return;
	}

	// Update evaluation interval from ConVar
	m_flEvaluationInterval = tf_bot_bt_eval_interval.GetFloat();

	// Only evaluate when timer expires (throttling)
	if ( !m_evaluationTimer.IsElapsed() )
	{
		return;
	}

	// Reset timer for next evaluation
	m_evaluationTimer.Start( m_flEvaluationInterval );

	// Select highest-weight action
	BehaviorTreeNode *pSelectedNode = SelectAction();

	if ( pSelectedNode )
	{
		m_pSelectedAction = pSelectedNode;

		// Execute the selected action
		pSelectedNode->Execute( m_pBot );

		// Debug logging
		if ( tf_bot_bt_debug_nodes.GetBool() )
		{
			DevMsg( "[TF Bot BT] %s: Selected action '%s' (interval: %.2fs)\n",
					m_pBot ? m_pBot->GetPlayerName() : "NULL",
					pSelectedNode->GetNodeName(),
					m_flEvaluationInterval );
		}
	}

	m_flLastEvalTime = gpGlobals->curtime;
}

//----------------------------------------------------------------------------
// BuildTree - Construct the behavior tree structure
// For Story 1.1, this creates a single pass-through node
//----------------------------------------------------------------------------
void CTFBotBehaviorTree::BuildTree()
{
	// Clean up existing tree
	if ( m_pRootNode )
	{
		delete m_pRootNode;
		m_pRootNode = NULL;
	}

	// Create a single pass-through action node
	// This proves integration without changing bot behavior
	m_pRootNode = new BTPassThroughActionNode();

	if ( tf_bot_bt_debug_nodes.GetBool() )
	{
		DevMsg( "[TF Bot BT] Built tree with pass-through node\n" );
	}
}

//----------------------------------------------------------------------------
// SelectAction - Evaluate tree and return highest-weight action
//----------------------------------------------------------------------------
BehaviorTreeNode *CTFBotBehaviorTree::SelectAction()
{
	if ( !m_pRootNode || !m_pBot )
	{
		return NULL;
	}

	// For Story 1.1, we only have one node, so just return it
	// Future stories will implement proper weight evaluation and selection

	// Calculate weight for the root node
	float flWeight = m_pRootNode->CalculateWeight( m_pBot );

	if ( flWeight > 0.0f )
	{
		return m_pRootNode;
	}

	return NULL;
}