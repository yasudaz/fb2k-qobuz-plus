#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
    "Qobuz Streaming",
    "0.1.0",
    "Streams music from Qobuz.\n"
    "Configure credentials under Advanced Preferences > Tools > Qobuz.\n"
    "Use View > Qobuz > Search... to search and add tracks to a playlist."
);

VALIDATE_COMPONENT_FILENAME("foo_qobuz.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
