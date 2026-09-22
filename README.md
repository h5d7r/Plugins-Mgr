# Plugins Mgr — PS4 Payload (Port 4002)

Web-based manager for GoldHEN plugins. Send the payload once, then open:

```text
http://<PS4-IP>:4002
```

Copyright © Haider A.H. All rights reserved.

## What it manages

* `data/GoldHEN/plugins.ini` — sections (`[default]`, `[CUSAxxxxx]`) and
  plugin path lines. Entries may end in `=true` (active) or `=false` (inactive);
  legacy lines without a suffix use a leading `;` to disable. Order and disabled
  lines are preserved.
* `data/GoldHEN/plugins/` — the actual plugin files: list, upload,
  copy/move (rename), delete. Deleting can also strip matching ini lines.
* `/data/payloads/` — installation location for the main payload when using
  the installer payload.

## Features

* Port **4002** web UI with a dark theme, cards, modals, and confirmation dialogs.
* Tabs: All / Enabled / Disabled / Unlisted.
* TitleID section filter, search, sorting, and file counts.
* Upload any file type from the browser with an 8 MB limit.
* Copy, move, rename, and delete plugin files.
* Every `plugins.ini` write first backs up to `plugins.ini.bak`.
* `GET /api/plugins` JSON endpoint.
* `GET /api/log` text log endpoint.
* `POST /api/stop` endpoint to stop the server.
* On-screen PS4 notification on activation.
* Dynamic notification showing the PS4 IP address and port.
* Kernel and file logging to `data/plugins-mgr/log.txt`.
* Secondary installer payload that installs the main payload to
  `/data/payloads/`.

## Payloads

The build produces two payloads:

### Main payload

```text
ps4-plugins-mgr.elf
```

This is the main Plugins Mgr payload. It starts the web interface on port
`4002`.

### Installer payload

```text
ps4-plugins-mgr-installer.elf
```

The installer contains the main payload internally. It does not require the
main ELF file to be placed inside the source directory.

When launched, it installs the main payload to:

```text
/data/payloads/ps4-plugins-mgr.elf
```

The installer uses a temporary file and renames it after a successful write to
reduce the chance of leaving an incomplete payload file.

## API

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Web UI |
| `GET` | `/api/plugins` | `{ sections, files, ini_exists }` JSON |
| `GET` | `/api/log` | Log file text |
| `GET` | `/api/download?name=x` | Download one plugin file as-is |
| `POST` | `/api/download` | `{"name":"x"}` — one file as-is; `{"names":"a,b,c"}` — ZIP of many files |
| `POST` | `/api/files` | `{"names":"a,b,c","clean_ini":true}` — delete multiple files, strip ini refs |
| `POST` | `/api/toggle` | `{"section","path"}` — flip `=true`/`=false`, or legacy `;` comment |
| `POST` | `/api/entry` | `{"section","path","op":"add\|remove"}` |
| `POST` | `/api/file` | `{"name","op":"delete\|copy\|move","dest?","clean_ini?":bool}` |
| `POST` | `/api/upload?filename=x` | Raw file bytes as the request body |
| `POST` | `/api/stop` | Stop the server and exit the payload |

## How to Run

### Method 1: Network Injection

Send the main payload over your local network using one of these tools:

* The [`payload_sender.py`](https://github.com/m2k7m/DB-REbuilder/blob/main/payload_sender.py) script included in this repository.
* [Al-Azif/hermes-link](https://github.com/Al-Azif/hermes-link/releases/latest).

Use the following payload:

```text
ps4-plugins-mgr.elf
```

### Method 2: On-Console Execution

To install the main payload automatically, first run:

```text
ps4-plugins-mgr-installer.elf
```

The installer places the main payload at:

```text
/data/payloads/ps4-plugins-mgr.elf
```

Alternatively, transfer `ps4-plugins-mgr.elf` directly to `/data/payloads/` using FTP, or place it on a USB drive at:

```text
usb/payloads/
```

Then launch the main payload using one of the following:

* [GoldHEN v2.4b18.10+](https://ko-fi.com/s/d64a916507)
* [Vuemony/vue-after-free](https://github.com/Vuemony/vue-after-free/releases/latest) (Full version)
* [Al-Azif/ps4-payload-guest](https://github.com/Al-Azif/ps4-payload-guest/releases/latest)

After launching the main payload, wait for the notification:

```text
plugins mgr v1.0 (c) Haider A.H Listening on <PS4-IP>:4002
```

Then open the displayed address in your browser:

```text
http://<PS4-IP>:4002
```

## Build

The project requires the PS4 Payload SDK.

### Local build

```sh
export PS4_PAYLOAD_SDK=/opt/ps4-payload-sdk
make
```

The build process works in this order:

```text
source/*.c
    ↓
ps4-plugins-mgr.elf
    ↓
installer/generated/payload_data.h
    ↓
ps4-plugins-mgr-installer.elf
```

The generated file:

```text
installer/generated/payload_data.h
```

is created automatically by the `Makefile`. No Python file is required for the
build, and the generated header should not be edited manually.

### GitHub Actions build

GitHub Actions builds both payloads:

```text
ps4-plugins-mgr.elf
ps4-plugins-mgr-installer.elf
```

The two built files are then placed into one compressed archive:

```text
ps4-plugins-mgr-payloads.zip
```

The archive contains:

```text
ps4-plugins-mgr/
├── ps4-plugins-mgr.elf
└── ps4-plugins-mgr-installer.elf
```

## Project Layout

* `source/main.c` — main payload entry point, notification, and server startup.
* `source/server.c` / `source/server.h` — socket, HTTP server, and routing.
* `source/ini.c` / `source/ini.h` — `plugins.ini` parsing and editing.
* `source/fs.c` / `source/fs.h` — directory listing, file copy, move, delete,
  backup, and atomic writes.
* `source/log.c` / `source/log.h` — kernel and file logging.
* `source/html.h` — embedded copy of `source/ui.html`.
* `source/ui.html` — standalone web UI source.
* `installer/main.c` — secondary installer payload.
* `installer/generated/` — automatically generated embedded payload header.
* `Makefile` — builds the main payload and installer payload.

## Credits

* **Plugins Mgr v1.0** by **Haider A.H.**
* **Copyright © Haider A.H. All rights reserved.**
* Built with [ps4-payload-dev/sdk](https://github.com/ps4-payload-dev/sdk).
* Web-server pattern:
  [h5d7r/ps4-port-8088-payload](https://github.com/h5d7r/ps4-port-8088-payload).
* GoldHEN plugin format:
  [GoldHEN/GoldHEN_Plugins_Repository](https://github.com/GoldHEN/GoldHEN_Plugins_Repository).
