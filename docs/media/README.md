# Videos for the README

Each module in the [README tour](../../README.md#the-command-center-tour) has a hidden marker where
its video goes:

```
<!-- VIDEO keymap: paste the video link on the next line (docs/media/README.md) -->
```

Markers: `dashboard`, `keymap`, `tap-and-sequences`, `commands`, `apps`, `lighting`, `macros`,
`update`. Best: film the DeepDeck and the app together, 10–30 seconds, so the reader sees a change
in the app and its effect on the screen and LEDs.

## Option A: the video plays inside the README (recommended)

1. Open `README.md` on GitHub and click the pencil (**Edit**).
2. Find the marker of the module and put the cursor on the empty line under it.
3. **Drag the video file** (`.mp4` or `.mov`, up to 100 MB) into the editor. GitHub uploads it and
   writes a line like `https://github.com/user-attachments/assets/…`.
4. **Commit changes**. GitHub shows that link as a video player.

## Option B: a GIF in the repository

GIFs play everywhere, also outside GitHub, but are larger. With [ffmpeg](https://ffmpeg.org):

```sh
ffmpeg -i keymap.mp4 -vf "fps=12,scale=900:-1:flags=lanczos" -loop 0 docs/media/keymap.gif
```

Then under the marker: `![Keymap on the DeepDeck](docs/media/keymap.gif)`. Keep GIFs under about
10 MB; shorten or lower `fps`/`scale` if they are bigger.

## Screenshots

The images in `docs/images/` were taken from the app at 1600 px width, with WiFi names, IP and MAC
addresses masked. Retake them after a design change the same way, so the README stays current.
