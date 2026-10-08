// SPDX-License-Identifier: GPL-3.0-or-later
// Vita3K build only (SWGB_VITA3K): no-op stand-ins for SceShaccCgExt, whose
// taiHEN hooks are unavailable in the emulator. vitashark then uses the
// plain libshacccg compiler.
int sceShaccCgExtEnableExtensions(void) { return 0; }
int sceShaccCgExtDisableExtensions(void) { return 0; }
