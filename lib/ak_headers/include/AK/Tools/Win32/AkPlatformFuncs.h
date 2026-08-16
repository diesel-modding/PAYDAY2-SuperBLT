/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided 
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the 
"Apache License"); you may not use this file except in compliance with the 
Apache License. You may obtain a copy of the Apache License at 
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2023 Audiokinetic Inc.
*******************************************************************************/

#ifndef _AK_PLATFORM_FUNCS_H_
#define _AK_PLATFORM_FUNCS_H_

#include "malloc.h"
#include <AK/Tools/Common/AkAssert.h>
#include <AK/SoundEngine/Common/AkAtomic.h>
#include <windows.h>
#include <stdio.h>

#if defined(_WIN64)
// on 64 bit, removes warning C4985: 'ceil': attributes not present on previous declaration.
// see http://connect.microsoft.com/VisualStudio/feedback/ViewFeedback.aspx?FeedbackID=294649
#include <math.h>
#endif // _WIN64
#include <intrin.h>

#if defined(AK_XBOXSERIESX)
#include <ammintrin.h>
#endif

//-----------------------------------------------------------------------------
// Platform-specific thread properties definition.
//-----------------------------------------------------------------------------
struct AkThreadProperties
{
    int                 nPriority;		///< Thread priority
#ifdef AK_WIN_UNIVERSAL_APP
	PROCESSOR_NUMBER    processorNumber;///< Ideal processor (passed to SetThreadIdealProcessorEx)
#else
	AkUInt32            dwAffinityMask;	///< Affinity mask
#endif
	AkUInt32			uStackSize;		///< Thread stack size.
};

//-----------------------------------------------------------------------------
// External variables.
//-----------------------------------------------------------------------------
// g_fFreqRatio is used by time helpers to return time values in milliseconds.
// It is declared and updated by the sound engine.
namespace AK
{
    extern AkReal32 g_fFreqRatio;
}

//-----------------------------------------------------------------------------
// Defines for Win32.
//-----------------------------------------------------------------------------
#define AK_DECLARE_THREAD_ROUTINE( FuncName )   DWORD WINAPI FuncName(LPVOID lpParameter)
#define AK_THREAD_RETURN( _param_ )				return (_param_);
#define AK_THREAD_ROUTINE_PARAMETER             lpParameter
#define AK_GET_THREAD_ROUTINE_PARAMETER_PTR(type) reinterpret_cast<type*>( AK_THREAD_ROUTINE_PARAMETER )
#define AK_RETURN_THREAD_OK                     0x00000000
#define AK_RETURN_THREAD_ERROR                  0x00000001
#define AK_DEFAULT_STACK_SIZE					(128*1024)
#define AK_THREAD_PRIORITY_NORMAL				THREAD_PRIORITY_NORMAL
#define AK_THREAD_PRIORITY_ABOVE_NORMAL			THREAD_PRIORITY_ABOVE_NORMAL
#define AK_THREAD_PRIORITY_BELOW_NORMAL			THREAD_PRIORITY_BELOW_NORMAL
#define AK_THREAD_PRIORITY_TIME_CRITICAL		THREAD_PRIORITY_TIME_CRITICAL
#define AK_THREAD_MODE_BACKGROUND_BEGIN			THREAD_MODE_BACKGROUND_BEGIN

// NULL objects
#define AK_NULL_THREAD                          NULL

#define AK_INFINITE                             INFINITE

#define AkMax(x1, x2)	(((x1) > (x2))? (x1): (x2))
#define AkMin(x1, x2)	(((x1) < (x2))? (x1): (x2))
#define AkClamp(x, min, max)  ((x) < (min)) ? (min) : (((x) > (max) ? (max) : (x)))

namespace AKPLATFORM
{
	// Simple automatic event API
    // ------------------------------------------------------------------
	
	/// Platform Independent Helper
	inline void AkClearEvent( AkEvent & out_event )
    {
		out_event = NULL;
	}

	/// Platform Independent Helper
	inline AKRESULT AkCreateEvent( AkEvent & out_event )
    {
#ifdef AK_USE_UWP_API
		out_event = CreateEventEx(nullptr, nullptr, 0, STANDARD_RIGHTS_ALL|EVENT_MODIFY_STATE);
#else
		out_event = ::CreateEvent( NULL,					// No security attributes
                                    false,					// Reset type: automatic
                                    false,					// Initial signaled state: not signaled
                                    NULL                    // No name
                                   );
#endif
		return ( out_event ) ? AK_Success : AK_Fail;
	}

	/// Platform Independent Helper
	inline void AkDestroyEvent( AkEvent & io_event )
	{
		if ( io_event )
			::CloseHandle( io_event );
		io_event = NULL;
	}

	/// Platform Independent Helper
	inline void AkWaitForEvent( AkEvent & in_event )
	{
#ifdef AK_USE_UWP_API
		DWORD dwWaitResult = ::WaitForSingleObjectEx( in_event, INFINITE, FALSE );
        AKASSERT( dwWaitResult == WAIT_OBJECT_0 );
#else
        AKVERIFY( ::WaitForSingleObject( in_event, INFINITE ) == WAIT_OBJECT_0 );
#endif
	}

	/// Platform Independent Helper
	inline void AkSignalEvent( const AkEvent & in_event )
	{
		AKVERIFY( ::SetEvent( in_event ) );
	}

	/// Platform Independent Helper
	AkForceInline void AkClearSemaphore(AkSemaphore& io_semaphore)
	{
		io_semaphore = NULL;
	}

	/// Platform Independent Helper
	inline AKRESULT AkCreateSemaphore(AkSemaphore& out_semaphore, AkUInt32 in_initialCount)
	{
#ifdef AK_USE_UWP_API
		out_semaphore = ::CreateSemaphoreEx(
			NULL,				// no security attributes
			in_initialCount,	// initial count
			INT_MAX,			// no maximum -- matches posix semaphore behaviour
			NULL,				// no name
			0,					// reserved
			STANDARD_RIGHTS_ALL | SEMAPHORE_MODIFY_STATE);
#else
		out_semaphore = ::CreateSemaphore(
			NULL,				// no security attributes
			in_initialCount,	// initial count
			INT_MAX,			// no maximum -- matches posix semaphore behaviour
			NULL);				// no name
#endif
		return (out_semaphore) ? AK_Success : AK_Fail;
	}

	/// Platform Independent Helper
	inline void AkDestroySemaphore(AkSemaphore& io_semaphore)
	{
		::CloseHandle(io_semaphore);
	}

	/// Platform Independent Helper - Semaphore wait, aka Operation P. Decrements value of semaphore, and, if the semaphore would be less than 0, waits for the semaphore to be released.
	inline void AkWaitForSemaphore(AkSemaphore& in_semaphore)
	{
		AKVERIFY(::WaitForSingleObject(in_semaphore, INFINITE) == WAIT_OBJECT_0);
	}

	/// Platform Independent Helper - Semaphore signal, aka Operation V. Increments value of semaphore by an arbitrary count.
	inline void AkReleaseSemaphore(AkSemaphore& in_semaphore, AkUInt32 in_count)
	{
		AKVERIFY(ReleaseSemaphore(in_semaphore, in_count, NULL) >= 0);
	}

	// Virtual Memory
	// ------------------------------------------------------------------

#ifdef AK_WIN_UNIVERSAL_APP
	AkForceInline void* AllocVM(size_t size, size_t* /*extra*/)
	{
		return VirtualAllocFromApp(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	}
#else
	AkForceInline void* AllocVM(size_t size, size_t* /*extra*/)
	{
		return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	}

#endif
	AkForceInline void FreeVM(void* address, size_t size, size_t /*extra*/, size_t release)
	{
		VirtualFree(address, release ? 0 : size, release ? MEM_RELEASE : MEM_DECOMMIT);
	}

    // Threads
    // ------------------------------------------------------------------

	/// Platform Independent Helper
	inline bool AkIsValidThread( AkThread * in_pThread )
	{
		return (*in_pThread != AK_NULL_THREAD);
	}

	/// Platform Independent Helper
	inline void AkClearThread( AkThread * in_pThread )
	{
		*in_pThread = AK_NULL_THREAD;
	}

	/// Platform Independent Helper
    inline void AkCloseThread( AkThread * in_pThread )
    {
        AKASSERT( in_pThread );
        AKASSERT( *in_pThread );
        AKVERIFY( ::CloseHandle( *in_pThread ) );
        AkClearThread( in_pThread );
    }

#define AkExitThread( _result ) return _result;

	/// Platform Independent Helper
	inline void AkGetDefaultThreadProperties( AkThreadProperties & out_threadProperties )
	{
		out_threadProperties.nPriority = AK_THREAD_PRIORITY_NORMAL;
		out_threadProperties.uStackSize= AK_DEFAULT_STACK_SIZE;
#ifdef AK_WIN_UNIVERSAL_APP
		out_threadProperties.processorNumber.Group = 0;
		out_threadProperties.processorNumber.Number = MAXIMUM_PROCESSORS;
		out_threadProperties.processorNumber.Reserved = 0;
#else
		out_threadProperties.dwAffinityMask = 0;
#endif
	}

	/// Set the name of a thread: see http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
	inline void AkSetThreadName( DWORD in_dwThreadID, LPCSTR in_szThreadName )
	{
		const DWORD MS_VC_EXCEPTION=0x406D1388;

#pragma pack(push,8)
		typedef struct tagTHREADNAME_INFO
		{
			DWORD dwType;
			LPCSTR szName;
			DWORD dwThreadID;
			DWORD dwFlags;
		} THREADNAME_INFO;
#pragma pack(pop)

		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = in_szThreadName;
		info.dwThreadID = in_dwThreadID;
		info.dwFlags = 0;

		// Only raise MS Exceptions if on MSVC
#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
		__try
		{
			RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
		}
#pragma warning(suppress: 6312 6322)
		__except(EXCEPTION_CONTINUE_EXECUTION)
		{
		}
#else
		abort();
#endif
	}

	/// Platform Independent Helper
	inline void AkCreateThread( 
		AkThreadRoutine pStartRoutine,					// Thread routine.
		void * pParams,									// Routine params.
		const AkThreadProperties & in_threadProperties,	// Properties. NULL for default.
		AkThread * out_pThread,							// Returned thread handle.
		const char * in_szThreadName )						// Opt thread name.
    {
		AKASSERT( out_pThread != NULL );
		AKASSERT( (in_threadProperties.nPriority >= THREAD_PRIORITY_LOWEST && in_threadProperties.nPriority <= THREAD_PRIORITY_HIGHEST)
			|| ( in_threadProperties.nPriority == THREAD_PRIORITY_TIME_CRITICAL )
			|| ( in_threadProperties.nPriority == THREAD_MODE_BACKGROUND_BEGIN ) );

		DWORD dwThreadID;
        *out_pThread = ::CreateThread( NULL,							// No security attributes
                                       in_threadProperties.uStackSize,	// StackSize (0 uses system default)
                                       pStartRoutine,                   // Thread start routine
                                       pParams,                         // Thread function parameter
                                       0,								// Creation flags: create running
                                       &dwThreadID );

		// ::CreateThread() return NULL if it fails.
        if ( !*out_pThread )
        {
			AkClearThread( out_pThread );
            return;
        }

        // Set thread name.
        AkSetThreadName( dwThreadID, in_szThreadName );

		// Set properties.
		if ( !::SetThreadPriority( *out_pThread, in_threadProperties.nPriority ) &&
			 in_threadProperties.nPriority != THREAD_MODE_BACKGROUND_BEGIN )
        {
            AKASSERT( !"Failed setting thread priority" );
			AkCloseThread( out_pThread );
            return;
        }
#ifdef AK_WIN_UNIVERSAL_APP
		if ( in_threadProperties.processorNumber.Number != MAXIMUM_PROCESSORS)
        {
			if ( !SetThreadIdealProcessorEx( *out_pThread, const_cast<PPROCESSOR_NUMBER>(&in_threadProperties.processorNumber), NULL) )
            {
                AKASSERT( !"Failed setting thread ideal processor" );
				AkCloseThread( out_pThread );
            }
		}
#else
		if (in_threadProperties.dwAffinityMask)
		{
			if (!::SetThreadAffinityMask(*out_pThread, in_threadProperties.dwAffinityMask))
			{
				AKASSERT(!"Failed setting thread affinity mask");
				AkCloseThread(out_pThread);
			}
		}
#endif
	}

	/// Platform Independent Helper
    inline void AkWaitForSingleThread( AkThread * in_pThread )
    {
        AKASSERT( in_pThread );
        AKASSERT( *in_pThread );
#ifdef AK_USE_UWP_API
        ::WaitForSingleObjectEx( *in_pThread, INFINITE, FALSE );
#else
        ::WaitForSingleObject( *in_pThread, INFINITE );
#endif
    }

	/// Returns the calling thread's ID.
	inline AkThreadID CurrentThread()
	{
		return ::GetCurrentThreadId();
	}

	/// Platform Independent Helper
    inline void AkSleep( AkUInt32 in_ulMilliseconds )
    {
		::Sleep( in_ulMilliseconds );
    }

	// Optimized memory functions
	// --------------------------------------------------------------------

	/// Platform Independent Helper
	inline void AkMemCpy( void * pDest, const void * pSrc, AkUInt32 uSize )
	{
		memcpy( pDest, pSrc, uSize );
	}

	/// Platform Independent Helper
	inline void AkMemMove( void* pDest, const void* pSrc, AkUInt32 uSize )
	{
		memmove( pDest, pSrc, uSize );
	}

	/// Platform Independent Helper
	inline void AkMemSet( void * pDest, AkInt32 iVal, AkUInt32 uSize )
	{
		memset( pDest, iVal, uSize );
	}

    // Time functions
    // ------------------------------------------------------------------

	/// Platform Independent Helper
    inline void PerformanceCounter( AkInt64 * out_piLastTime )
	{
		::QueryPerformanceCounter( (LARGE_INTEGER*)out_piLastTime );
	}

	/// Platform Independent Helper
	inline void PerformanceFrequency( AkInt64 * out_piFreq )
	{
		::QueryPerformanceFrequency( (LARGE_INTEGER*)out_piFreq );
	}

	/// Platform Independent Helper
    inline void UpdatePerformanceFrequency()
	{
        AkInt64 iFreq;
        PerformanceFrequency( &iFreq );
		AK::g_fFreqRatio = (AkReal32)((AkReal64)iFreq / 1000);
	}

	/// Returns a time range in milliseconds, using the sound engine's updated count->milliseconds ratio.
    inline AkReal32 Elapsed( const AkInt64 & in_iNow, const AkInt64 & in_iStart )
    {
        return ( in_iNow - in_iStart ) / AK::g_fFreqRatio;
    }

#if defined(AK_XBOXSERIESX)
	// Waits for a limited amount of time for in_pVal to hit zero (without yielding the thread)
	inline void AkLimitedSpinForZero(AkAtomic32* in_pVal)
	{
		// monitorx and waitx are available on certain AMD CPUs, so we can have a custom impl of AkLimitedSpinForZero
		AkInt64 endSpinTime = 0;
		AkInt64 currentTime = 0;
		PerformanceCounter(&endSpinTime);
		endSpinTime += AkInt64(AK::g_fFreqRatio * 0.01); // only spin for about 10us
		while (true)
		{
			// set up monitorx on pVal
			_mm_monitorx((void*)in_pVal, 0U, 0U);
			// if pval is zero, skip out
			if (AkAtomicLoad32(in_pVal) == 0)
			{
				break;
			}
			// wait until a store to pVal occurs (or ~1us passes)
			_mm_mwaitx(2U, 0U, 1000U);

			// Check if we've hit the deadline for the timeout
			PerformanceCounter(&currentTime);
			if (currentTime > endSpinTime)
			{
				break;
			}
		}
	}
#define AK_LIMITEDSPINFORZERO // mark AkLimitedSpinForZero as defined to avoid duplicate definitions
#endif

	/// String conversion helper. If io_pszAnsiString is null, the function returns the required size.
	inline AkInt32 AkWideCharToChar( const wchar_t*	in_pszUnicodeString,
									 AkUInt32	in_uiOutBufferSize,
									 char*	io_pszAnsiString )
	{
		if(!io_pszAnsiString)
			return WideCharToMultiByte(CP_UTF8, 0, in_pszUnicodeString, -1, NULL, 0, NULL, NULL);

		int iWritten = ::WideCharToMultiByte(CP_UTF8,																	// code page
									0,																		// performance and mapping flags
									in_pszUnicodeString,													// wide-character string
									(int)AkMin( ( (AkUInt32)wcslen( in_pszUnicodeString )), in_uiOutBufferSize-1 ),	// number of chars in string : -1 = NULL terminated string.
									io_pszAnsiString,														// buffer for new string
									in_uiOutBufferSize,														// size of buffer
									NULL,																	// default for unmappable chars
									NULL);																	// set when default char used
		io_pszAnsiString[iWritten] = 0;
		return iWritten;
	}

	/// String conversion helper
	inline AkInt32 AkCharToWideChar( const char*	in_pszAnsiString,
									 AkUInt32		in_uiOutBufferSize,
									 void*			io_pvUnicodeStringBuffer )
	{
		return ::MultiByteToWideChar(	CP_UTF8,							// code page
										0,									// performance and mapping flags
										in_pszAnsiString,					// wide-character string
										-1,									// number of chars in string : -1 = NULL terminated string.
										(wchar_t*)io_pvUnicodeStringBuffer,	// buffer for new string
										in_uiOutBufferSize);				// size of buffer
	}

	/// String conversion helper
	inline AkInt32 AkUtf8ToWideChar( const char*	in_pszUtf8String,
									 AkUInt32		in_uiOutBufferSize,
									 void*			io_pvUnicodeStringBuffer )
	{
		return ::MultiByteToWideChar(	CP_UTF8,							// code page
										0,									// performance and mapping flags
										in_pszUtf8String,					// wide-character string
										-1,									// number of chars in string : -1 = NULL terminated string.
										(wchar_t*)io_pvUnicodeStringBuffer,	// buffer for new string
										in_uiOutBufferSize);				// size of buffer
	}

	/// Safe unicode string copy.
	inline void SafeStrCpy( wchar_t * in_pDest, const wchar_t* in_pSrc, size_t in_uDestMaxNumChars )
	{
		size_t iSizeCopy = AkMin( in_uDestMaxNumChars - 1, wcslen( in_pSrc ) + 1 );
		wcsncpy_s( in_pDest, in_uDestMaxNumChars, in_pSrc, iSizeCopy );
		in_pDest[iSizeCopy] = '\0';
	}

	/// Safe string copy.
	inline void SafeStrCpy( char * in_pDest, const char* in_pSrc, size_t in_uDestMaxNumChars )
	{
		size_t iSizeCopy = AkMin( in_uDestMaxNumChars - 1, strlen( in_pSrc ) + 1 );
		strncpy_s( in_pDest, in_uDestMaxNumChars, in_pSrc, iSizeCopy );
		in_pDest[iSizeCopy] = '\0';
	}

	/// Safe unicode string concatenation.
	inline void SafeStrCat( wchar_t * in_pDest, const wchar_t* in_pSrc, size_t in_uDestMaxNumChars )
	{
		int iAvailableSize = (int)( in_uDestMaxNumChars - wcslen( in_pDest ) - 1 );
		wcsncat_s( in_pDest, in_uDestMaxNumChars, in_pSrc, AkMin( iAvailableSize, (int)wcslen( in_pSrc ) ) );
	}

	/// Safe string concatenation.
	inline void SafeStrCat( char * in_pDest, const char* in_pSrc, size_t in_uDestMaxNumChars )
	{
		int iAvailableSize = (int)( in_uDestMaxNumChars - strlen( in_pDest ) - 1 );
		strncat_s( in_pDest, in_uDestMaxNumChars, in_pSrc, AkMin( iAvailableSize, (int)strlen( in_pSrc ) ) );
	}

	inline int SafeStrFormat(wchar_t * in_pDest, size_t in_uDestMaxNumChars, const wchar_t* in_pszFmt, ...)
	{
		va_list args;
		va_start(args, in_pszFmt);
		int r = vswprintf(in_pDest, in_uDestMaxNumChars, in_pszFmt, args);
		va_end(args);
		return r;
	}

	inline int SafeStrFormat(char * in_pDest, size_t in_uDestMaxNumChars, const char* in_pszFmt, ...)
	{
		va_list args;
		va_start(args, in_pszFmt);
		int r = vsnprintf(in_pDest, in_uDestMaxNumChars, in_pszFmt, args);
		va_end(args);
		return r;
	}

	/// Stack allocations.
	#define AkAlloca( _size_ ) _alloca( _size_ )

	/// Output a debug message on the console
#if ! defined(AK_OPTIMIZED)
	inline void OutputDebugMsg( const wchar_t* in_pszMsg )
	{
		OutputDebugStringW( in_pszMsg );
	}

	/// Output a debug message on the console
	inline void OutputDebugMsg( const char* in_pszMsg )
	{
		OutputDebugStringA( in_pszMsg );
	}

	/// Output a debug message on the console (variadic function).
	/// Use MaxSize to specify the size to reserve for the message.
	/// Warning: On Win32, OutputDebugMsgV with wchar_t will truncate the string
	/// to MaxSize.
	template <int MaxSize = 256>
	inline void OutputDebugMsgV(const wchar_t* in_pszFmt, ...)
	{
		// No equivalent of snprintf for wide string so truncate if necessary
		// vswprintf returns the number of characters written and not the numbers
		// that would be written

		wchar_t* msg = (wchar_t*)AkAlloca(MaxSize * sizeof(wchar_t));
		msg[MaxSize - 1] = '\0';

		va_list args;
		va_start(args, in_pszFmt);
		vswprintf(msg, MaxSize, in_pszFmt, args);
		va_end(args);

		OutputDebugMsg(msg);
	}

	/// Output a debug message on the console (variadic function).
	/// Use MaxSize to specify the expected size of the message.
	/// If the required size is superior to MaxSize, a new string
	/// will be allocated on the stack with the required size.
	template <int MaxSize = 256>
	inline void OutputDebugMsgV(const char* in_pszFmt, ...)
	{
		int size = 0;
		{
			// Try with a reasonable guess first
			char msg[MaxSize];
			msg[MaxSize - 1] = '\0';

			va_list args;
			va_start(args, in_pszFmt);
			size = vsnprintf(msg, MaxSize, in_pszFmt, args);
			va_end(args);

			// If it was enough, print to debug log
			if (0 <= size && size <= MaxSize)
			{
				OutputDebugMsg(msg);
				return;
			}
		}

		// Else, we need more memory to prevent truncation
		{
			// size + 1 more char for the last '\0'
			size++;

			char* msg = (char*)AkAlloca((size) * sizeof(char));
			msg[size - 1] = '\0';

			va_list args;
			va_start(args, in_pszFmt);
			vsnprintf(msg, size, in_pszFmt, args);
			va_end(args);

			OutputDebugMsg(msg);
		}
	}

#else
	inline void OutputDebugMsg(const wchar_t*){}
	inline void OutputDebugMsg(const char*){}

	template <int MaxSize = 0>
	inline void OutputDebugMsgV(const wchar_t*, ...) {}

	template <int MaxSize = 0>
	inline void OutputDebugMsgV(const char*, ...) {}
#endif

	/// Converts a wchar_t string to an AkOSChar string.
	/// \remark On some platforms the AkOSChar string simply points to the same string,
	/// on others a new buffer is allocated on the stack using AkAlloca. This means
	/// you must make sure that:
	/// - The source string stays valid and unmodified for as long as you need the
	///   AkOSChar string (for cases where they point to the same string)
	/// - The AkOSChar string is used within this scope only -- for example, do NOT
	///   return that string from a function (for cases where it is allocated on the stack)
	#define CONVERT_WIDE_TO_OSCHAR( _wstring_, _oscharstring_ ) ( _oscharstring_ ) = (AkOSChar*)( _wstring_ )

	/// Converts a char string to an AkOSChar string.
	/// \remark On some platforms the AkOSChar string simply points to the same string,
	/// on others a new buffer is allocated on the stack using AkAlloca. This means
	/// you must make sure that:
	/// - The source string stays valid and unmodified for as long as you need the
	///   AkOSChar string (for cases where they point to the same string)
	/// - The AkOSChar string is used within this scope only -- for example, do NOT
	///   return that string from a function (for cases where it is allocated on the stack)
	#define CONVERT_CHAR_TO_OSCHAR( _astring_, _oscharstring_ ) \
		   _oscharstring_ = (AkOSChar*)AkAlloca( (1 + strlen( _astring_ )) * sizeof(AkOSChar)); \
		   AKPLATFORM::AkCharToWideChar( _astring_, (AkUInt32)(1 + strlen(_astring_ )), (AkOSChar*)( _oscharstring_ ) )

	/// Converts a AkOSChar string into wide char string.
	/// \remark On some platforms the AkOSChar string simply points to the same string,
	/// on others a new buffer is allocated on the stack using AkAlloca. This means
	/// you must make sure that:
	/// - The source string stays valid and unmodified for as long as you need the
	///   AkOSChar string (for cases where they point to the same string)
	/// - The AkOSChar string is used within this scope only -- for example, do NOT
	///   return that string from a function (for cases where it is allocated on the stack)
	#define CONVERT_OSCHAR_TO_WIDE( _osstring_, _wstring_ ) _wstring_ = _osstring_

	/// Converts a AkOSChar string into char string.
	/// \remark On some platforms the AkOSChar string simply points to the same string,
	/// on others a new buffer is allocated on the stack using AkAlloca. This means
	/// you must make sure that:
	/// - The source string stays valid and unmodified for as long as you need the
	///   AkOSChar string (for cases where they point to the same string)
	/// - The AkOSChar string is used within this scope only -- for example, do NOT
	///   return that string from a function (for cases where it is allocated on the stack)
	#define CONVERT_OSCHAR_TO_CHAR( _osstring_, _astring_ ) \
			_astring_ = (char*)AkAlloca( 1 + AKPLATFORM::AkWideCharToChar( _osstring_, 0, NULL )); \
			AKPLATFORM::AkWideCharToChar( _osstring_, AkUInt32(1 + AKPLATFORM::AkWideCharToChar( _osstring_, 0, NULL )), _astring_ );

	/// Get the length, in characters, of a NULL-terminated AkUtf16 string
	/// \return The length, in characters, of the specified string (excluding terminating NULL)
	inline size_t AkUtf16StrLen( const AkUtf16* in_pStr )
	{
		return ( wcslen( in_pStr ) );
	}

	/// Get the length, in characters, of a NULL-terminated AkOSChar string
	/// \return The length, in characters, of the specified string (excluding terminating NULL)
	inline size_t OsStrLen( const AkOSChar* in_pszString )
	{
		return ( wcslen( in_pszString ) );
	}

	/// AkOSChar version of sprintf().
	#define AK_OSPRINTF swprintf_s

	/// Compare two NULL-terminated AkOSChar strings
	/// \return
	/// - \< 0 if in_pszString1 \< in_pszString2
	/// -    0 if the two strings are identical
	/// - \> 0 if in_pszString1 \> in_pszString2
	/// \remark The comparison is case-sensitive
	inline int OsStrCmp( const AkOSChar* in_pszString1, const AkOSChar* in_pszString2 )
	{
		return ( wcscmp( in_pszString1,  in_pszString2 ) );
	}

	/// Compare two NULL-terminated AkOSChar strings up to the specified count of characters.
	/// \return
	/// - \< 0 if in_pszString1 \< in_pszString2
	/// -    0 if the two strings are identical
	/// - \> 0 if in_pszString1 \> in_pszString2
	/// \remark The comparison is case-sensitive
	inline int OsStrNCmp(  const AkOSChar* in_pszString1, const AkOSChar* in_pszString2, size_t in_MaxCountSize)
	{
		return wcsncmp(in_pszString1, in_pszString2, in_MaxCountSize);
	}
	
	#define AK_UTF16_TO_WCHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::SafeStrCpy(		in_pdDest, in_pSrc, in_MaxSize )
	#define AK_WCHAR_TO_UTF16(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::SafeStrCpy(		in_pdDest, in_pSrc, in_MaxSize )
	#define AK_UTF8_TO_OSCHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkCharToWideChar( in_pSrc, in_MaxSize, in_pdDest )
	#define AK_UTF16_TO_OSCHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::SafeStrCpy(		in_pdDest, in_pSrc, in_MaxSize )
	#define AK_UTF16_TO_CHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkWideCharToChar( in_pSrc, in_MaxSize, in_pdDest )
	#define AK_CHAR_TO_UTF16(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkCharToWideChar( in_pSrc, in_MaxSize, in_pdDest )
	#define AK_OSCHAR_TO_UTF16(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::SafeStrCpy(		in_pdDest, in_pSrc, in_MaxSize )

	/// Detects whether the string represents an absolute path to a file
	inline bool IsAbsolutePath(const AkOSChar* in_pszPath, size_t in_pathLen)
	{
		return
			(in_pathLen >= 3 && in_pszPath[1] == ':' && in_pszPath[2] == '\\') || // Classic "C:\..." DOS-style path
			(in_pathLen >= 2 && in_pszPath[0] == '\\'); // Uncommon "\..." absolute path from current drive, or UNC "\\..."
	}

	// Use with AkOSChar.
	#define AK_PATH_SEPARATOR	(L"\\")
	#define AK_LIBRARY_PREFIX	(L"")
	#define AK_DYNAMIC_LIBRARY_EXTENSION	(L".dll")

	#define AK_FILEHANDLE_TO_UINTPTR(_h) ((AkUIntPtr)_h)
	#define AK_SET_FILEHANDLE_TO_UINTPTR(_h,_u) _h = (AkFileHandle)_u

	#if defined(AK_ENABLE_PERF_RECORDING)
	
		static AkUInt32 g_uAkPerfRecExecCount = 0;	
		static AkReal32 g_fAkPerfRecExecTime = 0.f;

		#define AK_PERF_RECORDING_RESET()	\
			AKPLATFORM::g_uAkPerfRecExecCount = 0;\
			AKPLATFORM::g_fAkPerfRecExecTime = 0.f;

		#define AK_PERF_RECORDING_START( __StorageName__, __uExecutionCountStart__, __uExecutionCountStop__ )						\
			AkInt64 iAkPerfRecTimeBefore;																							\
			if ( (AKPLATFORM::g_uAkPerfRecExecCount >= (__uExecutionCountStart__)) && (AKPLATFORM::g_uAkPerfRecExecCount <= (__uExecutionCountStop__)) )	\
				AKPLATFORM::PerformanceCounter( &iAkPerfRecTimeBefore );

		#define AK_PERF_RECORDING_STOP( __StorageName__, __uExecutionCountStart__, __uExecutionCountStop__ )						\
			if ( (AKPLATFORM::g_uAkPerfRecExecCount >= (__uExecutionCountStart__)) && (AKPLATFORM::g_uAkPerfRecExecCount <= (__uExecutionCountStop__)) )	\
			{																														\
				AkInt64 iAkPerfRecTimeAfter;																						\
				AKPLATFORM::PerformanceCounter( &iAkPerfRecTimeAfter );																\
				AKPLATFORM::g_fAkPerfRecExecTime += AKPLATFORM::Elapsed( iAkPerfRecTimeAfter, iAkPerfRecTimeBefore );				\
				if ( AKPLATFORM::g_uAkPerfRecExecCount == (__uExecutionCountStop__) )												\
				{																													\
					AkReal32 fAverageExecutionTime = AKPLATFORM::g_fAkPerfRecExecTime/((__uExecutionCountStop__)-(__uExecutionCountStart__));	\
					AkOSChar str[256];																								\
					swprintf_s(str, 256, AKTEXT("%s average execution time: %f\n"), AKTEXT(__StorageName__), fAverageExecutionTime);\
					AKPLATFORM::OutputDebugMsg( str );																				\
				}																													\
			}																														\
			AKPLATFORM::g_uAkPerfRecExecCount++;
	#endif // AK_ENABLE_PERF_RECORDING

#if (defined(AK_CPU_X86_64) || defined(AK_CPU_X86))
	/// Support to fetch the CPUID for the platform. Only valid for X86 targets
	/// \remark Note that IAkProcessorFeatures should be preferred to fetch this data
	/// as it will have already translated the feature bits into AK-relevant enums
	inline void CPUID(AkUInt32 in_uLeafOpcode, AkUInt32 in_uSubLeafOpcode, unsigned int out_uCPUFeatures[4])
	{
		__cpuidex((int*)out_uCPUFeatures, in_uLeafOpcode, in_uSubLeafOpcode);
	}
#endif
}

#endif  // _AK_PLATFORM_FUNCS_H_
