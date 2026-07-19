//
// RT64 — Goemon64 Android performance diagnostic (TEMPORARY)
//
// Emits one "[g64prof] ..." line per second to stderr, which reaches Android
// logcat via the Goemon64-stdio pump in src/main/main.cpp. Grab it with:
//
//     adb logcat -s Goemon64-stdio | grep g64prof
//
// Auto-enabled on Android only. To disable without editing call sites, build
// with -DRT64_PROFILE_LOGCAT=0. Desktop builds are unaffected.
//
// This is diagnostic scaffolding for the menu-framerate investigation, NOT a
// shippable feature. Remove it (or gate it behind developerMode) before any
// public release.
//
#pragma once

#if defined(__ANDROID__) && !defined(RT64_PROFILE_LOGCAT)
#   define RT64_PROFILE_LOGCAT 1
#endif

#ifndef RT64_PROFILE_LOGCAT
#   define RT64_PROFILE_LOGCAT 0
#endif

#if RT64_PROFILE_LOGCAT
#   include <cstdio>
#endif
