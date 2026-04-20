// Force-included shim for clang-cl + xwin builds.
// WinSock2.h MUST come before windows.h to prevent winsock.h redefinition conflicts.
// Without WIN32_LEAN_AND_MEAN, windows.h brings in objbase.h (defines `interface` macro)
// and mmsystem.h -> timeapi.h (defines timeGetTime, needed by pfc/timers.h).
#include <WinSock2.h>
#include <windows.h>
