# Docker Desktop for air/OS

A native air/OS (Haiku) GUI for managing Docker, in the spirit of the
closed-source Docker Desktop on other platforms. Built with C++ and the
standard Be API (BApplication/BWindow/BTabView), it talks directly to the
Docker Engine API over the daemon's unix socket — no Electron, no Qt.

## Features (0.1)

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
- `src/Json.*` — minimal JSON parser for Engine responses
- `resources/` — rdef and PackageInfo

## Notes

- The Engine daemon itself is being ported in
  [airos-works/moby](https://github.com/airos-works/moby); the CLI is in
  [airos-works/docker](https://github.com/airos-works/docker).
- The package installs a Deskbar entry in the **Developer** folder
  (`data/deskbar/menu/Developer/`), alongside GitHub Desktop, OSS Code
  and pgAdmin4 in air/OS images.
