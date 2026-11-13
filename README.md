Niri-style scrollable tiling for Windows. The mod runs inside `explorer.exe`
and keeps, per monitor, a horizontal **strip of columns**. Each column holds
one or more windows stacked vertically. The strip can be wider than the screen
and scrolls to keep the focused column in view.

## Features

- **Infinite horizontal scroll.** Columns keep their width (default 50%) and
  the strip scrolls instead of squeezing everything into the screen.
- **Real column model.** A new window opens as a new column to the right of the
  focused one. Move, consume and expel operate on real columns.
- **Animations** for position and size, with easing.
- **Focus ring** through the DWM border color (Windows 11).
- **Tabbed columns**: the windows of a column share the same spot and focus
  cycles through them.
- **Multi-monitor.** One strip per monitor, focus and move columns between
  monitors, columns that are off-screen never leak onto a neighboring monitor.
- **Minimum-size aware.** Asks windows for their minimum size and also learns it
  from what the window actually ended up with, then widens the column.
- **Correct gaps.** Invisible resize borders of Win10/11 windows are compensated.
  Inner and outer gaps are separate settings.
- **Safe.** Positioning is asynchronous, so a hung app cannot freeze the mod or
  block unloading. Windows are restored on unload, and windows left off-screen
  by a crash are rescued on the next start.
- **Pause switch** that restores every window to where it was.
- Real move/size events for dragging, fullscreen apps pause the mod, elevated
  windows are detected and skipped, ignore lists for processes and classes,
  File Explorer windows can be tiled.

## Hotkeys

Default modifier is **Ctrl+Alt** (configurable in the settings).

| Shortcut | Action |
|----------|--------|
| `Left / Right` | Focus column left / right |
| `Up / Down` | Focus window above / below in the column |
| `Home / End` | Focus first / last column |
| `Shift+Left / Right` | Move column left / right |
| `Shift+Up / Down` | Move window up / down inside the column |
| `[` / `]` | Consume window into the left / right column, or expel it out of a stack |
| `W` | Cycle column width 33 / 50 / 67 / 100% |
| `Space` | Toggle full-width column |
| `C` | Center the focused column |
| `T` | Toggle tabbed column |
| `,` / `.` | Focus the previous / next monitor |
| `Shift+,` / `Shift+.` | Move the focused column to the previous / next monitor |
| `Shift+F` | Toggle floating |
| `R` | Retile (un-floats everything, forgets learned minimum sizes) |
| `P` | Pause / resume tiling (restores all windows while paused) |

Some combinations are already taken by Windows or by drivers (for example
Ctrl+Alt+Arrows with Intel graphics hotkeys, or Win+R / Win+T). Choose the
Ctrl+Alt+Win modifier in the settings to avoid most conflicts. Failed
registrations are written to the log.

## Multiple monitors

A window cannot be clipped to one monitor on Windows. Columns that are not
(fully) visible are therefore parked just outside the virtual desktop, so they
never show up on the neighboring monitor. Partially visible columns ("peeking")
are only shown on sides that have no neighboring monitor.

## Troubleshooting

- **A window does not tile.** It may be fixed-size, a dialog or tool window,
  always-on-top, maximized, elevated, UWP (see the setting), or matched by an
  ignore list. Turn on *Verbose log* to see what the mod decides.
- **A window is lost off-screen.** Press the pause hotkey, or disable the mod:
  both put every window back where it was.
- **Hotkeys do nothing.** Check the log for registration failures and change
  the hotkey modifier.
- **Flicker in heavy apps** (browsers, IDEs): turn off *Animate size changes*.
- **No focus ring.** It needs Windows 11 (build 22000 or newer).

## Limitations that cannot be fixed from inside a Windhawk mod

- **Elevated (admin) windows** cannot be moved by a non-elevated Explorer
  (UIPI). They are detected and never tiled.
- **Minimum size:** the mod can only respect it. It cannot make an app shrink
  below what the app allows, so the column becomes wider and the strip scrolls.
- **Focus ring** is the 1px DWM border color, not a thick compositor ring.
- **Tabbed columns** stack the windows in place, there is no tab bar.
- **Workspaces** are not implemented. Windows of all virtual desktops share the
  strip and windows of other desktops are skipped while they are cloaked.
- Fullscreen apps and presentations pause the mod.
*/
