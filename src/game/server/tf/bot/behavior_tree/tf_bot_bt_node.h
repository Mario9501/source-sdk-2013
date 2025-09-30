//========= Copyright TF2 Bot Overhaul Project, All rights reserved. ============//
// tf_bot_bt_node.h
// Behavior Tree Node Base Classes
// Created: 2025-09-29

#ifndef TF_BOT_BT_NODE_H
#define TF_BOT_BT_NODE_H

#ifdef _WIN32
#pragma once
#endif

class CTFBot;

//----------------------------------------------------------------------------
// Node types for behavior tree
//----------------------------------------------------------------------------
enum EBTNodeType
{
	BT_NODE_DECISION = 0,	// Decision/selector nodes that evaluate conditions
	BT_NODE_ACTION,			// Action/leaf nodes that execute behaviors
	BT_NODE_COMPOSITE,		// Composite nodes that contain child nodes
};

//----------------------------------------------------------------------------
// Base class for all behavior tree nodes
//----------------------------------------------------------------------------
class BehaviorTreeNode
{
public:
	BehaviorTreeNode( const char *pszNodeName, EBTNodeType nodeType );
	virtual ~BehaviorTreeNode();

	// Core evaluation method - returns weight indicating priority of this action
	// Higher weight = higher priority
	virtual float CalculateWeight( CTFBot *pBot ) = 0;

	// Execute the node's action (for action nodes)
	// Returns true if action was executed successfully
	virtual bool Execute( CTFBot *pBot ) { return true; }

	// Debug information
	const char *GetNodeName() const { return m_pszNodeName; }
	EBTNodeType GetNodeType() const { return m_nodeType; }

	// Tree structure
	void SetParent( BehaviorTreeNode *pParent ) { m_pParentNode = pParent; }
	BehaviorTreeNode *GetParent() const { return m_pParentNode; }

protected:
	const char *m_pszNodeName;
	EBTNodeType m_nodeType;
	BehaviorTreeNode *m_pParentNode;
};

//----------------------------------------------------------------------------
// Action node - represents a concrete action the bot can take
//----------------------------------------------------------------------------
class BTActionNode : public BehaviorTreeNode
{
public:
	BTActionNode( const char *pszNodeName );
	virtual ~BTActionNode();
};

//----------------------------------------------------------------------------
// Decision node - evaluates conditions and returns weight
//----------------------------------------------------------------------------
class BTDecisionNode : public BehaviorTreeNode
{
public:
	BTDecisionNode( const char *pszNodeName );
	virtual ~BTDecisionNode();
};

//----------------------------------------------------------------------------
// Composite node - contains multiple child nodes
//----------------------------------------------------------------------------
class BTCompositeNode : public BehaviorTreeNode
{
public:
	BTCompositeNode( const char *pszNodeName );
	virtual ~BTCompositeNode();

	void AddChild( BehaviorTreeNode *pChild );
	int GetChildCount() const { return m_children.Count(); }
	BehaviorTreeNode *GetChild( int index ) const { return m_children[index]; }

protected:
	CUtlVector< BehaviorTreeNode * > m_children;
};

//----------------------------------------------------------------------------
// Pass-through action node - for initial integration testing
// Always returns weight 1.0, does nothing when executed
//----------------------------------------------------------------------------
class BTPassThroughActionNode : public BTActionNode
{
public:
	BTPassThroughActionNode();
	virtual ~BTPassThroughActionNode();

	virtual float CalculateWeight( CTFBot *pBot ) OVERRIDE;
	virtual bool Execute( CTFBot *pBot ) OVERRIDE;
};

#endif // TF_BOT_BT_NODE_H