#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Claude direct API (NetDevice_ClaudeAPI) ──────────────────
//  Settings for the OPTIONAL NetDevice_ClaudeAPI plugin, which lets
//  this device call the Anthropic Messages API directly over HTTPS —
//  no orchestrator in between.
//
//  ⚠ MODEL, NOT AGENT.  This returns Claude the *model*: text in,
//  text out ("explain this trace", "draft this regex").  It is NOT
//  Claude *Code* — there is no filesystem, shell, git repo, or tool
//  loop, because this device cannot host one.  Anything that must
//  read or edit a codebase still has to go to Claude Code on the Pi
//  via NetDevice_Router above.  Use this as the MIDDLE of a 3-way
//  router (local model → Claude API → Claude Code), never the default.
//
//  ⚠ SECRET: CLAUDE_API_KEY is a real credential stored in firmware
//  on a desk device whose flash can be read.  Scope the key as
//  tightly as you can and rotate it if it leaks.  If the key matters,
//  prefer routing through the Pi (which keeps it on a real machine).
//  Keep Config.h out of public version control.
//
//  Leave CLAUDE_API_KEY empty ("") to keep the plugin compiled in
//  but inert — it logs a warning at boot and answers every query
//  with "[api key not set]" instead of calling out.
//  ⚠ CLAUDE_API_KEY is defined in Secrets.h (git-ignored).
// Model id.  A small, fast, cheap model suits a gadget; bump up only
// if you need stronger text answers.
[[maybe_unused]] constexpr char CLAUDE_MODEL[] = "claude-haiku-4-5";
// Optional persona / instruction prepended to every query ("" = none).
[[maybe_unused]] constexpr char CLAUDE_SYSTEM_PROMPT[] =
    "You are a concise assistant answering from a small desk device. "
    "Keep replies short and plain-text.";
// Longest reply the model will generate.
[[maybe_unused]] constexpr int CLAUDE_MAX_TOKENS = 512;
// Inactivity timeout (ms) and answer-buffer ceiling (bytes), as above.
[[maybe_unused]] constexpr unsigned long CLAUDE_REPLY_IDLE_MS = 30000;
[[maybe_unused]] constexpr unsigned CLAUDE_ANSWER_MAX = 4096;
// Anthropic API version header.  See docs.anthropic.com for the
// current value; this rarely needs changing.
[[maybe_unused]] constexpr char CLAUDE_API_VERSION[] = "2023-06-01";
