# niri-inspired-autotile-windhawk

Niri-style scrollable tiling for Windows. The mod runs inside `explorer.exe`
and keeps, per monitor, a horizontal **strip of columns**. Each column holds
one or more windows stacked vertically. The strip is wider than the screen
when needed and scrolls to keep the focused column in view.

## What's in 0.1.0

- **Infinite horizontal scroll.** Columns keep their width (default 50%) and
  the strip scrolls instead of squeezing everything into the screen.
- **Real column model.** New windows open as a new column right of the focused
  one. Move/consume/expel work on real columns, not on guessed geometry.
- **Animations** (position and size) with easing.
- **Focus ring** via the DWM border color (Windows 11).
- **Tabbed columns** (windows stacked in the same spot, focus cycles).
- **Minimum-size aware.** Asks windows for their minimum size and also learns it
  from what the window actually ended up with, then widens the column.
- **Correct gaps.** Invisible resize borders of Win10/11 windows are compensated.
- **Non-blocking.** Positioning is asynchronous, so a hung app cannot freeze
  the mod or block unloading.
- Dragging a window uses the real move/size events (not geometry guessing).
- File Explorer windows can be tiled. Elevated windows are detected and skipped.
- Ignore lists for processes and window classes.
- Windows parked off-screen are restored on unload, and rescued after a crash.
- Hardening (0.3.1): robust focus switching, stuck-drag recovery, no false
  "minimum size" learning from slow apps, windows that Windows pulls back
  from the parking area are re-parked, no animation across neighboring
  monitors (avoids DPI-change glitches), safer handling of reused window handles.

## Hotkeys (default modifier: Ctrl+Alt, configurable)

| Shortcut | Action |
|----------|--------|
| `Left / Right` | Focus column left / right |
| `Up / Down` | Focus window above / below in the column |
| `Home / End` | Focus first / last column |
| `Shift+Left / Right` | Move column left / right |
| `Shift+Up / Down` | Move window up / down inside the column |
| `[` / `]` | Consume window into the left/right column, or expel it out of a stack |
| `W` | Cycle column width 33 / 50 / 67 / 100% |
| `Space` | Toggle full-width column |
| `C` | Center the focused column |
| `T` | Toggle tabbed column |
| `Shift+F` | Toggle floating |
| `R` | Retile (also un-floats everything) |

Some combinations (for example Ctrl+Alt+Arrows when Intel graphics hotkeys are
enabled, or Win+R / Win+T) are already taken. Pick the Ctrl+Alt+Win modifier in
the settings to avoid most conflicts. Registration failures are written to the log.

## Behavior on multiple monitors

Windows on Windows cannot be clipped to a monitor. Columns that are not
(fully) visible are therefore parked just outside the virtual desktop, so they
never show up on the neighboring monitor. Partially visible columns ("peeking")
are only shown on sides that have no neighboring monitor.

## Limitations that cannot be fixed from inside a Windhawk mod

- **Elevated (admin) windows** cannot be moved by a non-elevated Explorer
  (UIPI). They are detected and left floating.
- **Minimum size:** the mod can only respect it. It cannot make an app shrink
  below what it allows, so the column becomes wider and the strip scrolls.
- **Focus ring** is the 1px DWM border color (Windows 11 only), not a thick
  compositor ring. A thick ring would need an overlay window.
- **Tabbed columns** stack the windows in place. There is no tab bar.
- Workspaces are not implemented. Windows from all virtual desktops share the
  strip; windows on other desktops are simply skipped while they are cloaked.
- Fullscreen apps and presentations pause the mod.
