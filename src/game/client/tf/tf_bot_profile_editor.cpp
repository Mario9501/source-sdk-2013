//========= TFBot overhaul: in-game bot profile editor (M1b) ==================//
//
// tf_bot_profile_editor opens a panel listing the bot profiles (stock and
// custom files) with a slider or choice per profile key. The controls are
// built from the shared schema (tf_bot_profile_schema.h), so keys added by
// later milestones appear here without GUI changes.
//
// Edits are sent as tf_bot_profile_set commands, which the server applies to
// live bots immediately; Save runs tf_bot_profile_save. The console commands
// are the editor's whole back end. Works on a listen server you host.
//
// The command toggles the editor (bind it to a key); Escape or Close also shut
// it. It is a popup, so while it is open the mouse drives a cursor instead of
// your view: opening it while aiming at a bot selects that bot's profile.
//
// See docs/tfbot/DESIGN.md M1b.
//
//=============================================================================//
#include "cbase.h"
#include <vgui_controls/Frame.h>
#include <vgui_controls/ListPanel.h>
#include <vgui_controls/Slider.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/TextEntry.h>
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include "ienginevgui.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "c_tf_player.h"
#include "tf_bot_profile_schema.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static const int kSliderSteps = 1000;

class CTFBotProfileEditor : public Frame
{
	DECLARE_CLASS_SIMPLE( CTFBotProfileEditor, Frame );

public:
	CTFBotProfileEditor( VPANEL parent );
	virtual ~CTFBotProfileEditor();

	virtual void PerformLayout() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodeTyped( KeyCode code ) OVERRIDE;

	void Refresh();
	bool SelectPlayerUnderCrosshair();

private:
	MESSAGE_FUNC( OnItemSelected, "ItemSelected" );
	MESSAGE_FUNC_PTR( OnSliderMoved, "SliderMoved", panel );
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

	void LoadFiles();
	KeyValues *FindEntry( const char *name );
	const char *ResolveValue( const char *name, const char *key, int depth );
	void SelectProfile( const char *name );
	void SendSet( const char *key, const char *value );
	float SliderToValue( int key, int pos ) const;
	int ValueToSlider( int key, float value ) const;

	KeyValues *m_stock;
	KeyValues *m_custom;
	char m_selected[ 64 ];
	bool m_updating;			// suppress change messages while filling controls

	ListPanel *m_list;
	Label *m_title;
	TextEntry *m_newName;
	ComboBox *m_base;
	Label *m_keyLabel[ TFBOT_PROFILE_KEY_COUNT ];
	Slider *m_slider[ TFBOT_PROFILE_KEY_COUNT ];
	ComboBox *m_choice[ TFBOT_PROFILE_KEY_COUNT ];
	Label *m_valueLabel[ TFBOT_PROFILE_KEY_COUNT ];
	Button *m_btnLookAt;
	Button *m_btnNew;
	Button *m_btnSpawnRed;
	Button *m_btnSpawnBlue;
	Button *m_btnSave;
	Button *m_btnReload;
	Button *m_btnClose;
};

static CTFBotProfileEditor *s_editor = NULL;


//-----------------------------------------------------------------------------
CTFBotProfileEditor::CTFBotProfileEditor( VPANEL parent ) : Frame( NULL, "TFBotProfileEditor" )
{
	SetParent( parent );
	SetScheme( "ClientScheme" );
	SetProportional( false );
	SetTitle( "Bot profile editor", true );
	SetSizeable( false );
	SetMoveable( true );
	SetDeleteSelfOnClose( false );
	SetSize( 760, 600 );
	MoveToCenterOfScreen();

	// Only popups get a mouse cursor and keyboard focus in game. Without this the
	// panel stays on screen with nothing to click it closed once the console shuts.
	MakePopup();
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );

	m_stock = NULL;
	m_custom = NULL;
	m_selected[0] = '\0';
	m_updating = false;

	m_list = new ListPanel( this, "ProfileList" );
	m_list->AddColumnHeader( 0, "name", "Profile", 200, ListPanel::COLUMN_RESIZEWITHWINDOW );
	m_list->SetMultiselectEnabled( false );

	m_title = new Label( this, "Title", "Select a profile" );
	m_newName = new TextEntry( this, "NewName" );
	m_base = new ComboBox( this, "Base", 12, false );

	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
		m_keyLabel[k] = new Label( this, NULL, info.m_name );
		m_keyLabel[k]->SetTooltip( NULL, CFmtStr( "%s (%s, %s)", info.m_description, info.m_group == TFBOT_PROFILE_GROUP_SKILL ? "skill" : "style", info.m_milestone ) );
		m_valueLabel[k] = new Label( this, NULL, "" );
		m_slider[k] = NULL;
		m_choice[k] = NULL;

		if ( info.m_enumNames )
		{
			m_choice[k] = new ComboBox( this, NULL, 4, false );
			CUtlStringList choices;
			V_SplitString( info.m_enumNames, ",", choices );
			FOR_EACH_VEC( choices, c )
				m_choice[k]->AddItem( choices[c], NULL );
		}
		else
		{
			m_slider[k] = new Slider( this, NULL );
			m_slider[k]->SetRange( 0, kSliderSteps );
			m_slider[k]->AddActionSignalTarget( this );
		}
	}

	m_btnLookAt = new Button( this, "LookAt", "Edit the bot I'm looking at", this, "lookat" );
	m_btnNew = new Button( this, "New", "New profile", this, "new" );
	m_btnSpawnRed = new Button( this, "SpawnRed", "Test bot (RED)", this, "spawn_red" );
	m_btnSpawnBlue = new Button( this, "SpawnBlue", "Test bot (BLU)", this, "spawn_blue" );
	m_btnSave = new Button( this, "Save", "Save", this, "save" );
	m_btnReload = new Button( this, "Reload", "Reload", this, "reload" );
	// The title bar's X is a Marlett glyph that ClientScheme may not draw, so
	// closing gets a plain button too ("Close" is handled by Frame).
	m_btnClose = new Button( this, "CloseEditor", "Close", this, "Close" );

	Refresh();
}

CTFBotProfileEditor::~CTFBotProfileEditor()
{
	if ( m_stock )
		m_stock->deleteThis();
	if ( m_custom )
		m_custom->deleteThis();
	s_editor = NULL;
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::PerformLayout()
{
	BaseClass::PerformLayout();

	const int margin = 10;
	const int top = 30;
	const int listWide = 220;
	int wide, tall;
	GetSize( wide, tall );

	m_list->SetBounds( margin, top, listWide, tall - top - 80 );
	m_btnLookAt->SetBounds( margin, tall - 70, listWide, 24 );
	m_btnNew->SetBounds( margin + 120, tall - 40, listWide - 120, 24 );
	m_newName->SetBounds( margin, tall - 40, 115, 24 );

	int x = margin * 2 + listWide;
	int y = top;
	int labelWide = 150;
	int controlWide = wide - x - labelWide - 70 - margin;

	m_title->SetBounds( x, y, wide - x - margin, 22 );
	y += 26;
	m_base->SetBounds( x + labelWide, y, controlWide, 22 );
	y += 30;

	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		m_keyLabel[k]->SetBounds( x, y, labelWide, 22 );
		if ( m_slider[k] )
			m_slider[k]->SetBounds( x + labelWide, y, controlWide, 22 );
		if ( m_choice[k] )
			m_choice[k]->SetBounds( x + labelWide, y, controlWide, 22 );
		m_valueLabel[k]->SetBounds( x + labelWide + controlWide + 6, y, 64, 22 );
		y += 28;
	}

	int bx = x;
	int by = tall - 40;
	m_btnSpawnRed->SetBounds( bx, by, 120, 24 );
	m_btnSpawnBlue->SetBounds( bx + 126, by, 120, 24 );
	m_btnReload->SetBounds( wide - margin - 246, by, 78, 24 );
	m_btnSave->SetBounds( wide - margin - 162, by, 78, 24 );
	m_btnClose->SetBounds( wide - margin - 78, by, 78, 24 );
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::LoadFiles()
{
	if ( m_stock )
		m_stock->deleteThis();
	if ( m_custom )
		m_custom->deleteThis();

	m_stock = new KeyValues( "TFBotProfiles" );
	m_stock->LoadFromFile( g_pFullFileSystem, TFBOT_PROFILES_STOCK_FILE, "MOD" );
	m_custom = new KeyValues( "TFBotProfiles" );
	m_custom->LoadFromFile( g_pFullFileSystem, TFBOT_PROFILES_CUSTOM_FILE, "MOD" );
}

KeyValues *CTFBotProfileEditor::FindEntry( const char *name )
{
	KeyValues *entry = m_custom ? m_custom->FindKey( name ) : NULL;
	if ( !entry && m_stock )
		entry = m_stock->FindKey( name );
	return entry;
}

// Value of 'key' for profile 'name': its own value, else its presets', else
// its base's. NULL if no file sets it.
const char *CTFBotProfileEditor::ResolveValue( const char *name, const char *key, int depth )
{
	KeyValues *entry = FindEntry( name );
	if ( !entry || depth > 8 )
		return NULL;

	const char *own = entry->GetString( key, NULL );
	if ( own && own[0] )
		return own;

	CUtlStringList presets;
	V_SplitString( entry->GetString( "presets", "" ), ",", presets );
	FOR_EACH_VEC_BACK( presets, i )
	{
		Q_StripPrecedingAndTrailingWhitespace( presets[i] );
		const char *v = ResolveValue( presets[i], key, depth + 1 );
		if ( v )
			return v;
	}

	const char *base = entry->GetString( "base", "" );
	return base[0] ? ResolveValue( base, key, depth + 1 ) : NULL;
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::Refresh()
{
	LoadFiles();

	m_list->DeleteAllItems();
	m_base->RemoveAll();

	CUtlVector< CUtlString > names;
	KeyValues *files[2] = { m_stock, m_custom };
	for ( int f = 0; f < 2; ++f )
	{
		for ( KeyValues *e = files[f] ? files[f]->GetFirstTrueSubKey() : NULL; e; e = e->GetNextTrueSubKey() )
		{
			bool dup = false;
			FOR_EACH_VEC( names, n )
				dup = dup || !V_stricmp( names[n].Get(), e->GetName() );
			if ( !dup )
				names.AddToTail( e->GetName() );
		}
	}

	FOR_EACH_VEC( names, n )
	{
		KeyValues *row = new KeyValues( "row" );
		row->SetString( "name", names[n].Get() );
		m_list->AddItem( row, 0, false, false );
		row->deleteThis();

		if ( V_strnicmp( names[n].Get(), "preset_", 7 ) )
			m_base->AddItem( names[n].Get(), NULL );
	}

	if ( m_selected[0] )
		SelectProfile( m_selected );
}


//-----------------------------------------------------------------------------
float CTFBotProfileEditor::SliderToValue( int key, int pos ) const
{
	const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( key );
	return info.m_min + ( info.m_max - info.m_min ) * pos / (float)kSliderSteps;
}

int CTFBotProfileEditor::ValueToSlider( int key, float value ) const
{
	const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( key );
	float t = ( info.m_max > info.m_min ) ? ( value - info.m_min ) / ( info.m_max - info.m_min ) : 0.0f;
	return (int)( clamp( t, 0.0f, 1.0f ) * kSliderSteps + 0.5f );
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::SelectProfile( const char *name )
{
	V_strncpy( m_selected, name, sizeof( m_selected ) );
	m_updating = true;

	KeyValues *entry = FindEntry( name );
	m_title->SetText( CFmtStr( "%s%s", name, entry ? "" : "   (no file entry yet: changes create one)" ) );

	const char *base = entry ? entry->GetString( "base", "" ) : "";
	m_base->SetText( base[0] ? base : g_szTFBotProfileTierNames[1] );

	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		const TFBotProfileKeyInfo_t &info = TFBotProfileKeyInfo( k );
		const char *text = ResolveValue( name, info.m_name, 0 );
		if ( !text && !entry )
			text = ResolveValue( g_szTFBotProfileTierNames[1], info.m_name, 0 );

		float value = 0.5f * ( info.m_min + info.m_max );
		if ( text )
			TFBotProfileParseValue( k, text, &value );
		value = TFBotProfileClampValue( k, value );

		char formatted[ 32 ];
		TFBotProfileFormatValue( k, value, formatted, sizeof( formatted ) );
		m_valueLabel[k]->SetText( text ? formatted : CFmtStr( "(%s)", formatted ).Access() );

		if ( m_slider[k] )
			m_slider[k]->SetValue( ValueToSlider( k, value ), false );
		if ( m_choice[k] )
			m_choice[k]->ActivateItemByRow( (int)( value + 0.5f ) );
	}

	m_updating = false;
}

void CTFBotProfileEditor::SendSet( const char *key, const char *value )
{
	if ( !m_selected[0] )
		return;
	engine->ClientCmd_Unrestricted( CFmtStr( "tf_bot_profile_set \"%s\" %s \"%s\"\n", m_selected, key, value ) );
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::OnItemSelected()
{
	int itemID = m_list->GetSelectedItem( 0 );
	KeyValues *row = ( itemID >= 0 ) ? m_list->GetItem( itemID ) : NULL;
	if ( row )
		SelectProfile( row->GetString( "name" ) );
}

void CTFBotProfileEditor::OnSliderMoved( Panel *panel )
{
	if ( m_updating )
		return;
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		if ( m_slider[k] != panel )
			continue;
		char formatted[ 32 ];
		TFBotProfileFormatValue( k, SliderToValue( k, m_slider[k]->GetValue() ), formatted, sizeof( formatted ) );
		m_valueLabel[k]->SetText( formatted );
		SendSet( TFBotProfileKeyInfo( k ).m_name, formatted );
		return;
	}
}

void CTFBotProfileEditor::OnTextChanged( Panel *panel )
{
	if ( m_updating )
		return;
	if ( panel == m_base )
	{
		char base[ 64 ];
		m_base->GetText( base, sizeof( base ) );
		if ( m_selected[0] && base[0] )
			engine->ClientCmd_Unrestricted( CFmtStr( "tf_bot_profile_new \"%s\" \"%s\"\n", m_selected, base ) );
		return;
	}
	for ( int k = 0; k < TFBOT_PROFILE_KEY_COUNT; ++k )
	{
		if ( m_choice[k] != panel )
			continue;
		char text[ 32 ];
		m_choice[k]->GetText( text, sizeof( text ) );
		m_valueLabel[k]->SetText( text );
		SendSet( TFBotProfileKeyInfo( k ).m_name, text );
		return;
	}
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::OnCommand( const char *command )
{
	if ( !V_stricmp( command, "save" ) )
	{
		engine->ClientCmd_Unrestricted( "tf_bot_profile_save\n" );
	}
	else if ( !V_stricmp( command, "reload" ) )
	{
		engine->ClientCmd_Unrestricted( "tf_bot_profile_reload\n" );
		Refresh();
	}
	else if ( !V_stricmp( command, "spawn_red" ) || !V_stricmp( command, "spawn_blue" ) )
	{
		if ( m_selected[0] )
			engine->ClientCmd_Unrestricted( CFmtStr( "tf_bot_add_profile \"%s\" %s\n", m_selected, !V_stricmp( command, "spawn_red" ) ? "red" : "blue" ) );
	}
	else if ( !V_stricmp( command, "new" ) )
	{
		char name[ 64 ];
		m_newName->GetText( name, sizeof( name ) );
		if ( name[0] )
		{
			engine->ClientCmd_Unrestricted( CFmtStr( "tf_bot_profile_new \"%s\"\n", name ) );
			V_strncpy( m_selected, name, sizeof( m_selected ) );
			SelectProfile( name );
		}
	}
	else if ( !V_stricmp( command, "lookat" ) )
	{
		if ( !SelectPlayerUnderCrosshair() )
			m_title->SetText( "No player under the crosshair (aim, then reopen the editor)" );
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}


//-----------------------------------------------------------------------------
void CTFBotProfileEditor::OnKeyCodeTyped( KeyCode code )
{
	// Same as TF2's own dialogs: Escape closes the popup that has focus.
	if ( code == KEY_ESCAPE )
		Close();
	else
		BaseClass::OnKeyCodeTyped( code );
}


//-----------------------------------------------------------------------------
// Selects the profile of the player under the crosshair, if there is one.
bool CTFBotProfileEditor::SelectPlayerUnderCrosshair()
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( !local )
		return false;

	Vector forward;
	AngleVectors( local->EyeAngles(), &forward );
	trace_t tr;
	UTIL_TraceLine( local->EyePosition(), local->EyePosition() + forward * 4000.0f, MASK_SHOT, local, COLLISION_GROUP_NONE, &tr );
	C_TFPlayer *target = ToTFPlayer( tr.m_pEnt );
	if ( !target || target == local )
		return false;

	SelectProfile( target->GetPlayerName() );
	return true;
}


//-----------------------------------------------------------------------------
// Toggles the editor, so one key bound to it opens and closes it. While the
// editor is open the mouse drives the cursor, not your view, so opening it while
// aiming at a bot selects that bot's profile.
CON_COMMAND( tf_bot_profile_editor, "Open or close the in-game bot profile editor (listen server host). Opening it while aiming at a bot selects that bot." )
{
	if ( s_editor && s_editor->IsVisible() )
	{
		s_editor->Close();
		return;
	}

	if ( !s_editor )
	{
		s_editor = new CTFBotProfileEditor( enginevgui->GetPanel( PANEL_CLIENTDLL ) );
	}
	else
	{
		s_editor->Refresh();
	}
	s_editor->SelectPlayerUnderCrosshair();
	s_editor->Activate();
	s_editor->MoveToFront();
}
