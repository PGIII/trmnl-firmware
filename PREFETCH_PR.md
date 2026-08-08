# Add an opt-in `prefetch` mode to silently warm the on-device image cache

## Problem

The device keeps a local cache of up to `MAX_CACHED_IMAGES` (30) images, ordered
in NVS via `PREFERENCES_PLAYLIST_ORDER_KEY`. The left/right touchbar taps browse
this cache instantly (`show_cached_image_by_offset` / `update_playlist_order` in
`src/bl.cpp`), with no network round trip — a nice bit of UX.

The catch: the cache can currently only be populated as a *side effect of
displaying* an image. Every one of the three places in `src/bl.cpp` that write
to the playlist order (`update_playlist_order(...)`) is immediately preceded by
a `display_show_image(...)` call, and `/api/display` returns exactly one
`image_url` per poll.

So a server that wants several screens ready for instant local browsing has no
choice today but to make the panel visibly cycle through each one — at roughly
10-15 seconds and a full e-ink refresh apiece. That's a bad experience for
something the user never asked to see yet.

## Design

Add an opt-in `prefetch` boolean to the `/api/display` response. When present
and `true`, the device still downloads the image and still registers it in the
playlist order (that's the entire point — it becomes instantly browsable), but
it does **not** put the image on the panel, and it does not disturb any of the
state that tracks what's currently displayed.

This is a generically useful primitive for any BYOS server or playlist
preloader — not specific to one application — so it lives at the protocol
level rather than as bespoke logic for a particular use case.

### Backward compatibility

- The field is opt-in and defaults to `false`. When a server's response omits
  `prefetch` entirely (as every server does today), behavior is byte-for-byte
  identical to before this change.
- Parsing follows the existing precedent set by `maximum_compatibility`
  (`lib/trmnl/src/parse_response_api_display.cpp`): `doc["prefetch"] | false`,
  so older servers that don't know about this field never affect newer
  firmware, and the deserialization-error path explicitly initializes the new
  field alongside the others.

### What changes when `prefetch: true`

In `src/bl.cpp`'s `downloadAndShow()`, there are three cache-write call sites
(the already-cached-on-flash fast path, the TRMNL-X 5GHz modem download path,
and the generic PNG/JPEG download path). At each one, when `prefetch` is set:

- The image is still downloaded (or read from flash) and written to the
  filesystem exactly as before.
- `display_show_image(...)` is skipped, so the panel keeps showing whatever
  it already had. No e-ink refresh happens.
- `DisplayedImage::remember(...)`, `PREFERENCES_CURRENT_PATH_KEY`, and
  `PREFERENCES_LAST_PATH_KEY` are left untouched — these all track *what is on
  screen*, and a prefetched image was never on screen, so it must not claim to
  be. Also skipped is the same-image dedupe check (`DisplayedImage::matches`),
  which only makes sense relative to what's actually displayed.
- `PREFERENCES_BROWSE_PATH_KEY` (the touchbar's current browse position) is
  also left alone, so a prefetch run doesn't shift where the user is browsing.
- The new image is still registered via `update_playlist_order(...)`, so it
  takes its place in the instant-browse order.
- The device still sleeps for the returned `refresh_rate` exactly as normal
  afterward, so a server can drive a rapid prefetch sequence (short interval,
  several `prefetch: true` responses in a row) followed by one final response
  with `prefetch` omitted/`false` to actually show the screen the user should
  see.

The three sites share this behavior through a small new helper,
`prefetch_into_playlist()`.

### Behavior on non-TRMNL-X boards

The instant-browse playlist cache (`update_playlist_order`,
`show_cached_image_by_offset`, the touchbar gestures that drive it) is a
TRMNL-X-only feature — it's compiled out entirely on other boards. That raised
the question of what `prefetch: true` should mean on a board with no playlist
to insert into.

I chose to keep "download without displaying" meaningful on every board,
rather than making `prefetch` a silent no-op off the X: `prefetch_into_playlist()`
is defined unconditionally, but its body — the NVS bookkeeping and the call to
the X-only `update_playlist_order()` — is compiled only under
`#ifdef BOARD_TRMNL_X`. So on a non-X board, setting `prefetch: true` still
downloads and caches the file to flash and still skips the display, it just
has no playlist to register it in (there being no browse feature to register
it with). This keeps the protocol semantics uniform across hardware instead of
having the same server response mean two different things depending on which
board answers it.

One consequence: because `update_playlist_order()` itself is only defined
inside the TRMNL-X-only region of `src/bl.cpp` (alongside the rest of the
touchbar/gesture code), `prefetch_into_playlist()` had to be placed *after*
that region ends in the file (immediately before `downloadAndShow()`, its only
caller) rather than next to `update_playlist_order()` — otherwise it would
itself only exist on TRMNL-X and the unconditional call sites in
`downloadAndShow()` wouldn't link on other boards.

### The ordering subtlety

`update_playlist_order(new_path, prev_path)` inserts `new_path` immediately
after `prev_path` in the playlist. The normal (displaying) call sites pass the
image that was *previously* current as `prev_path`, which is what you want
when there's one new image at a time: it lands right after the last thing
shown.

If a run of prefetches naively reused that same anchor — the image that's
still on screen, since prefetching by definition doesn't change it — every
prefetched image would be inserted right after that one fixed anchor, and the
whole run would land in **reverse** order relative to what the server sent.

To avoid this, prefetching tracks its own anchor in a new NVS key,
`PREFERENCES_PREFETCH_PATH_KEY` (`"prefetch_path"`). The first prefetch in a
run anchors on the currently-displayed image (the same anchor the display
path would have used); each subsequent prefetch anchors on the *previously
prefetched* path instead, so the run threads through the list in the order it
arrived. Any real (non-prefetch) display clears this key, so the next
prefetch run starts fresh from whatever is now on screen.

### Not touched

The BMP download path in `downloadAndShow()` doesn't have a corresponding
`update_playlist_order` call today and is unchanged; prefetching a BMP image
is out of scope for this change, matching the existing asymmetry between the
BMP and PNG/JPEG paths.

## Testing

- `lib/trmnl/src/parse_response_api_display.cpp` / `api_types.h`: added the
  `prefetch` field and its default-`false` parsing, following the
  `maximum_compatibility` precedent.
- `test/test_parse_api_display/api_display.test.cpp`: added cases for
  `prefetch` absent (defaults `false`), `prefetch: true`, and
  `prefetch: false`, following the file's existing conventions.
- Ran `pio test -e native` — all 77 cases pass, including the new ones.
- Ran `pio run -e TRMNL_X` (ESP32-S3, has the playlist cache) — compiles and
  links; `firmware.bin` is produced (~1.4 MB).
- Ran `pio run -e trmnl` (ESP32-C3 OG board, no playlist cache) — also
  compiles and links cleanly with no errors, confirming the change doesn't
  break non-X boards; `firmware.bin` is produced there too. This is the build
  that caught the first version of this patch defining `prefetch_into_playlist`
  inside the TRMNL-X-only region while calling it unconditionally.
- For both targets, the post-build `merge_bin` step fails locally with
  `ModuleNotFoundError: No module named 'rich_click'`, which is a pre-existing
  local toolchain issue in `tool-esptoolpy` unrelated to this change (it
  happens after each `firmware.bin` has already been generated).
- Manual on-device verification of a rapid prefetch sequence (multiple
  `prefetch: true` responses followed by a normal response) was not performed
  as part of this change; reviewers driving real hardware should confirm the
  browse order lands as expected.
