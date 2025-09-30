//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_node.cpp
// Behavior Tree Node Base Classes Implementation
// Created: 2025-09-29

#include "cbase.h"
#include "../tf_bot.h"
#include "tf_bot_bt_node.h"

// ConVar for debug logging
extern ConVar tf_bot_bt_debug_nodes;

//----------------------------------------------------------------------------
// BehaviorTreeNode - Base class implementation
//----------------------------------------------------------------------------
BehaviorTreeNode::BehaviorTreeNode( const char *pszNodeName, EBTNodeType nodeType )
	: m_pszNodeName( pszNodeName )
	, m_nodeType( nodeType )
	, m_pParentNode( NULL )
{
}

BehaviorTreeNode::~BehaviorTreeNode()
{
}

//----------------------------------------------------------------------------
// BTActionNode - Action node implementation
//----------------------------------------------------------------------------
BTActionNode::BTActionNode( const char *pszNodeName )
	: BehaviorTreeNode( pszNodeName, BT_NODE_ACTION )
{
}

BTActionNode::~BTActionNode()
{
}

//----------------------------------------------------------------------------
// BTDecisionNode - Decision node implementation
//----------------------------------------------------------------------------
BTDecisionNode::BTDecisionNode( const char *pszNodeName )
	: BehaviorTreeNode( pszNodeName, BT_NODE_DECISION )
{
}

BTDecisionNode::~BTDecisionNode()
{
}

//----------------------------------------------------------------------------
// BTCompositeNode - Composite node implementation
//----------------------------------------------------------------------------
BTCompositeNode::BTCompositeNode( const char *pszNodeName )
	: BehaviorTreeNode( pszNodeName, BT_NODE_COMPOSITE )
{
}

BTCompositeNode::~BTCompositeNode()
{
	// Clean up child nodes
	for ( int i = 0; i < m_children.Count(); ++i )
	{
		delete m_children[i];
	}
	m_children.RemoveAll();
}

void BTCompositeNode::AddChild( BehaviorTreeNode *pChild )
{
	if ( pChild )
	{
		pChild->SetParent( this );
		m_children.AddToTail( pChild );
	}
}

//----------------------------------------------------------------------------
// BTPassThroughActionNode - Pass-through node for initial integration
//----------------------------------------------------------------------------
BTPassThroughActionNode::BTPassThroughActionNode()
	: BTActionNode( "PassThrough" )
{
}

BTPassThroughActionNode::~BTPassThroughActionNode()
{
}

float BTPassThroughActionNode::CalculateWeight( CTFBot *pBot )
{
	// Always return weight of 1.0 (will be selected as the only node)
	return 1.0f;
}

bool BTPassThroughActionNode::Execute( CTFBot *pBot )
{
	// Pass-through node does nothing - existing behavior system handles actions
	if ( tf_bot_bt_debug_nodes.GetBool() )
	{
		DevMsg( "[TF Bot BT] %s: PassThrough node selected (delegating to legacy behaviors)\n",
				pBot ? pBot->GetPlayerName() : "NULL" );
	}

	return true;
}