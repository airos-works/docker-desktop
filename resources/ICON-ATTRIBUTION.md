# Docker Desktop for air/OS application icon

The application icon shows the Docker logo, the whale ("Moby") carrying a
stack of containers. The Docker logo and the name Docker are trademarks of
Docker, Inc. This project is not affiliated with or endorsed by Docker, Inc.;
the logo is used to identify this Docker-compatible frontend for air/OS.

| File | Source | Notes |
| --- | --- | --- |
| `docker-desktop.hvif` | `tools/make-icon.py` | Whale outline from the single colour Docker mark published by [simple-icons](https://simpleicons.org/) (`simple-icons@13.21.0/icons/docker.svg`), adapted to the [Haiku icon guidelines](https://www.haiku-os.org/development/icon-guidelines/) |
| `docker-desktop-icon-preview.png` | Native Haiku render of the HVIF | 64, 32 and 16 px on light and dark backgrounds, enlarged 2x |

Adaptations (detailed in the docstring of `tools/make-icon.py`): Haiku-style
gradients lit from the top left, a mixed Docker blue instead of the pure brand
colour, a dark navy outline, a cast shadow to the lower right, and a simplified
container stack (4-2-1 boxes on a pixel grid instead of 5-3-1) that stays
legible at 32 x 32, with a level-of-detail variant for 16 x 16. The eye follows
Docker's earlier full colour logo.

The HVIF writer `tools/hvif.py` is shared with the other air/OS apps.
