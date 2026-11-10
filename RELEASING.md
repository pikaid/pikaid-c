# Releasing pikaid-c

This document lists what must be verified before a `v*` tag or a GitHub release
is created. It is a checklist, not an announcement: **no tag, release or signed
artefact has been published from this repository.**

## What a release means here

- The **identifier format** is immutable. A release never changes the
  26-character string or 17-byte binary form, so a release is a library
  milestone only.
- The **library version** lives in `CMakeLists.txt` (`project(pikaid VERSION
  ...)`) and is what `pkg-config --modversion pikaid` and
  `pikaidConfigVersion.cmake` report.
- The **shared library** uses `SOVERSION 1`; bumping the major version requires
  deciding whether that ABI version changes.

## Prerequisites

1. **Version consistency.** `CMakeLists.txt`, `CHANGELOG.md`, `README.md` and
   the tag name all agree. `CHANGELOG.md` has a dated entry for the version
   being released, not only `[Unreleased]`.
2. **Tree state.** `git status` is clean, `HEAD` is the commit that will be
   tagged, and `git ls-files -- .ai` is empty.
3. **Green CI on that exact commit.** All required jobs in `.github/workflows/
   ci.yml` must pass on the commit being tagged: the four build/test targets,
   sanitizers, the 90% coverage gate, shared-library exports, install consumers
   (CMake and `pkg-config`), and repository/archive hygiene. A green run on an
   earlier commit does not count.
4. **Tag identity and signing.** The release manager has a signing identity
   configured (`user.signingkey`, `gpg.format`) if the tag is to be signed, and
   has decided whether signing is required for this release.
5. **Release permissions.** The publisher can create the tag and the GitHub
   release and has administrative access to any repository settings the release
   depends on (environments, protected tags). API access from the current
   automation reports `viewerPermission: WRITE` and `viewerCanAdminister:
   false`, so administrative settings cannot be confirmed from this session.
6. **Notes and artefacts.** Release notes are derived from `CHANGELOG.md`. If
   binary artefacts or checksums are to be attached, the build and checksum
   steps are defined and reproducible.

## Procedure (not yet executed)

```sh
# 1. Verify the tree and the exact commit
git status --short
git rev-parse HEAD
git ls-files -- .ai        # must print nothing

# 2. Verify CI is green on that exact commit
gh run list --commit "$(git rev-parse HEAD)" --limit 1

# 3. Create the tag (signed only if the signing decision was made)
git tag -a v1.0.0 -m "pikaid-c 1.0.0"
git push origin v1.0.0

# 4. Publish release notes from CHANGELOG.md
gh release create v1.0.0 --title "1.0.0" --notes-file <notes>
```

## Current status

- Version in the tree: `1.0.0`.
- Tag: **not created.**
- GitHub release: **none.**
- Blockers: prerequisites 3–5 above are unverified in this session (no
  administratively verified settings, no configured signing identity, and the
  release decision belongs to the maintainer). The tree itself is prepared for
  the tag; nothing in the code needs to change first.
