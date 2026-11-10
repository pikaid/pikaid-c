#!/usr/bin/env python3
"""Run the official PikaID shared conformance suite against the C adapter.

The suite is an immutable external checkout. This orchestrator fails closed:
it verifies the pinned Git commit, every SHA256SUMS entry, the official
fixture self-checks, the Node oracle, and finally the JSON-Lines adapter run.
It also supports a deterministic mutant adapter as a negative control.

This script is test tooling only. It is not installed and does not certify the
library; the suite's own report states that human security review remains a
separate obligation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SUITE_COMMIT = "059780cc0e5b113443812720d95c630649bf3aa3"
PACKAGE_REVISION = "1.0.0"
SPEC_REVISION = "1.0.4"
CONFORMANCE_DIR = "conformance"
MANIFEST_NAME = "manifest.json"
CHECKSUM_NAME = "SHA256SUMS"

EXPECTED_SUITE_COUNTS = {
    "valid": 596,
    "invalid-string": 258,
    "invalid-binary": 33,
    "ordering": 186,
    "generation": 12,
    "properties": 15,
}


class SuiteError(RuntimeError):
    """A trust, transport, or conformance failure."""


def run_step(name: str, command: list[str], cwd: Path, log_dir: Path,
             env: dict[str, str], timeout: float) -> subprocess.CompletedProcess[str]:
    log_path = log_dir / f"{name}.log"
    printable = " ".join(command)
    print(f"==> {name}: {printable}")
    try:
        result = subprocess.run(
            command,
            cwd=str(cwd),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise SuiteError(f"{name}: command not found: {command[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        raise SuiteError(f"{name}: timed out after {timeout:.0f}s") from exc
    log_path.write_text(result.stdout or "", encoding="utf-8")
    print(f"    exit {result.returncode}; log: {log_path}")
    return result


def require_success(name: str, result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        tail = "\n".join((result.stdout or "").splitlines()[-40:])
        raise SuiteError(f"{name} failed with exit {result.returncode}\n{tail}")


def verify_git_checkout(suite_dir: Path) -> None:
    if not (suite_dir / ".git").exists():
        raise SuiteError(f"{suite_dir} is not a Git checkout; provenance cannot be pinned")
    rev = subprocess.run(
        ["git", "-C", str(suite_dir), "rev-parse", "HEAD"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
    )
    if rev.returncode != 0:
        raise SuiteError(f"cannot read suite commit: {rev.stderr.strip()}")
    actual = rev.stdout.strip()
    if actual != SUITE_COMMIT:
        raise SuiteError(
            f"suite commit mismatch: expected {SUITE_COMMIT}, found {actual}"
        )
    dirty = subprocess.run(
        ["git", "-C", str(suite_dir), "status", "--porcelain", "--untracked-files=no"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
    )
    if dirty.returncode != 0:
        raise SuiteError(f"cannot inspect suite worktree: {dirty.stderr.strip()}")
    if dirty.stdout.strip():
        raise SuiteError(
            "suite checkout has modified tracked files; refusing to trust it\n"
            + dirty.stdout.strip()
        )
    print(f"==> suite commit verified: {actual}")


def verify_checksums(conformance: Path) -> int:
    checksum_file = conformance / CHECKSUM_NAME
    if not checksum_file.is_file():
        raise SuiteError(f"missing {checksum_file}")
    checked = 0
    for line_number, raw in enumerate(
        checksum_file.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            raise SuiteError(f"{CHECKSUM_NAME}:{line_number}: malformed entry")
        expected, relative = parts
        expected = expected.lower()
        if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
            raise SuiteError(f"{CHECKSUM_NAME}:{line_number}: invalid digest")
        relative_path = Path(relative)
        if relative_path.is_absolute() or ".." in relative_path.parts:
            raise SuiteError(f"{CHECKSUM_NAME}:{line_number}: unsafe path {relative}")
        target = conformance / relative_path
        if not target.is_file():
            raise SuiteError(f"{CHECKSUM_NAME}:{line_number}: missing {relative}")
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        if digest != expected:
            raise SuiteError(
                f"{CHECKSUM_NAME}:{line_number}: digest mismatch for {relative}"
            )
        checked += 1
    if checked == 0:
        raise SuiteError(f"{CHECKSUM_NAME} contains no entries")
    print(f"==> verified {checked} SHA-256 manifest entries")
    return checked


def verify_manifest(conformance: Path) -> None:
    manifest_path = conformance / MANIFEST_NAME
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SuiteError(f"cannot read {manifest_path}: {exc}") from exc
    expected = {
        "status": "OFFICIAL",
        "package_revision": PACKAGE_REVISION,
        "spec_revision": SPEC_REVISION,
    }
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise SuiteError(
                f"manifest {key} mismatch: expected {value!r}, found {manifest.get(key)!r}"
            )
    fixtures = manifest.get("fixtures")
    if not isinstance(fixtures, list):
        raise SuiteError("manifest fixtures is not a list")
    observed = {entry.get("path", "").split("/")[-1].removesuffix(".json"): entry.get("cases")
                for entry in fixtures if isinstance(entry, dict)}
    for suite, count in EXPECTED_SUITE_COUNTS.items():
        if observed.get(suite) != count:
            raise SuiteError(
                f"manifest count for {suite}: expected {count}, found {observed.get(suite)}"
            )
    total = sum(EXPECTED_SUITE_COUNTS.values())
    print(
        f"==> manifest verified: {manifest.get('name')} "
        f"OFFICIAL {PACKAGE_REVISION}/{SPEC_REVISION}, {total} fixture rows"
    )


def verify_report(report_path: Path, expected_exit: int,
                  require_full_counts: bool = True) -> dict:
    if not report_path.is_file():
        raise SuiteError(f"runner did not write report JSON: {report_path}")
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except ValueError as exc:
        raise SuiteError(f"runner report is not valid JSON: {exc}") from exc
    if report.get("exit_code") != expected_exit:
        raise SuiteError(
            f"runner report exit_code {report.get('exit_code')} != {expected_exit}"
        )
    counts = report.get("counts")
    if not isinstance(counts, dict):
        raise SuiteError("runner report has no suite counts")
    for suite, expected in EXPECTED_SUITE_COUNTS.items():
        entry = counts.get(suite)
        if not isinstance(entry, dict):
            raise SuiteError(f"runner report missing suite {suite}")
        if not require_full_counts:
            continue
        observed = sum(
            int(entry.get(key, 0)) for key in ("passed", "failed", "not_applicable")
        )
        if observed != expected:
            raise SuiteError(
                f"runner report {suite} rows: expected {expected}, found {observed}"
            )
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite-dir", required=True, type=Path,
                        help="root of the pinned pikaid-specs checkout")
    parser.add_argument("--adapter", required=True, type=Path,
                        help="real conformance adapter executable")
    parser.add_argument("--mutant-adapter", type=Path, default=None,
                        help="known-wrong adapter for the negative control")
    parser.add_argument("--report-json", type=Path, default=None,
                        help="path for the runner's JSON report")
    parser.add_argument("--work-dir", type=Path, default=None,
                        help="directory for logs and auxiliary reports")
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="per-step timeout in seconds (default 900)")
    args = parser.parse_args(argv)

    suite_dir = args.suite_dir.resolve()
    adapter = args.adapter.resolve()
    conformance = suite_dir / CONFORMANCE_DIR

    work_dir = (args.work_dir or Path(tempfile.mkdtemp(prefix="pikaid-conformance-"))).resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    report_json = (args.report_json or (work_dir / "conformance-report.json")).resolve()

    if not adapter.is_file():
        print(f"FAIL: adapter not found: {adapter}", file=sys.stderr)
        return 1
    if not conformance.is_dir():
        print(f"FAIL: conformance directory not found: {conformance}", file=sys.stderr)
        return 1

    python = sys.executable
    node = shutil.which("node")
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"

    try:
        verify_git_checkout(suite_dir)
        verify_checksums(conformance)
        verify_manifest(conformance)
        if node is None:
            raise SuiteError("node is required for tools/verify_oracle_node.mjs")

        require_success(
            "verify_vectors",
            run_step(
                "verify_vectors",
                [
                    python,
                    "tools/verify_vectors.py",
                    "--expect-status",
                    "OFFICIAL",
                    "--expect-package-revision",
                    PACKAGE_REVISION,
                    "--expect-spec-revision",
                    SPEC_REVISION,
                ],
                conformance,
                work_dir,
                env,
                args.timeout,
            ),
        )
        require_success(
            "runner_tests",
            run_step(
                "runner_tests",
                [python, "-m", "unittest", "discover", "-s", "tools/tests", "-v"],
                conformance,
                work_dir,
                env,
                args.timeout,
            ),
        )
        require_success(
            "node_oracle",
            run_step(
                "node_oracle",
                [node, "tools/verify_oracle_node.mjs"],
                conformance,
                work_dir,
                env,
                args.timeout,
            ),
        )

        adapter_result = run_step(
            "adapter_automated",
            [
                python,
                "tools/run_adapter.py",
                "--automated-only",
                "--report-json",
                str(report_json),
                "--",
                str(adapter),
            ],
            conformance,
            work_dir,
            env,
            args.timeout,
        )
        if adapter_result.returncode != 0:
            tail = "\n".join((adapter_result.stdout or "").splitlines()[-40:])
            raise SuiteError(
                f"official adapter run failed with exit {adapter_result.returncode}\n{tail}"
            )
        report = verify_report(report_json, 0)
        if report.get("automated_passed") is not True:
            raise SuiteError("runner report does not mark automated checks as passed")
        if report.get("status") not in {
            "AUTOMATED_PASS",
            "AUTOMATED_PASS_AUDIT_NOT_PERFORMED",
        }:
            raise SuiteError(f"unexpected runner status: {report.get('status')}")
        print(f"==> official adapter passed: status={report.get('status')}")

        if args.mutant_adapter is not None:
            mutant = args.mutant_adapter.resolve()
            if not mutant.is_file():
                raise SuiteError(f"mutant adapter not found: {mutant}")
            mutant_report = (work_dir / "mutant-report.json").resolve()
            mutant_result = run_step(
                "adapter_mutant_control",
                [
                    python,
                    "tools/run_adapter.py",
                    "--automated-only",
                    "--max-errors",
                    "1",
                    "--report-json",
                    str(mutant_report),
                    "--",
                    str(mutant),
                ],
                conformance,
                work_dir,
                env,
                args.timeout,
            )
            if mutant_result.returncode != 1:
                raise SuiteError(
                    "negative control did not fail as an automated defect: "
                    f"exit {mutant_result.returncode}"
                )
            mutant_document = verify_report(
                mutant_report, 1, require_full_counts=False
            )
            if mutant_document.get("status") != "FAIL":
                raise SuiteError(
                    "negative control report status is not FAIL: "
                    f"{mutant_document.get('status')}"
                )
            print("==> negative control rejected the mutant adapter (exit 1)")

        summary = {
            "suite_commit": SUITE_COMMIT,
            "package_revision": PACKAGE_REVISION,
            "spec_revision": SPEC_REVISION,
            "fixture_rows": EXPECTED_SUITE_COUNTS,
            "fixture_rows_total": sum(EXPECTED_SUITE_COUNTS.values()),
            "adapter": str(adapter),
            "status": report.get("status"),
            "counts": report.get("counts"),
            "generation_freshness": report.get("generation_freshness"),
            "outstanding_audits": report.get("outstanding_audits"),
            "report_json": str(report_json),
            "negative_control": args.mutant_adapter is not None,
        }
        summary_path = work_dir / "orchestration-summary.json"
        summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print("==> orchestration summary:")
        print(json.dumps(summary, indent=2))
        print(f"==> logs and reports: {work_dir}")
        return 0
    except SuiteError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
