#include "client.h"

#define JSON_IMPLEMENTATION
#include "../qcommon/json.h"
#undef JSON_IMPLEMENTATION

#define FNQ3_HUD_SCRIPT_FILE "fnq3-hud.json"
#define FNQ3_HUD_DUMP_FILE "fnq3-hud-dump.json"

#define HUD_MAX_RULES 256
#define HUD_MAX_SHADER_NAMES 1024
#define HUD_MAX_FRAME_DRAWS 4096
#define HUD_MAX_FRAME_GROUPS 512
#define HUD_MAX_DUMP_GROUPS 1024

typedef enum {
	HUD_TRANSFORM_STRETCH,
	HUD_TRANSFORM_UNIFORM
} hudTransformMode_t;

typedef enum {
	HUD_ALIGN_LEFT,
	HUD_ALIGN_CENTER,
	HUD_ALIGN_RIGHT
} hudAlignX_t;

typedef enum {
	HUD_ALIGN_TOP,
	HUD_ALIGN_MIDDLE,
	HUD_ALIGN_BOTTOM
} hudAlignY_t;

typedef struct {
	qhandle_t handle;
	char name[MAX_QPATH];
} hudShaderName_t;

typedef struct {
	char name[64];
	qboolean hasShader;
	char shader[MAX_QPATH];
	qboolean hasTextLike;
	qboolean textLike;
	qboolean hasRegion;
	float regionX;
	float regionY;
	float regionW;
	float regionH;
	qboolean hasAlignX;
	hudAlignX_t alignX;
	qboolean hasAlignY;
	hudAlignY_t alignY;
	hudTransformMode_t mode;
} hudRule_t;

typedef struct {
	float x;
	float y;
	float w;
	float h;
	float s1;
	float t1;
	float s2;
	float t2;
	qhandle_t shader;
	char shaderName[MAX_QPATH];
	qboolean textLike;
} hudDrawCapture_t;

typedef struct {
	float x1;
	float y1;
	float x2;
	float y2;
	char shaderName[MAX_QPATH];
	qboolean textLike;
	int drawCount;
	int samples;
	qboolean likelyAligned;
	hudTransformMode_t mode;
	hudAlignX_t alignX;
	hudAlignY_t alignY;
} hudDumpGroup_t;

static hudShaderName_t hudShaderNames[HUD_MAX_SHADER_NAMES];
static int hudNumShaderNames;

static hudRule_t hudRules[HUD_MAX_RULES];
static int hudNumRules;
static qboolean hudScriptLoaded;
static qboolean hudScriptWarned;
static int hudLastAspectMode;

static qboolean hudFrameActive;
static hudDrawCapture_t hudFrameDraws[HUD_MAX_FRAME_DRAWS];
static int hudFrameDrawCount;

static hudDumpGroup_t hudDumpGroups[HUD_MAX_DUMP_GROUPS];
static int hudDumpGroupCount;
static qboolean hudDumpDirty;
static int hudDumpPrevValue;


static qboolean CL_HudLoadRules( qboolean verbose );


static const char *CL_HudAlignXName( hudAlignX_t align ) {
	switch ( align ) {
	case HUD_ALIGN_LEFT:
		return "left";
	case HUD_ALIGN_RIGHT:
		return "right";
	default:
		return "center";
	}
}


static const char *CL_HudAlignYName( hudAlignY_t align ) {
	switch ( align ) {
	case HUD_ALIGN_TOP:
		return "top";
	case HUD_ALIGN_BOTTOM:
		return "bottom";
	default:
		return "center";
	}
}


static qboolean CL_HudParseAlignX( const char *value, hudAlignX_t *align ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "left" ) ) {
		*align = HUD_ALIGN_LEFT;
		return qtrue;
	}

	if ( !Q_stricmp( value, "right" ) ) {
		*align = HUD_ALIGN_RIGHT;
		return qtrue;
	}

	if ( !Q_stricmp( value, "center" ) || !Q_stricmp( value, "middle" ) ) {
		*align = HUD_ALIGN_CENTER;
		return qtrue;
	}

	return qfalse;
}


static qboolean CL_HudParseAlignY( const char *value, hudAlignY_t *align ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "top" ) ) {
		*align = HUD_ALIGN_TOP;
		return qtrue;
	}

	if ( !Q_stricmp( value, "bottom" ) ) {
		*align = HUD_ALIGN_BOTTOM;
		return qtrue;
	}

	if ( !Q_stricmp( value, "center" ) || !Q_stricmp( value, "middle" ) ) {
		*align = HUD_ALIGN_MIDDLE;
		return qtrue;
	}

	return qfalse;
}


static qboolean CL_HudParseMode( const char *value, hudTransformMode_t *mode ) {
	if ( !value || !*value ) {
		return qfalse;
	}

	if ( !Q_stricmp( value, "stretch" ) ) {
		*mode = HUD_TRANSFORM_STRETCH;
		return qtrue;
	}

	if ( !Q_stricmp( value, "uniform" ) ) {
		*mode = HUD_TRANSFORM_UNIFORM;
		return qtrue;
	}

	return qfalse;
}


static void CL_HudJsonWriteString( fileHandle_t file, const char *text ) {
	const char *s;
	char ch;

	FS_Write( "\"", 1, file );

	for ( s = text; s && *s; s++ ) {
		switch ( *s ) {
		case '\\':
		case '"':
			ch = '\\';
			FS_Write( &ch, 1, file );
			FS_Write( s, 1, file );
			break;
		case '\n':
			FS_Write( "\\n", 2, file );
			break;
		case '\r':
			FS_Write( "\\r", 2, file );
			break;
		case '\t':
			FS_Write( "\\t", 2, file );
			break;
		default:
			FS_Write( s, 1, file );
			break;
		}
	}

	FS_Write( "\"", 1, file );
}


static void CL_HudClearRules( void ) {
	hudNumRules = 0;
	hudScriptLoaded = qfalse;
}


static qboolean CL_HudLoadRules( qboolean verbose ) {
	void *buffer = NULL;
	int length;
	const char *json;
	const char *jsonEnd;
	const char *rulesJson;
	const char *ruleJson;

	CL_HudClearRules();

	length = FS_ReadFile( FNQ3_HUD_SCRIPT_FILE, &buffer );
	if ( length <= 0 || !buffer ) {
		hudScriptLoaded = qtrue;
		if ( verbose || ( !hudScriptWarned && cl_hudAspect && cl_hudAspect->integer > 0 ) ) {
			Com_Printf( S_COLOR_YELLOW "HUD: no script loaded from %s, using centered uniform placement.\n", FNQ3_HUD_SCRIPT_FILE );
			hudScriptWarned = qtrue;
		}
		return qfalse;
	}

	json = (const char *)buffer;
	jsonEnd = json + length;

	if ( JSON_ValueGetType( json, jsonEnd ) == JSONTYPE_OBJECT ) {
		rulesJson = JSON_ObjectGetNamedValue( json, jsonEnd, "rules" );
	} else if ( JSON_ValueGetType( json, jsonEnd ) == JSONTYPE_ARRAY ) {
		rulesJson = json;
	} else {
		rulesJson = NULL;
	}

	if ( !rulesJson || JSON_ValueGetType( rulesJson, jsonEnd ) != JSONTYPE_ARRAY ) {
		hudScriptLoaded = qtrue;
		if ( verbose || !hudScriptWarned ) {
			Com_Printf( S_COLOR_YELLOW "HUD: invalid script format in %s.\n", FNQ3_HUD_SCRIPT_FILE );
			hudScriptWarned = qtrue;
		}
		FS_FreeFile( buffer );
		return qfalse;
	}

	for ( ruleJson = JSON_ArrayGetFirstValue( rulesJson, jsonEnd );
		ruleJson && hudNumRules < HUD_MAX_RULES;
		ruleJson = JSON_ArrayGetNextValue( ruleJson, jsonEnd ) ) {
		hudRule_t *rule;
		const char *valueJson;
		const char *matchJson;
		const char *regionJson;
		const char *alignJson;
		char text[128];

		if ( JSON_ValueGetType( ruleJson, jsonEnd ) != JSONTYPE_OBJECT ) {
			continue;
		}

		rule = &hudRules[hudNumRules];
		Com_Memset( rule, 0, sizeof( *rule ) );
		rule->mode = HUD_TRANSFORM_UNIFORM;

		valueJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "name" );
		if ( valueJson ) {
			JSON_ValueGetString( valueJson, jsonEnd, rule->name, sizeof( rule->name ) );
		}

		valueJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "mode" );
		if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text, sizeof( text ) ) ) {
			CL_HudParseMode( text, &rule->mode );
		}

		alignJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "align" );
		if ( alignJson && JSON_ValueGetType( alignJson, jsonEnd ) == JSONTYPE_OBJECT ) {
			valueJson = JSON_ObjectGetNamedValue( alignJson, jsonEnd, "x" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text, sizeof( text ) ) ) {
				rule->hasAlignX = CL_HudParseAlignX( text, &rule->alignX );
			}

			valueJson = JSON_ObjectGetNamedValue( alignJson, jsonEnd, "y" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, text, sizeof( text ) ) ) {
				rule->hasAlignY = CL_HudParseAlignY( text, &rule->alignY );
			}
		}

		matchJson = JSON_ObjectGetNamedValue( ruleJson, jsonEnd, "match" );
		if ( matchJson && JSON_ValueGetType( matchJson, jsonEnd ) == JSONTYPE_OBJECT ) {
			valueJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "shader" );
			if ( valueJson && JSON_ValueGetString( valueJson, jsonEnd, rule->shader, sizeof( rule->shader ) ) ) {
				rule->hasShader = qtrue;
			}

			valueJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "textLike" );
			if ( valueJson ) {
				rule->hasTextLike = qtrue;
				rule->textLike = JSON_ValueGetInt( valueJson, jsonEnd ) ? qtrue : qfalse;
			}

			regionJson = JSON_ObjectGetNamedValue( matchJson, jsonEnd, "region" );
			if ( regionJson && JSON_ValueGetType( regionJson, jsonEnd ) == JSONTYPE_OBJECT ) {
				rule->regionX = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "x" ), jsonEnd );
				rule->regionY = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "y" ), jsonEnd );
				rule->regionW = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "w" ), jsonEnd );
				rule->regionH = JSON_ValueGetFloat( JSON_ObjectGetNamedValue( regionJson, jsonEnd, "h" ), jsonEnd );
				rule->hasRegion = qtrue;
			}
		}

		hudNumRules++;
	}

	hudScriptLoaded = qtrue;
	hudScriptWarned = qfalse;
	FS_FreeFile( buffer );
	if ( verbose ) {
		Com_Printf( "HUD: loaded %i rule%s from %s.\n", hudNumRules, hudNumRules == 1 ? "" : "s", FNQ3_HUD_SCRIPT_FILE );
	}
	return qtrue;
}


static void CL_HudEnsureRules( void ) {
	if ( !cl_hudAspect || cl_hudAspect->integer <= 0 ) {
		return;
	}

	if ( hudLastAspectMode != cl_hudAspect->integer ) {
		hudLastAspectMode = cl_hudAspect->integer;
		hudScriptLoaded = qfalse;
		hudScriptWarned = qfalse;
	}

	if ( !hudScriptLoaded ) {
		CL_HudLoadRules( qfalse );
	}
}


static void CL_HudReload_f( void ) {
	hudLastAspectMode = -1;
	hudScriptWarned = qfalse;
	CL_HudLoadRules( qtrue );
}


static const char *CL_HudLookupShaderName( qhandle_t shader ) {
	int i;

	if ( shader == cls.charSetShader ) {
		return "charset";
	}

	if ( shader == cls.whiteShader ) {
		return "white";
	}

	if ( shader == cls.consoleShader ) {
		return "console";
	}

	for ( i = 0; i < hudNumShaderNames; i++ ) {
		if ( hudShaderNames[ i ].handle == shader ) {
			return hudShaderNames[ i ].name;
		}
	}

	return "";
}


static qboolean CL_HudIsTextLike( qhandle_t shader, const char *shaderName, float w, float h ) {
	if ( shader == cls.charSetShader ) {
		return qtrue;
	}

	if ( shaderName && *shaderName && !Q_stricmp( shaderName, "charset" ) ) {
		return qtrue;
	}

	return ( w <= BIGCHAR_WIDTH * 2.0f && h <= GIANTCHAR_HEIGHT * 1.5f ) ? qtrue : qfalse;
}


static qboolean CL_HudIsFullscreenLike( float x, float y, float w, float h ) {
	return ( x <= 1.0f && y <= 1.0f && w >= SCREEN_WIDTH - 1.0f && h >= SCREEN_HEIGHT - 1.0f ) ? qtrue : qfalse;
}


static void CL_HudApplyStretch( float *x, float *y, float *w, float *h ) {
	const float xscale = cls.glconfig.vidWidth / 640.0f;
	const float yscale = cls.glconfig.vidHeight / 480.0f;

	*x *= xscale;
	*y *= yscale;
	*w *= xscale;
	*h *= yscale;
}


static void CL_HudUnstretch( float x, float y, float w, float h, float *outX, float *outY, float *outW, float *outH ) {
	const float xscale = cls.glconfig.vidWidth / 640.0f;
	const float yscale = cls.glconfig.vidHeight / 480.0f;

	if ( xscale == 0.0f || yscale == 0.0f ) {
		*outX = x;
		*outY = y;
		*outW = w;
		*outH = h;
		return;
	}

	*outX = x / xscale;
	*outY = y / yscale;
	*outW = w / xscale;
	*outH = h / yscale;
}


static void CL_HudApplyUniform( float *x, float *y, float *w, float *h, hudAlignX_t alignX, hudAlignY_t alignY ) {
	float originX;
	float originY;

	switch ( alignX ) {
	case HUD_ALIGN_LEFT:
		originX = 0.0f;
		break;
	case HUD_ALIGN_RIGHT:
		originX = cls.biasX * 2.0f;
		break;
	default:
		originX = cls.biasX;
		break;
	}

	switch ( alignY ) {
	case HUD_ALIGN_TOP:
		originY = 0.0f;
		break;
	case HUD_ALIGN_BOTTOM:
		originY = cls.biasY * 2.0f;
		break;
	default:
		originY = cls.biasY;
		break;
	}

	*x = *x * cls.scale + originX;
	*y = *y * cls.scale + originY;
	*w *= cls.scale;
	*h *= cls.scale;
}


static const hudRule_t *CL_HudFindRule( const hudDrawCapture_t *draw ) {
	int i;

	for ( i = 0; i < hudNumRules; i++ ) {
		const hudRule_t *rule = &hudRules[ i ];
		const float cx = draw->x + draw->w * 0.5f;
		const float cy = draw->y + draw->h * 0.5f;

		if ( rule->hasShader && Q_stricmp( rule->shader, draw->shaderName ) ) {
			continue;
		}

		if ( rule->hasTextLike && rule->textLike != draw->textLike ) {
			continue;
		}

		if ( rule->hasRegion ) {
			if ( cx < rule->regionX || cy < rule->regionY ||
				cx > rule->regionX + rule->regionW || cy > rule->regionY + rule->regionH ) {
				continue;
			}
		}

		return rule;
	}

	return NULL;
}


static void CL_HudCaptureDraw( const hudDrawCapture_t *draw ) {
	if ( !hudFrameActive || !cl_hudDump || !cl_hudDump->integer ) {
		return;
	}

	if ( hudFrameDrawCount >= HUD_MAX_FRAME_DRAWS ) {
		return;
	}

	hudFrameDraws[ hudFrameDrawCount++ ] = *draw;
}


static int CL_HudCompareDraws( const void *left, const void *right ) {
	const hudDrawCapture_t *a = (const hudDrawCapture_t *)left;
	const hudDrawCapture_t *b = (const hudDrawCapture_t *)right;

	if ( a->y < b->y ) {
		return -1;
	}
	if ( a->y > b->y ) {
		return 1;
	}
	if ( a->x < b->x ) {
		return -1;
	}
	if ( a->x > b->x ) {
		return 1;
	}
	return Q_stricmp( a->shaderName, b->shaderName );
}


static qboolean CL_HudCanMergeGroup( const hudDrawCapture_t *draw, const hudDumpGroup_t *group ) {
	const float gapX = draw->textLike ? 24.0f : 8.0f;
	const float gapY = draw->textLike ? 6.0f : 8.0f;
	const float drawX2 = draw->x + draw->w;
	const float drawY2 = draw->y + draw->h;

	if ( draw->textLike != group->textLike ) {
		return qfalse;
	}

	if ( Q_stricmp( draw->shaderName, group->shaderName ) ) {
		return qfalse;
	}

	if ( drawY2 < group->y1 - gapY || draw->y > group->y2 + gapY ) {
		return qfalse;
	}

	if ( drawX2 < group->x1 - gapX || draw->x > group->x2 + gapX ) {
		return qfalse;
	}

	return qtrue;
}


static void CL_HudSuggestPlacement( hudDumpGroup_t *group ) {
	const float width = group->x2 - group->x1;
	const float height = group->y2 - group->y1;
	const float centerX = group->x1 + width * 0.5f;
	const float centerY = group->y1 + height * 0.5f;

	if ( CL_HudIsFullscreenLike( group->x1, group->y1, width, height ) ) {
		group->mode = HUD_TRANSFORM_STRETCH;
		group->alignX = HUD_ALIGN_CENTER;
		group->alignY = HUD_ALIGN_MIDDLE;
		group->likelyAligned = qfalse;
		return;
	}

	group->mode = HUD_TRANSFORM_UNIFORM;

	if ( group->x1 <= 96.0f || centerX <= 160.0f ) {
		group->alignX = HUD_ALIGN_LEFT;
	} else if ( group->x2 >= SCREEN_WIDTH - 96.0f || centerX >= SCREEN_WIDTH - 160.0f ) {
		group->alignX = HUD_ALIGN_RIGHT;
	} else {
		group->alignX = HUD_ALIGN_CENTER;
	}

	if ( group->y1 <= 72.0f || centerY <= 120.0f ) {
		group->alignY = HUD_ALIGN_TOP;
	} else if ( group->y2 >= SCREEN_HEIGHT - 72.0f || centerY >= SCREEN_HEIGHT - 120.0f ) {
		group->alignY = HUD_ALIGN_BOTTOM;
	} else {
		group->alignY = HUD_ALIGN_MIDDLE;
	}

	group->likelyAligned = ( group->alignX != HUD_ALIGN_CENTER || group->alignY != HUD_ALIGN_MIDDLE ) ? qtrue : qfalse;
}


static qboolean CL_HudSameDumpGroup( const hudDumpGroup_t *left, const hudDumpGroup_t *right ) {
	const float leftW = left->x2 - left->x1;
	const float leftH = left->y2 - left->y1;
	const float rightW = right->x2 - right->x1;
	const float rightH = right->y2 - right->y1;
	const float leftCx = left->x1 + leftW * 0.5f;
	const float leftCy = left->y1 + leftH * 0.5f;
	const float rightCx = right->x1 + rightW * 0.5f;
	const float rightCy = right->y1 + rightH * 0.5f;

	if ( left->textLike != right->textLike ) {
		return qfalse;
	}

	if ( left->mode != right->mode || left->alignX != right->alignX || left->alignY != right->alignY ) {
		return qfalse;
	}

	if ( Q_stricmp( left->shaderName, right->shaderName ) ) {
		return qfalse;
	}

	if ( fabs( leftCx - rightCx ) > 24.0f || fabs( leftCy - rightCy ) > 24.0f ) {
		return qfalse;
	}

	if ( fabs( leftW - rightW ) > 96.0f || fabs( leftH - rightH ) > 48.0f ) {
		return qfalse;
	}

	return qtrue;
}


static void CL_HudAccumulateDumpGroup( const hudDumpGroup_t *group ) {
	int i;

	for ( i = 0; i < hudDumpGroupCount; i++ ) {
		hudDumpGroup_t *existing = &hudDumpGroups[ i ];

		if ( !CL_HudSameDumpGroup( existing, group ) ) {
			continue;
		}

		if ( group->x1 < existing->x1 ) {
			existing->x1 = group->x1;
		}
		if ( group->y1 < existing->y1 ) {
			existing->y1 = group->y1;
		}
		if ( group->x2 > existing->x2 ) {
			existing->x2 = group->x2;
		}
		if ( group->y2 > existing->y2 ) {
			existing->y2 = group->y2;
		}

		existing->drawCount += group->drawCount;
		existing->samples++;
		hudDumpDirty = qtrue;
		return;
	}

	if ( hudDumpGroupCount >= HUD_MAX_DUMP_GROUPS ) {
		return;
	}

	hudDumpGroups[ hudDumpGroupCount ] = *group;
	hudDumpGroups[ hudDumpGroupCount ].samples = 1;
	hudDumpGroupCount++;
	hudDumpDirty = qtrue;
}


static void CL_HudWriteDumpFile( void ) {
	fileHandle_t file;
	int i;

	if ( !hudDumpDirty ) {
		return;
	}

	file = FS_FOpenFileWrite( FNQ3_HUD_DUMP_FILE );
	if ( file == FS_INVALID_HANDLE ) {
		Com_Printf( S_COLOR_YELLOW "HUD: failed to write dump file %s.\n", FNQ3_HUD_DUMP_FILE );
		return;
	}

	FS_Printf( file, "{\n  \"version\": 1,\n  \"rules\": [\n" );

	for ( i = 0; i < hudDumpGroupCount; i++ ) {
		const hudDumpGroup_t *group = &hudDumpGroups[ i ];

		if ( i ) {
			FS_Printf( file, ",\n" );
		}

		FS_Printf( file, "    {\n" );
		FS_Printf( file, "      \"name\": " );
		CL_HudJsonWriteString( file, va( "hud_%04i", i ) );
		FS_Printf( file, ",\n      \"match\": {\n" );

		if ( group->shaderName[ 0 ] ) {
			FS_Printf( file, "        \"shader\": " );
			CL_HudJsonWriteString( file, group->shaderName );
			FS_Printf( file, ",\n" );
		}

		FS_Printf( file, "        \"textLike\": %i,\n", group->textLike ? 1 : 0 );
		FS_Printf( file, "        \"region\": { \"x\": %.3f, \"y\": %.3f, \"w\": %.3f, \"h\": %.3f }\n",
			group->x1, group->y1, group->x2 - group->x1, group->y2 - group->y1 );
		FS_Printf( file, "      },\n" );

		FS_Printf( file, "      \"mode\": " );
		CL_HudJsonWriteString( file, group->mode == HUD_TRANSFORM_STRETCH ? "stretch" : "uniform" );
		FS_Printf( file, ",\n      \"align\": {\n        \"x\": " );
		CL_HudJsonWriteString( file, CL_HudAlignXName( group->alignX ) );
		FS_Printf( file, ",\n        \"y\": " );
		CL_HudJsonWriteString( file, CL_HudAlignYName( group->alignY ) );
		FS_Printf( file, "\n      },\n" );
		FS_Printf( file, "      \"likelyAligned\": %i,\n", group->likelyAligned ? 1 : 0 );
		FS_Printf( file, "      \"samples\": %i,\n", group->samples );
		FS_Printf( file, "      \"drawCount\": %i\n", group->drawCount );
		FS_Printf( file, "    }" );
	}

	FS_Printf( file, "\n  ]\n}\n" );
	FS_FCloseFile( file );
	hudDumpDirty = qfalse;
}


static void CL_HudDumpFrame( void ) {
	hudDumpGroup_t frameGroups[HUD_MAX_FRAME_GROUPS];
	int frameGroupCount;
	int i;

	if ( hudFrameDrawCount <= 0 ) {
		return;
	}

	qsort( hudFrameDraws, hudFrameDrawCount, sizeof( hudFrameDraws[ 0 ] ), CL_HudCompareDraws );

	frameGroupCount = 0;

	for ( i = 0; i < hudFrameDrawCount; i++ ) {
		const hudDrawCapture_t *draw = &hudFrameDraws[ i ];
		const float drawX2 = draw->x + draw->w;
		const float drawY2 = draw->y + draw->h;
		int j;
		qboolean merged;

		merged = qfalse;

		for ( j = 0; j < frameGroupCount; j++ ) {
			hudDumpGroup_t *group = &frameGroups[ j ];

			if ( !CL_HudCanMergeGroup( draw, group ) ) {
				continue;
			}

			if ( draw->x < group->x1 ) {
				group->x1 = draw->x;
			}
			if ( draw->y < group->y1 ) {
				group->y1 = draw->y;
			}
			if ( drawX2 > group->x2 ) {
				group->x2 = drawX2;
			}
			if ( drawY2 > group->y2 ) {
				group->y2 = drawY2;
			}
			group->drawCount++;
			merged = qtrue;
			break;
		}

		if ( merged || frameGroupCount >= HUD_MAX_FRAME_GROUPS ) {
			continue;
		}

		frameGroups[ frameGroupCount ].x1 = draw->x;
		frameGroups[ frameGroupCount ].y1 = draw->y;
		frameGroups[ frameGroupCount ].x2 = drawX2;
		frameGroups[ frameGroupCount ].y2 = drawY2;
		Q_strncpyz( frameGroups[ frameGroupCount ].shaderName, draw->shaderName, sizeof( frameGroups[ frameGroupCount ].shaderName ) );
		frameGroups[ frameGroupCount ].textLike = draw->textLike;
		frameGroups[ frameGroupCount ].drawCount = 1;
		frameGroups[ frameGroupCount ].samples = 0;
		frameGroupCount++;
	}

	for ( i = 0; i < frameGroupCount; i++ ) {
		CL_HudSuggestPlacement( &frameGroups[ i ] );
		CL_HudAccumulateDumpGroup( &frameGroups[ i ] );
	}

	CL_HudWriteDumpFile();
}


void CL_HudInit( void ) {
	hudLastAspectMode = -1;
	hudDumpPrevValue = cl_hudDump ? cl_hudDump->integer : 0;
	hudNumShaderNames = 0;
	hudNumRules = 0;
	hudScriptLoaded = qfalse;
	hudScriptWarned = qfalse;
	hudFrameActive = qfalse;
	hudFrameDrawCount = 0;
	hudDumpGroupCount = 0;
	hudDumpDirty = qfalse;
	Cmd_AddCommand( "hud_reload", CL_HudReload_f );
}


void CL_HudShutdown( void ) {
	Cmd_RemoveCommand( "hud_reload" );
}


void CL_HudResetCGame( void ) {
	hudNumShaderNames = 0;
	hudFrameActive = qfalse;
	hudFrameDrawCount = 0;
}


void CL_HudBeginFrame( void ) {
	if ( cl_hudDump && cl_hudDump->integer != hudDumpPrevValue ) {
		if ( cl_hudDump->integer ) {
			hudDumpGroupCount = 0;
			hudDumpDirty = qtrue;
		}
		hudDumpPrevValue = cl_hudDump->integer;
	}

	hudFrameActive = qtrue;
	hudFrameDrawCount = 0;
	CL_HudEnsureRules();
}


void CL_HudEndFrame( void ) {
	if ( hudFrameActive && cl_hudDump && cl_hudDump->integer ) {
		CL_HudDumpFrame();
	}

	hudFrameActive = qfalse;
	hudFrameDrawCount = 0;
}


void CL_HudRegisterShaderName( qhandle_t shader, const char *name ) {
	int i;

	if ( shader <= 0 || !name || !*name ) {
		return;
	}

	for ( i = 0; i < hudNumShaderNames; i++ ) {
		if ( hudShaderNames[ i ].handle == shader ) {
			Q_strncpyz( hudShaderNames[ i ].name, name, sizeof( hudShaderNames[ i ].name ) );
			return;
		}
	}

	if ( hudNumShaderNames >= HUD_MAX_SHADER_NAMES ) {
		return;
	}

	hudShaderNames[ hudNumShaderNames ].handle = shader;
	Q_strncpyz( hudShaderNames[ hudNumShaderNames ].name, name, sizeof( hudShaderNames[ hudNumShaderNames ].name ) );
	hudNumShaderNames++;
}


void CL_HudDrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader ) {
	hudDrawCapture_t draw;
	hudTransformMode_t mode;
	hudAlignX_t alignX;
	hudAlignY_t alignY;
	float pixelX;
	float pixelY;
	float pixelW;
	float pixelH;

	pixelX = x;
	pixelY = y;
	pixelW = w;
	pixelH = h;

	CL_HudUnstretch( pixelX, pixelY, pixelW, pixelH, &draw.x, &draw.y, &draw.w, &draw.h );
	draw.s1 = s1;
	draw.t1 = t1;
	draw.s2 = s2;
	draw.t2 = t2;
	draw.shader = shader;
	Q_strncpyz( draw.shaderName, CL_HudLookupShaderName( shader ), sizeof( draw.shaderName ) );
	draw.textLike = CL_HudIsTextLike( shader, draw.shaderName, w, h );

	CL_HudCaptureDraw( &draw );

	if ( !cl_hudAspect || cl_hudAspect->integer <= 0 ) {
		re.DrawStretchPic( pixelX, pixelY, pixelW, pixelH, s1, t1, s2, t2, shader );
		return;
	}

	mode = CL_HudIsFullscreenLike( draw.x, draw.y, draw.w, draw.h ) ? HUD_TRANSFORM_STRETCH : HUD_TRANSFORM_UNIFORM;
	alignX = HUD_ALIGN_CENTER;
	alignY = HUD_ALIGN_MIDDLE;

	if ( hudScriptLoaded && hudNumRules > 0 ) {
		const hudRule_t *rule;

		rule = CL_HudFindRule( &draw );
		if ( rule ) {
			mode = rule->mode;
			if ( rule->hasAlignX ) {
				alignX = rule->alignX;
			}
			if ( rule->hasAlignY ) {
				alignY = rule->alignY;
			}
		}
	}

	if ( mode == HUD_TRANSFORM_STRETCH ) {
		x = draw.x;
		y = draw.y;
		w = draw.w;
		h = draw.h;
		CL_HudApplyStretch( &x, &y, &w, &h );
	} else {
		x = draw.x;
		y = draw.y;
		w = draw.w;
		h = draw.h;
		CL_HudApplyUniform( &x, &y, &w, &h, alignX, alignY );
	}

	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
}
