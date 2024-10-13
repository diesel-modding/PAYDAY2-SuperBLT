#include "console.h"
#include <FCNTL.H>
#include <conio.h>
#include <io.h>
#include <iostream>
#include <windows.h>

namespace
{
	class outbuf : public std::streambuf
	{
	  public:
		outbuf()
		{
			setp(0, 0);
		}

		virtual int_type overflow(int_type c = traits_type::eof()) override
		{
			return fputc(c, stdout) == EOF ? traits_type::eof() : c;
		}
	};

	outbuf obuf;
	std::streambuf* sb = nullptr;
} // namespace
static BOOL WINAPI MyConsoleCtrlHandler(DWORD dwCtrlEvent)
{
	return dwCtrlEvent == CTRL_C_EVENT;
}

CConsole::CConsole() : m_OwnConsole(false)
{
	if (!AllocConsole())
		return;

	SetConsoleCtrlHandler(MyConsoleCtrlHandler, TRUE);
	RemoveMenu(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_BYCOMMAND);

	FILE* tmp;
	freopen_s(&tmp, "conout$", "w", stdout);
	freopen_s(&tmp, "conout$", "w", stderr);
	freopen_s(&tmp, "CONIN$", "r", stdin);

	// Redirect std::cout to the same location as stdout, otherwise you it won't appear on the console.
	sb = std::cout.rdbuf(&obuf);

	m_OwnConsole = true;
}

CConsole::~CConsole()
{
	if (m_OwnConsole)
	{
		std::cout.rdbuf(sb);
		SetConsoleCtrlHandler(MyConsoleCtrlHandler, FALSE);
		FreeConsole();
	}
}
