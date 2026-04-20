// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>

#pragma once

#define NOMINMAX
#define _WIN32_WINNT 0x0600
#define WINVER       0x0600
#define _WIN32_IE    0x0700

// WinSock2.h must come before windows.h
#include <WinSock2.h>
#include <windows.h>
#include <commctrl.h>

#include <SDK/foobar2000.h>
#include <helpers/advconfig_impl.h>

#include <string>
#include <vector>
#include <functional>
