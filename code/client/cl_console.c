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

	qboolean newline;

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
static cvar_t	*con_lineColor;
static cvar_t	*con_versionColor;
static cvar_t	*con_fade;
static cvar_t	*con_speedLegacy;
static cvar_t	*con_scrollLines;

int			g_console_field_width;

static void Con_Fixup( void );


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


static void Con_DrawInputText( field_t *edit, float x, float y, float alphaScale ) {
	int len;
	int drawLen;
	int prestep;
	int cursorChar;
	int i;
	char str[ MAX_STRING_CHARS ];
	vec4_t color;

	drawLen = edit->widthInChars - 1;
	if ( drawLen < 1 ) {
		return;
	}

	len = strlen( edit->buffer );
	if ( len <= drawLen ) {
		prestep = 0;
	} else {
		if ( edit->scroll + drawLen > len ) {
			edit->scroll = len - drawLen;
			if ( edit->scroll < 0 ) {
				edit->scroll = 0;
			}
		}
		prestep = edit->scroll;
	}

	if ( prestep + drawLen > len ) {
		drawLen = len - prestep;
	}

	if ( drawLen < 0 ) {
		drawLen = 0;
	}

	if ( drawLen >= MAX_STRING_CHARS ) {
		Com_Error( ERR_DROP, "drawLen >= MAX_STRING_CHARS" );
	}

	Com_Memcpy( str, edit->buffer + prestep, drawLen );
	str[ drawLen ] = '\0';

	if ( prestep > 0 && str[ 0 ] ) {
		str[ 0 ] = '<';
	}

	color[ 0 ] = 1.0f;
	color[ 1 ] = 1.0f;
	color[ 2 ] = 1.0f;
	color[ 3 ] = 1.0f;
	Con_SetScaledColor( color, alphaScale );

	for ( i = 0; i < drawLen; i++ ) {
		Con_DrawSmallCharFloat( x + i * smallchar_width, y, str[ i ] );
	}

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
	// Can't toggle the console when it's the only thing available
    if ( cls.state == CA_DISCONNECTED && Key_GetCatcher() == KEYCATCH_CONSOLE ) {
		return;
	}

	if ( con_autoclear->integer ) {
		Field_Clear( &g_consoleField );
	}

	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_CONSOLE );
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
	con_scale->modified = qfalse;
	if ( con_scaleUniform ) {
		con_scaleUniform->modified = qfalse;
	}
	if ( con_screenExtents ) {
		con_screenExtents->modified = qfalse;
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
	con_scaleUniform = Cvar_Get( "con_scaleUniform", "0", CVAR_ARCHIVE_ND );
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
	con_backgroundOpacity = Cvar_Get( "con_backgroundOpacity", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_backgroundOpacity, "0", "1", CV_FLOAT );
	Cvar_SetDescription( con_backgroundOpacity, "Console background opacity from 0 to 1." );
	con_scrollSmooth = Cvar_Get( "con_scrollSmooth", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmooth, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scrollSmooth, "Smoothly animate console scrollback and new line movement." );
	con_scrollSmoothSpeed = Cvar_Get( "con_scrollSmoothSpeed", "24", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmoothSpeed, "1", "240", CV_FLOAT );
	Cvar_SetDescription( con_scrollSmoothSpeed, "Console smooth scrolling speed in lines per second." );
	con_lineColor = Cvar_Get( "con_lineColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_lineColor, "Console separator and scrollback marker RGB color as R G B values from 0-255." );
	con_versionColor = Cvar_Get( "con_versionColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_versionColor, "Console version text RGB color as R G B values from 0-255." );
	con_fade = Cvar_Get( "con_fade", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_fade, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_fade, "Fade console background and text in and out while opening or closing the console." );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

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
static void Con_DrawInput( float alphaScale ) {
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
	rows = lines / smallchar_height - 1;	// rows of text to draw

	drawY = lines - (smallchar_height * 3);
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
			Con_DrawSmallCharFloat( con.xadjust + (x+1)*smallchar_width, drawY, '^' );
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
	Con_DrawInput( alphaScale );

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
}
