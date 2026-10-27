# Docker Desktop for air/OS

A native air/OS (Haiku) GUI for managing Docker, in the spirit of the
closed-source Docker Desktop on other platforms. Built with C++ and the
standard Be API (BApplication/BWindow/BTabView), it talks directly to the
Docker Engine API over the daemon's unix socket — no Electron, no Qt.

## Features (0.1)

- **Containers** tab: lists all containers (name, image, state) with
  Start, Stop, Restart and Remove actions.
- **Images** tab: lists images with tags and size; Remove action.
- Talks to the Engine at `/boot/system/cache/docker/docker.sock`
  (the default for the air/OS Docker port); override by editing
  `kDefaultSocket` in `src/MainWindow.cpp` for now.
- All Engine I/O happens on worker threads; the UI stays responsive.

## Building

Native, on Haiku:

```sh
make            # build-haiku/DockerDesktop
make package    # artifacts/dockerdesktop-<version>-<arch>.hpkg
```

Cross, from Linux (x86_64, like airShot):

```sh
tools/build-cross.sh
```

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
- The Deskbar entry installs under Applications; air/OS images place it
  in the Developer folder alongside the other development tools.
