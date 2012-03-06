#pragma once

#if defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <SDKDDKVer.h>
#include <iso646.h>

#undef CreateFile

#undef fopen
#undef fclose
#undef fread

#pragma warning (disable : 4355)	// this is used in Base Initializer list
#pragma warning (disable : 4996)	// unsafe function or variable
#pragma warning (disable : 4068)	// unknown pragma
#pragma warning (disable : 4996)	// stl copy()
#pragma warning (disable : 4800)	// BOOL conversion

#pragma comment ( lib, "libzeep" )
#pragma comment ( lib, "libm6" )
#pragma comment ( lib, "libpcre" )
//#pragma comment ( lib, "libz" )

#if defined(_DEBUG)
#	define DEBUG	1
#endif

#endif

#include <boost/cstdint.hpp>
#include <boost/type_traits/is_integral.hpp>

typedef boost::int8_t		int8;
typedef boost::uint8_t		uint8;
typedef boost::int16_t		int16;
typedef boost::uint16_t		uint16;
typedef boost::int32_t		int32;
typedef boost::uint32_t		uint32;
typedef boost::int64_t		int64;
typedef boost::uint64_t		uint64;

// --------------------------------------------------------------------
// some types used throughout m6

enum M6DataType
{
	eM6NoData,
	
	eM6TextData			= 1,
	eM6StringData,
	eM6NumberData,
	eM6DateData
};

enum M6IndexType : uint32
{
	eM6CharIndex			= 'M6cu',
	eM6NumberIndex			= 'M6nu',
//	eM6DateIndex			= 'M6du',
	eM6CharMultiIndex		= 'M6cm',
	eM6NumberMultiIndex		= 'M6nm',
//	eM6DateMultiIndex		= 'M6dm',
	eM6CharMultiIDLIndex	= 'M6ci',
	eM6CharWeightedIndex	= 'M6cw'
};

extern const uint32
	kM6MaxWeight, kM6WeightBitCount;

extern int VERBOSE;
