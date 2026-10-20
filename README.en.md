# mote

[한국어](README.md) | English

An ultra-light Windows notepad that lets you color selected text.

Write as simply as in Windows Notepad, and apply text color, bold, italic or underline to the parts you want to stand out. No installation: just run `mote.exe`.

## Features

- **Text color**: Apply one of 8 preset colors or a custom color to the selected text. Your three favorite colors are one keystroke away with Ctrl+F1/F2/F3.
- **Basic formatting**: Bold, italic, underline
- **Files**
  - Rich text documents (.rtf) keep text colors and formatting.
  - Text documents (.txt) are saved with their original encoding and line endings.
- **Find/Replace**: Finds as you type and shows the number of matches. Case-insensitive.
- **Safe saving**
  - Asks before closing if there are unsaved changes.
  - Writes to a temporary file first and then replaces the original, so a problem during saving never corrupts the original.
- **Conveniences**
  - Recent files
  - Open files by dragging and dropping
  - Add mote to the "Open with" list (Help menu)
  - New window (separate process)
  - Remembers the last window position and size
  - Collapsible toolbar
- **Display**
  - Follows the Windows light/dark theme.
  - Stays sharp across monitors with different scaling (DPI).
  - Shows in Korean or English depending on the Windows display language.

No spell check, autocorrect, cloud, accounts, ads or telemetry.

## Requirements

- Windows 10 1809 or later (Windows 11 recommended)
- No runtime installation needed

## Usage

Download `mote.exe` from [Releases](https://github.com/oodhmo/mote-notepad/releases), put it in any folder and run it.

Settings (recent files, window position, color shortcuts, etc.) are stored in the registry under `HKCU\Software\mote`. To remove them, first click **Help > Remove mote from "Open with"** to remove the registration, then delete this key.

### Keyboard shortcuts

You can also see them in the app by pressing **F1** or from **Help > Shortcuts and Features**.

| Category | Shortcut | Action |
|---|---|---|
| File | Ctrl+N | New |
| | Ctrl+Shift+N | New window |
| | Ctrl+O | Open |
| | Ctrl+S | Save |
| | Ctrl+Shift+S | Save as |
| Edit | Ctrl+Z / Ctrl+Y | Undo / Redo |
| | Ctrl+X / C / V | Cut / Copy / Paste |
| | Ctrl+A | Select all |
| Formatting and color | Ctrl+B / I / U | Bold / Italic / Underline |
| | Ctrl+F1 / F2 / F3 | Apply shortcut color (default: red / blue / green) |
| Find and replace | Ctrl+F / Ctrl+H | Find / Replace |
| | F3 / Shift+F3 | Find next / Find previous |
| | Esc | Close the find bar |
| View and help | Ctrl+\ | Hide / show toolbar |
| | F1 | Shortcuts and Features |

To change the colors used by Ctrl+F1–F3, **right-click** a color in the toolbar's text color menu. Assigned colors are marked F1/F2/F3. Colors you pick with Custom color… appear in the **Recent colors** row of the menu (up to 8), and you can assign them the same way.

### Opening files with "Open with"

mote has no installer, so it is handy to register it once.

1. On first run, mote asks once whether to register. Click **Add** there, or later click **Help > Add mote to "Open with"...**.
   - If you click **Later**, it won't ask again.
   - No administrator rights needed.
   - Registered for the current user only.
2. mote now appears in the **Open with** list when you right-click a `.txt`/`.rtf` file. It also appears in Windows **Settings > Apps > Default apps**.
3. To always open files with mote, choose one of the following. Windows does not let apps make themselves the default, so you need to choose it yourself.
   - In **Open settings**, shown right after registering, choose mote.
   - Right-click a file and choose **Open with > mote > Always**.

If you move mote.exe to another folder, the registered path is corrected the next time you run it. To remove the registration, click **Help > Remove mote from "Open with"**.

To use mote without registering, right-click a file, choose **Open with > Choose another app > Choose an app on your PC** and select `mote.exe`. You can also drag a file onto the mote window.

## File formats and encoding

- **.rtf**: Saves text color, bold, italic and underline. Text in the theme's default color is saved as "automatic color", so it looks natural in other themes and other apps.
- **.txt**: Saves text only, without formatting.
  - **Reading**: Automatically detects UTF-8, UTF-8 with BOM, UTF-16 (LE/BE) and the system's legacy code page (CP949 for Korean).
  - **Saving**: Keeps the encoding and line endings (CRLF/LF/CR) of the opened file. New documents are saved as UTF-8 without BOM with CRLF.
  - **Characters the legacy code page can't represent**: If the document contains characters such as emoji that can't be stored in, for example, a CP949 document, mote asks whether to save as UTF-8 before saving.
  - **Mixed line endings**: Unified to the first line ending in the file when saving, same as Windows Notepad.
- Files up to 512MB can be opened.

## Performance

Test system: Intel Core i7-1165G7, 16GB RAM, Windows 11 Pro (10.0.26200)

| Item | Result |
|---|---|
| Executable size | 316KB |
| Startup time (until the window is ready for input, median of 10 runs) | 150ms |
| Idle memory (empty document) | Working set 21MB / private 3.6MB |
| Opening a 50MB text file | About 3.9 s |

## Build

Requirements:
- Visual Studio 2022 (or Build Tools) with "Desktop development with C++"
- CMake 3.21 or later

```powershell
cmake --preset vs2022
cmake --build --preset release
```

The output is `build\Release\mote.exe`. The CRT is linked statically, so it runs standalone on other PCs.

## Versioning and releases

Versions follow the `MAJOR.MINOR.PATCH` format.

| Change | Part to bump | Example |
|---|---|---|
| Bug fixes only (`Fix:`) | PATCH | 1.0.0 → 1.0.1 |
| New features (`Feat:`) | MINOR (reset PATCH to 0) | 1.0.1 → 1.1.0 |
| Incompatible changes (settings or file format, etc.) | MAJOR | 1.1.0 → 2.0.0 |

Changes to docs (`Docs:`) or housekeeping (`Chore:`) alone are not released.

For trying out a version before the official release, use a beta version `x.y.z-beta.N`.
- Bump N each time you fix something based on feedback.
- When it has been tried enough, drop `-beta.N` and release the official version.
- The exe's numeric version can't contain letters, so `-beta.N` is added only to the string in `version.h`.
- Publish it on GitHub as a pre-release by adding `--prerelease` in step 4 below. It is then not marked "Latest".

Release steps:
1. Bump the numbers and the string in `resources/version.h` and commit as `Chore: 버전 x.y.z`. Change the version only in this file.
2. Build in Release and check the version in **Help > About mote**.
3. Tag and push.
   ```powershell
   git tag vx.y.z
   git push origin main --tags
   ```
4. Create a GitHub release and upload `mote.exe`. In the notes, list the features and fixes since the last tag, in both Korean and English.
   ```powershell
   gh release create vx.y.z build/Release/mote.exe --title "mote x.y.z" --notes "..."
   ```

## License

[MIT](LICENSE) © oodhmo
