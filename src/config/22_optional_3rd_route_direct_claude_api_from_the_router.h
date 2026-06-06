#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Optional 3rd route: direct Claude API from the router ─────
//  By default the router is two-way: trivial → local Module LLM,
//  hard → Pi orchestrator (Claude Code).  Turn ROUTER_DIRECT_API on
//  to add a THIRD route for "smart text" turns — non-coding requests
//  the 0.5B local model would botch but that need no repo (explain,
//  summarise, draft, translate...).  These go straight to the
//  Anthropic API via a NetDevice_ClaudeAPI plugin, skipping the Pi.
//
//  This is the "A + B together" shape (see the doc, §15).  Two cases
//  earn it: (1) LATENCY/COST — a one-hop API answer beats spinning
//  up the full Claude Code agent for a question that produces no
//  diff; (2) RESILIENCE — smart-text answers keep working when the
//  Pi is powered down.  The cost is a real one: enabling this means
//  CLAUDE_API_KEY now lives in this device's flash (see that block).
//  If you don't need to survive a Pi outage, prefer leaving this OFF
//  and letting the Pi orchestrator own all cloud calls (no key here).
//
//  To enable: set this true, register a NetDevice_ClaudeAPI plugin,
//  and pass it to the router as its 2nd constructor argument:
//    auto* llm = new UartDevice_ModuleLLM(Serial2);
//    auto* api = new NetDevice_ClaudeAPI();
//    fw.addPlugin(llm); fw.addPlugin(api);
//    fw.addPlugin(new NetDevice_Router(llm, api));
#define ROUTER_DIRECT_API true
// A non-coding turn is sent to the direct API (instead of the local
// model) when it matches one of these words OR runs longer than
// ROUTER_DIRECT_MIN_WORDS — i.e. it's too rich for the 0.5B but needs
// no codebase.  Comma-separated, lower case.  Ignored unless
// ROUTER_DIRECT_API is true.
[[maybe_unused]] constexpr char ROUTER_DIRECT_KEYWORDS[] =
    "explain,summarise,summarize,draft,translate,rewrite,brainstorm,"
    "compare,outline,reword,paraphrase,why,how,what if";
[[maybe_unused]] constexpr unsigned ROUTER_DIRECT_MIN_WORDS = 12;
