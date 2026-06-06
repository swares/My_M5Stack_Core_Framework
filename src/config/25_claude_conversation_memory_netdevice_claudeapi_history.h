#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Claude conversation memory (NetDevice_ClaudeAPI_History) ──
//  Read only by the history-keeping variant of the Claude plugin.
//  Macros (not constexpr) so the plugin's #ifndef defaults defer to
//  these.  Total messages (user+assistant) resent each turn — keep
//  EVEN so trimming stays user-first + alternating.  CHARS bounds both
//  the heap and the tokens you resend.
#define CLAUDE_HISTORY_MAX_MSGS  8
#define CLAUDE_HISTORY_MAX_CHARS 4000
