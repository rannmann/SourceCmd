// SourceCmd.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <string>
#include <iostream>

#include <windows.h>
#include <Tlhelp32.h>




//----------------------------------------------------------------
// Function: GetRemoteModule
//----------------------------------------------------------------
//
// Gets the handle of a module in another process
//
bool GetRemoteModuleInfo( int pid, const TCHAR* name, MODULEENTRY32& me )
{
	HANDLE hSnap = ::CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, pid );
	if ( hSnap!=INVALID_HANDLE_VALUE )
	{
		me.dwSize = sizeof(me);

		if ( ::Module32First( hSnap, &me ) )
		{
			do
			{
				if ( !_tcscmp( name, me.szModule ) )
				{
					::CloseHandle( hSnap );
					return true;
				}
			}
			while( ::Module32Next( hSnap, &me ) );
		}

		::CloseHandle( hSnap );
	}
	return false;
}
//----------------------------------------------------------------
// Function: FindPattern
//----------------------------------------------------------------
//
// Finds the specified pattern in a memory range. It's a simple brute-force...
//
// Mask should either contain \xFF (scan) or \x00 (ignore).
// Templated version to make your life easier.
//
template< unsigned int L >
inline void* FindPattern( void* begin, void* end, const char (&pat)[L], const char (&mask)[L] )
{
	return FindPattern( begin, end, pat, mask, L );
}
template< unsigned int L >
inline void* FindPattern( void* ptr, unsigned int size, const char (&pat)[L], const char (&mask)[L] )
{
	return FindPattern( ptr, (char*)ptr + size, pat, mask, L );
}
void* FindPattern( void* begin, void* end, const char* pat, const char* mask, size_t len )
{
	// Adjust the end pointer
	end = reinterpret_cast<void*>( (size_t)end - len );
	// Brute-force it
	for ( char* p = (char*)begin; p<end; ++p )
	{
		for ( size_t i = 0; i<len; ++i )
		{
			if ( (p[i]&mask[i])!=pat[i] ) goto not_found;
		}
		return p;
not_found:	;
	}
	return NULL;
}


//----------------------------------------------------------------
// Class: CSourceCommand
//----------------------------------------------------------------
//
// Allows one to execute commands in a source engine game from a another process.
//
class CSourceCommand
{
public:
	static const size_t bufsize = 1024;

	CSourceCommand() : mhProcess(NULL), mpfnRunCmd(NULL), mpRemoteBuf(NULL) { };
	~CSourceCommand();
	bool Init( const TCHAR* process );
	bool Init( int pid );

	bool RunCmd( const char* cmd );

	static void RunInAllInstances( const TCHAR* bin, const char* cmd );

private:
	// Game process handle
	HANDLE mhProcess;
	// Address of ExecuteClientCmd
	void* mpfnRunCmd;
	// Temp buffer to store the command string
	void* mpRemoteBuf;
};

CSourceCommand::~CSourceCommand()
{
	if ( mpRemoteBuf )
		::VirtualFreeEx( mhProcess, mpRemoteBuf, bufsize, MEM_DECOMMIT );
	if ( mhProcess )
		::CloseHandle( mhProcess );
}
bool CSourceCommand::Init( const TCHAR* process )
{
	HANDLE hSnap = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if ( hSnap!=INVALID_HANDLE_VALUE )
	{
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(pe);

		if ( ::Process32First( hSnap, &pe ) )
		{
			do
			{
				if ( !_tcscmp( process, pe.szExeFile ) )
				{
					::CloseHandle( hSnap );
					return Init( pe.th32ProcessID );
				}
			}
			while( ::Process32Next( hSnap, &pe ) );
		}
		::CloseHandle( hSnap );
	}
	return false;
}
bool CSourceCommand::Init( int pid )
{
	// Access the process
	if ( mhProcess = ::OpenProcess( PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION, FALSE, pid ) )
	{
		// Create a temp buffer in the game process
		if ( mpRemoteBuf = ::VirtualAllocEx( mhProcess, NULL, bufsize, MEM_COMMIT, PAGE_READWRITE ) )
		{
			// Find engine.dll
			MODULEENTRY32 me;
			if ( GetRemoteModuleInfo( pid, TEXT("engine.dll"), me ) )
			{
				size_t size = me.modBaseSize;
				HMODULE hmEngine = (HMODULE)me.modBaseAddr;

				// And dump it
				void* dump = malloc( size );
				if ( ::ReadProcessMemory( mhProcess, hmEngine, dump, size, NULL ) )
				{
					// Do a sigscan for CEngineClient::ExecuteClientCmd
					// (old) Pattern: 8B 44 24 04 50 E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 83 C4 08 E8 ? ? ? ? C2 04 00
					// (old) Pattern: 55 8B EC 8B 45 08 50 E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 83 C4 08 5D C2 04 00
					// Pattern: 55 8B EC 8B 45 08 50 E8 ? ? ? ? 83 C4 04 5D C2 04 00
					if ( void* p = FindPattern( dump, size,
						"\x55\x8B\xEC\x8B\x45\x08\x50\xE8\x00\x00\x00\x00\x83\xC4\x04\x5D\xC2\x04\x00",
						"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF" ) )
					{
						mpfnRunCmd = reinterpret_cast<void*>( (size_t)p - (size_t)dump + (size_t)hmEngine );
						free( dump );
						return true;
					}
				}
				free( dump );
			}
		}
	}
	return false;
}
bool CSourceCommand::RunCmd( const char* cmd )
{
	// Check str length
	size_t size = strlen( cmd )+1;
	if ( size<=bufsize )
	{
		// Write the string
		if ( ::WriteProcessMemory( mhProcess, mpRemoteBuf, cmd, size, NULL ) )
		{
			// Run command
			if ( HANDLE hThread = ::CreateRemoteThread( mhProcess, NULL, 0, (LPTHREAD_START_ROUTINE)mpfnRunCmd, mpRemoteBuf, 0, NULL ) )
			{
				::WaitForSingleObject( hThread, INFINITE );
				::CloseHandle( hThread );
				// Success
				return true;
			}
		}
	}
	return false;
}
void CSourceCommand::RunInAllInstances( const TCHAR* bin, const char* cmd )
{
	HANDLE hSnap = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if ( hSnap!=INVALID_HANDLE_VALUE )
	{
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(pe);

		// Loop over & run command in all matching processes
		if ( ::Process32First( hSnap, &pe ) )
		{
			do
			{
				if ( !wcscmp( bin, pe.szExeFile ) )
				{
					// Try to run the command for this process
					CSourceCommand sc;
					if ( sc.Init( pe.th32ProcessID ) && sc.RunCmd( cmd ) )
						continue;
				}
			}
			while( ::Process32Next( hSnap, &pe ) );
		}
		::CloseHandle( hSnap );
	}
}






//----------------------------------------------------------------
// Function: main
//----------------------------------------------------------------
//
// Run without args or:
//  sourcecmd <process> <command>
//
// Example
//  sourcecmd "hl2.exe" "echo Hello World!"
//
int _tmain( int argc, TCHAR* argv[] )
{
	// Interactive mode
	if ( argc<3 )
	{
		CSourceCommand src;

		std::cout <<
			"Welcome to SourceCmd made by Casual!\n"
			"This program allows you to execute commands in any opened source engine game.\n"
			"Command line mode: SourceCmd.exe <process> <commands>\n"
			"Type 'q' to end.\n"
			"\n"
			"Process: ";
		
		{
			std::wstring game;
			std::getline( std::wcin, game );

			if ( !src.Init( game.c_str() ) )
			{
				std::cout << "Process not found!\n";
				return 1;
			}
		}

		std::string str;
		while ( true )
		{
			std::cout << "> ";
			std::getline( std::cin, str );

			if ( str=="q" )
			{
				break;
			}
			if ( !src.RunCmd( str.c_str() ) )
			{
				std::cout << "Error executing command!\n";
			}
		}
	}
	// Auto mode
	else
	{
		// Ok lol, input is in wchar_t and I need chars...
		std::string cmd("");
		for ( const TCHAR* str = argv[2]; *str; str++ )
		{
			cmd.push_back( static_cast<char>( *str ) );
		}

		// No error reporting... *yikes*
		CSourceCommand::RunInAllInstances( argv[1], cmd.c_str() );
	}

	return 0;
}

