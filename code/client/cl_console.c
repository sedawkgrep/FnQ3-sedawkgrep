/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// console.c

#include "client.h"

#define  DEFAULT_CONSOLE_WIDTH 78
#define  MAX_CONSOLE_WIDTH 120

#define  NUM_CON_TIMES  4

#define  CON_TEXTSIZE   65536
#define  CON_SCROLLBAR_BASE_WIDTH  3.0f
#define  CON_SCROLLBAR_HOVER_GROW  5.0f
#define  CON_SCROLLBAR_HIT_PAD     5.0f
#define  CON_SCROLLBAR_SIDE_PAD    3.0f
#define  CON_SCROLLBAR_MIN_THUMB   18.0f
#define  CON_SCROLLBAR_LERP_SPEED  12.0f
#define  CON_SELECTION_ALPHA       0.35f
#define  CON_COMPLETION_MAX_MATCHES 64
#define  CON_COMPLETION_MAX_VISIBLE 8
#define  CON_TEXT_DRAG_THRESHOLD   4.0f

typedef enum {
	CON_FOCUS_INPUT,
	CON_FOCUS_LOG
} conFocus_t;

int bigchar_width;
int bigchar_height;
int smallchar_width;
int smallchar_height;

typedef struct {
	qboolean	initialized;

	short	text[CON_TEXTSIZE];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line

	int 	linewidth;		// characters across screen
	int		totallines;		// total lines in console scrollback

	float	xadjust;		// for wide aspect screens

	float	displayFrac;	// aproaches finalFrac at con_speed
	float	finalFrac;		// 0.0 to 1.0 lines of console to display

	int		vislines;		// in scanlines
	float	displayWidth;
	float	displayLine;

	int		times[NUM_CON_TIMES];	// cls.realtime time the line was generated
								// for transparent notify lines
	vec4_t	color;

	int		viswidth;
	int		visheight;
	int		vispage;		
	int		inputSelectionAnchor;
	int		logSelectionAnchorLine;
	int		logSelectionAnchorColumn;
	int		logSelectionLine;
	int		logSelectionColumn;
	float	mouseX;
	float	mouseY;
	float	scrollbarHover;
	float	scrollbarDragOffset;
	int		completionCount;
	int		completionSelection;
	int		completionReplaceOffset;
	int		completionReplaceLength;
	int		completionSnapshotCursor;
	qboolean completionAppendSpace;
	qboolean completionPopupVisible;
	qboolean completionPrependSlash;
	qboolean completionSnapshotValid;
	qboolean textDragPending;
	qboolean textDragging;
	qboolean textDragFromInput;
	qboolean textDragTargetInput;
	int		textDragSourceStart;
	int		textDragSourceEnd;
	int		textDragDropCursor;
	int		textDragTextLength;
	float	textDragStartMouseX;
	float	textDragStartMouseY;

	conFocus_t focus;
	qboolean mouseInitialized;
	qboolean scrollbarDragging;
	qboolean inputSelecting;
	qboolean logSelecting;
	qboolean newline;
	char	completionSnapshotBuffer[MAX_EDIT_LINE];
	char	completionMatches[CON_COMPLETION_MAX_MATCHES][MAX_EDIT_LINE];
	char	textDragText[MAX_EDIT_LINE];

} console_t;

extern  qboolean    chat_team;
extern  int         chat_playerNum;

console_t	con;

cvar_t		*con_conspeed;
cvar_t		*con_autoclear;
cvar_t		*con_notifytime;
cvar_t		*con_scale;
cvar_t		*con_scaleUniform;
cvar_t		*con_screenExtents;
static cvar_t	*con_backgroundStyle;
static cvar_t	*con_backgroundColor;
static cvar_t	*con_backgroundOpacity;
static cvar_t	*con_scrollSmooth;
static cvar_t	*con_scrollSmoothSpeed;
static cvar_t	*con_completionPopup;
static cvar_t	*con_lineColor;
static cvar_t	*con_versionColor;
static cvar_t	*con_fade;
static cvar_t	*con_speedLegacy;
static cvar_t	*con_scrollLines;

int			g_console_field_width;

static void Con_Fixup( void );
static void Con_ClampMouseToConsole( void );
static void Con_ClearInputSelection( void );
static void Con_ClearLogSelection( void );
static int Con_GetScrollStep( int lines );
static void Con_InvalidateCompletionState( void );
static void Con_RefreshCompletionState( void );
static void Con_ApplySelectedCompletion( int direction );


static void Con_ParseColorString( const char *string, const vec4_t defaultColor, vec4_t outColor, qboolean allowAlpha ) {
	char buffer[MAX_CVAR_VALUE_STRING];
	char *parts[4];
	int i;
	int count;

	for ( i = 0; i < 4; i++ ) {
		outColor[ i ] = defaultColor[ i ];
	}

	if ( !string || !string[ 0 ] ) {
		return;
	}

	Q_strncpyz( buffer, string, sizeof( buffer ) );
	count = Com_Split( buffer, parts, allowAlpha ? 4 : 3, ' ' );
	if ( count < 3 ) {
		return;
	}

	for ( i = 0; i < 3; i++ ) {
		float value = Q_atof( parts[ i ] ) / 255.0f;
		if ( value < 0.0f ) {
			value = 0.0f;
		} else if ( value > 1.0f ) {
			value = 1.0f;
		}
		outColor[ i ] = value;
	}

	if ( allowAlpha && count >= 4 ) {
		float value = Q_atof( parts[ 3 ] ) / 255.0f;
		if ( value < 0.0f ) {
			value = 0.0f;
		} else if ( value > 1.0f ) {
			value = 1.0f;
		}
		outColor[ 3 ] = value;
	}
}


static void Con_GetColorCvar( const cvar_t *cvar, const vec4_t defaultColor, vec4_t outColor, qboolean allowAlpha ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		outColor[ i ] = defaultColor[ i ];
	}

	if ( cvar && cvar->string[ 0 ] ) {
		Con_ParseColorString( cvar->string, defaultColor, outColor, allowAlpha );
	}
}


static void Con_GetBackgroundColor( vec4_t outColor ) {
	vec4_t defaultColor;
	float opacity;

	if ( con_backgroundStyle && !con_backgroundStyle->integer ) {
		defaultColor[ 0 ] = 1.0f;
		defaultColor[ 1 ] = 1.0f;
		defaultColor[ 2 ] = 1.0f;
		defaultColor[ 3 ] = 1.0f;
	} else {
		defaultColor[ 0 ] = 0.0f;
		defaultColor[ 1 ] = 0.0f;
		defaultColor[ 2 ] = 0.0f;
		defaultColor[ 3 ] = 1.0f;
	}

	Con_GetColorCvar( NULL, defaultColor, outColor, qfalse );

	if ( cl_conColor && cl_conColor->string[ 0 ] ) {
		Con_ParseColorString( cl_conColor->string, outColor, outColor, qtrue );
	}

	if ( con_backgroundColor && con_backgroundColor->string[ 0 ] ) {
		float alpha = outColor[ 3 ];
		Con_ParseColorString( con_backgroundColor->string, outColor, outColor, qfalse );
		outColor[ 3 ] = alpha;
	}

	opacity = con_backgroundOpacity ? con_backgroundOpacity->value : 1.0f;
	if ( opacity < 0.0f ) {
		opacity = 0.0f;
	} else if ( opacity > 1.0f ) {
		opacity = 1.0f;
	}

	outColor[ 3 ] *= opacity;
}


static void Con_SetScaledColor( const vec4_t color, float alphaScale ) {
	vec4_t scaledColor;

	scaledColor[ 0 ] = color[ 0 ];
	scaledColor[ 1 ] = color[ 1 ];
	scaledColor[ 2 ] = color[ 2 ];
	scaledColor[ 3 ] = color[ 3 ] * alphaScale;
	re.SetColor( scaledColor );
}


static void Con_DrawSolidRect( float x, float y, float w, float h, const vec4_t color, float alphaScale ) {
	if ( w <= 0.0f || h <= 0.0f ) {
		return;
	}

	Con_SetScaledColor( color, alphaScale );
	re.DrawStretchPic( x, y, w, h, 0, 0, 1, 1, cls.whiteShader );
}


static void Con_LightenColor( const vec4_t color, float amount, vec4_t outColor ) {
	int i;

	if ( amount < 0.0f ) {
		amount = 0.0f;
	} else if ( amount > 1.0f ) {
		amount = 1.0f;
	}

	for ( i = 0; i < 3; i++ ) {
		outColor[ i ] = color[ i ] + ( 1.0f - color[ i ] ) * amount;
	}
	outColor[ 3 ] = color[ 3 ];
}


static float Con_GetFadeAlpha( float frac ) {
	float alphaScale;

	if ( !con_fade || !con_fade->integer ) {
		return 1.0f;
	}

	alphaScale = frac / 0.5f;
	if ( alphaScale < 0.0f ) {
		alphaScale = 0.0f;
	} else if ( alphaScale > 1.0f ) {
		alphaScale = 1.0f;
	}

	return alphaScale;
}


static void Con_DrawSmallCharFloat( float x, float y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -smallchar_height ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	re.DrawStretchPic( x, y, smallchar_width, smallchar_height,
		fcol, frow, fcol + size, frow + size,
		cls.charSetShader );
}


static int Con_GetOldestLine( void ) {
	if ( con.current >= con.totallines ) {
		return con.current - con.totallines + 1;
	}

	return 0;
}


static void Con_GetInputDrawInfo( field_t *edit, int *prestep, int *drawLen ) {
	int len;
	int start;
	int count;

	count = edit->widthInChars - 1;
	if ( count < 1 ) {
		count = 1;
	}

	len = strlen( edit->buffer );
	start = edit->scroll;

	if ( len <= count ) {
		start = 0;
	} else {
		if ( start + count > len ) {
			start = len - count;
			if ( start < 0 ) {
				start = 0;
			}
		}
	}

	if ( start + count > len ) {
		count = len - start;
	}

	if ( count < 0 ) {
		count = 0;
	}

	edit->scroll = start;

	if ( prestep ) {
		*prestep = start;
	}
	if ( drawLen ) {
		*drawLen = count;
	}
}


static void Con_AdjustInputScroll( field_t *edit ) {
	int len;
	int drawLen;

	len = strlen( edit->buffer );
	drawLen = edit->widthInChars - 1;
	if ( drawLen < 1 ) {
		drawLen = 1;
	}

	if ( edit->cursor < 0 ) {
		edit->cursor = 0;
	} else if ( edit->cursor > len ) {
		edit->cursor = len;
	}

	if ( edit->scroll < 0 ) {
		edit->scroll = 0;
	}

	if ( edit->cursor < edit->scroll ) {
		edit->scroll = edit->cursor;
	} else if ( edit->cursor >= edit->scroll + drawLen ) {
		edit->scroll = edit->cursor - drawLen + 1;
	}

	if ( edit->scroll > len ) {
		edit->scroll = len;
	}

	if ( len > drawLen && edit->scroll > len - drawLen ) {
		edit->scroll = len - drawLen;
	}

	if ( edit->scroll < 0 ) {
		edit->scroll = 0;
	}
}


static qboolean Con_HasInputSelection( void ) {
	return con.inputSelectionAnchor >= 0 && con.inputSelectionAnchor != g_consoleField.cursor;
}


static void Con_GetInputSelectionRange( int *start, int *end ) {
	if ( !Con_HasInputSelection() ) {
		if ( start ) {
			*start = g_consoleField.cursor;
		}
		if ( end ) {
			*end = g_consoleField.cursor;
		}
		return;
	}

	if ( con.inputSelectionAnchor < g_consoleField.cursor ) {
		if ( start ) {
			*start = con.inputSelectionAnchor;
		}
		if ( end ) {
			*end = g_consoleField.cursor;
		}
	} else {
		if ( start ) {
			*start = g_consoleField.cursor;
		}
		if ( end ) {
			*end = con.inputSelectionAnchor;
		}
	}
}


static void Con_ClearInputSelection( void ) {
	con.inputSelectionAnchor = -1;
	con.inputSelecting = qfalse;
}


static void Con_DeleteInputRange( field_t *edit, int start, int end ) {
	int len;

	if ( start < 0 ) {
		start = 0;
	}

	len = strlen( edit->buffer );
	if ( end > len ) {
		end = len;
	}

	if ( end <= start ) {
		edit->cursor = start;
		Con_AdjustInputScroll( edit );
		Con_ClearInputSelection();
		return;
	}

	memmove( edit->buffer + start, edit->buffer + end, len + 1 - end );
	edit->cursor = start;
	Con_AdjustInputScroll( edit );
	Con_ClearInputSelection();
	Con_InvalidateCompletionState();
}


static void Con_DeleteInputSelection( void ) {
	int start, end;

	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_GetInputSelectionRange( &start, &end );
	Con_DeleteInputRange( &g_consoleField, start, end );
}


static int Con_SeekWordCursor( const field_t *edit, int cursor, int direction ) {
	const char *buffer = edit->buffer;
	int len = strlen( buffer );

	if ( direction > 0 ) {
		while ( cursor < len && buffer[ cursor ] == ' ' ) {
			cursor++;
		}
		while ( cursor < len && buffer[ cursor ] != ' ' ) {
			cursor++;
		}
		while ( cursor < len && buffer[ cursor ] == ' ' ) {
			cursor++;
		}
	} else {
		while ( cursor > 0 && buffer[ cursor - 1 ] == ' ' ) {
			cursor--;
		}
		while ( cursor > 0 && buffer[ cursor - 1 ] != ' ' ) {
			cursor--;
		}
		if ( cursor == 0 && ( buffer[ 0 ] == '/' || buffer[ 0 ] == '\\' ) ) {
			cursor++;
		}
	}

	return cursor;
}


static void Con_SetInputCursor( int cursor, qboolean keepSelection ) {
	int oldCursor;
	int len;

	len = strlen( g_consoleField.buffer );
	oldCursor = g_consoleField.cursor;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	if ( keepSelection ) {
		if ( con.inputSelectionAnchor < 0 ) {
			con.inputSelectionAnchor = oldCursor;
		}
	} else {
		Con_ClearInputSelection();
	}

	g_consoleField.cursor = cursor;
	Con_AdjustInputScroll( &g_consoleField );
	con.focus = CON_FOCUS_INPUT;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_SelectAllInput( void ) {
	con.focus = CON_FOCUS_INPUT;
	con.inputSelectionAnchor = 0;
	g_consoleField.cursor = strlen( g_consoleField.buffer );
	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_InsertInputChar( int ch ) {
	int len;

	if ( ch < ' ' ) {
		return;
	}

	Con_DeleteInputSelection();
	len = strlen( g_consoleField.buffer );

	if ( key_overstrikeMode ) {
		if ( g_consoleField.cursor == MAX_EDIT_LINE - 2 ) {
			return;
		}

		g_consoleField.buffer[ g_consoleField.cursor ] = ch;
		g_consoleField.cursor++;
		if ( g_consoleField.cursor > len ) {
			g_consoleField.buffer[ g_consoleField.cursor ] = '\0';
		}
	} else {
		if ( len == MAX_EDIT_LINE - 2 ) {
			return;
		}

		memmove( g_consoleField.buffer + g_consoleField.cursor + 1,
			g_consoleField.buffer + g_consoleField.cursor,
			len + 1 - g_consoleField.cursor );
		g_consoleField.buffer[ g_consoleField.cursor ] = ch;
		g_consoleField.cursor++;
	}

	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearInputSelection();
	con.focus = CON_FOCUS_INPUT;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static int Con_BuildInputSelectionText( char *buffer, int bufferSize ) {
	int start, end;
	int length;

	if ( !buffer || bufferSize < 1 || !Con_HasInputSelection() ) {
		return 0;
	}

	Con_GetInputSelectionRange( &start, &end );
	length = end - start;
	if ( length <= 0 ) {
		buffer[ 0 ] = '\0';
		return 0;
	}

	if ( length >= bufferSize ) {
		length = bufferSize - 1;
	}

	Com_Memcpy( buffer, g_consoleField.buffer + start, length );
	buffer[ length ] = '\0';
	return length;
}


static void Con_CopyInputSelection( void ) {
	char text[ MAX_EDIT_LINE ];

	if ( !Con_BuildInputSelectionText( text, sizeof( text ) ) ) {
		return;
	}
	Sys_SetClipboardData( text );
}


static void Con_CutInputSelection( void ) {
	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_CopyInputSelection();
	Con_DeleteInputSelection();
}


static void Con_PasteClipboardToInput( void ) {
	char *text;
	int i;

	text = Sys_GetClipboardData();
	if ( !text ) {
		return;
	}

	con.focus = CON_FOCUS_INPUT;
	Con_DeleteInputSelection();

	for ( i = 0; text[ i ]; i++ ) {
		if ( text[ i ] >= ' ' ) {
			Con_InsertInputChar( text[ i ] );
		}
	}

	Z_Free( text );
}


static int Con_CompareLogPos( int line1, int column1, int line2, int column2 ) {
	if ( line1 < line2 ) {
		return -1;
	}
	if ( line1 > line2 ) {
		return 1;
	}
	if ( column1 < column2 ) {
		return -1;
	}
	if ( column1 > column2 ) {
		return 1;
	}
	return 0;
}


static void Con_ClampLogPosition( int *line, int *column ) {
	int oldestLine = Con_GetOldestLine();

	if ( *line < oldestLine ) {
		*line = oldestLine;
	} else if ( *line > con.current ) {
		*line = con.current;
	}

	if ( *column < 0 ) {
		*column = 0;
	} else if ( *column > con.linewidth ) {
		*column = con.linewidth;
	}
}


static void Con_ClearLogSelection( void ) {
	con.logSelectionAnchorLine = con.logSelectionLine;
	con.logSelectionAnchorColumn = con.logSelectionColumn;
	con.logSelecting = qfalse;
}


static qboolean Con_HasLogSelection( void ) {
	return Con_CompareLogPos( con.logSelectionAnchorLine, con.logSelectionAnchorColumn,
		con.logSelectionLine, con.logSelectionColumn ) != 0;
}


static void Con_GetLogSelectionRange( int *startLine, int *startColumn, int *endLine, int *endColumn ) {
	if ( Con_CompareLogPos( con.logSelectionAnchorLine, con.logSelectionAnchorColumn,
		con.logSelectionLine, con.logSelectionColumn ) <= 0 ) {
		if ( startLine ) {
			*startLine = con.logSelectionAnchorLine;
		}
		if ( startColumn ) {
			*startColumn = con.logSelectionAnchorColumn;
		}
		if ( endLine ) {
			*endLine = con.logSelectionLine;
		}
		if ( endColumn ) {
			*endColumn = con.logSelectionColumn;
		}
	} else {
		if ( startLine ) {
			*startLine = con.logSelectionLine;
		}
		if ( startColumn ) {
			*startColumn = con.logSelectionColumn;
		}
		if ( endLine ) {
			*endLine = con.logSelectionAnchorLine;
		}
		if ( endColumn ) {
			*endColumn = con.logSelectionAnchorColumn;
		}
	}
}


static void Con_SetLogCursor( int line, int column, qboolean keepSelection ) {
	Con_ClampLogPosition( &line, &column );

	if ( !keepSelection ) {
		con.logSelectionAnchorLine = line;
		con.logSelectionAnchorColumn = column;
	}

	con.logSelectionLine = line;
	con.logSelectionColumn = column;
	con.focus = CON_FOCUS_LOG;
	Con_ClearInputSelection();
	Con_InvalidateCompletionState();
}


static short *Con_GetLogLineText( int line ) {
	int oldestLine;

	oldestLine = Con_GetOldestLine();
	if ( line < oldestLine || line > con.current ) {
		return NULL;
	}

	return con.text + ( line % con.totallines ) * con.linewidth;
}


static int Con_BuildLogSelectionText( char *buffer, int bufferSize ) {
	int startLine, startColumn, endLine, endColumn;
	int line;
	int length;

	if ( !buffer || bufferSize < 1 || !Con_HasLogSelection() ) {
		return 0;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	length = 0;

	for ( line = startLine; line <= endLine; line++ ) {
		short *lineText = Con_GetLogLineText( line );
		int segmentStart = ( line == startLine ) ? startColumn : 0;
		int segmentEnd = ( line == endLine ) ? endColumn : con.linewidth;
		int copyEnd;
		int i;

		if ( !lineText ) {
			continue;
		}

		if ( segmentStart < 0 ) {
			segmentStart = 0;
		}
		if ( segmentEnd > con.linewidth ) {
			segmentEnd = con.linewidth;
		}
		if ( segmentEnd < segmentStart ) {
			segmentEnd = segmentStart;
		}

		copyEnd = segmentEnd;
		while ( copyEnd > segmentStart && ( lineText[ copyEnd - 1 ] & 0xff ) == ' ' ) {
			copyEnd--;
		}

		for ( i = segmentStart; i < copyEnd && length < bufferSize - 1; i++ ) {
			buffer[ length++ ] = lineText[ i ] & 0xff;
		}

		if ( line < endLine && length < bufferSize - 1 ) {
			buffer[ length++ ] = '\n';
		}
	}

	buffer[ length ] = '\0';
	return length;
}


static void Con_SelectAllLog( void ) {
	con.focus = CON_FOCUS_LOG;
	Con_ClearInputSelection();
	con.logSelectionAnchorLine = Con_GetOldestLine();
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = con.current;
	con.logSelectionColumn = con.linewidth;
	Con_InvalidateCompletionState();
}


static void Con_CopyLogSelection( void ) {
	char *text;
	int length;
	int startLine, endLine;
	int bufferSize;

	if ( !Con_HasLogSelection() ) {
		return;
	}

	Con_GetLogSelectionRange( &startLine, NULL, &endLine, NULL );
	bufferSize = ( endLine - startLine + 1 ) * ( con.linewidth + 1 ) + 1;
	text = Z_Malloc( bufferSize );
	length = Con_BuildLogSelectionText( text, bufferSize );
	text[ length ] = '\0';
	Sys_SetClipboardData( text );
	Z_Free( text );
}


static void Con_CopySelection( void ) {
	if ( Con_HasInputSelection() ) {
		Con_CopyInputSelection();
	} else if ( Con_HasLogSelection() ) {
		Con_CopyLogSelection();
	}
}


static void Con_InvalidateCompletionState( void ) {
	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionReplaceOffset = 0;
	con.completionReplaceLength = 0;
	con.completionAppendSpace = qfalse;
	con.completionPopupVisible = qfalse;
	con.completionPrependSlash = qfalse;
	con.completionSnapshotValid = qfalse;
	con.completionSnapshotCursor = 0;
	con.completionSnapshotBuffer[ 0 ] = '\0';
}


static void Con_InsertInputTextAt( const char *text, int cursor ) {
	qboolean lastWasConvertedSpace;
	int i;

	if ( !text || !text[ 0 ] ) {
		Con_SetInputCursor( cursor, qfalse );
		return;
	}

	Con_SetInputCursor( cursor, qfalse );
	lastWasConvertedSpace = qfalse;

	for ( i = 0; text[ i ]; i++ ) {
		int ch = (unsigned char)text[ i ];

		if ( ch == '\r' || ch == '\n' || ch == '\t' ) {
			if ( lastWasConvertedSpace ) {
				continue;
			}
			ch = ' ';
			lastWasConvertedSpace = qtrue;
		} else {
			lastWasConvertedSpace = qfalse;
			if ( ch < ' ' ) {
				continue;
			}
		}

		Con_InsertInputChar( ch );
	}
}


static int QDECL Con_CompareCompletionMatches( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, (const char *)b );
}


static qboolean Con_CollectCompletionMatch( const char *match, void *context ) {
	int i;
	(void)context;

	if ( !match || !match[ 0 ] ) {
		return qtrue;
	}

	for ( i = 0; i < con.completionCount; i++ ) {
		if ( !Q_stricmp( con.completionMatches[ i ], match ) ) {
			return qtrue;
		}
	}

	if ( con.completionCount >= CON_COMPLETION_MAX_MATCHES ) {
		return qfalse;
	}

	Q_strncpyz( con.completionMatches[ con.completionCount ], match,
		sizeof( con.completionMatches[ con.completionCount ] ) );
	con.completionCount++;
	return qtrue;
}


static void Con_FindCompletionSegment( int cursor, int *segmentStart, int *segmentEnd ) {
	const char *buffer = g_consoleField.buffer;
	int len = strlen( buffer );
	int start = 0;
	int end = len;
	int i;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	for ( i = 0; i < cursor; i++ ) {
		if ( buffer[ i ] == ';' ) {
			start = i + 1;
		}
	}

	for ( i = cursor; i < len; i++ ) {
		if ( buffer[ i ] == ';' ) {
			end = i;
			break;
		}
	}

	if ( segmentStart ) {
		*segmentStart = start;
	}
	if ( segmentEnd ) {
		*segmentEnd = end;
	}
}


static qboolean Con_CurrentTokenMatchesSelectedCompletion( void ) {
	int matchLen;

	if ( con.completionCount < 1 ||
		con.completionSelection < 0 ||
		con.completionSelection >= con.completionCount ) {
		return qfalse;
	}

	matchLen = strlen( con.completionMatches[ con.completionSelection ] );
	if ( matchLen != con.completionReplaceLength ) {
		return qfalse;
	}

	return !Q_stricmpn( g_consoleField.buffer + con.completionReplaceOffset,
		con.completionMatches[ con.completionSelection ], matchLen );
}


static qboolean Con_CompletionPopupEnabled( void ) {
	return ( con_completionPopup && con_completionPopup->integer ) ? qtrue : qfalse;
}


static qboolean Con_HasActiveCompletionPopup( void ) {
	return ( Con_CompletionPopupEnabled() &&
		con.completionPopupVisible &&
		con.focus == CON_FOCUS_INPUT &&
		!con.textDragging &&
		con.completionCount > 0 ) ? qtrue : qfalse;
}


static void Con_DismissCompletionPopup( void ) {
	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionAppendSpace = qfalse;
	con.completionPopupVisible = qfalse;
	con.completionPrependSlash = qfalse;
	con.completionSnapshotValid = qtrue;
	con.completionSnapshotCursor = g_consoleField.cursor;
	Q_strncpyz( con.completionSnapshotBuffer, g_consoleField.buffer, sizeof( con.completionSnapshotBuffer ) );
}


static void Con_MoveCompletionSelection( int delta ) {
	if ( con.completionCount < 1 || delta == 0 ) {
		return;
	}

	if ( delta < 0 ) {
		con.completionSelection = ( con.completionSelection + con.completionCount - 1 ) % con.completionCount;
	} else {
		con.completionSelection = ( con.completionSelection + 1 ) % con.completionCount;
	}
}


static void Con_RefreshCompletionState( void ) {
	char prefixBuffer[ MAX_EDIT_LINE ];
	char fullSegment[ MAX_EDIT_LINE ];
	char previousMatch[ MAX_EDIT_LINE ];
	const char *buffer = g_consoleField.buffer;
	int cursor = g_consoleField.cursor;
	int len = strlen( buffer );
	int segmentStart, segmentEnd;
	int prefixLen, fullLen;
	int relativeCursor;
	int argIndex;
	int currentLen;
	int i;
	qboolean appendSpace;
	qboolean keepPrevious = qfalse;
	qboolean firstArg = qfalse;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	if ( con.focus != CON_FOCUS_INPUT || con.textDragging ) {
		con.completionCount = 0;
		con.completionSelection = 0;
		con.completionAppendSpace = qfalse;
		con.completionPopupVisible = qfalse;
		con.completionPrependSlash = qfalse;
		con.completionSnapshotValid = qtrue;
		con.completionSnapshotCursor = cursor;
		Q_strncpyz( con.completionSnapshotBuffer, buffer, sizeof( con.completionSnapshotBuffer ) );
		return;
	}

	if ( con.completionSnapshotValid &&
		con.completionSnapshotCursor == cursor &&
		!Q_stricmp( con.completionSnapshotBuffer, buffer ) ) {
		return;
	}

	if ( con.completionCount > 0 &&
		con.completionSelection >= 0 &&
		con.completionSelection < con.completionCount ) {
		Q_strncpyz( previousMatch, con.completionMatches[ con.completionSelection ], sizeof( previousMatch ) );
		keepPrevious = qtrue;
	} else {
		previousMatch[ 0 ] = '\0';
	}

	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionAppendSpace = qfalse;
	con.completionPrependSlash = qfalse;

	Con_FindCompletionSegment( cursor, &segmentStart, &segmentEnd );

	prefixLen = cursor - segmentStart;
	if ( prefixLen < 0 ) {
		prefixLen = 0;
	}
	if ( prefixLen >= (int)sizeof( prefixBuffer ) ) {
		prefixLen = sizeof( prefixBuffer ) - 1;
	}

	Com_Memcpy( prefixBuffer, buffer + segmentStart, prefixLen );
	prefixBuffer[ prefixLen ] = '\0';

	appendSpace = qfalse;
	if ( Field_QueryCompletionMatches( prefixBuffer, &appendSpace, Con_CollectCompletionMatch, NULL ) < 1 ||
		con.completionCount < 1 ) {
		con.completionPopupVisible = qfalse;
		con.completionSnapshotValid = qtrue;
		con.completionSnapshotCursor = cursor;
		Q_strncpyz( con.completionSnapshotBuffer, buffer, sizeof( con.completionSnapshotBuffer ) );
		return;
	}

	fullLen = segmentEnd - segmentStart;
	if ( fullLen < 0 ) {
		fullLen = 0;
	}
	if ( fullLen >= (int)sizeof( fullSegment ) ) {
		fullLen = sizeof( fullSegment ) - 1;
	}

	Com_Memcpy( fullSegment, buffer + segmentStart, fullLen );
	fullSegment[ fullLen ] = '\0';

	Cmd_TokenizeString( fullSegment );
	relativeCursor = cursor - segmentStart;
	if ( relativeCursor < 0 ) {
		relativeCursor = 0;
	} else if ( relativeCursor > fullLen ) {
		relativeCursor = fullLen;
	}

	if ( Cmd_Argc() < 1 ) {
		con.completionReplaceOffset = cursor;
		con.completionReplaceLength = 0;
		firstArg = qtrue;
	} else if ( relativeCursor > 0 && fullSegment[ relativeCursor - 1 ] <= ' ' ) {
		con.completionReplaceOffset = cursor;
		con.completionReplaceLength = 0;
		firstArg = ( Cmd_Argc() < 1 ) ? qtrue : qfalse;
	} else {
		argIndex = Cmd_ArgIndexFromOffset( relativeCursor );
		if ( argIndex < 0 ) {
			con.completionReplaceOffset = cursor;
			con.completionReplaceLength = 0;
			firstArg = qfalse;
		} else {
			con.completionReplaceOffset = segmentStart + Cmd_ArgOffset( argIndex );
			con.completionReplaceLength = strlen( Cmd_Argv( argIndex ) );
			firstArg = ( argIndex == 0 ) ? qtrue : qfalse;
		}
	}

	con.completionAppendSpace = appendSpace;
	con.completionPrependSlash = ( segmentStart == 0 && firstArg &&
		con.completionReplaceOffset == 0 &&
		buffer[ 0 ] != '\\' && buffer[ 0 ] != '/' ) ? qtrue : qfalse;

	qsort( con.completionMatches, con.completionCount,
		sizeof( con.completionMatches[ 0 ] ), Con_CompareCompletionMatches );

	if ( keepPrevious ) {
		for ( i = 0; i < con.completionCount; i++ ) {
			if ( !Q_stricmp( con.completionMatches[ i ], previousMatch ) ) {
				con.completionSelection = i;
				break;
			}
		}
	}

	currentLen = con.completionReplaceLength;
	if ( currentLen > 0 ) {
		for ( i = 0; i < con.completionCount; i++ ) {
			if ( (int)strlen( con.completionMatches[ i ] ) == currentLen &&
				!Q_stricmpn( buffer + con.completionReplaceOffset, con.completionMatches[ i ], currentLen ) ) {
				con.completionSelection = i;
				break;
			}
		}
	}

	con.completionSnapshotValid = qtrue;
	con.completionSnapshotCursor = cursor;
	Q_strncpyz( con.completionSnapshotBuffer, buffer, sizeof( con.completionSnapshotBuffer ) );
}


static void Con_ApplySelectedCompletion( int direction ) {
	char completed[ MAX_EDIT_LINE ];
	const char *buffer = g_consoleField.buffer;
	const char *match;
	int len = strlen( buffer );
	int replaceOffset = con.completionReplaceOffset;
	int replaceLength = con.completionReplaceLength;
	int suffixOffset;
	int outLen = 0;
	int matchLen;
	int copyLen;
	int suffixLen;
	qboolean addSpace = qfalse;

	if ( !Con_CompletionPopupEnabled() ) {
		Field_AutoComplete( &g_consoleField );
		Con_InvalidateCompletionState();
		return;
	}

	Con_RefreshCompletionState();

	if ( con.completionCount < 1 ) {
		Field_AutoComplete( &g_consoleField );
		Con_InvalidateCompletionState();
		return;
	}

	if ( direction < 0 ) {
		if ( !Con_CurrentTokenMatchesSelectedCompletion() ) {
			con.completionSelection = con.completionCount - 1;
		} else if ( con.completionCount > 1 ) {
			con.completionSelection = ( con.completionSelection + con.completionCount - 1 ) % con.completionCount;
		}
	} else if ( direction > 0 && Con_CurrentTokenMatchesSelectedCompletion() && con.completionCount > 1 ) {
		con.completionSelection = ( con.completionSelection + 1 ) % con.completionCount;
	}

	match = con.completionMatches[ con.completionSelection ];
	matchLen = strlen( match );

	if ( replaceOffset < 0 ) {
		replaceOffset = 0;
	} else if ( replaceOffset > len ) {
		replaceOffset = len;
	}
	if ( replaceLength < 0 ) {
		replaceLength = 0;
	}

	suffixOffset = replaceOffset + replaceLength;
	if ( suffixOffset > len ) {
		suffixOffset = len;
	}

	if ( con.completionAppendSpace ) {
		char next = buffer[ suffixOffset ];

		if ( next == '\0' || next == ';' || next > ' ' ) {
			addSpace = qtrue;
		}
	}

	if ( con.completionPrependSlash ) {
		completed[ outLen++ ] = '\\';
	}

	copyLen = replaceOffset;
	if ( copyLen > sizeof( completed ) - 1 - outLen ) {
		copyLen = sizeof( completed ) - 1 - outLen;
	}
	if ( copyLen > 0 ) {
		Com_Memcpy( completed + outLen, buffer, copyLen );
		outLen += copyLen;
	}

	copyLen = matchLen;
	if ( copyLen > sizeof( completed ) - 1 - outLen ) {
		copyLen = sizeof( completed ) - 1 - outLen;
	}
	if ( copyLen > 0 ) {
		Com_Memcpy( completed + outLen, match, copyLen );
		outLen += copyLen;
	}

	if ( addSpace && outLen < sizeof( completed ) - 1 ) {
		completed[ outLen++ ] = ' ';
	}

	suffixLen = len - suffixOffset;
	if ( suffixLen > sizeof( completed ) - 1 - outLen ) {
		suffixLen = sizeof( completed ) - 1 - outLen;
	}
	if ( suffixLen > 0 ) {
		Com_Memcpy( completed + outLen, buffer + suffixOffset, suffixLen );
		outLen += suffixLen;
	}

	completed[ outLen ] = '\0';
	Q_strncpyz( g_consoleField.buffer, completed, sizeof( g_consoleField.buffer ) );
	g_consoleField.cursor = ( con.completionPrependSlash ? 1 : 0 ) +
		replaceOffset + matchLen + ( addSpace ? 1 : 0 );
	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearInputSelection();
	con.focus = CON_FOCUS_INPUT;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_GetConsoleRect( float *x, float *y, float *w, float *h ) {
	float rectHeight;

	if ( x ) {
		*x = con.xadjust;
	}

	if ( y ) {
		*y = 0.0f;
	}

	if ( w ) {
		*w = ( con.displayWidth > 0.0f ) ? con.displayWidth : cls.glconfig.vidWidth;
	}

	rectHeight = (float)con.vislines;
	if ( rectHeight <= 0.0f ) {
		rectHeight = cls.glconfig.vidHeight * con.displayFrac;
		if ( ( Key_GetCatcher() & KEYCATCH_CONSOLE ) && rectHeight < cls.glconfig.vidHeight * 0.5f ) {
			rectHeight = cls.glconfig.vidHeight * 0.5f;
		}
		if ( rectHeight > cls.glconfig.vidHeight ) {
			rectHeight = cls.glconfig.vidHeight;
		}
	}

	if ( h ) {
		*h = rectHeight;
	}
}


static int Con_GetLogRowCount( void ) {
	int rows;

	rows = con.vislines / smallchar_height - 1;
	if ( rows < 1 ) {
		rows = 1;
	}

	return rows;
}


static void Con_GetLogAreaRect( float *x, float *y, float *w, float *h ) {
	float consoleX, consoleY, consoleW, consoleH;
	float logBottom;
	float logTop;
	int rows;

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	rows = Con_GetLogRowCount();
	logBottom = consoleY + consoleH - smallchar_height * 2.0f;
	logTop = logBottom - rows * smallchar_height;
	if ( logTop < consoleY ) {
		logTop = consoleY;
	}

	if ( x ) {
		*x = consoleX;
	}
	if ( y ) {
		*y = logTop;
	}
	if ( w ) {
		*w = consoleW;
	}
	if ( h ) {
		*h = logBottom - logTop;
	}
}


static qboolean Con_GetInputAreaRect( float *x, float *y, float *w, float *h ) {
	float consoleX, consoleY, consoleW, consoleH;

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	if ( consoleW <= 0.0f || consoleH <= smallchar_height * 2.0f ) {
		return qfalse;
	}

	if ( x ) {
		*x = consoleX + 2 * smallchar_width;
	}
	if ( y ) {
		*y = consoleY + consoleH - smallchar_height * 2.0f;
	}
	if ( w ) {
		*w = consoleW - 3 * smallchar_width;
	}
	if ( h ) {
		*h = smallchar_height;
	}

	return qtrue;
}


static int Con_GetInputCursorFromMouse( void ) {
	float inputX, inputY, inputW, inputH;
	int prestep, drawLen;
	int cursor;
	int len;

	if ( !Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) ) {
		return g_consoleField.cursor;
	}

	Con_GetInputDrawInfo( &g_consoleField, &prestep, &drawLen );
	len = strlen( g_consoleField.buffer );

	cursor = prestep + (int)( ( con.mouseX - inputX ) / smallchar_width + 0.5f );
	if ( con.mouseX <= inputX ) {
		cursor = prestep;
	}
	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	return cursor;
}


static qboolean Con_GetLogPositionFromMouse( int *line, int *column ) {
	float logX, logY, logW, logH;
	int rows;
	int rowIndex;
	int outLine;
	int outColumn;

	Con_GetLogAreaRect( &logX, &logY, &logW, &logH );
	if ( con.mouseX < logX || con.mouseX > logX + logW || con.mouseY < logY || con.mouseY > logY + logH ) {
		return qfalse;
	}

	rows = Con_GetLogRowCount();
	rowIndex = (int)( ( con.mouseY - logY ) / smallchar_height );
	if ( rowIndex < 0 ) {
		rowIndex = 0;
	} else if ( rowIndex >= rows ) {
		rowIndex = rows - 1;
	}

	if ( con.display != con.current && rows > 1 && rowIndex == rows - 1 ) {
		rowIndex = rows - 2;
	}

	outLine = con.display - ( rows - 1 - rowIndex );
	outColumn = (int)( ( con.mouseX - ( con.xadjust + smallchar_width ) ) / smallchar_width );

	Con_ClampLogPosition( &outLine, &outColumn );

	if ( line ) {
		*line = outLine;
	}
	if ( column ) {
		*column = outColumn;
	}

	return qtrue;
}


static qboolean Con_IsInputSelectionHit( int cursor ) {
	int start, end;

	if ( !Con_HasInputSelection() ) {
		return qfalse;
	}

	Con_GetInputSelectionRange( &start, &end );
	return ( cursor >= start && cursor <= end ) ? qtrue : qfalse;
}


static qboolean Con_IsLogSelectionHit( int line, int column ) {
	int startLine, startColumn, endLine, endColumn;

	if ( !Con_HasLogSelection() ) {
		return qfalse;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	return ( Con_CompareLogPos( line, column, startLine, startColumn ) >= 0 &&
		Con_CompareLogPos( line, column, endLine, endColumn ) <= 0 ) ? qtrue : qfalse;
}


static void Con_ClearTextDragState( void ) {
	con.textDragPending = qfalse;
	con.textDragging = qfalse;
	con.textDragFromInput = qfalse;
	con.textDragTargetInput = qfalse;
	con.textDragSourceStart = 0;
	con.textDragSourceEnd = 0;
	con.textDragDropCursor = g_consoleField.cursor;
	con.textDragTextLength = 0;
	con.textDragText[ 0 ] = '\0';
}


static qboolean Con_BeginTextDrag( qboolean fromInput ) {
	int length;

	if ( fromInput ) {
		length = Con_BuildInputSelectionText( con.textDragText, sizeof( con.textDragText ) );
	} else {
		length = Con_BuildLogSelectionText( con.textDragText, sizeof( con.textDragText ) );
	}

	if ( length < 1 ) {
		Con_ClearTextDragState();
		return qfalse;
	}

	con.textDragging = qtrue;
	con.textDragPending = qfalse;
	con.textDragFromInput = fromInput;
	con.textDragTargetInput = qfalse;
	con.textDragTextLength = length;
	con.inputSelecting = qfalse;
	con.logSelecting = qfalse;
	return qtrue;
}


static void Con_UpdateTextDragTarget( void ) {
	float inputX, inputY, inputW, inputH;

	if ( !con.textDragging ) {
		return;
	}

	if ( Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) &&
		con.mouseX >= inputX && con.mouseX <= inputX + inputW &&
		con.mouseY >= inputY && con.mouseY <= inputY + inputH ) {
		con.textDragTargetInput = qtrue;
		con.textDragDropCursor = Con_GetInputCursorFromMouse();
	} else {
		con.textDragTargetInput = qfalse;
	}
}


static void Con_FinishTextDrag( void ) {
	char draggedText[ MAX_EDIT_LINE ];
	int dropCursor;

	if ( !con.textDragging ) {
		return;
	}

	if ( con.textDragTargetInput && con.textDragTextLength > 0 ) {
		Q_strncpyz( draggedText, con.textDragText, sizeof( draggedText ) );
		dropCursor = con.textDragDropCursor;

		if ( con.textDragFromInput ) {
			const int sourceStart = con.textDragSourceStart;
			const int sourceEnd = con.textDragSourceEnd;

			if ( dropCursor > sourceStart && dropCursor < sourceEnd ) {
				Con_ClearTextDragState();
				return;
			}

			if ( dropCursor > sourceStart ) {
				dropCursor -= sourceEnd - sourceStart;
			}

			Con_DeleteInputRange( &g_consoleField, sourceStart, sourceEnd );
		}

		Con_InsertInputTextAt( draggedText, dropCursor );
	}

	Con_ClearTextDragState();
}


static qboolean Con_GetScrollRange( int *minDisplay, int *maxDisplay, int *filled ) {
	int totalLines;

	totalLines = ( con.current >= con.totallines ) ? con.totallines : con.current + 1;
	if ( filled ) {
		*filled = totalLines;
	}

	if ( totalLines <= con.vispage || con.vispage < 1 ) {
		if ( minDisplay ) {
			*minDisplay = con.current;
		}
		if ( maxDisplay ) {
			*maxDisplay = con.current;
		}
		return qfalse;
	}

	if ( maxDisplay ) {
		*maxDisplay = con.current;
	}
	if ( minDisplay ) {
		*minDisplay = con.current - totalLines + con.vispage;
	}

	return qtrue;
}


static qboolean Con_GetScrollbarGeometry( float hoverFrac, float *trackX, float *trackY, float *trackW, float *trackH,
	float *thumbY, float *thumbH, float *hitX, float *hitW ) {
	float consoleX, consoleY, consoleW, consoleH;
	float logX, logY, logW, logH;
	float width;
	float maxWidth;
	float displayFrac;
	int minDisplay, maxDisplay, filled;

	if ( !Con_GetScrollRange( &minDisplay, &maxDisplay, &filled ) ) {
		return qfalse;
	}

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	if ( consoleW <= 0.0f || consoleH <= smallchar_height * 4.0f ) {
		return qfalse;
	}

	Con_GetLogAreaRect( &logX, &logY, &logW, &logH );
	if ( logH <= 0.0f ) {
		return qfalse;
	}

	maxWidth = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW;
	width = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW * hoverFrac;

	if ( trackX ) {
		*trackX = consoleX + consoleW - CON_SCROLLBAR_SIDE_PAD - width;
	}
	if ( trackY ) {
		*trackY = logY;
	}
	if ( trackW ) {
		*trackW = width;
	}
	if ( trackH ) {
		*trackH = logH;
	}
	if ( hitX ) {
		*hitX = consoleX + consoleW - CON_SCROLLBAR_SIDE_PAD - maxWidth - CON_SCROLLBAR_HIT_PAD;
	}
	if ( hitW ) {
		*hitW = maxWidth + CON_SCROLLBAR_HIT_PAD * 2.0f;
	}

	if ( thumbH ) {
		*thumbH = logH * ( (float)con.vispage / (float)filled );
		if ( *thumbH < CON_SCROLLBAR_MIN_THUMB ) {
			*thumbH = CON_SCROLLBAR_MIN_THUMB;
		}
		if ( *thumbH > logH ) {
			*thumbH = logH;
		}
	}

	if ( thumbY ) {
		if ( maxDisplay <= minDisplay || logH <= *thumbH ) {
			*thumbY = logY;
		} else {
			displayFrac = ( con.displayLine - minDisplay ) / (float)( maxDisplay - minDisplay );
			if ( displayFrac < 0.0f ) {
				displayFrac = 0.0f;
			} else if ( displayFrac > 1.0f ) {
				displayFrac = 1.0f;
			}
			*thumbY = logY + ( logH - *thumbH ) * displayFrac;
		}
	}

	return qtrue;
}


static void Con_ClampMouseToConsole( void ) {
	float consoleX, consoleY, consoleW, consoleH;
	float maxX, maxY;

	if ( cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ) {
		return;
	}

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );

	if ( !con.mouseInitialized ) {
		con.mouseX = consoleX + consoleW - 12.0f;
		con.mouseY = consoleY + ( consoleH > 1.0f ? consoleH * 0.5f : cls.glconfig.vidHeight * 0.25f );
		con.mouseInitialized = qtrue;
	}

	maxX = consoleX + consoleW - 1.0f;
	maxY = consoleY + consoleH - 1.0f;
	if ( maxX < consoleX ) {
		maxX = consoleX;
	}
	if ( maxY < consoleY ) {
		maxY = consoleY;
	}

	if ( con.mouseX < consoleX ) {
		con.mouseX = consoleX;
	} else if ( con.mouseX > maxX ) {
		con.mouseX = maxX;
	}

	if ( con.mouseY < consoleY ) {
		con.mouseY = consoleY;
	} else if ( con.mouseY > maxY ) {
		con.mouseY = maxY;
	}
}


static void Con_SetScrollbarDisplayFrac( float frac ) {
	int minDisplay, maxDisplay;
	int displayRange;

	if ( !Con_GetScrollRange( &minDisplay, &maxDisplay, NULL ) ) {
		return;
	}

	if ( frac < 0.0f ) {
		frac = 0.0f;
	} else if ( frac > 1.0f ) {
		frac = 1.0f;
	}

	displayRange = maxDisplay - minDisplay;
	if ( displayRange <= 0 ) {
		con.display = maxDisplay;
	} else {
		con.display = minDisplay + (int)( frac * displayRange + 0.5f );
	}

	Con_Fixup();
	con.displayLine = (float)con.display;
}


static void Con_UpdateScrollbarDrag( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float frac;

	if ( !con.scrollbarDragging || !keys[ K_MOUSE1 ].down ) {
		return;
	}

	if ( !Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, NULL, NULL ) ) {
		con.scrollbarDragging = qfalse;
		return;
	}

	if ( trackH <= thumbH ) {
		Con_SetScrollbarDisplayFrac( 1.0f );
		return;
	}

	frac = ( con.mouseY - con.scrollbarDragOffset - trackY ) / ( trackH - thumbH );
	Con_SetScrollbarDisplayFrac( frac );
}


static void Con_UpdateScrollbarHover( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	float target;
	float step;

	if ( !keys[ K_MOUSE1 ].down ) {
		con.scrollbarDragging = qfalse;
	}

	Con_ClampMouseToConsole();

	target = 0.0f;
	if ( ( Key_GetCatcher() & KEYCATCH_CONSOLE ) &&
		Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, &hitX, &hitW ) ) {
		if ( con.scrollbarDragging ||
			( con.mouseX >= hitX && con.mouseX <= hitX + hitW &&
			  con.mouseY >= trackY && con.mouseY <= trackY + trackH ) ) {
			target = 1.0f;
		}
	}

	step = cls.realFrametime * 0.001f * CON_SCROLLBAR_LERP_SPEED;
	if ( step > 1.0f ) {
		step = 1.0f;
	}

	if ( con.scrollbarHover < target ) {
		con.scrollbarHover += step;
		if ( con.scrollbarHover > target ) {
			con.scrollbarHover = target;
		}
	} else if ( con.scrollbarHover > target ) {
		con.scrollbarHover -= step;
		if ( con.scrollbarHover < target ) {
			con.scrollbarHover = target;
		}
	}

	Con_UpdateScrollbarDrag();
}


static void Con_UpdateDisplayLine( void ) {
	float target;
	float delta;
	float step;
	float speed;

	target = (float)con.display;

	if ( !con_scrollSmooth || !con_scrollSmooth->integer ) {
		con.displayLine = target;
		return;
	}

	if ( con.displayLine < 0.0f || con.displayLine > con.current + con.vispage + 1 ) {
		con.displayLine = target;
		return;
	}

	delta = target - con.displayLine;
	if ( fabs( delta ) < 0.01f ) {
		con.displayLine = target;
		return;
	}

	speed = con_scrollSmoothSpeed ? con_scrollSmoothSpeed->value : 24.0f;
	if ( speed < 1.0f ) {
		speed = 1.0f;
	}

	step = speed * cls.realFrametime * 0.001f;
	if ( step >= fabs( delta ) ) {
		con.displayLine = target;
	} else if ( delta > 0.0f ) {
		con.displayLine += step;
	} else {
		con.displayLine -= step;
	}
}


static void Con_GetSelectionColor( vec4_t outColor ) {
	vec4_t baseColor;

	Con_GetColorCvar( con_lineColor, g_color_table[ ColorIndex( COLOR_RED ) ], baseColor, qfalse );
	Con_LightenColor( baseColor, 0.55f, outColor );
	outColor[ 3 ] = CON_SELECTION_ALPHA;
}


static void Con_DrawInputSelection( float x, float y, int prestep, int drawLen, float alphaScale ) {
	int start, end;
	int visibleStart, visibleEnd;
	vec4_t selectionColor;

	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_GetInputSelectionRange( &start, &end );
	visibleStart = start;
	if ( visibleStart < prestep ) {
		visibleStart = prestep;
	}
	visibleEnd = end;
	if ( visibleEnd > prestep + drawLen ) {
		visibleEnd = prestep + drawLen;
	}

	if ( visibleEnd <= visibleStart ) {
		return;
	}

	Con_GetSelectionColor( selectionColor );
	Con_DrawSolidRect( x + ( visibleStart - prestep ) * smallchar_width, y,
		( visibleEnd - visibleStart ) * smallchar_width, smallchar_height,
		selectionColor, alphaScale );
}


static void Con_DrawLogSelectionRow( int line, float y, float alphaScale ) {
	int startLine, startColumn, endLine, endColumn;
	int segmentStart;
	int segmentEnd;
	vec4_t selectionColor;

	if ( !Con_HasLogSelection() ) {
		return;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	if ( line < startLine || line > endLine ) {
		return;
	}

	segmentStart = ( line == startLine ) ? startColumn : 0;
	segmentEnd = ( line == endLine ) ? endColumn : con.linewidth;
	if ( segmentEnd <= segmentStart ) {
		return;
	}

	Con_GetSelectionColor( selectionColor );
	Con_DrawSolidRect( con.xadjust + ( segmentStart + 1 ) * smallchar_width, y,
		( segmentEnd - segmentStart ) * smallchar_width, smallchar_height,
		selectionColor, alphaScale );
}


static void Con_DrawInputText( field_t *edit, float x, float y, float alphaScale ) {
	int len;
	int drawLen;
	int prestep;
	int cursorChar;
	int i;
	int currentColorIndex;
	char str[ MAX_STRING_CHARS ];

	Con_GetInputDrawInfo( edit, &prestep, &drawLen );
	len = strlen( edit->buffer );

	if ( drawLen >= MAX_STRING_CHARS ) {
		Com_Error( ERR_DROP, "drawLen >= MAX_STRING_CHARS" );
	}

	Com_Memcpy( str, edit->buffer + prestep, drawLen );
	str[ drawLen ] = '\0';

	if ( prestep > 0 && str[ 0 ] ) {
		str[ 0 ] = '<';
	}

	currentColorIndex = ColorIndex( COLOR_WHITE );
	for ( i = 0; i < prestep; i++ ) {
		if ( Q_IsColorString( edit->buffer + i ) ) {
			currentColorIndex = ColorIndexFromChar( edit->buffer[ i + 1 ] );
			i++;
		}
	}

	Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

	Con_DrawInputSelection( x, y, prestep, drawLen, alphaScale );

	for ( i = 0; i < drawLen; i++ ) {
		if ( Q_IsColorString( str + i ) ) {
			int colorIndex = ColorIndexFromChar( str[ i + 1 ] );

			if ( colorIndex != currentColorIndex ) {
				currentColorIndex = colorIndex;
				Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );
			}
		}

		Con_DrawSmallCharFloat( x + i * smallchar_width, y, str[ i ] );
	}

	Con_SetScaledColor( g_color_table[ ColorIndex( COLOR_WHITE ) ], alphaScale );

	if ( len > drawLen + prestep ) {
		Con_DrawSmallCharFloat( x + ( edit->widthInChars - 1 ) * smallchar_width, y, '>' );
	}

	if ( cls.realtime & 256 ) {
		re.SetColor( NULL );
		return;
	}

	if ( key_overstrikeMode ) {
		cursorChar = 11;
	} else {
		cursorChar = 10;
	}

	Con_DrawSmallCharFloat( x + ( edit->cursor - prestep ) * smallchar_width, y, cursorChar );

	re.SetColor( NULL );
}


static void Con_DrawInputDropCursor( field_t *edit, float x, float y, float alphaScale ) {
	int prestep, drawLen;
	int dropCursor;
	vec4_t selectionColor;

	if ( !con.textDragging || !con.textDragTargetInput ) {
		return;
	}

	Con_GetInputDrawInfo( edit, &prestep, &drawLen );
	dropCursor = con.textDragDropCursor;
	if ( dropCursor < prestep ) {
		dropCursor = prestep;
	} else if ( dropCursor > prestep + drawLen ) {
		dropCursor = prestep + drawLen;
	}

	Con_GetSelectionColor( selectionColor );
	selectionColor[ 3 ] = 0.9f;
	Con_DrawSolidRect( x + ( dropCursor - prestep ) * smallchar_width, y,
		2.0f, smallchar_height, selectionColor, alphaScale );
}


static void Con_DrawCompletionPopup( float x, float y, float alphaScale, const vec4_t lineColor ) {
	float consoleX, consoleW;
	float popupX, popupY;
	float popupW, popupH;
	int first;
	int visibleCount;
	int maxChars;
	int longest;
	int i;
	vec4_t backgroundColor;
	vec4_t borderColor;
	vec4_t selectionColor;
	vec4_t textColor;

	if ( !Con_CompletionPopupEnabled() ) {
		con.completionPopupVisible = qfalse;
		return;
	}

	Con_RefreshCompletionState();
	if ( con.completionCount < 1 || con.textDragging ) {
		con.completionPopupVisible = qfalse;
		return;
	}
	con.completionPopupVisible = qtrue;

	visibleCount = con.completionCount;
	if ( visibleCount > CON_COMPLETION_MAX_VISIBLE ) {
		visibleCount = CON_COMPLETION_MAX_VISIBLE;
	}

	first = 0;
	if ( con.completionSelection >= visibleCount ) {
		first = con.completionSelection - visibleCount + 1;
		if ( first > con.completionCount - visibleCount ) {
			first = con.completionCount - visibleCount;
		}
	}
	if ( first < 0 ) {
		first = 0;
	}

	longest = 0;
	for ( i = 0; i < visibleCount; i++ ) {
		int matchLen = strlen( con.completionMatches[ first + i ] );

		if ( matchLen > longest ) {
			longest = matchLen;
		}
	}
	if ( longest < 8 ) {
		longest = 8;
	}

	Con_GetConsoleRect( &consoleX, NULL, &consoleW, NULL );
	maxChars = (int)( ( consoleW - 6.0f * smallchar_width ) / smallchar_width );
	if ( maxChars < 8 ) {
		maxChars = 8;
	}
	if ( longest > maxChars ) {
		longest = maxChars;
	}

	popupW = ( longest + 2 ) * smallchar_width;
	popupH = visibleCount * smallchar_height + 4.0f;
	popupX = x;
	if ( popupX + popupW > consoleX + consoleW - smallchar_width ) {
		popupX = consoleX + consoleW - popupW - smallchar_width;
	}
	if ( popupX < consoleX + smallchar_width ) {
		popupX = consoleX + smallchar_width;
	}

	popupY = y - popupH - 4.0f;
	if ( popupY < 0.0f ) {
		popupY = 0.0f;
	}

	backgroundColor[ 0 ] = 0.0f;
	backgroundColor[ 1 ] = 0.0f;
	backgroundColor[ 2 ] = 0.0f;
	backgroundColor[ 3 ] = 0.72f;
	Con_LightenColor( lineColor, 0.25f, borderColor );
	borderColor[ 3 ] = 0.7f;
	Con_GetSelectionColor( selectionColor );
	selectionColor[ 3 ] = 0.85f;
	textColor[ 0 ] = 1.0f;
	textColor[ 1 ] = 1.0f;
	textColor[ 2 ] = 1.0f;
	textColor[ 3 ] = 0.95f;

	Con_DrawSolidRect( popupX, popupY, popupW, popupH, backgroundColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY, popupW, 1.0f, borderColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY + popupH - 1.0f, popupW, 1.0f, borderColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY, 1.0f, popupH, borderColor, alphaScale );
	Con_DrawSolidRect( popupX + popupW - 1.0f, popupY, 1.0f, popupH, borderColor, alphaScale );

	for ( i = 0; i < visibleCount; i++ ) {
		const char *match = con.completionMatches[ first + i ];
		float rowY = popupY + 2.0f + i * smallchar_height;
		int drawLen = strlen( match );
		int j;

		if ( drawLen > longest ) {
			drawLen = longest;
		}

		if ( first + i == con.completionSelection ) {
			Con_DrawSolidRect( popupX + 1.0f, rowY, popupW - 2.0f, smallchar_height,
				selectionColor, alphaScale );
		}

		Con_SetScaledColor( textColor, alphaScale );
		for ( j = 0; j < drawLen; j++ ) {
			Con_DrawSmallCharFloat( popupX + smallchar_width + j * smallchar_width, rowY, match[ j ] );
		}
	}

	if ( first > 0 ) {
		Con_SetScaledColor( borderColor, alphaScale );
		Con_DrawSmallCharFloat( popupX + popupW - 2.0f * smallchar_width, popupY + 2.0f, '^' );
	}
	if ( first + visibleCount < con.completionCount ) {
		Con_SetScaledColor( borderColor, alphaScale );
		Con_DrawSmallCharFloat( popupX + popupW - 2.0f * smallchar_width,
			popupY + popupH - smallchar_height - 2.0f, 'v' );
	}

	re.SetColor( NULL );
}


static void Con_DrawScrollbar( float alphaScale, const vec4_t lineColor ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	vec4_t trackColor;
	vec4_t thumbColor;

	if ( !Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, NULL, NULL ) ) {
		return;
	}

	trackColor[ 0 ] = lineColor[ 0 ];
	trackColor[ 1 ] = lineColor[ 1 ];
	trackColor[ 2 ] = lineColor[ 2 ];
	trackColor[ 3 ] = 0.14f + con.scrollbarHover * 0.08f;

	Con_LightenColor( lineColor, 0.18f + con.scrollbarHover * 0.3f, thumbColor );
	thumbColor[ 3 ] = 0.6f + con.scrollbarHover * 0.2f;

	Con_DrawSolidRect( trackX, trackY, trackW, trackH, trackColor, alphaScale );
	Con_DrawSolidRect( trackX, thumbY, trackW, thumbH, thumbColor, alphaScale );
}


static void Con_DrawMouseCursor( float alphaScale, const vec4_t lineColor ) {
	vec4_t cursorColor;
	vec4_t shadowColor;
	float x, y;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return;
	}

	Con_ClampMouseToConsole();
	x = con.mouseX;
	y = con.mouseY;

	if ( cls.cursorShader ) {
		cursorColor[ 0 ] = 1.0f;
		cursorColor[ 1 ] = 1.0f;
		cursorColor[ 2 ] = 1.0f;
		cursorColor[ 3 ] = alphaScale;
		re.SetColor( cursorColor );
		re.DrawStretchPic( x - 16.0f, y - 16.0f, 32.0f, 32.0f, 0, 0, 1, 1, cls.cursorShader );
		re.SetColor( NULL );
		return;
	}

	Con_LightenColor( lineColor, 0.65f, cursorColor );
	cursorColor[ 3 ] = 0.95f;

	shadowColor[ 0 ] = 0.0f;
	shadowColor[ 1 ] = 0.0f;
	shadowColor[ 2 ] = 0.0f;
	shadowColor[ 3 ] = 0.35f;

	Con_DrawSolidRect( x + 1.0f, y + 1.0f, 3.0f, 13.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 1.0f, y + 1.0f, 10.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 4.0f, y + 4.0f, 3.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 7.0f, y + 7.0f, 3.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 4.0f, y + 11.0f, 7.0f, 3.0f, shadowColor, alphaScale );

	Con_DrawSolidRect( x, y, 3.0f, 13.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x, y, 10.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 3.0f, y + 3.0f, 3.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 6.0f, y + 6.0f, 3.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 3.0f, y + 10.0f, 7.0f, 3.0f, cursorColor, alphaScale );
}


static void Con_ScrollToLogCursor( void ) {
	int rows;
	int topLine;

	rows = Con_GetLogRowCount();
	topLine = con.display - ( rows - 1 );

	if ( con.logSelectionLine > con.display ) {
		con.display = con.logSelectionLine;
	} else if ( con.logSelectionLine < topLine ) {
		con.display = con.logSelectionLine + rows - 1;
	}

	Con_Fixup();
	con.displayLine = (float)con.display;
}


static void Con_MoveLogCursorByChars( int delta, qboolean keepSelection ) {
	int line = con.logSelectionLine;
	int column = con.logSelectionColumn;
	int oldestLine = Con_GetOldestLine();

	while ( delta < 0 ) {
		if ( column > 0 ) {
			column--;
		} else if ( line > oldestLine ) {
			line--;
			column = con.linewidth;
		}
		delta++;
	}

	while ( delta > 0 ) {
		if ( column < con.linewidth ) {
			column++;
		} else if ( line < con.current ) {
			line++;
			column = 0;
		}
		delta--;
	}

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static void Con_MoveLogCursorByLines( int delta, qboolean keepSelection ) {
	int line = con.logSelectionLine + delta;
	int column = con.logSelectionColumn;

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static void Con_MoveLogCursorToBoundary( qboolean toStart, qboolean wholeLog, qboolean keepSelection ) {
	int line = con.logSelectionLine;
	int column = con.logSelectionColumn;

	if ( wholeLog ) {
		line = toStart ? Con_GetOldestLine() : con.current;
		column = toStart ? 0 : con.linewidth;
	} else {
		column = toStart ? 0 : con.linewidth;
	}

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static qboolean Con_HandleLogSelectionKey( int key ) {
	if ( con.focus != CON_FOCUS_LOG || !keys[ K_CTRL ].down || !keys[ K_SHIFT ].down ) {
		return qfalse;
	}

	switch ( key ) {
	case K_LEFTARROW:
		Con_MoveLogCursorByChars( -1, qtrue );
		return qtrue;
	case K_RIGHTARROW:
		Con_MoveLogCursorByChars( 1, qtrue );
		return qtrue;
	case K_UPARROW:
	case K_KP_UPARROW:
		Con_MoveLogCursorByLines( -1, qtrue );
		return qtrue;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		Con_MoveLogCursorByLines( 1, qtrue );
		return qtrue;
	case K_PGUP:
		Con_MoveLogCursorByLines( -Con_GetScrollStep( 0 ), qtrue );
		return qtrue;
	case K_PGDN:
		Con_MoveLogCursorByLines( Con_GetScrollStep( 0 ), qtrue );
		return qtrue;
	case K_HOME:
		Con_MoveLogCursorToBoundary( qtrue, qtrue, qtrue );
		return qtrue;
	case K_END:
		Con_MoveLogCursorToBoundary( qfalse, qtrue, qtrue );
		return qtrue;
	default:
		return qfalse;
	}
}


qboolean Con_InputKey( int key ) {
	int cursor;
	int len;
	int lowerKey = ( key >= 0 && key < 128 ) ? tolower( key ) : key;

	if ( Con_HandleLogSelectionKey( key ) ) {
		return qtrue;
	}

	if ( Con_HasActiveCompletionPopup() ) {
		switch ( key ) {
		case K_UPARROW:
		case K_KP_UPARROW:
		case K_MWHEELUP:
			Con_MoveCompletionSelection( -1 );
			return qtrue;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
		case K_MWHEELDOWN:
			Con_MoveCompletionSelection( 1 );
			return qtrue;
		case K_PGUP:
			con.completionSelection -= CON_COMPLETION_MAX_VISIBLE;
			if ( con.completionSelection < 0 ) {
				con.completionSelection = 0;
			}
			return qtrue;
		case K_PGDN:
			con.completionSelection += CON_COMPLETION_MAX_VISIBLE;
			if ( con.completionSelection >= con.completionCount ) {
				con.completionSelection = con.completionCount - 1;
			}
			return qtrue;
		case K_HOME:
			con.completionSelection = 0;
			return qtrue;
		case K_END:
			con.completionSelection = con.completionCount - 1;
			return qtrue;
		case K_ENTER:
		case K_KP_ENTER:
			Con_ApplySelectedCompletion( 0 );
			Con_DismissCompletionPopup();
			return qtrue;
		default:
			break;
		}

		if ( keys[ K_CTRL ].down ) {
			switch ( lowerKey ) {
			case 'p':
				Con_MoveCompletionSelection( -1 );
				return qtrue;
			case 'n':
				Con_MoveCompletionSelection( 1 );
				return qtrue;
			default:
				break;
			}
		}
	}

	if ( keys[ K_CTRL ].down ) {
		switch ( lowerKey ) {
		case 'a':
			if ( con.focus == CON_FOCUS_LOG ) {
				Con_SelectAllLog();
			} else {
				Con_SelectAllInput();
			}
			return qtrue;
		case 'c':
			Con_CopySelection();
			return qtrue;
		case 'v':
			Con_PasteClipboardToInput();
			return qtrue;
		case 'x':
			Con_CutInputSelection();
			return qtrue;
		default:
			break;
		}
	}

	if ( key == K_INS || key == K_KP_INS ) {
		if ( keys[ K_SHIFT ].down ) {
			Con_PasteClipboardToInput();
		} else {
			key_overstrikeMode = !key_overstrikeMode;
		}
		return qtrue;
	}

	if ( key == K_TAB ) {
		if ( Con_CompletionPopupEnabled() ) {
			Con_ApplySelectedCompletion( keys[ K_SHIFT ].down ? -1 : 1 );
		} else {
			Field_AutoComplete( &g_consoleField );
			Con_InvalidateCompletionState();
		}
		return qtrue;
	}

	switch ( key ) {
	case K_BACKSPACE:
		if ( Con_HasInputSelection() ) {
			Con_DeleteInputSelection();
		} else if ( g_consoleField.cursor > 0 ) {
			Con_DeleteInputRange( &g_consoleField, g_consoleField.cursor - 1, g_consoleField.cursor );
		}
		return qtrue;
	case K_DEL:
		if ( Con_HasInputSelection() ) {
			Con_DeleteInputSelection();
		} else {
			len = strlen( g_consoleField.buffer );
			if ( g_consoleField.cursor < len ) {
				Con_DeleteInputRange( &g_consoleField, g_consoleField.cursor, g_consoleField.cursor + 1 );
			}
		}
		return qtrue;
	case K_LEFTARROW:
		if ( keys[ K_SHIFT ].down ) {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, -1 ) : g_consoleField.cursor - 1;
			Con_SetInputCursor( cursor, qtrue );
		} else if ( Con_HasInputSelection() ) {
			int start, end;
			Con_GetInputSelectionRange( &start, &end );
			Con_SetInputCursor( start, qfalse );
		} else {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, -1 ) : g_consoleField.cursor - 1;
			Con_SetInputCursor( cursor, qfalse );
		}
		return qtrue;
	case K_RIGHTARROW:
		if ( keys[ K_SHIFT ].down ) {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, 1 ) : g_consoleField.cursor + 1;
			Con_SetInputCursor( cursor, qtrue );
		} else if ( Con_HasInputSelection() ) {
			int start, end;
			Con_GetInputSelectionRange( &start, &end );
			Con_SetInputCursor( end, qfalse );
		} else {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, 1 ) : g_consoleField.cursor + 1;
			Con_SetInputCursor( cursor, qfalse );
		}
		return qtrue;
	case K_HOME:
		Con_SetInputCursor( 0, keys[ K_SHIFT ].down ? qtrue : qfalse );
		return qtrue;
	case K_END:
		Con_SetInputCursor( strlen( g_consoleField.buffer ), keys[ K_SHIFT ].down ? qtrue : qfalse );
		return qtrue;
	default:
		return qfalse;
	}
}


void Con_CharEvent( int key ) {
	if ( key < ' ' ) {
		return;
	}

	con.focus = CON_FOCUS_INPUT;
	Con_InsertInputChar( key );
}


void Con_MouseEvent( int dx, int dy ) {
	int line, column;
	float moveX, moveY;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return;
	}

	Con_ClampMouseToConsole();

	con.mouseX += dx;
	con.mouseY += dy;

	Con_ClampMouseToConsole();

	if ( con.scrollbarDragging ) {
		Con_UpdateScrollbarDrag();
		return;
	}

	if ( con.textDragPending && keys[ K_MOUSE1 ].down ) {
		moveX = fabs( con.mouseX - con.textDragStartMouseX );
		moveY = fabs( con.mouseY - con.textDragStartMouseY );
		if ( moveX >= CON_TEXT_DRAG_THRESHOLD || moveY >= CON_TEXT_DRAG_THRESHOLD ) {
			Con_BeginTextDrag( con.textDragFromInput );
		}
	}

	if ( con.textDragging ) {
		Con_UpdateTextDragTarget();
		return;
	}

	if ( con.inputSelecting && keys[ K_MOUSE1 ].down ) {
		Con_SetInputCursor( Con_GetInputCursorFromMouse(), qtrue );
	}

	if ( con.logSelecting && keys[ K_MOUSE1 ].down && Con_GetLogPositionFromMouse( &line, &column ) ) {
		Con_SetLogCursor( line, column, qtrue );
	}

	Con_UpdateScrollbarDrag();
}


qboolean Con_KeyEvent( int key, qboolean down ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	float inputX, inputY, inputW, inputH;
	int line, column;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return qfalse;
	}

	if ( !down ) {
		if ( key == K_MOUSE1 ) {
			con.scrollbarDragging = qfalse;
			con.inputSelecting = qfalse;
			con.logSelecting = qfalse;
			if ( con.textDragging ) {
				Con_FinishTextDrag();
			} else if ( con.textDragPending ) {
				if ( con.textDragFromInput ) {
					Con_SetInputCursor( Con_GetInputCursorFromMouse(), qfalse );
				} else if ( Con_GetLogPositionFromMouse( &line, &column ) ) {
					Con_SetLogCursor( line, column, qfalse );
				}
				Con_ClearTextDragState();
			}
		}
		return ( key >= K_MOUSE1 && key <= K_MOUSE5 ) ? qtrue : qfalse;
	}

	if ( key < K_MOUSE1 || key > K_MOUSE5 ) {
		return qfalse;
	}

	Con_ClampMouseToConsole();

	if ( key == K_MOUSE1 &&
		Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, &hitX, &hitW ) &&
		con.mouseX >= hitX && con.mouseX <= hitX + hitW &&
		con.mouseY >= trackY && con.mouseY <= trackY + trackH ) {
		Con_ClearTextDragState();
		con.scrollbarDragging = qtrue;
		con.inputSelecting = qfalse;
		con.logSelecting = qfalse;

		if ( con.mouseY >= thumbY && con.mouseY <= thumbY + thumbH ) {
			con.scrollbarDragOffset = con.mouseY - thumbY;
		} else {
			con.scrollbarDragOffset = thumbH * 0.5f;
			Con_UpdateScrollbarDrag();
		}

		return qtrue;
	}

	if ( key == K_MOUSE1 && Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) &&
		con.mouseY >= inputY && con.mouseY <= inputY + inputH &&
		con.mouseX >= con.xadjust &&
		con.mouseX <= con.xadjust + ( ( con.displayWidth > 0.0f ) ? con.displayWidth : cls.glconfig.vidWidth ) ) {
		int inputCursor = Con_GetInputCursorFromMouse();

		if ( !keys[ K_SHIFT ].down && Con_IsInputSelectionHit( inputCursor ) ) {
			Con_GetInputSelectionRange( &con.textDragSourceStart, &con.textDragSourceEnd );
			con.textDragPending = qtrue;
			con.textDragFromInput = qtrue;
			con.textDragStartMouseX = con.mouseX;
			con.textDragStartMouseY = con.mouseY;
			con.inputSelecting = qfalse;
			con.logSelecting = qfalse;
			con.focus = CON_FOCUS_INPUT;
			return qtrue;
		}

		Con_ClearTextDragState();
		Con_SetInputCursor( inputCursor, keys[ K_SHIFT ].down ? qtrue : qfalse );
		if ( !keys[ K_SHIFT ].down ) {
			Con_ClearLogSelection();
		}
		con.inputSelecting = qtrue;
		con.logSelecting = qfalse;
		return qtrue;
	}

	if ( key == K_MOUSE1 && Con_GetLogPositionFromMouse( &line, &column ) ) {
		if ( !keys[ K_SHIFT ].down && Con_IsLogSelectionHit( line, column ) ) {
			con.textDragPending = qtrue;
			con.textDragFromInput = qfalse;
			con.textDragStartMouseX = con.mouseX;
			con.textDragStartMouseY = con.mouseY;
			con.inputSelecting = qfalse;
			con.logSelecting = qfalse;
			return qtrue;
		}

		Con_ClearTextDragState();
		Con_ClearInputSelection();
		Con_SetLogCursor( line, column, ( keys[ K_SHIFT ].down && con.focus == CON_FOCUS_LOG ) ? qtrue : qfalse );
		con.inputSelecting = qfalse;
		con.logSelecting = qtrue;
		return qtrue;
	}

	return qtrue;
}


static int Con_GetScrollStep( int lines ) {
	int maxLines;

	if ( lines > 0 ) {
		return lines;
	}

	maxLines = con.vispage - 2;
	if ( maxLines < 1 ) {
		maxLines = 1;
	}

	if ( lines == 0 ) {
		return maxLines;
	}

	if ( !con_scrollLines ) {
		return maxLines < 8 ? maxLines : 8;
	}

	lines = con_scrollLines->integer;
	if ( lines < 1 ) {
		lines = 1;
	}

	if ( lines > maxLines ) {
		lines = maxLines;
	}

	return lines;
}


static void Con_SyncSpeedCvars( void ) {
	if ( con_speedLegacy && con_speedLegacy->modified ) {
		if ( con_conspeed && Q_stricmp( con_conspeed->string, con_speedLegacy->string ) ) {
			Cvar_Set2( "con_speed", con_speedLegacy->string, qtrue );
		}
		con_speedLegacy->modified = qfalse;
		if ( con_conspeed ) {
			con_conspeed->modified = qfalse;
		}
	} else if ( con_conspeed && con_conspeed->modified ) {
		if ( con_speedLegacy && Q_stricmp( con_speedLegacy->string, con_conspeed->string ) ) {
			Cvar_Set2( "scr_conspeed", con_conspeed->string, qtrue );
			con_speedLegacy->modified = qfalse;
		}
		con_conspeed->modified = qfalse;
	}
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void ) {
	int catcher;

	// Can't toggle the console when it's the only thing available
    if ( cls.state == CA_DISCONNECTED && Key_GetCatcher() == KEYCATCH_CONSOLE ) {
		return;
	}

	if ( con_autoclear->integer ) {
		Field_Clear( &g_consoleField );
	}

	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify();
	catcher = Key_GetCatcher() ^ KEYCATCH_CONSOLE;
	Key_SetCatcher( catcher );

	if ( catcher & KEYCATCH_CONSOLE ) {
		con.focus = CON_FOCUS_INPUT;
		Con_ClearInputSelection();
		Con_ClearLogSelection();
		con.mouseInitialized = qfalse;
		con.scrollbarDragging = qfalse;
		con.scrollbarHover = 0.0f;
		con.inputSelecting = qfalse;
		con.logSelecting = qfalse;
	} else {
		con.scrollbarDragging = qfalse;
		con.inputSelecting = qfalse;
		con.logSelecting = qfalse;
	}
}


/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f( void ) {
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f( void ) {
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode3_f
================
*/
static void Con_MessageMode3_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_CROSSHAIR_PLAYER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode4_f
================
*/
static void Con_MessageMode4_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_LAST_ATTACKER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void ) {
	int		i;

	for ( i = 0 ; i < con.linewidth ; i++ ) {
		con.text[i] = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';
	}

	con.x = 0;
	con.current = 0;
	con.newline = qtrue;
	con.logSelectionAnchorLine = 0;
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = 0;
	con.logSelectionColumn = 0;
	con.focus = CON_FOCUS_INPUT;
	Con_ClearInputSelection();
	Con_ClearLogSelection();

	Con_Bottom();		// go to end
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f( void )
{
	int		l, x, i, n;
	short	*line;
	fileHandle_t	f;
	int		bufferlen;
	char	*buffer;
	char	filename[ MAX_OSPATH ];
	const char *ext;

	if ( Cmd_Argc() != 2 )
	{
		Com_Printf( "usage: condump <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if ( !FS_AllowedExtension( filename, qfalse, &ext ) ) {
		Com_Printf( "%s: Invalid filename extension '%s'.\n", __func__, ext );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if ( f == FS_INVALID_HANDLE )
	{
		Com_Printf( "ERROR: couldn't open %s.\n", filename );
		return;
	}

	Com_Printf( "Dumped console text to %s.\n", filename );

	if ( con.current >= con.totallines ) {
		n = con.totallines;
		l = con.current + 1;
	} else {
		n = con.current + 1;
		l = 0;
	}

	bufferlen = con.linewidth + ARRAY_LEN( Q_NEWLINE ) * sizeof( char );
	buffer = Hunk_AllocateTempMemory( bufferlen );

	// write the remaining lines
	buffer[ bufferlen - 1 ] = '\0';

	for ( i = 0; i < n ; i++, l++ ) 
	{
		line = con.text + (l % con.totallines) * con.linewidth;
		// store line
		for( x = 0; x < con.linewidth; x++ )
			buffer[ x ] = line[ x ] & 0xff;
		buffer[ con.linewidth ] = '\0';
		// terminate on ending space characters
		for ( x = con.linewidth - 1 ; x >= 0 ; x-- ) {
			if ( buffer[ x ] == ' ' )
				buffer[ x ] = '\0';
			else
				break;
		}
		Q_strcat( buffer, bufferlen, Q_NEWLINE );
		FS_Write( buffer, strlen( buffer ), f );
	}

	Hunk_FreeTempMemory( buffer );
	FS_FCloseFile( f );
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;
	
	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( void )
{
	int		i, j, width, oldwidth, oldtotallines, oldcurrent, numlines, numchars;
	short	tbuf[CON_TEXTSIZE], *src, *dst;
	int		vispage;
	float	scale;
	float	charScale;
	float	contentWidth;
	float	contentHeight;
	float	xadjust;
	float	displayWidth;
	qboolean uniformScale;
	qboolean centeredExtents;
	qboolean scaleModified;
	qboolean uniformModified;
	qboolean extentsModified;
	qboolean sizeChanged;
	qboolean widthChanged;

	scale = con_scale ? con_scale->value : 1.0f;
	if ( scale <= 0.0f ) {
		scale = 1.0f;
	}

	uniformScale = ( con_scaleUniform && con_scaleUniform->integer ) ? qtrue : qfalse;
	centeredExtents = ( con_screenExtents && con_screenExtents->integer ) ? qtrue : qfalse;
	scaleModified = con_scale ? con_scale->modified : qfalse;
	uniformModified = con_scaleUniform ? con_scaleUniform->modified : qfalse;
	extentsModified = con_screenExtents ? con_screenExtents->modified : qfalse;

	charScale = scale * cls.con_factor;
	if ( uniformScale ) {
		charScale *= cls.scale;
	}

	smallchar_width = (int)( SMALLCHAR_WIDTH * charScale + 0.5f );
	smallchar_height = (int)( SMALLCHAR_HEIGHT * charScale + 0.5f );
	bigchar_width = (int)( BIGCHAR_WIDTH * charScale + 0.5f );
	bigchar_height = (int)( BIGCHAR_HEIGHT * charScale + 0.5f );

	if ( smallchar_width < 1 ) {
		smallchar_width = 1;
	}
	if ( smallchar_height < 1 ) {
		smallchar_height = 1;
	}
	if ( bigchar_width < 1 ) {
		bigchar_width = 1;
	}
	if ( bigchar_height < 1 ) {
		bigchar_height = 1;
	}

	if ( cls.glconfig.vidWidth == 0 ) // video hasn't been initialized yet
	{
		g_console_field_width = DEFAULT_CONSOLE_WIDTH;
		width = (int)( DEFAULT_CONSOLE_WIDTH * scale + 0.5f );
		if ( width < 1 ) {
			width = 1;
		}
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		con.vispage = 4;
		con.viswidth = 0;
		con.visheight = 0;
		con.xadjust = 0.0f;
		con.displayWidth = 0.0f;
		con.displayLine = 0.0f;
		g_consoleField.widthInChars = g_console_field_width;

		Con_Clear_f();
	}
	else
	{
		contentWidth = cls.glconfig.vidWidth;
		contentHeight = cls.glconfig.vidHeight;
		xadjust = 0.0f;
		displayWidth = cls.glconfig.vidWidth;

		if ( centeredExtents ) {
			if ( cls.biasX > 0.0f ) {
				contentWidth -= cls.biasX * 2.0f;
				xadjust = cls.biasX;
				displayWidth = contentWidth;
			}
		}

		if ( contentWidth <= 0.0f ) {
			contentWidth = cls.glconfig.vidWidth;
			xadjust = 0.0f;
			displayWidth = cls.glconfig.vidWidth;
		}

		width = (int)( contentWidth / smallchar_width ) - 2;
		if ( width < 1 ) {
			width = 1;
		}

		if ( width > MAX_CONSOLE_WIDTH )
			width = MAX_CONSOLE_WIDTH;

		vispage = (int)( contentHeight / ( smallchar_height * 2 ) ) - 1;
		if ( vispage < 1 ) {
			vispage = 1;
		}

		sizeChanged = ( con.viswidth != cls.glconfig.vidWidth || con.visheight != cls.glconfig.vidHeight ||
			con.xadjust != xadjust || con.displayWidth != displayWidth ) ? qtrue : qfalse;
		widthChanged = ( con.linewidth != width ) ? qtrue : qfalse;

		con.viswidth = cls.glconfig.vidWidth;
		con.visheight = cls.glconfig.vidHeight;
		con.xadjust = xadjust;
		con.displayWidth = displayWidth;
		g_console_field_width = width;
		g_consoleField.widthInChars = g_console_field_width;

		if ( !widthChanged && con.vispage == vispage && !sizeChanged && !scaleModified && !uniformModified && !extentsModified ) {
			return;
		}

		if ( !widthChanged ) {
			con.vispage = vispage;
			Con_Fixup();
			con.displayLine = (float)con.display;
			goto done;
		}

		oldwidth = con.linewidth;
		oldtotallines = con.totallines;
		oldcurrent = con.current;

		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		con.vispage = vispage;

		numchars = oldwidth;
		if ( numchars > con.linewidth )
			numchars = con.linewidth;

		if ( oldcurrent > oldtotallines )
			numlines = oldtotallines;	
		else
			numlines = oldcurrent + 1;	

		if ( numlines > con.totallines )
			numlines = con.totallines;

		Com_Memcpy( tbuf, con.text, CON_TEXTSIZE * sizeof( short ) );

		for ( i = 0; i < CON_TEXTSIZE; i++ ) 
			con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';

		for ( i = 0; i < numlines; i++ )
		{
			src = &tbuf[ ((oldcurrent - i + oldtotallines) % oldtotallines) * oldwidth ];
			dst = &con.text[ (numlines - 1 - i) * con.linewidth ];
			for ( j = 0; j < numchars; j++ )
				*dst++ = *src++;
		}

		Con_ClearNotify();

		con.current = numlines - 1;
		con.display = con.current;
		con.displayLine = (float)con.display;
	}

done:
	Con_AdjustInputScroll( &g_consoleField );
	con_scale->modified = qfalse;
	if ( con_scaleUniform ) {
		con_scaleUniform->modified = qfalse;
	}
	if ( con_screenExtents ) {
		con_screenExtents->modified = qfalse;
	}
	if ( con.mouseInitialized ) {
		Con_ClampMouseToConsole();
	}
}


/*
==================
Cmd_CompleteTxtName
==================
*/
static void Cmd_CompleteTxtName(const char *args, int argNum ) {
	if ( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", qfalse, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
================
Con_Init
================
*/
void Con_Init( void ) 
{
	const char *speedValue;

	con_notifytime = Cvar_Get( "con_notifytime", "3", 0 );
	Cvar_SetDescription( con_notifytime, "Defines how long messages (from players or the system) are on the screen (in seconds)." );
	speedValue = Cvar_VariableString( "con_speed" );
	if ( !speedValue[ 0 ] ) {
		speedValue = Cvar_VariableString( "scr_conspeed" );
	}
	if ( !speedValue[ 0 ] ) {
		speedValue = "3";
	}
	con_conspeed = Cvar_Get( "con_speed", speedValue, CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_conspeed, "Console opening/closing scroll speed." );
	con_speedLegacy = Cvar_Get( "scr_conspeed", con_conspeed->string, CVAR_NOTABCOMPLETE );
	Cvar_SetDescription( con_speedLegacy, "Deprecated alias for con_speed." );
	con_autoclear = Cvar_Get("con_autoclear", "1", CVAR_ARCHIVE_ND);
	Cvar_SetDescription( con_autoclear, "Enable/disable clearing console input text when console is closed." );
	con_scale = Cvar_Get( "con_scale", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scale, "0.5", "8", CV_FLOAT );
	Cvar_SetDescription( con_scale, "Console font size scale." );
	con_scaleUniform = Cvar_Get( "con_scaleUniform", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scaleUniform, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scaleUniform, "Use centered 4:3 uniform scaling for console font metrics instead of native pixel sizing." );
	con_screenExtents = Cvar_Get( "con_screenExtents", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_screenExtents, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_screenExtents,
		"Console display extents:\n"
		" 0 - use the full screen width\n"
		" 1 - keep the console display in centered 4:3 space" );
	con_scrollLines = Cvar_Get( "con_scrollLines", "8", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollLines, "1", "256", CV_INTEGER );
	Cvar_SetDescription( con_scrollLines, "Number of console lines scrolled per step, clamped to the current visible console page." );
	con_backgroundStyle = Cvar_Get( "con_backgroundStyle", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_backgroundStyle, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_backgroundStyle,
		"Console background style:\n"
		" 0 - legacy textured background\n"
		" 1 - flat shaded background" );
	con_backgroundColor = Cvar_Get( "con_backgroundColor", "", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_backgroundColor, "Console background RGB color as R G B values from 0-255. Empty keeps the style default or legacy cl_conColor fallback." );
	con_backgroundOpacity = Cvar_Get( "con_backgroundOpacity", "0.8", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_backgroundOpacity, "0", "1", CV_FLOAT );
	Cvar_SetDescription( con_backgroundOpacity, "Console background opacity from 0 to 1." );
	con_scrollSmooth = Cvar_Get( "con_scrollSmooth", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmooth, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scrollSmooth, "Smoothly animate console scrollback and new line movement." );
	con_scrollSmoothSpeed = Cvar_Get( "con_scrollSmoothSpeed", "24", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmoothSpeed, "1", "240", CV_FLOAT );
	Cvar_SetDescription( con_scrollSmoothSpeed, "Console smooth scrolling speed in lines per second." );
	con_completionPopup = Cvar_Get( "con_completionPopup", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_completionPopup, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_completionPopup, "Show the live console completion popup while typing. Disable to keep classic Tab completion behavior." );
	con_lineColor = Cvar_Get( "con_lineColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_lineColor, "Console separator and scrollback marker RGB color as R G B values from 0-255." );
	con_versionColor = Cvar_Get( "con_versionColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_versionColor, "Console version text RGB color as R G B values from 0-255." );
	con_fade = Cvar_Get( "con_fade", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_fade, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_fade, "Fade console background and text in and out while opening or closing the console." );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	con.inputSelectionAnchor = -1;
	con.logSelectionAnchorLine = 0;
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = 0;
	con.logSelectionColumn = 0;
	con.focus = CON_FOCUS_INPUT;
	Con_ClearTextDragState();
	Con_InvalidateCompletionState();

	Cmd_AddCommand( "clear", Con_Clear_f );
	Cmd_AddCommand( "condump", Con_Dump_f );
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f );
	Cmd_AddCommand( "messagemode3", Con_MessageMode3_f );
	Cmd_AddCommand( "messagemode4", Con_MessageMode4_f );
}


/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void )
{
	Cmd_RemoveCommand( "clear" );
	Cmd_RemoveCommand( "condump" );
	Cmd_RemoveCommand( "toggleconsole" );
	Cmd_RemoveCommand( "messagemode" );
	Cmd_RemoveCommand( "messagemode2" );
	Cmd_RemoveCommand( "messagemode3" );
	Cmd_RemoveCommand( "messagemode4" );
}


/*
===============
Con_Fixup
===============
*/
static void Con_Fixup( void ) 
{
	int filled;

	if ( con.current >= con.totallines ) {
		filled = con.totallines;
	} else {
		filled = con.current + 1;
	}

	if ( filled <= con.vispage ) {
		con.display = con.current;
	} else if ( con.current - con.display > filled - con.vispage ) {
		con.display = con.current - filled + con.vispage;
	} else if ( con.display > con.current ) {
		con.display = con.current;
	}

	if ( !con_scrollSmooth || !con_scrollSmooth->integer ||
		con.displayLine < (float)( con.current - con.totallines ) ||
		con.displayLine > (float)( con.current + con.vispage + 1 ) ) {
		con.displayLine = (float)con.display;
	}

	Con_ClampLogPosition( &con.logSelectionAnchorLine, &con.logSelectionAnchorColumn );
	Con_ClampLogPosition( &con.logSelectionLine, &con.logSelectionColumn );
	if ( con.focus != CON_FOCUS_LOG && !Con_HasLogSelection() ) {
		con.logSelectionAnchorLine = con.current;
		con.logSelectionAnchorColumn = 0;
		con.logSelectionLine = con.current;
		con.logSelectionColumn = 0;
	}

	if ( con.scrollbarDragging && !keys[ K_MOUSE1 ].down ) {
		con.scrollbarDragging = qfalse;
	}
	if ( ( con.textDragging || con.textDragPending ) && !keys[ K_MOUSE1 ].down ) {
		Con_ClearTextDragState();
	}
}


/*
===============
Con_Linefeed

Move to newline only when we _really_ need this
===============
*/
static void Con_NewLine( void )
{
	short *s;
	int i;

	// follow last line
	if ( con.display == con.current )
		con.display++;
	con.current++;

	s = &con.text[ ( con.current % con.totallines ) * con.linewidth ];
	for ( i = 0; i < con.linewidth ; i++ ) 
		*s++ = (ColorIndex(COLOR_WHITE)<<8) | ' ';

	con.x = 0;
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed( qboolean skipnotify )
{
	// mark time for transparent overlay
	if ( con.current >= 0 )	{
		if ( skipnotify )
			con.times[ con.current % NUM_CON_TIMES ] = 0;
		else
			con.times[ con.current % NUM_CON_TIMES ] = cls.realtime;
	}

	if ( con.newline ) {
		Con_NewLine();
	} else {
		con.newline = qtrue;
		con.x = 0;
	}

	Con_Fixup();
}


/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt ) {
	int		y;
	int		c, l;
	int		colorIndex;
	qboolean skipnotify = qfalse;		// NERVE - SMF
	int prev;							// NERVE - SMF

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = qtrue;
		txt += 12;
	}

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}
	
	if ( !con.initialized ) {
		static cvar_t null_cvar = { 0 };
		con.color[0] =
		con.color[1] =
		con.color[2] =
		con.color[3] = 1.0f;
		con.viswidth = -9999;
		cls.con_factor = 1.0f;
		con_scale = &null_cvar;
		con_scale->value = 1.0f;
		con_scale->modified = qtrue;
		Con_CheckResize();
		con.initialized = qtrue;
	}

	colorIndex = ColorIndex( COLOR_WHITE );

	while ( (c = *txt) != 0 ) {
		if ( Q_IsColorString( txt ) && *(txt+1) != '\n' ) {
			colorIndex = ColorIndexFromChar( *(txt+1) );
			txt += 2;
			continue;
		}

		// count word length
		for ( l = 0 ; l < con.linewidth ; l++ ) {
			if ( txt[l] <= ' ' ) {
				break;
			}
		}

		// word wrap
		if ( l != con.linewidth && ( con.x + l >= con.linewidth ) ) {
			Con_Linefeed( skipnotify );
		}

		txt++;

		switch( c )
		{
		case '\n':
			Con_Linefeed( skipnotify );
			break;
		case '\r':
			con.x = 0;
			break;
		default:
			if ( con.newline ) {
				Con_NewLine();
				Con_Fixup();
				con.newline = qfalse;
			}
			// display character and advance
			y = con.current % con.totallines;
			con.text[y * con.linewidth + con.x ] = (colorIndex << 8) | (c & 255);
			con.x++;
			if ( con.x >= con.linewidth ) {
				Con_Linefeed( skipnotify );
			}
			break;
		}
	}

	// mark time for transparent overlay
	if ( con.current >= 0 ) {
		if ( skipnotify ) {
			prev = con.current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con.times[ prev ] = 0;
		} else {
			con.times[ con.current % NUM_CON_TIMES ] = cls.realtime;
		}
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInput( float alphaScale, const vec4_t lineColor ) {
	int		y;
	vec4_t	color;

	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( smallchar_height * 2 );

	color[ 0 ] = con.color[ 0 ];
	color[ 1 ] = con.color[ 1 ];
	color[ 2 ] = con.color[ 2 ];
	color[ 3 ] = con.color[ 3 ];

	Con_SetScaledColor( color, alphaScale );
	Con_DrawSmallCharFloat( con.xadjust + 1 * smallchar_width, y, ']' );
	Con_DrawInputText( &g_consoleField, con.xadjust + 2 * smallchar_width, y, alphaScale );
	Con_DrawInputDropCursor( &g_consoleField, con.xadjust + 2 * smallchar_width, y, alphaScale );
	Con_DrawCompletionPopup( con.xadjust + 2 * smallchar_width, y, alphaScale, lineColor );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify( void )
{
	int		x, v;
	short	*text;
	int		i;
	int		time;
	int		skip;
	int		currentColorIndex;
	int		colorIndex;

	currentColorIndex = ColorIndex( COLOR_WHITE );
	re.SetColor( g_color_table[ currentColorIndex ] );

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if ( time >= con_notifytime->value*1000 )
			continue;
		text = con.text + (i % con.totallines)*con.linewidth;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}

		for (x = 0 ; x < con.linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			colorIndex = ( text[x] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				re.SetColor( g_color_table[ colorIndex ] );
			}
			SCR_DrawSmallChar( cl_conXOffset->integer + con.xadjust + (x+1)*smallchar_width, v, text[x] & 0xff );
		}

		v += smallchar_height;
	}

	re.SetColor( NULL );

	if ( Key_GetCatcher() & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE )
	{
		// rescale to virtual 640x480 space
		v /= cls.glconfig.vidHeight / 480.0;

		if (chat_team)
		{
			SCR_DrawBigString( SMALLCHAR_WIDTH, v, "say_team:", 1.0f, qfalse );
			skip = 10;
		}
		else
		{
			SCR_DrawBigString( SMALLCHAR_WIDTH, v, "say:", 1.0f, qfalse );
			skip = 5;
		}

		Field_BigDraw( &chatField, skip * BIGCHAR_WIDTH, v,
			SCREEN_WIDTH - ( skip + 1 ) * BIGCHAR_WIDTH, qtrue, qtrue );
	}
}


/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole( float frac ) {
	int				i, x;
	int				rows;
	short			*text;
	int				row;
	int				lines;
	int				currentColorIndex;
	int				colorIndex;
	float			xf, yf, wf;
	float			alphaScale;
	float			markerY;
	float			drawY;
	vec4_t			backgroundColor;
	vec4_t			lineColor;
	vec4_t			versionColor;

	lines = cls.glconfig.vidHeight * frac;
	if ( lines <= 0 )
		return;

	if ( re.FinishBloom )
		re.FinishBloom();

	if ( lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	xf = con.xadjust;
	wf = con.displayWidth;
	if ( wf <= 0.0f ) {
		xf = 0.0f;
		wf = cls.glconfig.vidWidth;
	}
	yf = lines;
	alphaScale = Con_GetFadeAlpha( frac );
	Con_GetBackgroundColor( backgroundColor );
	Con_GetColorCvar( con_lineColor, g_color_table[ ColorIndex( COLOR_RED ) ], lineColor, qfalse );
	Con_GetColorCvar( con_versionColor, lineColor, versionColor, qfalse );

	if ( yf < 1.0 ) {
		yf = 0;
	} else {
		Con_SetScaledColor( backgroundColor, alphaScale );
		if ( con_backgroundStyle && con_backgroundStyle->integer ) {
			re.DrawStretchPic( xf, 0, wf, yf, 0, 0, 1, 1, cls.whiteShader );
		} else {
			re.DrawStretchPic( xf, 0, wf, yf, 0, 0, 1, 1, cls.consoleShader );
		}
	}

	Con_SetScaledColor( lineColor, alphaScale );
	re.DrawStretchPic( xf, yf, wf, 2, 0, 0, 1, 1, cls.whiteShader );

	// draw the version number
	Con_SetScaledColor( versionColor, alphaScale );
	SCR_DrawSmallString( xf + wf - ( ARRAY_LEN( Q3_VERSION ) - 1 ) * smallchar_width,
		lines - smallchar_height, Q3_VERSION, ARRAY_LEN( Q3_VERSION ) - 1 );

	// draw the text
	con.vislines = lines;
	Con_UpdateScrollbarHover();
	rows = Con_GetLogRowCount();	// rows of text to draw

	markerY = lines - (smallchar_height * 3);
	drawY = markerY;
	row = (int)con.displayLine;
	if ( (float)row < con.displayLine ) {
		row++;
	}
	drawY += ( row - con.displayLine ) * smallchar_height;

	// draw from the bottom up
	if ( con.display != con.current )
	{
		// draw arrows to show the buffer is backscrolled
		Con_SetScaledColor( lineColor, alphaScale );
		for ( x = 0 ; x < con.linewidth ; x += 4 )
			Con_DrawSmallCharFloat( con.xadjust + (x+1)*smallchar_width, markerY, '^' );
		drawY -= smallchar_height;
		row--;
	}

#ifdef USE_CURL
	if ( download.progress[ 0 ] ) 
	{
		currentColorIndex = ColorIndex( COLOR_CYAN );
		Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

		i = strlen( download.progress );
		for ( x = 0 ; x < i ; x++ ) 
		{
			Con_DrawSmallCharFloat( con.xadjust + ( x + 1 ) * smallchar_width,
				lines - smallchar_height, download.progress[x] );
		}
	}
#endif

	currentColorIndex = ColorIndex( COLOR_WHITE );
	Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

	for ( i = 0 ; i <= rows ; i++, drawY -= smallchar_height, row-- )
	{
		if ( row < 0 )
			break;

		if ( con.current - row >= con.totallines ) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines) * con.linewidth;
		Con_DrawLogSelectionRow( row, drawY, alphaScale );

		for ( x = 0 ; x < con.linewidth ; x++ ) {
			// skip rendering whitespace
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			// track color changes
			colorIndex = ( text[ x ] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				Con_SetScaledColor( g_color_table[ colorIndex ], alphaScale );
			}
			Con_DrawSmallCharFloat( con.xadjust + (x + 1) * smallchar_width, drawY, text[x] & 0xff );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( alphaScale, lineColor );
	Con_DrawScrollbar( alphaScale, lineColor );
	Con_DrawMouseCursor( alphaScale, lineColor );

	re.SetColor( NULL );
}


/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {

	// check for console width changes from a vid mode change
	Con_CheckResize();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void ) 
{
	Con_SyncSpeedCvars();

	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = 0.5;	// half screen
	else
		con.finalFrac = 0.0;	// none visible
	
	// scroll towards the destination height
	if ( con.finalFrac < con.displayFrac )
	{
		con.displayFrac -= con_conspeed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac > con.displayFrac )
			con.displayFrac = con.finalFrac;

	}
	else if ( con.finalFrac > con.displayFrac )
	{
		con.displayFrac += con_conspeed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac < con.displayFrac )
			con.displayFrac = con.finalFrac;
	}

	Con_UpdateDisplayLine();
}


void Con_PageUp( int lines )
{
	lines = Con_GetScrollStep( lines );

	con.display -= lines;
	
	Con_Fixup();
}


void Con_PageDown( int lines )
{
	lines = Con_GetScrollStep( lines );

	con.display += lines;

	Con_Fixup();
}


void Con_Top( void )
{
	// this is generally incorrect but will be adjusted in Con_Fixup()
	con.display = con.current - con.totallines;

	Con_Fixup();
}


void Con_Bottom( void )
{
	con.display = con.current;

	Con_Fixup();
}


void Con_Close( void )
{
	if ( !com_cl_running->integer )
		return;

	Field_Clear( &g_consoleField );
	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0.0;			// none visible
	con.displayFrac = 0.0;
	con.displayLine = (float)con.display;
	con.scrollbarDragging = qfalse;
	con.scrollbarHover = 0.0f;
	con.mouseInitialized = qfalse;
	con.inputSelecting = qfalse;
	con.logSelecting = qfalse;
	con.focus = CON_FOCUS_INPUT;
	Con_ClearTextDragState();
	Con_InvalidateCompletionState();
	Con_ClearInputSelection();
	Con_ClearLogSelection();
}
