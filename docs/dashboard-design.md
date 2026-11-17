# Dashboard Product and Visual Design

## Product vision

Build a calm, professional equipment health console for one Raspberry Pi. A
technician should be able to tell the machine's overall state, identify its
most pressured available resource, and understand whether the displayed data is
current without studying the screen. The visual tone is precise and technical,
with enough hierarchy and whitespace to keep a dense monitoring view readable.

The first release covers only CPU utilization, memory utilization, and root
filesystem utilization, plus overall status, thresholds, sample time, and
service connection state. Do not imply that the monitor measures temperature,
network activity, process details, or any other unexposed metric.

## Design principles

- **State before detail:** put overall equipment status and freshness in the
  first visual scan path; show resource values immediately below.
- **Make pressure comparable:** use the same percent scale and visual grammar
  for CPU, memory, and storage while retaining their distinct labels.
- **Treat missing data honestly:** distinguish unavailable from zero and from
  a failed connection. Do not draw missing values as a zero line or healthy
  green gauge.
- **Use color as reinforcement:** pair status color with text, icon/shape, or
  pattern so color is never the only signal.
- **Keep the interface focused:** no extra telemetry, administration controls,
  or decorative charts that do not answer an equipment-health question.
- **Respect the source of truth:** use `docs/api.md` for current data and status
  semantics. HTTP success means the service answered; it does not by itself
  mean the equipment is healthy.

## Proposed page layout

Use a full-viewport dark shell with a restrained top bar and a centered,
max-width content grid. On a large display, use a 12-column grid with generous
gutters and a consistent spacing scale based on 4 px (4, 8, 12, 16, 24, 32,
40, 48). Keep content comfortable to scan at a typical  desktop distance.

```text
┌──────────────────────────────────────────────────────────────────────┐
│ PI / EQUIPMENT HEALTH       connection ● Connected    Updated 12:04:21│
├──────────────────────────────────────────────────────────────────────┤
│ ┌──────────────────────────────┐ ┌─────────────────────────────────┐ │
│ │ OVERALL HEALTH               │ │ RESOURCE PRESSURE               │ │
│ │ HEALTHY                      │ │ Highest available: Memory 72%   │ │
│ │ All available metrics normal │ │ Current value compared to limit│ │
│ └──────────────────────────────┘ └─────────────────────────────────┘ │
│ ┌───────────────┐ ┌───────────────┐ ┌───────────────────────────────┐ │
│ │ CPU           │ │ MEMORY        │ │ ROOT STORAGE                  │ │
│ │ 24.8%         │ │ 72.0%         │ │ 61.2%                         │ │
│ │ gauge + limit │ │ gauge + limit │ │ gauge + limit                 │ │
│ └───────────────┘ └───────────────┘ └───────────────────────────────┘ │
│ ┌──────────────────────────────┐ ┌─────────────────────────────────┐ │
│ │ CPU HISTORY                  │ │ MEMORY HISTORY                  │ │
│ │ line chart                   │ │ line chart                      │ │
│ └──────────────────────────────┘ └─────────────────────────────────┘ │
│ ┌──────────────────────────────────────────────────────────────────┐ │
│ │ STORAGE HISTORY                                                  │ │
│ └──────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

Recommended hierarchy:

1. **Top bar:** product/device identity, connection state, and last successful
   update. Keep these visible at the top without making the header tall.
2. **Health summary:** a prominent status card with the exact status label and
   a short factual explanation. For `WARNING`, name the measured resource(s)
   above threshold when available. For `UNKNOWN`, explain that no valid
   readings are available.
3. **Pressure summary:** identify the highest current `used_pct` among available
   metrics. If none are available, show “No current readings.” Do not rank
   unavailable values.
4. **Metric cards:** CPU, Memory, and Root Storage in that order. Give each a
   large percentage, compact threshold context, a visual utilization meter,
   and relevant capacity values where the API supplies them. Use readable
   units (for example, convert memory kB and storage bytes to scaled display
   units without changing the underlying meaning).
5. **Trend section:** one chart per resource, aligned to a common time axis
   where possible. On wide screens use two charts on the first row and one
   full-width chart below; at narrower sizes stack them.

For the pressure summary, compare only available measurements and use the
threshold values from the response. CPU has its own threshold; memory and root
filesystem use the shared threshold represented in their respective fields.

## Component hierarchy

- **Application shell** — page background, responsive content width, header.
- **Connection indicator** — connected, reconnecting, or disconnected label
  with a compact status marker and accessible text.
- **Last update** — formatted local display time with the API's UTC timestamp
  available in a title or secondary detail. If `sampled_at` is null, show
  “Timestamp unavailable”; do not manufacture a current sample time.
- **Overall health panel** — `HEALTHY`, `WARNING`, or `UNKNOWN`, plus concise
  explanation and optional list of exceeded resources based on current values
  and thresholds.
- **Resource pressure summary** — highest available `used_pct` and its resource
  name, or a no-readings state.
- **Metric card** — resource name, large value, unit, threshold marker, concise
  status, optional capacity details, and an accessible meter.
- **Trend chart card** — resource name, percent axis, time axis, threshold
  reference line, legend/description, and empty-data treatment.
- **Status/empty state** — consistent visual and text treatment for loading,
  unavailable measurements, and disconnected service.

## Visual language and color palette

Use a dark graphite/blue-black foundation with slightly lifted slate cards,
thin low-contrast borders, and restrained shadows. Avoid a pure-black canvas
and avoid bright gradients behind text. Keep accent color for focus and
interactive elements rather than using it on every surface.

Suggested tokens (adjustable during implementation while preserving the
semantic roles):

| Role | Suggested color | Use |
|---|---|---|
| Page background | `#0B1220` | Main canvas |
| Raised surface | `#111C2D` | Cards and header |
| Elevated surface | `#172438` | Hover/selected sub-panels |
| Border | `#26364B` | Card edges and separators |
| Primary text | `#F3F6FA` | Main readings and headings |
| Secondary text | `#A9B7C9` | Labels and supporting details |
| Muted text | `#7F91A8` | Timestamps and axis labels, while maintaining contrast |
| Accent | `#55B8FF` | Focus ring, links, selected chart series |
| Healthy | `#38C992` | Healthy label and normal state |
| Warning | `#FFB547` | Threshold warning and attention state |
| Unavailable | `#8B9AAF` | Missing metric state, paired with explicit text |
| Disconnected/error | `#FF6B72` | Service connection failure |

Maintain WCAG AA contrast for normal text and controls. Use a modern sans-serif
for interface text (system UI or a bundled equivalent). Use tabular numerals
for large values, and optionally a restrained monospaced face for timestamps
and technical metadata. Typography should have a clear scale: compact uppercase
eyebrow labels, strong card headings, large metric values, and comfortably
readable body/support text. Avoid tiny all-caps text for important state.

Cards should have consistent corner radius (roughly 10–14 px), 1 px borders,
and 20–24 px inner padding on desktop. Reserve strong outlines or glow for
keyboard focus and active states. Use simple line icons only when they clarify
meaning; do not depend on emoji.

## Chart and visualization recommendations

Use Chart.js or another established, maintained chart library; do not build a
chart engine from scratch. Use line charts for historical usage, with a
consistent 0–100% vertical scale and readable time labels. Keep grid lines
subtle, chart colors consistent with metric identity, and tooltip values
explicitly labeled with resource, percent, and time. Avoid smoothing that
could suggest measurements the system did not collect.

Show each metric's warning threshold as a thin dashed horizontal reference
line, labeled in the legend or chart context. Since thresholds can differ,
read the threshold from the relevant API object. Use the same resource color
for its metric card and trend line; warning state should be conveyed separately
so a resource color does not imply health.

The current dashboard builds a short rolling series from samples observed by
the browser during that session. It does not use the persistent `/api/history`
route for its chart. Keep the chart labels clear that these are session samples;
never imply that mock data is live or that the chart shows persistent backend
history. If the dashboard later uses `/api/history`, integrate against the
contract documented in `docs/api.md`.

Keep charts legible at their rendered size. Provide an accessible textual
summary or data table alternative for chart values, and do not rely on hover
tooltips as the only way to read a point.

## Responsive behavior

- **Wide desktop (about 1200 px and up):** full 12-column composition; health
  and pressure panels side by side; three equal metric cards; history charts
  in a two-plus-one arrangement. Use available space without stretching text
  measures excessively.
- **Tablet (about 700–1199 px):** health and pressure panels can remain side
  by side if legible; metric cards use a 2+1 grid; charts use two columns only
  when each remains usable, otherwise stack.
- **Phone (below about 700 px):** single-column flow; compact header with
  wrapping or stacked connection/update metadata; health panel first; metric
  cards remain full-width and preserve large values; trend charts stack and
  retain readable axis labels. Avoid horizontal page scrolling.
- Reduce outer gutters on small screens while keeping at least 16 px between
  content and viewport edges. Use fluid type and card padding, but do not shrink
  metric values or controls below comfortable reading/touch sizes.
- Allow the document to scroll vertically on small screens. The desktop should
  use the available viewport efficiently; do not force a fixed-height layout
  that clips cards or charts.

## Loading, warning, unavailable, and disconnected states

- **Initial loading:** show the dashboard structure with restrained skeletons
  or clear loading text. Do not show zero values as placeholders.
- **Connected with fresh sample:** show a connected label, status from the
  response, and the sample timestamp. Keep “service connected” visually
  separate from equipment health.
- **Warning:** use amber plus the word `WARNING`; identify each available
  metric that exceeds its own response threshold. Do not infer warning for an
  unavailable metric.
- **Healthy:** use green plus `HEALTHY` only when the API reports that status.
  Preserve exact backend status semantics.
- **Unknown:** show neutral styling and `UNKNOWN`; explain when measurements
  are unavailable. Do not turn `UNKNOWN` into green or red by assumption.
- **One unavailable measurement:** keep that resource card in place, show
  “Unavailable” or “No reading,” and omit its gauge value/line segment rather
  than rendering zero. Other valid cards remain useful.
- **All measurements unavailable:** show the overall `UNKNOWN` state and an
  explanatory empty state; retain layout stability.
- **Disconnected or request failure:** show a distinct disconnected/error
  connection indicator and last successful update if one exists. Preserve the
  last known reading only if it is clearly marked stale; do not label stale
  data as current. Retry behavior should be quiet and should not flash the
  entire page on each poll.
- **Threshold context:** display the threshold value and mark it on meters and
  charts. State when a value is at/above the line according to the backend's
  actual status rules; do not alter backend status semantics.

The API's `sampled_at` uses UTC with a `Z` suffix. Localize the primary display
for the viewer, and preserve a way to inspect the original UTC value. A null
timestamp is explicitly unavailable.

## Accessibility considerations

- Meet WCAG AA contrast for text, controls, focus outlines, and status labels.
- Never encode healthy/warning/unavailable/disconnected state by color alone;
  include visible words and, where useful, distinct marker shapes or patterns.
- Ensure all interactive elements are keyboard accessible with visible focus.
- Use semantic headings and landmarks, descriptive button/link names, and
  announce meaningful connection or health changes without repeatedly
  announcing every polling update.
- Set accessible names/values for meters, including resource, current percent,
  and warning threshold. Do not expose a null measurement as `0%`.
- Give charts accessible titles and summaries; provide a table or equivalent
  text representation for the plotted values and timestamps.
- Respect reduced-motion preferences. Keep animation short and informational;
  do not pulse warning panels continuously.
- Keep touch targets comfortable on phones and avoid hover-only information.

## Frontend acceptance criteria

1. At first glance, a user can identify overall equipment health, service
   connection state, and the last sample time (or that it is unavailable).
2. CPU, memory, and root filesystem are the only monitored resource cards;
   their current percentages and warning thresholds are easy to compare.
3. The interface identifies the highest-pressure available resource and does
   not rank unavailable metrics.
4. Trend views use an established charting library, a consistent percentage
   scale, clear time context, and threshold references. Missing history is
   represented honestly; mock/demo data is labeled, or a collecting-history
   state is shown until browser-observed samples exist.
5. Healthy, warning, unknown, per-metric unavailable, and disconnected states
   are visually and textually distinct. Missing data is never presented as
   zero, and stale values are never presented as current.
6. The desktop layout uses the full display effectively without crowding;
   tablet and phone layouts reflow without horizontal scrolling or clipped
   charts/cards.
7. Keyboard focus, semantic structure, contrast, accessible meter labels, and
   a text alternative for charts are implemented.
8. All current live data and status semantics follow `docs/api.md`. No extra
   metrics, controls, or assumed history API are added.
