/*
===========================================================================
Copyright (C) 2026

This file is part of FnQuake3.

FnQuake3 is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/

extern "C" {
#include "../client/snd_local.h"
#include "../client/client.h"
#include "../client/snd_codec.h"
#include "../qcommon/cm_public.h"
}

#include "../openal/include/AL/al.h"
#include "../openal/include/AL/alc.h"
#include "../openal/include/AL/alext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kMaxVoices = MAX_CHANNELS;
constexpr int kReservedLoopFloor = 12;
constexpr int kInitialStreamBuffers = 48;
constexpr int kQueuedStreamChunks = 4;
constexpr int kStreamRate = 44100;
constexpr int kStreamChannels = 2;
constexpr int kEnvironmentProbeIntervalMs = 200;
constexpr int kVoiceEnvironmentIntervalMs = 75;
constexpr float kEnvironmentProbeDistance = 768.0f;
constexpr float kDiagonalProbeComponent = 0.70710677f;
constexpr float kPanScale = 2.0f;
constexpr float kDopplerSpeedOfSound = 6000.0f;
constexpr int kOcclusionMask = CONTENTS_SOLID | CONTENTS_SLIME | CONTENTS_LAVA;
constexpr int kLiquidContents = CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA;

cvar_t *s_alReverb = nullptr;
cvar_t *s_alOcclusion = nullptr;
cvar_t *s_alReverbGain = nullptr;
cvar_t *s_alOcclusionStrength = nullptr;
cvar_t *s_alDebugOverlay = nullptr;
cvar_t *s_alDebugVoice = nullptr;

struct Vec3f {
	float v[3];

	Vec3f() : v{ 0.0f, 0.0f, 0.0f } {}

	explicit Vec3f( const float *src ) : v{ 0.0f, 0.0f, 0.0f } {
		if ( src != nullptr ) {
			VectorCopy( src, v );
		}
	}

	void Set( const float *src ) {
		if ( src != nullptr ) {
			VectorCopy( src, v );
		} else {
			VectorClear( v );
		}
	}

	const float *Data() const { return v; }
	float *Data() { return v; }
};

static int ClampInt( int value, int minimum, int maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

static float ClampFloat( float value, float minimum, float maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

static std::string NormalizeSoundName( const char *name ) {
	std::string normalized = name != nullptr ? name : "";
	for ( char &ch : normalized ) {
		if ( ch == '\\' ) {
			ch = '/';
		} else if ( ch >= 'A' && ch <= 'Z' ) {
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
	}
	return normalized;
}

static std::string SafeString( const char *value ) {
	return value != nullptr ? std::string( value ) : std::string();
}

struct ReverbPreset {
	const char *name;
	float density;
	float diffusion;
	float gain;
	float gainHF;
	float decayTime;
	float decayHFRatio;
	float reflectionsGain;
	float reflectionsDelay;
	float lateReverbGain;
	float lateReverbDelay;
	float airAbsorptionGainHF;
	float roomRolloffFactor;
	ALint decayHFLimit;
	float baseWet;
	float directHF;
	float wetHF;
};

static constexpr ReverbPreset kReverbPresets[] = {
	{ "small-room", 0.70f, 0.60f, 0.28f, 0.89f, 0.90f, 0.78f, 0.20f, 0.002f, 1.10f, 0.011f, 0.994f, 0.0f, AL_TRUE, 0.18f, 0.95f, 1.00f },
	{ "room", 0.80f, 0.70f, 0.32f, 0.70f, 1.40f, 0.83f, 0.25f, 0.003f, 1.20f, 0.016f, 0.994f, 0.0f, AL_TRUE, 0.22f, 1.00f, 1.00f },
	{ "stone-room", 1.00f, 0.85f, 0.36f, 0.50f, 2.20f, 0.72f, 0.45f, 0.012f, 1.45f, 0.028f, 0.993f, 0.0f, AL_TRUE, 0.28f, 0.95f, 0.95f },
	{ "hallway", 1.00f, 0.78f, 0.33f, 0.59f, 1.80f, 0.69f, 0.42f, 0.008f, 1.35f, 0.019f, 0.994f, 0.0f, AL_TRUE, 0.24f, 0.95f, 0.95f },
	{ "hall", 1.00f, 0.92f, 0.35f, 0.66f, 3.10f, 0.78f, 0.50f, 0.020f, 1.55f, 0.036f, 0.993f, 0.0f, AL_TRUE, 0.34f, 1.00f, 1.00f },
	{ "outdoors", 0.25f, 0.30f, 0.18f, 0.99f, 1.60f, 0.85f, 0.05f, 0.007f, 0.28f, 0.011f, 0.999f, 0.0f, AL_TRUE, 0.10f, 1.00f, 1.00f },
	{ "underwater", 1.00f, 1.00f, 0.20f, 0.01f, 1.50f, 0.10f, 0.59f, 0.007f, 1.18f, 0.011f, 0.994f, 0.0f, AL_TRUE, 0.45f, 0.25f, 0.30f }
};

struct EnvironmentState {
	int presetIndex = 0;
	const char *name = "small-room";
	float baseWet = 0.18f;
	float directHF = 0.95f;
	float wetHF = 1.0f;
	qboolean outdoors = qfalse;
	qboolean underwater = qfalse;
};

struct ProbeResult {
	float distance = 0.0f;
	qboolean blocked = qfalse;
	qboolean hitSky = qfalse;
};

static float DistanceBetweenPoints( const float *a, const float *b ) {
	if ( a == nullptr || b == nullptr ) {
		return 0.0f;
	}

	vec3_t delta;
	VectorSubtract( a, b, delta );
	return VectorLength( delta );
}

static qboolean CollisionWorldReady( void ) {
	return ( CM_NumInlineModels() > 0 ) ? qtrue : qfalse;
}

static ProbeResult ProbeEnvironment( const float *origin, const vec3_t dir, float maxDistance ) {
	ProbeResult result;
	result.distance = maxDistance;

	if ( origin == nullptr || !CollisionWorldReady() ) {
		return result;
	}

	trace_t trace;
	vec3_t end;
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	VectorMA( origin, maxDistance, dir, end );
	CM_BoxTrace( &trace, origin, end, zero, zero, 0, kOcclusionMask, qfalse );
	if ( trace.allsolid ) {
		result.distance = 0.0f;
		result.blocked = qtrue;
		return result;
	}

	result.distance = ClampFloat( trace.fraction, 0.0f, 1.0f ) * maxDistance;
	if ( trace.startsolid || trace.fraction < 1.0f ) {
		result.blocked = qtrue;
		result.hitSky = ( trace.surfaceFlags & SURF_SKY ) ? qtrue : qfalse;
	}

	return result;
}

static EnvironmentState EvaluateListenerEnvironment( const float *listenerOrigin ) {
	EnvironmentState state;
	if ( listenerOrigin == nullptr || !CollisionWorldReady() ) {
		return state;
	}

	const int contents = CM_PointContents( listenerOrigin, 0 );
	if ( contents & kLiquidContents ) {
		const ReverbPreset &preset = kReverbPresets[6];
		state.presetIndex = 6;
		state.name = preset.name;
		state.baseWet = preset.baseWet;
		state.directHF = preset.directHF;
		state.wetHF = preset.wetHF;
		state.underwater = qtrue;
		return state;
	}

	const vec3_t probes[6] = {
		{ 1.0f, 0.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, -1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, -1.0f }
	};

	float distances[6];
	for ( int i = 0; i < 6; ++i ) {
		distances[i] = ProbeEnvironment( listenerOrigin, probes[i], kEnvironmentProbeDistance ).distance;
	}

	const vec3_t skyProbes[5] = {
		{ 0.0f, 0.0f, 1.0f },
		{ kDiagonalProbeComponent, 0.0f, kDiagonalProbeComponent },
		{ -kDiagonalProbeComponent, 0.0f, kDiagonalProbeComponent },
		{ 0.0f, kDiagonalProbeComponent, kDiagonalProbeComponent },
		{ 0.0f, -kDiagonalProbeComponent, kDiagonalProbeComponent }
	};

	int skyHits = 0;
	int skyOpenings = 0;
	for ( const vec3_t &probe : skyProbes ) {
		const ProbeResult result = ProbeEnvironment( listenerOrigin, probe, kEnvironmentProbeDistance );
		if ( result.hitSky ) {
			++skyHits;
		}
		if ( result.hitSky || !result.blocked || result.distance > 512.0f ) {
			++skyOpenings;
		}
	}

	const float horizontalAverage = ( distances[0] + distances[1] + distances[2] + distances[3] ) * 0.25f;
	const float horizontalMin = ( std::min )( ( std::min )( distances[0], distances[1] ), ( std::min )( distances[2], distances[3] ) );
	const float horizontalMax = ( std::max )( ( std::max )( distances[0], distances[1] ), ( std::max )( distances[2], distances[3] ) );
	const float ceilingDistance = distances[4];
	const bool skyDominated = ( skyHits >= 3 ) || ( skyHits >= 2 && skyOpenings >= 4 );
	const bool outdoors = skyDominated || ( skyOpenings >= 4 && horizontalAverage > 192.0f && ceilingDistance > 256.0f );
	const bool hallway = ( horizontalMin > 1.0f && ( horizontalMax / horizontalMin ) > 1.85f && horizontalAverage < 320.0f );

	int presetIndex = 0;
	if ( outdoors ) {
		presetIndex = 5;
	} else if ( hallway ) {
		presetIndex = 3;
	} else if ( horizontalAverage < 110.0f ) {
		presetIndex = 0;
	} else if ( horizontalAverage < 220.0f ) {
		presetIndex = 1;
	} else if ( horizontalAverage < 360.0f ) {
		presetIndex = 2;
	} else {
		presetIndex = 4;
	}

	const ReverbPreset &preset = kReverbPresets[presetIndex];
	const float reverbGain = ( s_alReverbGain != nullptr ) ? s_alReverbGain->value : 1.0f;
	state.presetIndex = presetIndex;
	state.name = preset.name;
	state.baseWet = ClampFloat( preset.baseWet * reverbGain, 0.0f, 1.0f );
	state.directHF = preset.directHF;
	state.wetHF = preset.wetHF;
	state.outdoors = outdoors ? qtrue : qfalse;
	return state;
}

static qboolean TraceBlocked( const float *listenerOrigin, const float *sourceOrigin ) {
	if ( !CollisionWorldReady() ) {
		return qfalse;
	}

	trace_t trace;
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	CM_BoxTrace( &trace, listenerOrigin, sourceOrigin, zero, zero, 0, kOcclusionMask, qfalse );
	return ( trace.allsolid || trace.startsolid || trace.fraction < 0.999f ) ? qtrue : qfalse;
}

static float ComputeOcclusionFactor( const float *listenerOrigin, const float *sourceOrigin ) {
	if ( listenerOrigin == nullptr || sourceOrigin == nullptr || !CollisionWorldReady() ||
		( s_alOcclusion != nullptr && s_alOcclusion->integer == 0 ) ) {
		return 0.0f;
	}

	vec3_t toSource;
	VectorSubtract( sourceOrigin, listenerOrigin, toSource );
	const float distance = VectorNormalize( toSource );
	if ( distance < 64.0f ) {
		return 0.0f;
	}

	vec3_t worldUp = { 0.0f, 0.0f, 1.0f };
	vec3_t right;
	vec3_t vertical;
	CrossProduct( toSource, worldUp, right );
	if ( VectorNormalize( right ) == 0.0f ) {
		right[0] = 1.0f;
		right[1] = 0.0f;
		right[2] = 0.0f;
	}
	CrossProduct( right, toSource, vertical );
	if ( VectorNormalize( vertical ) == 0.0f ) {
		vertical[0] = 0.0f;
		vertical[1] = 0.0f;
		vertical[2] = 1.0f;
	}

	int blocked = TraceBlocked( listenerOrigin, sourceOrigin ) ? 1 : 0;
	int total = 1;

	if ( distance > 96.0f ) {
		const float spread = ClampFloat( distance * 0.04f, 10.0f, 28.0f );
		vec3_t shifted;

		VectorMA( sourceOrigin, spread, right, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, -spread, right, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, spread, vertical, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, -spread, vertical, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		total = 5;
	}

	float occlusion = static_cast<float>( blocked ) / static_cast<float>( total );
	if ( blocked > 0 && blocked < total ) {
		occlusion = 0.15f + occlusion * 0.85f;
	}

	const float strength = ( s_alOcclusionStrength != nullptr ) ? s_alOcclusionStrength->value : 1.0f;
	return ClampFloat( occlusion * strength, 0.0f, 1.0f );
}

static float ComputeDopplerPitch( const float *listenerOrigin, const float *sourceOrigin, const float *sourceVelocity ) {
	if ( listenerOrigin == nullptr || sourceOrigin == nullptr || sourceVelocity == nullptr ) {
		return 1.0f;
	}

	vec3_t toListener;
	VectorSubtract( listenerOrigin, sourceOrigin, toListener );
	if ( VectorNormalize( toListener ) == 0.0f ) {
		return 1.0f;
	}

	const float radialVelocity = DotProduct( sourceVelocity, toListener );
	const float denominator = ClampFloat( kDopplerSpeedOfSound - radialVelocity, kDopplerSpeedOfSound * 0.6f, kDopplerSpeedOfSound * 1.4f );
	const float pitch = kDopplerSpeedOfSound / denominator;
	return ClampFloat( pitch, 0.85f, 1.15f );
}

class OpenALLoader {
public:
	bool Load();
	void Unload();
	bool Ready() const { return handle_ != nullptr; }
	const std::string &LibraryName() const { return libraryName_; }

#define FNQ3_AL_SYMBOLS(X) \
	X(alGetError) \
	X(alGetProcAddress) \
	X(alGenBuffers) \
	X(alDeleteBuffers) \
	X(alBufferData) \
	X(alGenSources) \
	X(alDeleteSources) \
	X(alSourcePlay) \
	X(alSourceStop) \
	X(alSourcePause) \
	X(alSourcei) \
	X(alSource3i) \
	X(alSourcef) \
	X(alSource3f) \
	X(alSourceQueueBuffers) \
	X(alSourceUnqueueBuffers) \
	X(alGetSourcei) \
	X(alListenerf) \
	X(alListener3f) \
	X(alListenerfv) \
	X(alDistanceModel) \
	X(alDopplerFactor)

#define FNQ3_ALC_SYMBOLS(X) \
	X(alcOpenDevice) \
	X(alcCloseDevice) \
	X(alcCreateContext) \
	X(alcDestroyContext) \
	X(alcMakeContextCurrent) \
	X(alcGetError) \
	X(alcGetIntegerv) \
	X(alcIsExtensionPresent) \
	X(alcGetString)

#define FNQ3_DECLARE_SYMBOL(name) decltype(&::name) name = nullptr;
	FNQ3_AL_SYMBOLS(FNQ3_DECLARE_SYMBOL)
	FNQ3_ALC_SYMBOLS(FNQ3_DECLARE_SYMBOL)
#undef FNQ3_DECLARE_SYMBOL

private:
	void *handle_ = nullptr;
	std::string libraryName_;
};

bool OpenALLoader::Load() {
	if ( handle_ != nullptr ) {
		return true;
	}

#if defined(_WIN32)
	std::vector<std::string> candidateLibraries;
	const char *binaryPath = Sys_Pwd();
	if ( binaryPath != nullptr && binaryPath[0] != '\0' ) {
		const std::string binaryDir = binaryPath;
		candidateLibraries.push_back( binaryDir + "\\OpenAL32.dll" );
#if defined(_M_ARM64)
		// No bundled ARM64 OpenAL runtime is staged currently.
#elif defined(_WIN64)
		candidateLibraries.push_back( binaryDir + "\\..\\..\\..\\openal\\windows\\x64\\OpenAL32.dll" );
#else
		candidateLibraries.push_back( binaryDir + "\\..\\..\\..\\openal\\windows\\x86\\OpenAL32.dll" );
#endif
	}
	candidateLibraries.push_back( "OpenAL32.dll" );
#elif defined(__APPLE__)
	const char *candidateLibraries[] = {
		"/System/Library/Frameworks/OpenAL.framework/OpenAL",
		"OpenAL.framework/OpenAL",
		"libopenal.1.dylib",
		"libopenal.dylib",
		nullptr
	};
#else
	const char *candidateLibraries[] = { "libopenal.so.1", "libopenal.so", nullptr };
#endif

#if defined(_WIN32)
	for ( const std::string &candidate : candidateLibraries ) {
		handle_ = Sys_LoadLibrary( candidate.c_str() );
		if ( handle_ != nullptr ) {
			libraryName_ = candidate;
			break;
		}
	}
#else
	for ( int i = 0; candidateLibraries[i] != nullptr; ++i ) {
		handle_ = Sys_LoadLibrary( candidateLibraries[i] );
		if ( handle_ != nullptr ) {
			libraryName_ = candidateLibraries[i];
			break;
		}
	}
#endif

	if ( handle_ == nullptr ) {
		return false;
	}

	bool ok = true;

#define FNQ3_LOAD_SYMBOL(name) \
	name = reinterpret_cast<decltype(name)>( Sys_LoadFunction( handle_, #name ) ); \
	ok = ok && ( name != nullptr );
	FNQ3_AL_SYMBOLS(FNQ3_LOAD_SYMBOL)
	FNQ3_ALC_SYMBOLS(FNQ3_LOAD_SYMBOL)
#undef FNQ3_LOAD_SYMBOL

	if ( !ok ) {
		Unload();
	}

	return ok;
}

void OpenALLoader::Unload() {
	if ( handle_ != nullptr ) {
		Sys_UnloadLibrary( handle_ );
	}
	handle_ = nullptr;
	libraryName_.clear();

#define FNQ3_CLEAR_SYMBOL(name) name = nullptr;
	FNQ3_AL_SYMBOLS(FNQ3_CLEAR_SYMBOL)
	FNQ3_ALC_SYMBOLS(FNQ3_CLEAR_SYMBOL)
#undef FNQ3_CLEAR_SYMBOL
}

class StreamBufferPool;

class OpenALDevice {
public:
	bool Init();
	void Shutdown();

	OpenALLoader &AL() { return loader_; }
	const OpenALLoader &AL() const { return loader_; }
	bool Ready() const { return context_ != nullptr; }

	ALuint AcquireVoiceSource();
	void ReleaseVoiceSource( ALuint source );

	ALuint MusicSource() const { return musicSource_; }
	ALuint RawSource() const { return rawSource_; }

	int FreeVoiceCount() const { return static_cast<int>( freeVoiceSources_.size() ); }
	int TotalVoiceCount() const { return static_cast<int>( allVoiceSources_.size() ); }

	const std::string &RequestedDeviceName() const { return requestedDeviceName_; }
	const std::string &ActiveDeviceName() const { return activeDeviceName_; }
	const std::string &LibraryName() const { return loader_.LibraryName(); }
	bool UsingDefaultFallback() const { return usingDefaultFallback_; }
	bool HasEFX() const { return efxAvailable_; }
	bool HasReverb() const { return reverbEnabled_; }
	int MaxAuxiliarySends() const { return maxAuxSends_; }
	const char *CurrentReverbName() const { return currentReverbName_; }
	void SetMasterGain( float gain );

	StreamBufferPool &BufferPool();
	bool CreateVoiceFilters( ALuint &directFilter, ALuint &sendFilter );
	void DestroyVoiceFilters( ALuint &directFilter, ALuint &sendFilter );
	void ApplyVoiceRouting( ALuint source, ALuint directFilter, ALuint sendFilter, float sourceGain, float dryGain, float dryGainHF, float wetGain, float wetGainHF ) const;
	void UpdateReverb( const EnvironmentState &environment );

private:
	bool CreateSource( ALuint &sourceOut );
	void ResetSource( ALuint source ) const;
	bool InitEFX();
	void ShutdownEFX();
	bool CreateLowPassFilter( ALuint &filter );
	void DestroyFilter( ALuint &filter );

	OpenALLoader loader_;
	ALCdevice *device_ = nullptr;
	ALCcontext *context_ = nullptr;
	ALuint musicSource_ = 0;
	ALuint rawSource_ = 0;
	std::vector<ALuint> allVoiceSources_;
	std::vector<ALuint> freeVoiceSources_;
	std::string requestedDeviceName_;
	std::string activeDeviceName_;
	bool usingDefaultFallback_ = false;
	bool efxAvailable_ = false;
	bool filterAvailable_ = false;
	bool reverbEnabled_ = false;
	int maxAuxSends_ = 0;
	ALuint auxEffectSlot_ = 0;
	ALuint reverbEffect_ = 0;
	int activeReverbPreset_ = -1;
	const char *currentReverbName_ = "disabled";
	LPALGENEFFECTS alGenEffects_ = nullptr;
	LPALDELETEEFFECTS alDeleteEffects_ = nullptr;
	LPALEFFECTI alEffecti_ = nullptr;
	LPALEFFECTF alEffectf_ = nullptr;
	LPALGENFILTERS alGenFilters_ = nullptr;
	LPALDELETEFILTERS alDeleteFilters_ = nullptr;
	LPALFILTERI alFilteri_ = nullptr;
	LPALFILTERF alFilterf_ = nullptr;
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots_ = nullptr;
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots_ = nullptr;
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti_ = nullptr;
	StreamBufferPool *bufferPool_ = nullptr;
};

class StreamBufferPool {
public:
	bool Init( OpenALDevice *device, int initialCount );
	void Shutdown();
	ALuint Acquire();
	void Release( ALuint buffer );
	int FreeCount() const { return static_cast<int>( freeBuffers_.size() ); }
	int TotalCount() const { return static_cast<int>( allBuffers_.size() ); }

private:
	OpenALDevice *device_ = nullptr;
	std::vector<ALuint> allBuffers_;
	std::vector<ALuint> freeBuffers_;
};

StreamBufferPool &OpenALDevice::BufferPool() {
	return *bufferPool_;
}

bool OpenALDevice::CreateSource( ALuint &sourceOut ) {
	sourceOut = 0;
	if ( !loader_.Ready() ) {
		return false;
	}

	loader_.alGenSources( 1, &sourceOut );
	if ( loader_.alGetError() != AL_NO_ERROR || sourceOut == 0 ) {
		sourceOut = 0;
		return false;
	}

	ResetSource( sourceOut );
	return true;
}

void OpenALDevice::ResetSource( ALuint source ) const {
	loader_.alSourceStop( source );
	loader_.alSourcei( source, AL_BUFFER, 0 );
	loader_.alSourcei( source, AL_LOOPING, AL_FALSE );
	if ( efxAvailable_ ) {
		loader_.alSourcei( source, AL_DIRECT_FILTER, AL_FILTER_NULL );
		loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}
	loader_.alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
	loader_.alSourcef( source, AL_GAIN, 1.0f );
	loader_.alSourcef( source, AL_PITCH, 1.0f );
	loader_.alSourcef( source, AL_ROLLOFF_FACTOR, 0.0f );
	loader_.alSource3f( source, AL_POSITION, 0.0f, 0.0f, -1.0f );
	loader_.alSource3f( source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	loader_.alGetError();
}

bool OpenALDevice::InitEFX() {
	efxAvailable_ = false;
	filterAvailable_ = false;
	reverbEnabled_ = false;
	maxAuxSends_ = 0;
	auxEffectSlot_ = 0;
	reverbEffect_ = 0;
	activeReverbPreset_ = -1;
	currentReverbName_ = "disabled";

	if ( loader_.alcIsExtensionPresent == nullptr || loader_.alGetProcAddress == nullptr || device_ == nullptr ) {
		return true;
	}

	if ( loader_.alcIsExtensionPresent( device_, ALC_EXT_EFX_NAME ) != ALC_TRUE ) {
		return true;
	}

#define FNQ3_LOAD_EFX_PROC(member, name) \
	member = reinterpret_cast<decltype(member)>( loader_.alGetProcAddress( name ) );
	FNQ3_LOAD_EFX_PROC( alGenFilters_, "alGenFilters" )
	FNQ3_LOAD_EFX_PROC( alDeleteFilters_, "alDeleteFilters" )
	FNQ3_LOAD_EFX_PROC( alFilteri_, "alFilteri" )
	FNQ3_LOAD_EFX_PROC( alFilterf_, "alFilterf" )
	FNQ3_LOAD_EFX_PROC( alGenEffects_, "alGenEffects" )
	FNQ3_LOAD_EFX_PROC( alDeleteEffects_, "alDeleteEffects" )
	FNQ3_LOAD_EFX_PROC( alEffecti_, "alEffecti" )
	FNQ3_LOAD_EFX_PROC( alEffectf_, "alEffectf" )
	FNQ3_LOAD_EFX_PROC( alGenAuxiliaryEffectSlots_, "alGenAuxiliaryEffectSlots" )
	FNQ3_LOAD_EFX_PROC( alDeleteAuxiliaryEffectSlots_, "alDeleteAuxiliaryEffectSlots" )
	FNQ3_LOAD_EFX_PROC( alAuxiliaryEffectSloti_, "alAuxiliaryEffectSloti" )
#undef FNQ3_LOAD_EFX_PROC

	efxAvailable_ = true;
	filterAvailable_ = ( alGenFilters_ != nullptr && alDeleteFilters_ != nullptr && alFilteri_ != nullptr && alFilterf_ != nullptr );

	if ( alGenEffects_ == nullptr || alDeleteEffects_ == nullptr || alEffecti_ == nullptr || alEffectf_ == nullptr ||
		alGenAuxiliaryEffectSlots_ == nullptr || alDeleteAuxiliaryEffectSlots_ == nullptr || alAuxiliaryEffectSloti_ == nullptr ) {
		return true;
	}

	loader_.alcGetIntegerv( device_, ALC_MAX_AUXILIARY_SENDS, 1, &maxAuxSends_ );
	if ( maxAuxSends_ < 1 || s_alReverb == nullptr || s_alReverb->integer == 0 ) {
		return true;
	}

	alGenEffects_( 1, &reverbEffect_ );
	if ( loader_.alGetError() != AL_NO_ERROR || reverbEffect_ == 0 ) {
		reverbEffect_ = 0;
		return true;
	}

	alEffecti_( reverbEffect_, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		alDeleteEffects_( 1, &reverbEffect_ );
		reverbEffect_ = 0;
		return true;
	}

	alGenAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
	if ( loader_.alGetError() != AL_NO_ERROR || auxEffectSlot_ == 0 ) {
		auxEffectSlot_ = 0;
		alDeleteEffects_( 1, &reverbEffect_ );
		reverbEffect_ = 0;
		return true;
	}

	alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, reverbEffect_ );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		alDeleteAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
		auxEffectSlot_ = 0;
		alDeleteEffects_( 1, &reverbEffect_ );
		reverbEffect_ = 0;
		return true;
	}

	reverbEnabled_ = true;
	currentReverbName_ = "pending";
	return true;
}

void OpenALDevice::ShutdownEFX() {
	if ( reverbEnabled_ && alAuxiliaryEffectSloti_ != nullptr && auxEffectSlot_ != 0 ) {
		alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
	}
	if ( auxEffectSlot_ != 0 && alDeleteAuxiliaryEffectSlots_ != nullptr ) {
		alDeleteAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
	}
	if ( reverbEffect_ != 0 && alDeleteEffects_ != nullptr ) {
		alDeleteEffects_( 1, &reverbEffect_ );
	}

	auxEffectSlot_ = 0;
	reverbEffect_ = 0;
	efxAvailable_ = false;
	filterAvailable_ = false;
	reverbEnabled_ = false;
	maxAuxSends_ = 0;
	activeReverbPreset_ = -1;
	currentReverbName_ = "disabled";
	alGenEffects_ = nullptr;
	alDeleteEffects_ = nullptr;
	alEffecti_ = nullptr;
	alEffectf_ = nullptr;
	alGenFilters_ = nullptr;
	alDeleteFilters_ = nullptr;
	alFilteri_ = nullptr;
	alFilterf_ = nullptr;
	alGenAuxiliaryEffectSlots_ = nullptr;
	alDeleteAuxiliaryEffectSlots_ = nullptr;
	alAuxiliaryEffectSloti_ = nullptr;
}

bool OpenALDevice::CreateLowPassFilter( ALuint &filter ) {
	filter = 0;
	if ( !filterAvailable_ || alGenFilters_ == nullptr || alFilteri_ == nullptr || alFilterf_ == nullptr ) {
		return false;
	}

	alGenFilters_( 1, &filter );
	if ( loader_.alGetError() != AL_NO_ERROR || filter == 0 ) {
		filter = 0;
		return false;
	}

	alFilteri_( filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	alFilterf_( filter, AL_LOWPASS_GAIN, 1.0f );
	alFilterf_( filter, AL_LOWPASS_GAINHF, 1.0f );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		DestroyFilter( filter );
		return false;
	}

	return true;
}

void OpenALDevice::DestroyFilter( ALuint &filter ) {
	if ( filter != 0 && alDeleteFilters_ != nullptr ) {
		alDeleteFilters_( 1, &filter );
	}
	filter = 0;
}

bool OpenALDevice::CreateVoiceFilters( ALuint &directFilter, ALuint &sendFilter ) {
	directFilter = 0;
	sendFilter = 0;
	CreateLowPassFilter( directFilter );
	CreateLowPassFilter( sendFilter );
	return ( directFilter != 0 || sendFilter != 0 || reverbEnabled_ ) ? true : false;
}

void OpenALDevice::DestroyVoiceFilters( ALuint &directFilter, ALuint &sendFilter ) {
	DestroyFilter( directFilter );
	DestroyFilter( sendFilter );
}

void OpenALDevice::ApplyVoiceRouting( ALuint source, ALuint directFilter, ALuint sendFilter, float sourceGain, float dryGain, float dryGainHF, float wetGain, float wetGainHF ) const {
	sourceGain = ClampFloat( sourceGain, 0.0f, 1.0f );
	dryGain = ClampFloat( dryGain, 0.0f, 1.0f );
	dryGainHF = ClampFloat( dryGainHF, 0.0f, 1.0f );
	wetGain = ClampFloat( wetGain, 0.0f, 1.0f );
	wetGainHF = ClampFloat( wetGainHF, 0.0f, 1.0f );

	if ( !efxAvailable_ ) {
		loader_.alSourcef( source, AL_GAIN, sourceGain * dryGain );
		return;
	}

	if ( directFilter != 0 && filterAvailable_ && alFilterf_ != nullptr ) {
		alFilterf_( directFilter, AL_LOWPASS_GAIN, dryGain );
		alFilterf_( directFilter, AL_LOWPASS_GAINHF, dryGainHF );
		loader_.alSourcei( source, AL_DIRECT_FILTER, directFilter );
		loader_.alSourcef( source, AL_GAIN, sourceGain );
	} else {
		loader_.alSourcei( source, AL_DIRECT_FILTER, AL_FILTER_NULL );
		loader_.alSourcef( source, AL_GAIN, sourceGain * dryGain );
	}

	if ( reverbEnabled_ && wetGain > 0.001f && auxEffectSlot_ != 0 ) {
		if ( sendFilter != 0 && filterAvailable_ && alFilterf_ != nullptr ) {
		alFilterf_( sendFilter, AL_LOWPASS_GAIN, wetGain );
		alFilterf_( sendFilter, AL_LOWPASS_GAINHF, wetGainHF );
		loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, auxEffectSlot_, 0, sendFilter );
		} else {
			loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, auxEffectSlot_, 0, AL_FILTER_NULL );
		}
	} else {
		loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}
}

void OpenALDevice::SetMasterGain( float gain ) {
	if ( !loader_.Ready() || loader_.alListenerf == nullptr ) {
		return;
	}

	loader_.alListenerf( AL_GAIN, ClampFloat( gain, 0.0f, 1.0f ) );
}

void OpenALDevice::UpdateReverb( const EnvironmentState &environment ) {
	if ( !reverbEnabled_ || reverbEffect_ == 0 || alEffectf_ == nullptr || alAuxiliaryEffectSloti_ == nullptr ) {
		return;
	}
	if ( environment.presetIndex < 0 || environment.presetIndex >= static_cast<int>( sizeof( kReverbPresets ) / sizeof( kReverbPresets[0] ) ) ) {
		return;
	}
	if ( activeReverbPreset_ == environment.presetIndex ) {
		return;
	}

	const ReverbPreset &preset = kReverbPresets[environment.presetIndex];
	alEffectf_( reverbEffect_, AL_REVERB_DENSITY, preset.density );
	alEffectf_( reverbEffect_, AL_REVERB_DIFFUSION, preset.diffusion );
	alEffectf_( reverbEffect_, AL_REVERB_GAIN, preset.gain );
	alEffectf_( reverbEffect_, AL_REVERB_GAINHF, preset.gainHF );
	alEffectf_( reverbEffect_, AL_REVERB_DECAY_TIME, preset.decayTime );
	alEffectf_( reverbEffect_, AL_REVERB_DECAY_HFRATIO, preset.decayHFRatio );
	alEffectf_( reverbEffect_, AL_REVERB_REFLECTIONS_GAIN, preset.reflectionsGain );
	alEffectf_( reverbEffect_, AL_REVERB_REFLECTIONS_DELAY, preset.reflectionsDelay );
	alEffectf_( reverbEffect_, AL_REVERB_LATE_REVERB_GAIN, preset.lateReverbGain );
	alEffectf_( reverbEffect_, AL_REVERB_LATE_REVERB_DELAY, preset.lateReverbDelay );
	alEffectf_( reverbEffect_, AL_REVERB_AIR_ABSORPTION_GAINHF, preset.airAbsorptionGainHF );
	alEffectf_( reverbEffect_, AL_REVERB_ROOM_ROLLOFF_FACTOR, preset.roomRolloffFactor );
	alEffecti_( reverbEffect_, AL_REVERB_DECAY_HFLIMIT, preset.decayHFLimit );
	alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, reverbEffect_ );
	loader_.alGetError();

	activeReverbPreset_ = environment.presetIndex;
	currentReverbName_ = preset.name;
}

bool OpenALDevice::Init() {
	if ( !loader_.Load() ) {
		return false;
	}

	requestedDeviceName_ = ( s_alDevice != nullptr ) ? SafeString( s_alDevice->string ) : std::string();
	usingDefaultFallback_ = false;

	device_ = loader_.alcOpenDevice( requestedDeviceName_.empty() ? nullptr : requestedDeviceName_.c_str() );
	if ( device_ == nullptr && !requestedDeviceName_.empty() ) {
		device_ = loader_.alcOpenDevice( nullptr );
		usingDefaultFallback_ = ( device_ != nullptr );
	}
	if ( device_ == nullptr ) {
		Shutdown();
		return false;
	}

	context_ = loader_.alcCreateContext( device_, nullptr );
	if ( context_ == nullptr || loader_.alcMakeContextCurrent( context_ ) == ALC_FALSE ) {
		Shutdown();
		return false;
	}

	const ALCchar *activeDevice = loader_.alcGetString( device_, ALC_DEVICE_SPECIFIER );
	if ( activeDevice != nullptr ) {
		activeDeviceName_ = reinterpret_cast<const char *>( activeDevice );
	}

	loader_.alDistanceModel( AL_NONE );
	loader_.alDopplerFactor( 0.0f );
	loader_.alListenerf( AL_GAIN, ( s_volume != nullptr ) ? ClampFloat( s_volume->value, 0.0f, 1.0f ) : 1.0f );
	loader_.alListener3f( AL_POSITION, 0.0f, 0.0f, 0.0f );
	const float orientation[6] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
	loader_.alListenerfv( AL_ORIENTATION, orientation );
	InitEFX();

	if ( !CreateSource( musicSource_ ) || !CreateSource( rawSource_ ) ) {
		Shutdown();
		return false;
	}

	for ( int i = 0; i < kMaxVoices + 8; ++i ) {
		ALuint source = 0;
		if ( !CreateSource( source ) ) {
			break;
		}
		allVoiceSources_.push_back( source );
		freeVoiceSources_.push_back( source );
	}

	if ( allVoiceSources_.empty() ) {
		Shutdown();
		return false;
	}

	bufferPool_ = new StreamBufferPool();
	if ( !bufferPool_->Init( this, kInitialStreamBuffers ) ) {
		Shutdown();
		return false;
	}

	return true;
}

void OpenALDevice::Shutdown() {
	if ( bufferPool_ != nullptr ) {
		bufferPool_->Shutdown();
		delete bufferPool_;
		bufferPool_ = nullptr;
	}

	if ( loader_.Ready() ) {
		for ( ALuint source : allVoiceSources_ ) {
			ResetSource( source );
			loader_.alDeleteSources( 1, &source );
		}
		allVoiceSources_.clear();
		freeVoiceSources_.clear();

		if ( musicSource_ != 0 ) {
			ResetSource( musicSource_ );
			loader_.alDeleteSources( 1, &musicSource_ );
			musicSource_ = 0;
		}
		if ( rawSource_ != 0 ) {
			ResetSource( rawSource_ );
			loader_.alDeleteSources( 1, &rawSource_ );
			rawSource_ = 0;
		}

		ShutdownEFX();
	}

	if ( loader_.Ready() && context_ != nullptr ) {
		loader_.alcMakeContextCurrent( nullptr );
		loader_.alcDestroyContext( context_ );
	}
	context_ = nullptr;

	if ( loader_.Ready() && device_ != nullptr ) {
		loader_.alcCloseDevice( device_ );
	}
	device_ = nullptr;

	requestedDeviceName_.clear();
	activeDeviceName_.clear();
	usingDefaultFallback_ = false;

	loader_.Unload();
}

ALuint OpenALDevice::AcquireVoiceSource() {
	if ( freeVoiceSources_.empty() ) {
		return 0;
	}

	const ALuint source = freeVoiceSources_.back();
	freeVoiceSources_.pop_back();
	ResetSource( source );
	return source;
}

void OpenALDevice::ReleaseVoiceSource( ALuint source ) {
	if ( source == 0 ) {
		return;
	}

	ResetSource( source );
	if ( std::find( freeVoiceSources_.begin(), freeVoiceSources_.end(), source ) == freeVoiceSources_.end() ) {
		freeVoiceSources_.push_back( source );
	}
}

bool StreamBufferPool::Init( OpenALDevice *device, int initialCount ) {
	device_ = device;
	for ( int i = 0; i < initialCount; ++i ) {
		ALuint buffer = 0;
		device_->AL().alGenBuffers( 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR || buffer == 0 ) {
			break;
		}
		allBuffers_.push_back( buffer );
		freeBuffers_.push_back( buffer );
	}

	return !allBuffers_.empty();
}

void StreamBufferPool::Shutdown() {
	if ( device_ != nullptr && device_->AL().Ready() ) {
		for ( ALuint buffer : allBuffers_ ) {
			device_->AL().alDeleteBuffers( 1, &buffer );
		}
	}
	allBuffers_.clear();
	freeBuffers_.clear();
	device_ = nullptr;
}

ALuint StreamBufferPool::Acquire() {
	if ( device_ == nullptr ) {
		return 0;
	}

	if ( freeBuffers_.empty() ) {
		ALuint buffer = 0;
		device_->AL().alGenBuffers( 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR || buffer == 0 ) {
			return 0;
		}
		allBuffers_.push_back( buffer );
		freeBuffers_.push_back( buffer );
	}

	const ALuint buffer = freeBuffers_.back();
	freeBuffers_.pop_back();
	return buffer;
}

void StreamBufferPool::Release( ALuint buffer ) {
	if ( buffer == 0 ) {
		return;
	}

	if ( std::find( freeBuffers_.begin(), freeBuffers_.end(), buffer ) == freeBuffers_.end() ) {
		freeBuffers_.push_back( buffer );
	}
}

class SoundSample {
public:
	explicit SoundSample( std::string sampleName )
		: name_( std::move( sampleName ) ) {
	}

	bool EnsureLoaded( OpenALDevice &device, bool allowToneFallback );
	void Unload( OpenALDevice &device );

	const std::string &Name() const { return name_; }
	bool Missing() const { return missing_; }
	bool Loaded() const { return loaded_; }
	bool DefaultSample() const { return defaultSample_; }
	ALuint Buffer() const { return buffer_; }
	int Channels() const { return channels_; }
	int DurationMs() const { return durationMs_; }

private:
	static std::vector<short> ConvertToPCM16( const snd_info_t &info, const byte *data );
	bool UploadPCM( OpenALDevice &device, const std::vector<short> &pcm, int channels, int rate );
	bool GenerateFallbackTone( OpenALDevice &device );

	std::string name_;
	bool loadAttempted_ = false;
	bool loaded_ = false;
	bool missing_ = false;
	bool defaultSample_ = false;
	ALuint buffer_ = 0;
	int channels_ = 0;
	int rate_ = 0;
	int durationMs_ = 0;
};

std::vector<short> SoundSample::ConvertToPCM16( const snd_info_t &info, const byte *data ) {
	std::vector<short> pcm;

	if ( data == nullptr || info.samples <= 0 || info.channels <= 0 ) {
		return pcm;
	}

	const int outputChannels = ClampInt( info.channels, 1, 2 );
	pcm.resize( static_cast<size_t>( info.samples ) * static_cast<size_t>( outputChannels ) );

	for ( int i = 0; i < info.samples; ++i ) {
		for ( int c = 0; c < outputChannels; ++c ) {
			short sample = 0;
			if ( info.width == 2 ) {
				const short *input = reinterpret_cast<const short *>( data );
				sample = LittleShort( input[i * info.channels + c] );
			} else {
				const byte input = data[i * info.channels + c];
				sample = static_cast<short>( ( static_cast<int>( input ) - 128 ) << 8 );
			}
			pcm[static_cast<size_t>( i ) * static_cast<size_t>( outputChannels ) + static_cast<size_t>( c )] = sample;
		}
	}

	return pcm;
}

bool SoundSample::UploadPCM( OpenALDevice &device, const std::vector<short> &pcm, int channels, int rate ) {
	if ( pcm.empty() || ( channels != 1 && channels != 2 ) || rate <= 0 ) {
		return false;
	}

	device.AL().alGenBuffers( 1, &buffer_ );
	if ( device.AL().alGetError() != AL_NO_ERROR || buffer_ == 0 ) {
		buffer_ = 0;
		return false;
	}

	const ALenum format = ( channels == 1 ) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	device.AL().alBufferData( buffer_, format, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), rate );
	if ( device.AL().alGetError() != AL_NO_ERROR ) {
		device.AL().alDeleteBuffers( 1, &buffer_ );
		buffer_ = 0;
		return false;
	}

	channels_ = channels;
	rate_ = rate;
	const int frames = static_cast<int>( pcm.size() / static_cast<size_t>( channels ) );
	durationMs_ = ( frames * 1000 ) / rate;
	return true;
}

bool SoundSample::GenerateFallbackTone( OpenALDevice &device ) {
	const int frames = 22050 / 4;
	std::vector<short> pcm( static_cast<size_t>( frames ) );

	for ( int i = 0; i < frames; ++i ) {
		const float phase = static_cast<float>( i ) * 440.0f * 6.2831853071795864769f / 22050.0f;
		pcm[static_cast<size_t>( i )] = static_cast<short>( std::sin( phase ) * 12000.0f );
	}

	defaultSample_ = true;
	missing_ = false;
	loaded_ = UploadPCM( device, pcm, 1, 22050 );
	durationMs_ = 250;
	return loaded_;
}

bool SoundSample::EnsureLoaded( OpenALDevice &device, bool allowToneFallback ) {
	if ( loaded_ ) {
		return true;
	}
	if ( loadAttempted_ ) {
		return false;
	}

	loadAttempted_ = true;

	snd_info_t info;
	void *data = S_CodecLoad( name_.c_str(), &info );
	if ( data == nullptr ) {
		missing_ = true;
		defaultSample_ = allowToneFallback;
		if ( allowToneFallback ) {
			return GenerateFallbackTone( device );
		}
		return false;
	}

	const std::vector<short> pcm = ConvertToPCM16( info, reinterpret_cast<const byte *>( data ) + info.dataofs );
	Hunk_FreeTempMemory( data );

	if ( pcm.empty() ) {
		missing_ = true;
		defaultSample_ = allowToneFallback;
		if ( allowToneFallback ) {
			return GenerateFallbackTone( device );
		}
		return false;
	}

	missing_ = false;
	defaultSample_ = false;
	loaded_ = UploadPCM( device, pcm, ClampInt( info.channels, 1, 2 ), info.rate );
	return loaded_;
}

void SoundSample::Unload( OpenALDevice &device ) {
	if ( buffer_ != 0 ) {
		device.AL().alDeleteBuffers( 1, &buffer_ );
		buffer_ = 0;
	}
	loadAttempted_ = false;
	loaded_ = false;
	missing_ = false;
	defaultSample_ = false;
	channels_ = 0;
	rate_ = 0;
	durationMs_ = 0;
}

enum class VoiceKind {
	OneShot,
	Loop
};

class SoundVoice {
public:
	VoiceKind kind = VoiceKind::OneShot;
	bool active = false;
	bool fixedOrigin = false;
	bool looping = false;
	bool killWhenUnrefreshed = false;
	bool refreshedThisFrame = false;
	bool doppler = false;
	int handle = 0;
	int entnum = ENTITYNUM_WORLD;
	int entchannel = CHAN_AUTO;
	int allocTime = 0;
	ALuint source = 0;
	ALuint directFilter = 0;
	ALuint sendFilter = 0;
	SoundSample *sample = nullptr;
	Vec3f origin;
	Vec3f velocity;
	float occlusion = 0.0f;
	int nextEnvironmentUpdateTime = 0;
	float debugGain = 0.0f;
	float debugPan = 0.0f;
	float debugDryGain = 1.0f;
	float debugWetGain = 0.0f;
	float debugPitch = 1.0f;
	float debugDistance = 0.0f;

	void Release( OpenALDevice &device ) {
		if ( source != 0 ) {
			device.ReleaseVoiceSource( source );
			source = 0;
		}
		device.DestroyVoiceFilters( directFilter, sendFilter );
		active = false;
		fixedOrigin = false;
		looping = false;
		killWhenUnrefreshed = false;
		refreshedThisFrame = false;
		doppler = false;
		handle = 0;
		entnum = ENTITYNUM_WORLD;
		entchannel = CHAN_AUTO;
		allocTime = 0;
		sample = nullptr;
		origin = Vec3f();
		velocity = Vec3f();
		occlusion = 0.0f;
		nextEnvironmentUpdateTime = 0;
		debugGain = 0.0f;
		debugPan = 0.0f;
		debugDryGain = 1.0f;
		debugWetGain = 0.0f;
		debugPitch = 1.0f;
		debugDistance = 0.0f;
	}
};

struct SpatialParams {
	float gain = 0.0f;
	float pan = 0.0f;
};

static SpatialParams ComputeSpatialParams( const float *origin, const float *listenerOrigin, const float listenerAxis[3][3], int masterVol ) {
	const float soundFullVolume = 80.0f;
	const float soundAttenuate = 0.0008f;
	float sourceVec[3];
	float rotated[3];

	VectorSubtract( origin, listenerOrigin, sourceVec );

	float dist = VectorNormalize( sourceVec );
	dist -= soundFullVolume;
	if ( dist < 0.0f ) {
		dist = 0.0f;
	}
	dist *= soundAttenuate;

	VectorRotate( sourceVec, listenerAxis, rotated );

	float rightScale = 0.5f * ( 1.0f - rotated[1] );
	float leftScale = 0.5f * ( 1.0f + rotated[1] );
	rightScale = ClampFloat( rightScale, 0.0f, 1.0f );
	leftScale = ClampFloat( leftScale, 0.0f, 1.0f );

	const float right = static_cast<float>( masterVol ) * ( 1.0f - dist ) * rightScale;
	const float left = static_cast<float>( masterVol ) * ( 1.0f - dist ) * leftScale;
	const float maxSide = ( std::max )( left, right );
	const float sum = left + right;

	SpatialParams params;
	params.gain = ClampFloat( maxSide / 127.0f, 0.0f, 1.0f );
	if ( sum > 0.0f ) {
		params.pan = ClampFloat( ( right - left ) / sum, -1.0f, 1.0f );
	}
	return params;
}

static bool IsLocalOnlyChannel( int entchannel ) {
	switch ( entchannel ) {
	case CHAN_LOCAL:
	case CHAN_LOCAL_SOUND:
	case CHAN_ANNOUNCER:
		return true;
	default:
		return false;
	}
}

class Q3SoundWorld {
public:
	void Reset( OpenALDevice *device );
	void StopAllSounds();
	void ClearSoundBuffer();
	void ClearLoopingSounds( qboolean killall );
	void StopLoopingSound( int entityNum );
	void AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t handle, SoundSample *sample, qboolean realLoop );
	void StartSound( int entityNum, int entchannel, sfxHandle_t handle, SoundSample *sample, const float *origin );
	void StartLocalSound( sfxHandle_t handle, SoundSample *sample, int channelNum );
	void UpdateEntityPosition( int entityNum, const float *origin );
	void Respatialize( int entityNum, const float *origin, float axis[3][3] );
	void Update( qboolean softMuted );
	qboolean GetSpatialDebugInfo( spatialAudioDebugInfo_t *info, const OpenALDevice &device, int preferredEntity, int overlayMode ) const;
	void DumpSpatialDebug( const OpenALDevice &device, int preferredEntity ) const;

	int ListenerNumber() const { return listenerNumber_; }

private:
	void ApplyVoice( SoundVoice &voice, qboolean softMuted );
	void CleanupFinishedVoices();
	void RefreshEnvironment();
	void UpdateVoiceEnvironment( SoundVoice &voice, const float *voiceOrigin );
	SoundVoice *FindFreeOneShot();
	SoundVoice *FindEvictionCandidate();
	ALuint AllocateVoiceSourceForLoop();
	const SoundVoice *SelectDebugVoice( int preferredEntity ) const;

	OpenALDevice *device_ = nullptr;
	std::array<SoundVoice, kMaxVoices> oneShots_;
	std::array<SoundVoice, MAX_GENTITIES> loopVoices_;
	std::array<Vec3f, MAX_GENTITIES> entityOrigins_;
	int listenerNumber_ = 0;
	Vec3f listenerOrigin_;
	Vec3f lastEnvironmentProbeOrigin_;
	EnvironmentState environment_;
	int nextEnvironmentProbeTime_ = 0;
	float listenerAxis_[3][3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }
	};
};

void Q3SoundWorld::Reset( OpenALDevice *device ) {
	device_ = device;
	listenerNumber_ = 0;
	listenerOrigin_ = Vec3f();
	lastEnvironmentProbeOrigin_ = Vec3f();
	environment_ = EnvironmentState();
	nextEnvironmentProbeTime_ = 0;
	for ( int i = 0; i < 3; ++i ) {
		for ( int j = 0; j < 3; ++j ) {
			listenerAxis_[i][j] = ( i == j ) ? 1.0f : 0.0f;
		}
	}
	for ( SoundVoice &voice : oneShots_ ) {
		voice.Release( *device_ );
	}
	for ( SoundVoice &voice : loopVoices_ ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
	}
	for ( Vec3f &origin : entityOrigins_ ) {
		origin = Vec3f();
	}
	if ( device_ != nullptr ) {
		device_->UpdateReverb( environment_ );
	}
}

void Q3SoundWorld::StopAllSounds() {
	for ( SoundVoice &voice : oneShots_ ) {
		voice.Release( *device_ );
	}
	for ( SoundVoice &voice : loopVoices_ ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
	}
}

void Q3SoundWorld::ClearSoundBuffer() {
	StopAllSounds();
}

void Q3SoundWorld::ClearLoopingSounds( qboolean killall ) {
	for ( SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( killall ) {
			voice.Release( *device_ );
			voice.kind = VoiceKind::Loop;
			continue;
		}
		voice.refreshedThisFrame = qfalse;
	}
}

void Q3SoundWorld::StopLoopingSound( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}
	loopVoices_[entityNum].Release( *device_ );
	loopVoices_[entityNum].kind = VoiceKind::Loop;
}

ALuint Q3SoundWorld::AllocateVoiceSourceForLoop() {
	if ( device_->FreeVoiceCount() <= kReservedLoopFloor ) {
		return 0;
	}
	return device_->AcquireVoiceSource();
}

void Q3SoundWorld::AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t handle, SoundSample *sample, qboolean realLoop ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES || sample == nullptr ) {
		return;
	}

	SoundVoice &voice = loopVoices_[entityNum];
	if ( !voice.active || voice.handle != handle ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
		voice.active = true;
		voice.handle = handle;
		voice.sample = sample;
		voice.entnum = entityNum;
		voice.entchannel = CHAN_AUTO;
		voice.looping = true;
	}

	voice.fixedOrigin = qtrue;
	voice.killWhenUnrefreshed = !realLoop;
	voice.refreshedThisFrame = qtrue;
	voice.origin.Set( origin );
	voice.velocity.Set( velocity );
	voice.doppler = ( s_doppler != nullptr && s_doppler->integer != 0 && VectorLengthSquared( voice.velocity.Data() ) > 0.0f );
	voice.occlusion = 0.0f;
	voice.nextEnvironmentUpdateTime = 0;
}

SoundVoice *Q3SoundWorld::FindFreeOneShot() {
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			return &voice;
		}
	}
	return nullptr;
}

SoundVoice *Q3SoundWorld::FindEvictionCandidate() {
	SoundVoice *chosen = nullptr;

	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active || voice.entnum == listenerNumber_ || voice.entchannel == CHAN_ANNOUNCER ) {
			continue;
		}
		if ( chosen == nullptr || voice.allocTime < chosen->allocTime ) {
			chosen = &voice;
		}
	}

	if ( chosen != nullptr ) {
		return chosen;
	}

	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( chosen == nullptr || voice.allocTime < chosen->allocTime ) {
			chosen = &voice;
		}
	}

	return chosen;
}

void Q3SoundWorld::StartSound( int entityNum, int entchannel, sfxHandle_t handle, SoundSample *sample, const float *origin ) {
	if ( sample == nullptr ) {
		return;
	}

	if ( origin == nullptr && ( entityNum < 0 || entityNum >= MAX_GENTITIES ) ) {
		Com_Error( ERR_DROP, "S_StartSound: bad entitynum %i", entityNum );
	}

	const int startTime = Com_Milliseconds();

	if ( entityNum != ENTITYNUM_WORLD ) {
		for ( SoundVoice &voice : oneShots_ ) {
			if ( !voice.active ) {
				continue;
			}
			if ( voice.entnum == entityNum && voice.allocTime == startTime && voice.handle == handle ) {
				return;
			}
		}
	}

	const int allowed = ( entityNum == listenerNumber_ ) ? 16 : 8;
	int inPlay = 0;
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( voice.entnum == entityNum && voice.handle == handle ) {
			if ( startTime - voice.allocTime < 20 ) {
				return;
			}
			++inPlay;
		}
	}

	if ( inPlay > allowed ) {
		return;
	}

	SoundVoice *voice = FindFreeOneShot();
	if ( voice == nullptr ) {
		voice = FindEvictionCandidate();
		if ( voice == nullptr ) {
			return;
		}
		voice->Release( *device_ );
	}

	if ( voice->source == 0 ) {
		voice->source = device_->AcquireVoiceSource();
		if ( voice->source == 0 ) {
			SoundVoice *candidate = FindEvictionCandidate();
			if ( candidate != nullptr && candidate != voice ) {
				candidate->Release( *device_ );
				voice->source = device_->AcquireVoiceSource();
			}
			if ( voice->source == 0 ) {
				voice->active = qfalse;
				return;
			}
		}
		device_->CreateVoiceFilters( voice->directFilter, voice->sendFilter );
	}

	voice->kind = VoiceKind::OneShot;
	voice->active = qtrue;
	voice->sample = sample;
	voice->handle = handle;
	voice->entnum = entityNum;
	voice->entchannel = entchannel;
	voice->allocTime = startTime;
	voice->fixedOrigin = ( origin != nullptr );
	voice->origin.Set( origin );
	voice->velocity = Vec3f();
	voice->doppler = qfalse;
	voice->looping = qfalse;
	voice->killWhenUnrefreshed = qfalse;
	voice->refreshedThisFrame = qfalse;
	voice->occlusion = 0.0f;
	voice->nextEnvironmentUpdateTime = 0;

	device_->AL().alSourceStop( voice->source );
	device_->AL().alSourcei( voice->source, AL_BUFFER, sample->Buffer() );
	device_->AL().alSourcei( voice->source, AL_LOOPING, AL_FALSE );
	device_->AL().alSourcePlay( voice->source );
	ApplyVoice( *voice, qfalse );
}

void Q3SoundWorld::StartLocalSound( sfxHandle_t handle, SoundSample *sample, int channelNum ) {
	StartSound( listenerNumber_, channelNum, handle, sample, nullptr );
}

void Q3SoundWorld::UpdateEntityPosition( int entityNum, const float *origin ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "S_UpdateEntityPosition: bad entitynum %i", entityNum );
	}
	entityOrigins_[entityNum].Set( origin );
}

void Q3SoundWorld::Respatialize( int entityNum, const float *origin, float axis[3][3] ) {
	listenerNumber_ = entityNum;
	listenerOrigin_.Set( origin );
	for ( int i = 0; i < 3; ++i ) {
		VectorCopy( axis[i], listenerAxis_[i] );
	}
	RefreshEnvironment();
}

void Q3SoundWorld::RefreshEnvironment() {
	if ( device_ == nullptr ) {
		return;
	}

	const int now = Com_Milliseconds();
	const float moved = DistanceBetweenPoints( listenerOrigin_.Data(), lastEnvironmentProbeOrigin_.Data() );
	if ( now < nextEnvironmentProbeTime_ && moved < 48.0f ) {
		return;
	}

	environment_ = EvaluateListenerEnvironment( listenerOrigin_.Data() );
	lastEnvironmentProbeOrigin_ = listenerOrigin_;
	nextEnvironmentProbeTime_ = now + kEnvironmentProbeIntervalMs;
	device_->UpdateReverb( environment_ );
}

void Q3SoundWorld::UpdateVoiceEnvironment( SoundVoice &voice, const float *voiceOrigin ) {
	if ( !voice.active || voiceOrigin == nullptr ) {
		return;
	}

	const int now = Com_Milliseconds();
	if ( now < voice.nextEnvironmentUpdateTime ) {
		return;
	}

	if ( voice.sample == nullptr ) {
		voice.occlusion = 0.0f;
		voice.nextEnvironmentUpdateTime = now + kVoiceEnvironmentIntervalMs;
		return;
	}

	voice.occlusion = ComputeOcclusionFactor( listenerOrigin_.Data(), voiceOrigin );
	voice.nextEnvironmentUpdateTime = now + kVoiceEnvironmentIntervalMs;
}

void Q3SoundWorld::ApplyVoice( SoundVoice &voice, qboolean softMuted ) {
	if ( !voice.active || voice.source == 0 || voice.sample == nullptr ) {
		return;
	}

	const float *voiceOrigin = voice.fixedOrigin ? voice.origin.Data() : entityOrigins_[voice.entnum].Data();
	const bool listenerAttached = ( voice.entnum == listenerNumber_ );
	const bool localOnly = IsLocalOnlyChannel( voice.entchannel );
	SpatialParams spatial;
	if ( listenerAttached || localOnly ) {
		spatial.gain = 1.0f;
		spatial.pan = 0.0f;
	} else {
		spatial = ComputeSpatialParams( voiceOrigin, listenerOrigin_.Data(), listenerAxis_, 127 );
	}

	float gain = spatial.gain;
	float dryGain = 1.0f;
	float dryGainHF = environment_.directHF;
	float wetGain = 0.0f;
	float wetGainHF = environment_.wetHF;
	float pitch = 1.0f;

	if ( voice.sample->Channels() > 1 ) {
		spatial.pan = 0.0f;
	}

	if ( !localOnly ) {
		const float distance = listenerAttached ? 0.0f : DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );
		if ( listenerAttached ) {
			voice.occlusion = 0.0f;
		} else {
			UpdateVoiceEnvironment( voice, voiceOrigin );
		}
		const float distanceMix = ClampFloat( distance / 512.0f, 0.0f, 1.0f );
		dryGain = ClampFloat( 1.0f - voice.occlusion * 0.55f, 0.05f, 1.0f );
		dryGainHF = ClampFloat( environment_.directHF * ( 1.0f - voice.occlusion * 0.85f ), 0.05f, 1.0f );
		wetGain = ClampFloat( environment_.baseWet * ( 0.35f + distanceMix * 0.65f ) + voice.occlusion * 0.25f, 0.0f, 1.0f );
		wetGainHF = ClampFloat( environment_.wetHF * ( 1.0f - voice.occlusion * 0.45f ), 0.05f, 1.0f );
	} else {
		voice.occlusion = 0.0f;
	}

	if ( softMuted ) {
		gain = 0.0f;
		wetGain = 0.0f;
	} else if ( voice.doppler ) {
		pitch = ComputeDopplerPitch( listenerOrigin_.Data(), voiceOrigin, voice.velocity.Data() );
	}

	voice.debugGain = gain;
	voice.debugPan = spatial.pan;
	voice.debugDryGain = dryGain;
	voice.debugWetGain = wetGain;
	voice.debugPitch = pitch;
	voice.debugDistance = DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );

	device_->AL().alSourcei( voice.source, AL_SOURCE_RELATIVE, AL_TRUE );
	device_->AL().alSourcef( voice.source, AL_PITCH, pitch );
	device_->AL().alSource3f( voice.source, AL_POSITION, spatial.pan * kPanScale, 0.0f, -1.0f );
	device_->ApplyVoiceRouting( voice.source, voice.directFilter, voice.sendFilter, gain, dryGain, dryGainHF, wetGain, wetGainHF );

	device_->AL().alSource3f( voice.source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );

	if ( voice.looping ) {
		ALint state = 0;
		device_->AL().alGetSourcei( voice.source, AL_SOURCE_STATE, &state );
		if ( state != AL_PLAYING ) {
			device_->AL().alSourcei( voice.source, AL_BUFFER, voice.sample->Buffer() );
			device_->AL().alSourcei( voice.source, AL_LOOPING, AL_TRUE );
			device_->AL().alSourcePlay( voice.source );
		}
	}
}

const SoundVoice *Q3SoundWorld::SelectDebugVoice( int preferredEntity ) const {
	const SoundVoice *selected = nullptr;
	float bestDistance = 0.0f;

	auto considerVoice = [&]( const SoundVoice &voice ) {
		if ( !voice.active || voice.sample == nullptr ) {
			return;
		}

		if ( preferredEntity >= 0 && voice.entnum != preferredEntity ) {
			return;
		}

		const float *voiceOrigin = voice.fixedOrigin ? voice.origin.Data() : entityOrigins_[voice.entnum].Data();
		const float distance = DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );
		if ( selected == nullptr || distance < bestDistance ) {
			selected = &voice;
			bestDistance = distance;
		}
	};

	if ( preferredEntity >= 0 ) {
		for ( const SoundVoice &voice : loopVoices_ ) {
			considerVoice( voice );
		}
		for ( const SoundVoice &voice : oneShots_ ) {
			considerVoice( voice );
		}
		return selected;
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( voice.entnum == listenerNumber_ ) {
			continue;
		}
		considerVoice( voice );
	}
	if ( selected != nullptr ) {
		return selected;
	}

	for ( const SoundVoice &voice : oneShots_ ) {
		if ( voice.entnum == listenerNumber_ ) {
			continue;
		}
		considerVoice( voice );
	}
	if ( selected != nullptr ) {
		return selected;
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		considerVoice( voice );
	}
	for ( const SoundVoice &voice : oneShots_ ) {
		considerVoice( voice );
	}

	return selected;
}

qboolean Q3SoundWorld::GetSpatialDebugInfo( spatialAudioDebugInfo_t *info, const OpenALDevice &device, int preferredEntity, int overlayMode ) const {
	const SoundVoice *selected;
	int activeOneShots = 0;
	int activeLoops = 0;

	if ( info == nullptr || overlayMode <= 0 ) {
		return qfalse;
	}

	Com_Memset( info, 0, sizeof( *info ) );
	info->active = qtrue;

	for ( const SoundVoice &voice : oneShots_ ) {
		if ( voice.active ) {
			++activeOneShots;
		}
	}
	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( voice.active ) {
			++activeLoops;
		}
	}

	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"spatial %s reverb:%s env:%s",
		device.LibraryName().empty() ? "openal" : "openal-soft",
		device.CurrentReverbName(),
		environment_.name );
	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"listener:%d voices shot:%d loop:%d outdoors:%d underwater:%d",
		listenerNumber_, activeOneShots, activeLoops, environment_.outdoors, environment_.underwater );

	selected = SelectDebugVoice( preferredEntity );
	if ( selected == nullptr ) {
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS, "selected voice: none" );
		return qtrue;
	}

	info->hasSelectedVoice = qtrue;
	info->dryGain = selected->debugDryGain;
	info->wetGain = selected->debugWetGain;
	info->occlusion = selected->occlusion;
	info->pan = selected->debugPan;
	info->pitch = selected->debugPitch;

	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"selected ent:%d %s dist:%.0f occ:%.2f",
		selected->entnum, selected->looping ? "loop" : "shot", selected->debugDistance, selected->occlusion );

	if ( overlayMode > 1 ) {
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"sample:%s",
			( selected->sample != nullptr ) ? selected->sample->Name().c_str() : "none" );
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"gain:%.2f pan:%.2f pitch:%.2f dry:%.2f wet:%.2f transient:%d",
			selected->debugGain, selected->debugPan, selected->debugPitch,
			selected->debugDryGain, selected->debugWetGain, selected->killWhenUnrefreshed ? 1 : 0 );
	}

	return qtrue;
}

void Q3SoundWorld::DumpSpatialDebug( const OpenALDevice &device, int preferredEntity ) const {
	const SoundVoice *selected = SelectDebugVoice( preferredEntity );

	Com_Printf( "----- Spatial Audio Debug -----\n" );
	Com_Printf( "Environment: %s (reverb %s, outdoors %d, underwater %d)\n",
		environment_.name, device.CurrentReverbName(), environment_.outdoors, environment_.underwater );
	Com_Printf( "Listener entity: %d\n", listenerNumber_ );

	if ( selected != nullptr ) {
		Com_Printf( "Selected voice: ent=%d type=%s sample=%s dist=%.1f occ=%.2f gain=%.2f pan=%.2f pitch=%.2f dry=%.2f wet=%.2f transient=%d\n",
			selected->entnum,
			selected->looping ? "loop" : "shot",
			( selected->sample != nullptr ) ? selected->sample->Name().c_str() : "none",
			selected->debugDistance,
			selected->occlusion,
			selected->debugGain,
			selected->debugPan,
			selected->debugPitch,
			selected->debugDryGain,
			selected->debugWetGain,
			selected->killWhenUnrefreshed ? 1 : 0 );
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		Com_Printf( "loop ent=%d sample=%s dist=%.1f occ=%.2f gain=%.2f pan=%.2f pitch=%.2f transient=%d refreshed=%d\n",
			voice.entnum,
			( voice.sample != nullptr ) ? voice.sample->Name().c_str() : "none",
			voice.debugDistance,
			voice.occlusion,
			voice.debugGain,
			voice.debugPan,
			voice.debugPitch,
			voice.killWhenUnrefreshed ? 1 : 0,
			voice.refreshedThisFrame ? 1 : 0 );
	}
	for ( const SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		Com_Printf( "shot ent=%d sample=%s dist=%.1f occ=%.2f gain=%.2f pan=%.2f pitch=%.2f\n",
			voice.entnum,
			( voice.sample != nullptr ) ? voice.sample->Name().c_str() : "none",
			voice.debugDistance,
			voice.occlusion,
			voice.debugGain,
			voice.debugPan,
			voice.debugPitch );
	}
	Com_Printf( "-------------------------------\n" );
}

void Q3SoundWorld::CleanupFinishedVoices() {
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active || voice.source == 0 ) {
			continue;
		}
		ALint state = 0;
		device_->AL().alGetSourcei( voice.source, AL_SOURCE_STATE, &state );
		if ( state == AL_STOPPED ) {
			voice.Release( *device_ );
		}
	}
}

void Q3SoundWorld::Update( qboolean softMuted ) {
	CleanupFinishedVoices();
	RefreshEnvironment();

	for ( SoundVoice &voice : oneShots_ ) {
		if ( voice.active ) {
			ApplyVoice( voice, softMuted );
		}
	}

	for ( SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( voice.killWhenUnrefreshed && !voice.refreshedThisFrame ) {
			voice.Release( *device_ );
			voice.kind = VoiceKind::Loop;
			continue;
		}
		if ( voice.source == 0 ) {
			voice.source = AllocateVoiceSourceForLoop();
			if ( voice.source == 0 ) {
				continue;
			}
			device_->CreateVoiceFilters( voice.directFilter, voice.sendFilter );
			device_->AL().alSourcei( voice.source, AL_BUFFER, voice.sample->Buffer() );
			device_->AL().alSourcei( voice.source, AL_LOOPING, AL_TRUE );
			device_->AL().alSourcePlay( voice.source );
		}
		ApplyVoice( voice, softMuted );
		voice.refreshedThisFrame = voice.killWhenUnrefreshed ? qfalse : qtrue;
	}
}

class StreamPlayer {
public:
	void Init( OpenALDevice *device, ALuint source );
	void Shutdown();
	void Clear();
	void Update( float gain );
	bool QueuePCM16( const short *samples, int frameCount, int sampleRate );
	int QueuedBufferCount() const { return queuedBuffers_; }

private:
	void ReclaimProcessedBuffers();

	OpenALDevice *device_ = nullptr;
	ALuint source_ = 0;
	int queuedBuffers_ = 0;
};

void StreamPlayer::Init( OpenALDevice *device, ALuint source ) {
	device_ = device;
	source_ = source;
	Clear();
}

void StreamPlayer::ReclaimProcessedBuffers() {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	ALint processed = 0;
	device_->AL().alGetSourcei( source_, AL_BUFFERS_PROCESSED, &processed );
	while ( processed-- > 0 ) {
		ALuint buffer = 0;
		device_->AL().alSourceUnqueueBuffers( source_, 1, &buffer );
		if ( buffer != 0 ) {
			device_->BufferPool().Release( buffer );
			--queuedBuffers_;
		}
	}
}

void StreamPlayer::Clear() {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	device_->AL().alSourceStop( source_ );
	ReclaimProcessedBuffers();

	ALint queued = 0;
	device_->AL().alGetSourcei( source_, AL_BUFFERS_QUEUED, &queued );
	while ( queued-- > 0 ) {
		ALuint buffer = 0;
		device_->AL().alSourceUnqueueBuffers( source_, 1, &buffer );
		if ( buffer != 0 ) {
			device_->BufferPool().Release( buffer );
		}
	}
	queuedBuffers_ = 0;
	device_->AL().alSourcei( source_, AL_BUFFER, 0 );
	device_->AL().alSourcei( source_, AL_SOURCE_RELATIVE, AL_TRUE );
	device_->AL().alSource3f( source_, AL_POSITION, 0.0f, 0.0f, -1.0f );
	device_->AL().alSource3f( source_, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	device_->AL().alSourcef( source_, AL_GAIN, 1.0f );
}

void StreamPlayer::Shutdown() {
	Clear();
	device_ = nullptr;
	source_ = 0;
}

bool StreamPlayer::QueuePCM16( const short *samples, int frameCount, int sampleRate ) {
	if ( device_ == nullptr || source_ == 0 || samples == nullptr || frameCount <= 0 || sampleRate <= 0 ) {
		return false;
	}

	ALuint buffer = device_->BufferPool().Acquire();
	if ( buffer == 0 ) {
		return false;
	}

	const int sampleCount = frameCount * kStreamChannels;
	device_->AL().alBufferData( buffer, AL_FORMAT_STEREO16, samples, sampleCount * static_cast<int>( sizeof( short ) ), sampleRate );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		device_->BufferPool().Release( buffer );
		return false;
	}

	device_->AL().alSourceQueueBuffers( source_, 1, &buffer );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		device_->BufferPool().Release( buffer );
		return false;
	}

	++queuedBuffers_;

	ALint state = 0;
	device_->AL().alGetSourcei( source_, AL_SOURCE_STATE, &state );
	if ( state != AL_PLAYING ) {
		device_->AL().alSourcePlay( source_ );
	}

	return true;
}

void StreamPlayer::Update( float gain ) {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	device_->AL().alSourcef( source_, AL_GAIN, gain );
	ReclaimProcessedBuffers();

	ALint state = 0;
	device_->AL().alGetSourcei( source_, AL_SOURCE_STATE, &state );
	if ( state != AL_PLAYING && queuedBuffers_ > 0 ) {
		device_->AL().alSourcePlay( source_ );
	}
}

class AudioSystem {
public:
	static AudioSystem &Get();

	bool Init( soundInterface_t *si );
	void Shutdown();

	void StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle );
	void StartLocalSound( sfxHandle_t sfxHandle, int channelNum );
	void StartBackgroundTrack( const char *intro, const char *loop );
	void StopBackgroundTrack();
	void RawSamples( int samples, int rate, int width, int channels, const byte *data, float volume );
	void StopAllSounds();
	void ClearLoopingSounds( qboolean killall );
	void AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle );
	void AddRealLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle );
	void StopLoopingSound( int entityNum );
	void Respatialize( int entityNum, const float *origin, float axis[3][3], int inwater );
	void UpdateEntityPosition( int entityNum, const float *origin );
	void Update( int msec );
	void DisableSounds();
	void BeginRegistration();
	sfxHandle_t RegisterSound( const char *sample, qboolean compressed );
	void ClearSoundBuffer();
	void SoundInfo();
	void SoundList();
	qboolean GetSpatialDebugInfo( spatialAudioDebugInfo_t *info ) const;
	void DumpSpatialDebug() const;

private:
	static std::vector<short> ConvertStreamToStereo16( int samples, int rate, int width, int channels, const byte *data, float volume, int outputRate );
	void ServiceBackgroundTrack();
	void CloseBackgroundStream();
	SoundSample *GetSample( sfxHandle_t handle );
	const SoundSample *GetSample( sfxHandle_t handle ) const;
	qboolean IsSoftMuted() const;

	OpenALDevice device_;
	Q3SoundWorld world_;
	StreamPlayer musicPlayer_;
	StreamPlayer rawPlayer_;
	std::deque<SoundSample> samples_;
	std::unordered_map<std::string, sfxHandle_t> sampleLookup_;
	snd_stream_t *backgroundStream_ = nullptr;
	std::string backgroundIntro_;
	std::string backgroundLoop_;
	bool started_ = false;
	bool hardMuted_ = true;
};

AudioSystem &AudioSystem::Get() {
	static AudioSystem audioSystem;
	return audioSystem;
}

std::vector<short> AudioSystem::ConvertStreamToStereo16( int samples, int rate, int width, int channels, const byte *data, float volume, int outputRate ) {
	std::vector<short> pcm;
	if ( samples <= 0 || rate <= 0 || data == nullptr || ( width != 1 && width != 2 ) || ( channels != 1 && channels != 2 ) ) {
		return pcm;
	}

	const float gain = ClampFloat( volume, 0.0f, 8.0f );
	const int outputSamples = ( std::max )( 1, static_cast<int>( std::ceil( static_cast<float>( samples ) * static_cast<float>( outputRate ) / static_cast<float>( rate ) ) ) );
	pcm.resize( static_cast<size_t>( outputSamples ) * 2u );

	for ( int i = 0; i < outputSamples; ++i ) {
		const int sourceIndex = ClampInt( static_cast<int>( static_cast<int64_t>( i ) * static_cast<int64_t>( rate ) / outputRate ), 0, samples - 1 );
		short left = 0;
		short right = 0;

		if ( width == 2 ) {
			const short *input = reinterpret_cast<const short *>( data );
			if ( channels == 2 ) {
				left = LittleShort( input[sourceIndex * 2] );
				right = LittleShort( input[sourceIndex * 2 + 1] );
			} else {
				left = right = LittleShort( input[sourceIndex] );
			}
		} else {
			const byte *input = reinterpret_cast<const byte *>( data );
			if ( channels == 2 ) {
				left = static_cast<short>( ( static_cast<int>( input[sourceIndex * 2] ) - 128 ) << 8 );
				right = static_cast<short>( ( static_cast<int>( input[sourceIndex * 2 + 1] ) - 128 ) << 8 );
			} else {
				left = right = static_cast<short>( ( static_cast<int>( input[sourceIndex] ) - 128 ) << 8 );
			}
		}

		const int scaledLeft = static_cast<int>( static_cast<float>( left ) * gain );
		const int scaledRight = static_cast<int>( static_cast<float>( right ) * gain );
		pcm[static_cast<size_t>( i ) * 2u] = static_cast<short>( ClampInt( scaledLeft, -32768, 32767 ) );
		pcm[static_cast<size_t>( i ) * 2u + 1u] = static_cast<short>( ClampInt( scaledRight, -32768, 32767 ) );
	}

	return pcm;
}

bool AudioSystem::Init( soundInterface_t *si ) {
	if ( si == nullptr ) {
		return false;
	}

	s_alReverb = Cvar_Get( "s_alReverb", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alReverb, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alReverb, "Enables OpenAL environmental reverb sends when the active device supports EFX. Requires snd_restart to fully apply." );
	s_alOcclusion = Cvar_Get( "s_alOcclusion", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alOcclusion, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alOcclusion, "Enables world-geometry occlusion checks for the OpenAL backend." );
	s_alReverbGain = Cvar_Get( "s_alReverbGain", "1.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alReverbGain, "0", "2", CV_FLOAT );
	Cvar_SetDescription( s_alReverbGain, "Scales wet reverb send level for the OpenAL backend." );
	s_alOcclusionStrength = Cvar_Get( "s_alOcclusionStrength", "1.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alOcclusionStrength, "0", "2", CV_FLOAT );
	Cvar_SetDescription( s_alOcclusionStrength, "Scales how strongly world occlusion muffles OpenAL sounds." );
	s_alDebugOverlay = Cvar_Get( "s_alDebugOverlay", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alDebugOverlay, "0", "2", CV_INTEGER );
	Cvar_SetDescription( s_alDebugOverlay, "Draws OpenAL spatial audio debug overlay.\n"
		"0 disables the overlay.\n"
		"1 shows summary environment and selected voice state.\n"
		"2 adds sample and gain details for the selected voice." );
	s_alDebugVoice = Cvar_Get( "s_alDebugVoice", "-1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( s_alDebugVoice, "Selects which entity the OpenAL spatial audio debug tools inspect.\n"
		"Use -1 to automatically pick the nearest active voice." );

	if ( !device_.Init() ) {
		return false;
	}

	world_.Reset( &device_ );
	musicPlayer_.Init( &device_, device_.MusicSource() );
	rawPlayer_.Init( &device_, device_.RawSource() );

	samples_.clear();
	sampleLookup_.clear();
	started_ = true;
	hardMuted_ = true;

	si->Shutdown = []() { AudioSystem::Get().Shutdown(); };
	si->StartSound = []( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfxHandle ) { AudioSystem::Get().StartSound( origin, entnum, entchannel, sfxHandle ); };
	si->StartLocalSound = []( sfxHandle_t sfxHandle, int channelNum ) { AudioSystem::Get().StartLocalSound( sfxHandle, channelNum ); };
	si->StartBackgroundTrack = []( const char *intro, const char *loop ) { AudioSystem::Get().StartBackgroundTrack( intro, loop ); };
	si->StopBackgroundTrack = []() { AudioSystem::Get().StopBackgroundTrack(); };
	si->RawSamples = []( int samples, int rate, int width, int channels, const byte *data, float volume ) { AudioSystem::Get().RawSamples( samples, rate, width, channels, data, volume ); };
	si->StopAllSounds = []() { AudioSystem::Get().StopAllSounds(); };
	si->ClearLoopingSounds = []( qboolean killall ) { AudioSystem::Get().ClearLoopingSounds( killall ); };
	si->AddLoopingSound = []( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) { AudioSystem::Get().AddLoopingSound( entityNum, origin, velocity, sfxHandle ); };
	si->AddRealLoopingSound = []( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) { AudioSystem::Get().AddRealLoopingSound( entityNum, origin, velocity, sfxHandle ); };
	si->StopLoopingSound = []( int entityNum ) { AudioSystem::Get().StopLoopingSound( entityNum ); };
	si->Respatialize = []( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater ) { AudioSystem::Get().Respatialize( entityNum, origin, axis, inwater ); };
	si->UpdateEntityPosition = []( int entityNum, const vec3_t origin ) { AudioSystem::Get().UpdateEntityPosition( entityNum, origin ); };
	si->Update = []( int msec ) { AudioSystem::Get().Update( msec ); };
	si->DisableSounds = []() { AudioSystem::Get().DisableSounds(); };
	si->BeginRegistration = []() { AudioSystem::Get().BeginRegistration(); };
	si->RegisterSound = []( const char *sample, qboolean compressed ) { return AudioSystem::Get().RegisterSound( sample, compressed ); };
	si->ClearSoundBuffer = []() { AudioSystem::Get().ClearSoundBuffer(); };
	si->SoundInfo = []() { AudioSystem::Get().SoundInfo(); };
	si->SoundList = []() { AudioSystem::Get().SoundList(); };
	si->GetSpatialDebugInfo = []( spatialAudioDebugInfo_t *info ) { return AudioSystem::Get().GetSpatialDebugInfo( info ); };
	si->DumpSpatialDebug = []() { AudioSystem::Get().DumpSpatialDebug(); };

	return true;
}

void AudioSystem::Shutdown() {
	if ( !started_ ) {
		return;
	}

	StopAllSounds();
	CloseBackgroundStream();
	for ( SoundSample &sample : samples_ ) {
		sample.Unload( device_ );
	}
	samples_.clear();
	sampleLookup_.clear();
	musicPlayer_.Shutdown();
	rawPlayer_.Shutdown();
	device_.Shutdown();
	started_ = false;
	hardMuted_ = true;
	cls.soundRegistered = qfalse;
}

qboolean AudioSystem::IsSoftMuted() const {
	return ( ( !gw_active && !gw_minimized && s_muteWhenUnfocused != nullptr && s_muteWhenUnfocused->integer ) ||
		( gw_minimized && s_muteWhenMinimized != nullptr && s_muteWhenMinimized->integer ) ) ? qtrue : qfalse;
}

SoundSample *AudioSystem::GetSample( sfxHandle_t handle ) {
	if ( handle < 0 || handle >= static_cast<sfxHandle_t>( samples_.size() ) ) {
		return nullptr;
	}
	return &samples_[static_cast<size_t>( handle )];
}

const SoundSample *AudioSystem::GetSample( sfxHandle_t handle ) const {
	if ( handle < 0 || handle >= static_cast<sfxHandle_t>( samples_.size() ) ) {
		return nullptr;
	}
	return &samples_[static_cast<size_t>( handle )];
}

void AudioSystem::BeginRegistration() {
	hardMuted_ = false;

	if ( !samples_.empty() ) {
		return;
	}

	samples_.emplace_back( "sound/feedback/hit.wav" );
	sampleLookup_.emplace( NormalizeSoundName( "sound/feedback/hit.wav" ), 0 );
	samples_[0].EnsureLoaded( device_, true );
}

sfxHandle_t AudioSystem::RegisterSound( const char *sample, qboolean /*compressed*/ ) {
	if ( !started_ || sample == nullptr || sample[0] == '\0' ) {
		return 0;
	}

	if ( std::strlen( sample ) >= MAX_QPATH ) {
		Com_Printf( "Sound name exceeds MAX_QPATH\n" );
		return 0;
	}

	if ( samples_.empty() ) {
		BeginRegistration();
	}

	const std::string normalized = NormalizeSoundName( sample );
	const auto existing = sampleLookup_.find( normalized );
	if ( existing != sampleLookup_.end() ) {
		SoundSample &found = samples_[static_cast<size_t>( existing->second )];
		if ( found.EnsureLoaded( device_, existing->second == 0 ) ) {
			return existing->second;
		}
		return 0;
	}

	const sfxHandle_t handle = static_cast<sfxHandle_t>( samples_.size() );
	samples_.emplace_back( normalized );
	sampleLookup_.emplace( normalized, handle );
	if ( !samples_.back().EnsureLoaded( device_, false ) ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: couldn't load sound: %s\n", sample );
		return 0;
	}

	return handle;
}

void AudioSystem::StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr ) {
		sample = GetSample( 0 );
		sfxHandle = 0;
	}
	if ( sample == nullptr || !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.StartSound( entnum, entchannel, sfxHandle, sample, origin );
}

void AudioSystem::StartLocalSound( sfxHandle_t sfxHandle, int channelNum ) {
	StartSound( nullptr, world_.ListenerNumber(), channelNum, sfxHandle );
}

void AudioSystem::CloseBackgroundStream() {
	if ( backgroundStream_ != nullptr ) {
		S_CodecCloseStream( backgroundStream_ );
		backgroundStream_ = nullptr;
	}
}

void AudioSystem::StartBackgroundTrack( const char *intro, const char *loop ) {
	if ( !started_ ) {
		return;
	}

	backgroundIntro_ = SafeString( intro );
	backgroundLoop_ = ( loop != nullptr && loop[0] != '\0' ) ? SafeString( loop ) : backgroundIntro_;

	if ( backgroundIntro_.empty() ) {
		StopBackgroundTrack();
		return;
	}

	CloseBackgroundStream();
	backgroundStream_ = S_CodecOpenStream( backgroundIntro_.c_str() );
	musicPlayer_.Clear();
}

void AudioSystem::StopBackgroundTrack() {
	CloseBackgroundStream();
	backgroundIntro_.clear();
	backgroundLoop_.clear();
	musicPlayer_.Clear();
}

void AudioSystem::ServiceBackgroundTrack() {
	if ( backgroundStream_ == nullptr ) {
		return;
	}

	while ( musicPlayer_.QueuedBufferCount() < kQueuedStreamChunks ) {
		byte raw[32768];
		const int bytesRead = S_CodecReadStream( backgroundStream_, sizeof( raw ), raw );
		if ( bytesRead <= 0 ) {
			if ( !backgroundLoop_.empty() ) {
				CloseBackgroundStream();
				backgroundStream_ = S_CodecOpenStream( backgroundLoop_.c_str() );
				if ( backgroundStream_ == nullptr ) {
					break;
				}
				continue;
			}
			StopBackgroundTrack();
			break;
		}

		const int frameBytes = backgroundStream_->info.width * backgroundStream_->info.channels;
		if ( frameBytes <= 0 ) {
			StopBackgroundTrack();
			break;
		}

		const int inputSamples = bytesRead / frameBytes;
		const std::vector<short> pcm = ConvertStreamToStereo16( inputSamples, backgroundStream_->info.rate, backgroundStream_->info.width, backgroundStream_->info.channels, raw, 1.0f, kStreamRate );
		if ( pcm.empty() ) {
			break;
		}

		musicPlayer_.QueuePCM16( pcm.data(), static_cast<int>( pcm.size() / 2u ), kStreamRate );
	}
}

void AudioSystem::RawSamples( int samples, int rate, int width, int channels, const byte *data, float volume ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	const std::vector<short> pcm = ConvertStreamToStereo16( samples, rate, width, channels, data, volume, kStreamRate );
	if ( !pcm.empty() ) {
		rawPlayer_.QueuePCM16( pcm.data(), static_cast<int>( pcm.size() / 2u ), kStreamRate );
	}
}

void AudioSystem::StopAllSounds() {
	if ( !started_ ) {
		return;
	}

	world_.StopAllSounds();
	rawPlayer_.Clear();
	StopBackgroundTrack();
}

void AudioSystem::ClearLoopingSounds( qboolean killall ) {
	world_.ClearLoopingSounds( killall );
}

qboolean AudioSystem::GetSpatialDebugInfo( spatialAudioDebugInfo_t *info ) const {
	int preferredEntity;
	int overlayMode;

	if ( !started_ || info == nullptr || s_alDebugOverlay == nullptr ) {
		return qfalse;
	}

	overlayMode = s_alDebugOverlay->integer;
	if ( overlayMode <= 0 ) {
		return qfalse;
	}

	preferredEntity = ( s_alDebugVoice != nullptr ) ? s_alDebugVoice->integer : -1;
	return world_.GetSpatialDebugInfo( info, device_, preferredEntity, overlayMode );
}

void AudioSystem::DumpSpatialDebug() const {
	const int preferredEntity = ( s_alDebugVoice != nullptr ) ? s_alDebugVoice->integer : -1;

	if ( !started_ ) {
		Com_Printf( "OpenAL spatial audio debug unavailable: backend not started\n" );
		return;
	}

	world_.DumpSpatialDebug( device_, preferredEntity );
}

void AudioSystem::AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr || !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.AddLoopingSound( entityNum, origin, velocity, sfxHandle, sample, qfalse );
}

void AudioSystem::AddRealLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr || !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.AddLoopingSound( entityNum, origin, velocity, sfxHandle, sample, qtrue );
}

void AudioSystem::StopLoopingSound( int entityNum ) {
	world_.StopLoopingSound( entityNum );
}

void AudioSystem::Respatialize( int entityNum, const float *origin, float axis[3][3], int /*inwater*/ ) {
	if ( !started_ ) {
		return;
	}

	world_.Respatialize( entityNum, origin, axis );
}

void AudioSystem::UpdateEntityPosition( int entityNum, const float *origin ) {
	world_.UpdateEntityPosition( entityNum, origin );
}

void AudioSystem::Update( int /*msec*/ ) {
	if ( !started_ ) {
		return;
	}

	const qboolean softMuted = IsSoftMuted();
	device_.SetMasterGain( ( s_volume != nullptr ) ? s_volume->value : 1.0f );
	device_.AL().alDopplerFactor( 0.0f );
	world_.Update( softMuted );
	rawPlayer_.Update( softMuted ? 0.0f : 1.0f );
	ServiceBackgroundTrack();
	musicPlayer_.Update( softMuted ? 0.0f : ( s_musicVolume != nullptr ? s_musicVolume->value : 1.0f ) );
}

void AudioSystem::DisableSounds() {
	world_.StopAllSounds();
	rawPlayer_.Clear();
	musicPlayer_.Clear();
	hardMuted_ = true;
}

void AudioSystem::ClearSoundBuffer() {
	world_.ClearSoundBuffer();
	rawPlayer_.Clear();
}

void AudioSystem::SoundInfo() {
	Com_Printf( "----- Sound Info -----\n" );
	Com_Printf( "Using OpenAL backend\n" );
	Com_Printf( "OpenAL library: %s\n", device_.LibraryName().empty() ? "unknown" : device_.LibraryName().c_str() );
	Com_Printf( "Requested device: %s\n", device_.RequestedDeviceName().empty() ? "default" : device_.RequestedDeviceName().c_str() );
	Com_Printf( "Active device: %s\n", device_.ActiveDeviceName().empty() ? "unknown" : device_.ActiveDeviceName().c_str() );
	if ( device_.UsingDefaultFallback() ) {
		Com_Printf( "Device fallback: using system default device\n" );
	}
	Com_Printf( "EFX support: %s\n", device_.HasEFX() ? "enabled" : "unavailable" );
	if ( device_.HasEFX() ) {
		Com_Printf( "Auxiliary sends: %d\n", device_.MaxAuxiliarySends() );
		Com_Printf( "Reverb send: %s (%s)\n", device_.HasReverb() ? "enabled" : "disabled", device_.CurrentReverbName() );
	}
	Com_Printf( "Occlusion: %s (strength %.2f)\n",
		( s_alOcclusion != nullptr && s_alOcclusion->integer ) ? "enabled" : "disabled",
		( s_alOcclusionStrength != nullptr ) ? s_alOcclusionStrength->value : 1.0f );
	Com_Printf( "%5d voice sources (%d free)\n", device_.TotalVoiceCount(), device_.FreeVoiceCount() );
	Com_Printf( "%5d stream buffers (%d free)\n", device_.BufferPool().TotalCount(), device_.BufferPool().FreeCount() );
	Com_Printf( "%5zu registered samples\n", samples_.size() );
	Com_Printf( "Background track: %s\n", backgroundIntro_.empty() ? "none" : backgroundIntro_.c_str() );
	Com_Printf( "----------------------\n" );
}

void AudioSystem::SoundList() {
	for ( size_t i = 0; i < samples_.size(); ++i ) {
		const SoundSample &sample = samples_[i];
		Com_Printf( "%4d : %s [%s]\n",
			static_cast<int>( i ),
			sample.Name().c_str(),
			sample.Missing() ? "missing" : ( sample.Loaded() ? "loaded" : "unloaded" ) );
	}
}

} // namespace

extern "C" qboolean S_OpenAL_Init( soundInterface_t *si ) {
	return AudioSystem::Get().Init( si ) ? qtrue : qfalse;
}
