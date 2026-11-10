# Automated C source release archives

GitHub automatically creates basic source ZIP/TAR archives for Git tags. This
repository also uses `.github/workflows/release-assets.yml` to validate a
release build, run CTest and attach independently generated CPack source
archives with SHA-256 checksums.

The workflow validates archives on every push to `main` and on pull requests.
It uploads assets **only after** a GitHub release is published, using the
existing release tag. No third-party registry account, secret or manual
re-upload is necessary.

For each release:

1. Update the CMake `project(... VERSION X.Y.Z ...)` source version.
2. Verify normal CI passes, create the matching `vX.Y.Z` tag and publish the
   GitHub release.
3. GitHub builds, tests, packages and uploads verified source archives.

The workflow fails if the tag does not match CMake, if tests fail, or if
forbidden local agent files are present in the archive. It does not overwrite
existing release assets; publication is intentionally immutable. These are
**source distributions**, not prebuilt binaries or a Conan/vcpkg registry.
