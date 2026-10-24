# Fuetem Photo

A simple, clean photo viewer for Omarchy, built with Qt 6 Widgets and C++.
Point it at a folder and browse: a grid of thumbnails, one picture at a time,
lights out. It is a viewer only. It has no catalogue or database, it skips
videos, and it never changes, moves or deletes a picture.

It follows the Omarchy theme (background, text, accent and selection colours)
and switches with it while running. Lights out is always black.

## Browsing

- Opens the folder or picture given on the command line, otherwise the last
  folder you had open, otherwise `~/Pictures`. `Ctrl+O` (or **Open**) picks
  another folder.
- Subfolders come first, as large tiles showing their first picture. The path
  along the top takes you back up a level or more; `Backspace` goes up one.
  Month folders (January, February, …) are in calendar order, and file names
  are in natural order (`IMG_2` before `IMG_10`).
- The thumbnail size changes with `Ctrl`+scroll or `+` / `−`. Columns always
  fill the window.
- `D` shows details under each thumbnail: file name, date taken (the file's
  date when there is no EXIF date), size in pixels, format and file size.
  Folders show their name and how many pictures and folders they hold. It is
  remembered between launches; hovering still shows the name without it.
  The date and size come from the file headers, read alongside the thumbnail
  and cached with it.
- `S` (or the **Name ↑** button) changes the order: name, then name reversed,
  then date taken (oldest first), then newest first. Pictures without an
  EXIF date fall back to the file's date.

## One picture

- `E`, `Enter` or a double-click opens the selected picture (`E` is
  Lightroom's Loupe). `G`, `Esc`, `Enter` or a double-click goes back to the
  grid (Lightroom's `G`), at that picture.
- `←` / `→` show the previous and next picture; `Home` / `End` the first and
  last.
- The picture is fitted to the window. Scroll or pinch to zoom around the
  pointer, `Z` or `1` toggles actual size, `0` fits again, and a drag moves a
  zoomed picture.
- `I` shows the file name, size, date taken, camera, lens and exposure.
- `Space` starts and stops a slideshow (a picture every four seconds).

## Everywhere

- `L` cycles the lights: on, dimmed, out (everything but the picture black,
  pointer hidden when still).
- `F` or `F11` toggles fullscreen.
- `Ctrl+C` copies the path of the picture or folder. `Ctrl+Shift+O` shows the
  folder in the file manager.
- `?` lists the shortcuts. `Ctrl+Q` quits.
- `Delete` does nothing except say so.
- Letter keys never fire while you are typing in a text field.

## Formats

JPEG, PNG, WebP, GIF (first frame), TIFF, BMP, HEIC/HEIF, AVIF, JPEG XL, PSD
(the flattened image, 8 or 16 bit), and camera RAW: CR2, CR3, NEF, ARW, DNG,
RAF, ORF, RW2, PEF, SRW and more. EXIF orientation is applied, and pictures
are shown in sRGB.

Qt's own plugins and `kimageformats` (libheif, libavif, libjxl) decode most of
these. RAW files are read with LibRaw: the grid and the viewer use the preview
the camera embedded when it is big enough, and develop the RAW data when it
is not (some drones and phones embed only a small one). PSDs that the
kimageformats plugin refuses are read by a small composite reader in
`src/formats.cpp`. EXIF comes from exiv2.

## Speed

Thumbnails are decoded on worker threads, nearest to the screen first, and
anything scrolled past before its turn is dropped. They are kept on disk in
`~/.cache/fuetem-photo` (600 px, keyed on path, size and modification time),
so a folder is quick the second time. The grid never loads a full picture;
the viewer loads the one shown plus its neighbours.

Settings (last folder, window size, thumbnail size, order) are in
`~/.config/Fuetem/fuetem-photo.conf`.

## Build and install

```sh
./bin/build      # build/fuetem-photo
./bin/test       # the test suite, offscreen
cd pkgbuild && makepkg -f && sudo pacman -U fuetem-photo-*.pkg.tar.zst
```

`./bin/install` does the build and the package install in one go.

Requires Qt 6 (`qt6-base`, `qt6-imageformats`), `kimageformats`, `libraw`,
`libheif`, `libavif`, `libjxl`, `exiv2` and `xdg-desktop-portal` with a backend
(for the Open window).

To check that every format in a folder of real photos decodes:

```sh
FUETEM_SAMPLE_DIR="$HOME/Pictures/My Pictures/2018/December" ./bin/test
```

`FUETEM_PHOTO_SCRIPT` drives the window from a list of steps, for checking it
without touching anything else on the desktop, e.g.
`FUETEM_PHOTO_SCRIPT="wait:2000;key:Return;key:Right;shot:/tmp/a.png;quit"`.

## Credits

Built with [Claude Code](https://claude.com/claude-code).

Licensed under the MIT License; see `LICENSE`.
