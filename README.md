# Docker Desktop for air/OS

A native air/OS (Haiku) GUI for managing Docker, in the spirit of the
closed-source Docker Desktop on other platforms. Built with C++ and the
standard Be API (BApplication/BWindow/BTabView), it talks directly to the
Docker Engine API over the daemon's unix socket — no Electron, no Qt.

<!-- airos-ci:latest-builds:start -->
## Latest builds

Built automatically by air/OS CI from commit `9d28140` on 2026-10-28 ([all files](https://github.com/airos-works/docker-desktop/releases/tag/latest)).

| Architecture | Package |
|---|---|
| arm64 | [dockerdesktop-0.1.0.dev.9.9d28140-1-arm64.hpkg](https://github.com/airos-works/docker-desktop/releases/download/latest/dockerdesktop-0.1.0.dev.9.9d28140-1-arm64.hpkg) |
| x86_64 | [dockerdesktop-0.1.0.dev.9.9d28140-1-x86_64.hpkg](https://github.com/airos-works/docker-desktop/releases/download/latest/dockerdesktop-0.1.0.dev.9.9d28140-1-x86_64.hpkg) |

Install with `pkgman install <file>`, or copy the file into `/boot/system/packages`. Haiku SDK: x86_64 hrev60207-2103-g6a7d4fd5a3, arm64 hrev60207-2103-g6a7d4fd5a3.
<!-- airos-ci:latest-builds:end -->

## Features (0.1)

- **Docker Engine** bar at the top of the window: whether the Engine is
  running (checked every 2 seconds), a **Start Engine** button, and a
  **Start the Docker Engine at boot** check box. Ticking the check box also
  starts the Engine right away; unticking it leaves a running Engine alone.
  Start Engine runs `dockerd --userland-proxy=false` in a session of its own
  (it keeps running after Docker Desktop quits), logging to
  `/boot/system/var/log/dockerd.log`; the bar says when it exits again.
- **Containers** tab: lists all containers (name, image, state) with
  Start, Stop, Restart and Remove actions.
- **Images** tab: lists images with tags and size; Remove action.
- Talks to the Engine at `/var/run/docker.sock`
  (the default for the air/OS Docker port); override by editing
  `kDefaultSocket` in `src/MainWindow.cpp` for now.
- All Engine I/O happens on worker threads; the UI stays responsive.

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

- `src/App.*` — BApplication entry
- `src/MainWindow.*` — window, tabs, action bar, status line; owns the
  worker-thread plumbing
- `src/DockerClient.*` — HTTP/1.1 over unix sockets (Engine API subset)
- `src/EngineControl.*` — the start-at-boot setting and starting dockerd
- `src/Json.*` — minimal JSON parser for Engine responses
- `resources/` — rdef and PackageInfo

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
- The package installs a Deskbar entry in the **Developer** folder
  (`data/deskbar/menu/Developer/`), alongside GitHub Desktop, OSS Code
  and pgAdmin4 in air/OS images.
