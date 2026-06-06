#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Claude escalation: local→cloud router (NetDevice_Router) ──
//  Settings for the NetDevice_Router plugin — the "router + voice"
//  tier that makes this device the single chat entry point.  It
//  classifies each turn on-device, answers trivial ones from the
//  on-board Module LLM, and ESCALATES hard ones (anything that
//  needs a filesystem, a shell, or multi-file reasoning) to an
//  orchestrator running on another box — typically an Orange Pi —
//  which fronts Claude Code.
//
//  This device never holds an Anthropic key in this mode: it only
//  knows how to reach your orchestrator; the orchestrator owns
//  Claude Code.  The chain is  this device → Pi → Claude Code.
//
//  To enable: install NetDevice_Router.h into plugins/, register it
//  in the .ino AFTER the Module LLM (so the router can delegate to
//  it), and point ROUTER_PI_* at your orchestrator's /delegate
//  endpoint.  The orchestrator is expected to answer a streamed
//  text/event-stream reply (data: {"delta":"..."} ... data: [DONE]).
//
//    fw.addPlugin(llm);                        // Module LLM first
//    fw.addPlugin(new NetDevice_Router(llm));  // router second
//
//  Leave ROUTER_PI_HOST empty ("") to keep the plugin compiled in
//  but inert — every turn is then answered locally and nothing is
//  ever escalated.
[[maybe_unused]] constexpr char     ROUTER_PI_HOST[] = "";  // set to your Pi orchestrator (e.g. "pi1.local") to enable escalation
[[maybe_unused]] constexpr unsigned ROUTER_PI_PORT = 443;
[[maybe_unused]] constexpr char     ROUTER_PI_PATH[] = "/delegate";
// ⚠ ROUTER_BEARER (the token the orchestrator checks; "" = no
// Authorization header) is defined in Secrets.h (git-ignored).
// TLS verification for the orchestrator connection.  A LAN box with
// a self-signed cert → true (encrypt only, skip the cert check).
// For a pinned CA, set false and add ROUTER_CA_CERT + setCACert()
// in the plugin's beginPins(), mirroring the MQTT_CA_CERT pattern.
#define ROUTER_TLS_INSECURE true
// Abandon a reply only after this long with NO token at all — an
// INACTIVITY timeout, not a total cap (a long answer that keeps
// streaming stays healthy).  Matches the Module LLM's REPLY_IDLE_MS.
[[maybe_unused]] constexpr unsigned long ROUTER_REPLY_IDLE_MS = 60000;
// Ceiling on the in-RAM answer buffer (bytes).
[[maybe_unused]] constexpr unsigned ROUTER_ANSWER_MAX = 4096;
// Opening the outbound TLS socket to the Pi allocates a large mbedTLS
// buffer.  If the dashboard's HTTPS server is mid-handshake on several
// browser sockets at that instant, heap can be momentarily too
// fragmented and connect() fails (the same condition behind the
// dashboard's intermittent "SSL_new failed" log lines).  Retry a few
// times with a short backoff so a transient burst doesn't drop an
// escalation.  ROUTER_CONNECT_TRIES total attempts; ROUTER_CONNECT_
// BACKOFF_MS is the gap, doubled each retry.
[[maybe_unused]] constexpr unsigned ROUTER_CONNECT_TRIES = 3;
[[maybe_unused]] constexpr unsigned ROUTER_CONNECT_BACKOFF_MS = 250;
// If the on-board Module LLM is unavailable (e.g. it failed to load
// at boot, or is wedged), a "local" turn would normally dead-end with
// "[local LLM busy or unavailable]".  Turn this on to instead ESCALATE
// those turns to the Pi orchestrator, so the device stays useful while
// the local model is down.  Only helps if ROUTER_PI_HOST is set and the
// Pi is up.  Off by default (a down local model fails the turn rather
// than silently sending everything to the cloud).
#define ROUTER_FALLBACK_ESCALATE false
// The on-device escalation prefilter (the §02 heuristic).  A turn is
// escalated if it matches any of these words, contains a path-like
// "/", or names a source-file extension.  Comma-separated, lower
// case; bias toward escalating — deflecting an easy turn is cheaper
// than under-serving a hard one.  Edit freely to tune routing.
[[maybe_unused]] constexpr char ROUTER_ESCALATE_KEYWORDS[] =
    "refactor,debug,test,implement,rewrite,fix,build,run,compile,"
    "grep,commit,deploy,stack trace,exception,migrate,function,class";
