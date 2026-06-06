#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Optional LLM yes/no tiebreaker (the §02 "model-judged middle") ──
//  The keyword/extension prefilter above is fast but blunt: it escalates
//  only on an EXPLICIT signal, so a genuinely hard request worded without
//  any trigger word silently stays on the tiny local model.  Turn this on
//  to let the ON-DEVICE model adjudicate the ambiguous middle — the turns
//  the prefilter did NOT flag as hard.  Such a turn first runs ONE short
//  yes/no classification inference on the local Module LLM; "yes" routes
//  it to the escalation target (Pi, else the direct API), "no" answers it
//  locally as usual.
//
//  Decisive keyword/path/extension hits skip the tiebreaker entirely (no
//  added latency on the obvious cases).  The tiebreaker is also skipped
//  when there is nowhere to escalate to (no Pi and no direct API) or when
//  no local model is wired.  Cost: one extra short local inference before
//  an ambiguous turn is dispatched.  Requires a local Module LLM.
#define ROUTER_LLM_TIEBREAK false
//  The classification prompt.  Steer the model to answer with a single
//  decisive token; the reply is matched (case-insensitive) for
//  "yes"/"escalate"/"code" to mean "escalate", anything else means
//  "handle locally".  Keep it short — the 0.5B model follows terse,
//  explicit instructions best.
[[maybe_unused]] constexpr char ROUTER_TIEBREAK_PROMPT[] =
    "Classify the request below. Answer with ONE word only: YES if it "
    "needs a powerful coding or agent assistant (writing/editing code, "
    "multi-step problem solving, running tools, reading files), or NO if "
    "a small on-device assistant can answer it directly. YES or NO only.";
//  Trace switch for tuning the tiebreaker.  When true the router prints
//  the model's RAW classification reply (not just the yes/no verdict) to
//  the serial console — so you can see exactly what the 0.5B said and
//  refine ROUTER_TIEBREAK_PROMPT.  Leave false in normal use; ignored
//  unless ROUTER_LLM_TIEBREAK is true.
#define ROUTER_TIEBREAK_TRACE false
