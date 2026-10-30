# Docker Desktop for air/OS

A native air/OS (Haiku) GUI for managing Docker, in the spirit of the
closed-source Docker Desktop on other platforms. Built with C++ and the
standard Be API (BApplication/BWindow/BTabView), it talks directly to the
Docker Engine API over the daemon's unix socket — no Electron, no Qt.

<!-- airos-ci:latest-builds:start -->
## Latest builds

Built automatically by air/OS CI from commit `a7781bf` on 2026-10-30 ([all files](https://github.com/airos-works/docker-desktop/releases/tag/latest)).

| Architecture | Package |
|---|---|
| arm64 | [dockerdesktop-0.1.0.dev.19.a7781bf-1-arm64.hpkg](https://github.com/airos-works/docker-desktop/releases/download/latest/dockerdesktop-0.1.0.dev.19.a7781bf-1-arm64.hpkg) |
| x86_64 | [dockerdesktop-0.1.0.dev.19.a7781bf-1-x86_64.hpkg](https://github.com/airos-works/docker-desktop/releases/download/latest/dockerdesktop-0.1.0.dev.19.a7781bf-1-x86_64.hpkg) |

Install with `pkgman install <file>`, or copy the file into `/boot/system/packages`. Haiku SDK: x86_64 hrev60207-2122-g35b1bdfeea, arm64 hrev60207-2122-g35b1bdfeea.
<!-- airos-ci:latest-builds:end -->

## Features

Docker Desktop for air/OS covers what the Docker Desktop dashboard does
day to day, on the air/OS Engine:

- **Docker Engine bar** at the top of the window: the Engine's state (a
  coloured dot, checked every 2 seconds) with its version and API level,
  **Start Engine** / **Stop Engine** and **Restart** buttons, and **Start
  at boot**. Ticking Start at boot also starts the Engine right away;
  unticking it leaves a running Engine alone. Start runs `dockerd
  --userland-proxy=false` in a session of its own (it keeps running after
  Docker Desktop quits), logging to `/boot/system/var/log/dockerd.log`.
  Restart waits for the old dockerd to be gone before starting the new one.
- **Live updates**: the lists follow the Engine's event stream (`/events`),
  so containers started or removed from Terminal or `docker compose` show
  up without refreshing.
- **Containers** tab: name with a state dot, image, status, published
  ports, age and ID, in sortable, resizable columns. Containers of a
  **Compose project** are grouped under the project, with its running
  count. Search, **Only running**, and multiple selection; Start, Stop,
  Restart, Pause/Resume, Kill, Rename, Delete (also for whole projects),
  Copy ID/name, **Open in Terminal** (a shell in the container), and **Open
  in browser** for a published port, from the toolbar or a context menu.
- **Container details** (double-click): live **logs** (follow, timestamps,
  stderr in red; they pick up again after a restart), **inspect** JSON,
  and **stats** (CPU, memory, network and disk I/O, processes), with the
  container's actions.
- **Images** tab: one row per tag (and untagged ones), size, age and
  whether a container uses it. **Run…** opens a run dialog (name, ports,
  volumes, environment, command, restart policy, remove when it exits, or
  run interactively in Terminal) and pulls the image first if needed;
  **Pull…** shows per-layer progress; **Search Hub…** searches Docker Hub
  (stars, official images) and pulls or runs from the results; **Inspect**,
  **History** (layers), **Tag…**, **Push**, **Pull again** and Delete/Force
  delete.
- **Volumes** and **Networks** tabs: create (driver, subnet), inspect,
  delete, **Delete unused…**; a volume's folder opens in Tracker.
- **Disk usage** tab: the Engine's facts (version, OS, architecture, CPUs,
  storage driver, root directory), the space taken by images, containers,
  volumes and build cache with what can be reclaimed, and **Clean up** of
  stopped containers, unused networks, dangling or unused images, unused
  volumes and the build cache.
- **Deskbar item** (Docker menu › Show in Deskbar, or Settings): the Docker
  whale with a dot for the Engine's state; its menu opens Docker Desktop,
  pulls an image, opens Settings, and starts, stops or restarts the Engine,
  like the whale menu of Docker Desktop elsewhere.
- **Settings**: the Engine socket (default `/var/run/docker.sock`), start
  the Engine when Docker Desktop opens, ask before deleting, Docker Hub
  sign in/out (through `docker login` in Terminal); the Engine's
  `/etc/docker/daemon.json`, checked for valid JSON, with **Apply &
  restart**; and **Troubleshoot**: restart/start/stop the Engine, the Engine
  log, **Clean / Purge data**, and resetting Docker Desktop's settings.
- Images menu: **Build an image…** (a folder with a Dockerfile; `docker
  build` in Terminal) and **Load an image from a file…**; Containers menu:
  **Compose up…** for a compose file.
- Keyboard: Alt+1…5 for the tabs, Alt+F search, Alt+R refresh, Alt+N run,
  Alt+P pull, Alt+K search Docker Hub, Alt+B build, Alt+L Engine log,
  Alt+, settings.
- All Engine I/O happens on worker threads; the UI stays responsive.

### On the air/OS Engine

Containers from Linux images run in air/OS's Linux VM (the linuxvm package:
QEMU with NVMM, started on demand for the first one), with Linux's
namespaces, cgroups and networks: they appear in the Containers tab like any
other, with their logs, stats, processes (Top), terminals and published
ports. Haiku images still run natively. The VM's memory and processors are
set in `~/config/settings/docker_linuxvm` (`memory 4096`, `cpus 4`).

Engine builds from 2026-10-30 on (airos-works/moby and
airos-works/containerd) pull linux images when no haiku one exists, give
`docker run -t` and `docker exec -t` a terminal, report container CPU,
memory and process counts, create and link `local` volumes, and extract
images with hard links (busybox) by copying the linked files. Docker Desktop
keeps fallbacks for older Engines:

- When a pull finds no image for the Engine's own platform, Docker Desktop
  pulls the Linux one of the Engine's architecture (`linux/amd64`,
  `linux/arm64`), as `docker pull --platform` would. The Pull window takes
  an explicit platform, too.
- Older Engines give `docker exec` no terminal, so **Open in Terminal** and
  interactive runs use `-i` with an interactive shell rather than `-it`
  (which would leave Terminal in raw mode with nothing echoed). The Engine
  reports no version that tells the two apart, so this stays until older
  Engines are gone.
- When an Engine reports no resource usage, the Stats tab says so instead
  of showing zeros.

## Building

Native, on Haiku:

```sh
make            # build-haiku/DockerDesktop
make package    # artifacts/dockerdesktop-<version>-<arch>.hpkg
```

Cross, from Linux (like airShot):

```sh
tools/build-cross.sh            # build-cross/DockerDesktop (x86_64)
tools/build-cross-arm64.sh      # build-cross-arm64/DockerDesktop (arm64)
tools/package-haiku-cross.sh x86_64 arm64   # artifacts/dockerdesktop-<version>-<arch>.hpkg
```

The hpkg drops the binary in `apps/` and a symlink in the Deskbar
Developer folder.

## Layout

- `src/App.*` — the BApplication (single launch; takes requests from the
  Deskbar item)
- `src/MainWindow.*` — the window: menus, Engine bar, tabs, status line,
  Engine polling and the event stream
- `src/ResourceView.*` — what the list tabs share: toolbar, search, a
  column list updated in place, context menu, actions on worker threads
- `src/ContainersView.*`, `src/ImagesView.*`, `src/VolumesView.*` (with
  the networks), `src/DiskUsageView.*` — the tabs
- `src/ContainerWindow.*` — container details (logs, inspect, stats)
- `src/RunWindow.*`, `src/PullWindow.*`, `src/Pull.*`,
  `src/HubSearchWindow.*`, `src/PromptWindow.*`, `src/TextWindow.*`,
  `src/SettingsWindow.*` — the other windows
- `src/DeskbarView.*` — the Deskbar item (runs inside Deskbar)
- `src/DockerClient.*` — HTTP/1.1 over the Engine's unix socket: calls,
  cancellable streams, chunked bodies, multiplexed logs
- `src/EngineControl.*` — starting and stopping dockerd, start at boot,
  `daemon.json`
- `src/Json.*` — JSON parser, writer and validator; `src/Util.*`,
  `src/Columns.*`, `src/Settings.*` — helpers, list columns, settings
- `resources/` — rdef, PackageInfo and the icon (`docker-desktop.hvif`,
  made by `tools/make-icon.py`; see `resources/ICON-ATTRIBUTION.md`)

The column lists are Haiku's `BColumnListView`, whose headers are private
ones of the SDK (`develop/headers/private/interface`) and whose code is the
static `libcolumnlistview.a`; file panels need `libtracker`. The executable
exports its symbols (`--export-dynamic`) because Deskbar loads it as the
add-on of its replicant.

## Notes

- Starting at boot: the `docker_engine` package (air/OS CI) has a
  launch_daemon job, `data/user_launch/docker_engine`, that starts dockerd
  in each user session when `~/config/settings/docker_engine` (a flattened
  BMessage) holds `start_at_boot = true`. The check box writes that file.
  The Engine cannot bind `/var/run/docker.sock` in a live "Try air/OS"
  session, whose boot volume is read-only beneath a write overlay; it needs
  an installed system.

- The Engine daemon itself is being ported in
  [airos-works/moby](https://github.com/airos-works/moby); the CLI is in
  [airos-works/docker](https://github.com/airos-works/docker).
- Docker Desktop's settings are in `~/config/settings/DockerDesktop` (a
  flattened BMessage).
- The icon is the Docker logo (a trademark of Docker, Inc.), redrawn for
  Haiku's icon guidelines: lit from the top left, a darker outline and a
  shadow to the right, and fewer containers so it reads at 16 and 32 pixels.
- The package installs a Deskbar entry in the **Developer** folder
  (`data/deskbar/menu/Developer/`), alongside GitHub Desktop, OSS Code
  and pgAdmin4 in air/OS images.
