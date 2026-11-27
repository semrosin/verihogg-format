#!/usr/bin/env python3
"""Render the formatter support timeline in the sv-tests report."""

import argparse
import html
from collections import defaultdict
from itertools import groupby
from pathlib import Path

from history import commit_distance, load_history, read_history, read_json, record


def timeline(history):
    by_sha = defaultdict(list)
    for run in history["runs"]:
        by_sha[run["sha"]].append(run)

    entries = []
    for commit in history.get("backfill", []):
        measured = by_sha.pop(commit["sha"], [])
        if not measured:
            entries.append(commit)
            continue
        entries.extend(measured[:-1])
        entries.append({**commit, **measured[-1]})
    for run in history["runs"]:
        if run["sha"] in by_sha:
            entries.append(run)
    return entries


def commit_link(run, repository):
    sha = html.escape(run["sha"], quote=True)
    subject = html.escape(run["subject"] or sha)
    return f'<a href="{repository}/commit/{sha}">{subject} <code>{sha[:8]}</code></a>'


def render_history(archive, repository, current):
    history = load_history(archive)
    runs = timeline(history)
    if not runs:
        return "<p>History will appear after the first recorded run on main.</p>"

    repository = "https://github.com/" + html.escape(repository, quote=True)
    latest_report = next(
        (i for i in range(len(runs) - 1, -1, -1)
         if runs[i]["sha"] == current["sha"] and runs[i].get("config_id") == current["config_id"]),
        None,
    )

    def render_commit(run):
        date = html.escape(run["date"][:10])
        return f'{commit_link(run, repository)} <time datetime="{date}">{date}</time>'

    def group_kind(run):
        if run.get("before_diagnostics"):
            return "early"
        if "baseline" not in run:
            return "missing"
        if not (run["baseline"] or run["added"] or run["lost"]
                or run.get("diagnostics_removed") or run.get("diagnostics_added")):
            return "unchanged"
        return "entry"

    entries = []
    for kind, group in groupby(reversed(list(enumerate(runs))), lambda item: group_kind(item[1])):
        if kind != "entry":
            items = list(group)
            count = len(items)
            noun = "commit" if count == 1 else "commits"
            label = {
                "early": f"Before unsupported-construct diagnostics ({count} {noun})",
                "missing": f"No measured results ({count} {noun})",
                "unchanged": f"{count} tested {noun} with no confirmed support changes",
            }[kind]
            lines = []
            for index, run in items:
                summary = render_commit(run)
                if index == latest_report:
                    summary += ' <a href="unsupported.html">See full report</a>'
                if (run.get("commit_count") or 0) > 1:
                    summary += render_range(run, repository)
                lines.append(f"<li>{summary}</li>")
            entries.append(
                f'<li class="no-changes"><details><summary>{label}</summary>'
                f'<ul>{"".join(lines)}</ul></details></li>'
            )
            continue

        for index, run in group:
            lines = [f"<h3>{render_commit(run)}</h3>"]
            if (run.get("commit_count") or 0) > 1:
                lines.append(render_range(run, repository))
            if run["baseline"]:
                message = ("Test configuration changed. Tracking starts again here."
                           if run["base_sha"] else "First recorded run.")
                lines.append(f"<p>{message}</p>")
            lines.extend(render_changes(run))
            if index == latest_report:
                lines.append('<p><a href="unsupported.html">See full report</a></p>')
            entries.append(f"<li>{''.join(lines)}</li>")
    return '<ol class="timeline">' + "\n".join(entries) + "</ol>"


def render_changes(run):
    lines = []
    for key, label, noun in (
        ("added", "newly supported {}", "construct"),
        ("lost", "regressed {}", "construct"),
        ("diagnostics_removed", "{} no longer observed", "diagnostic"),
        ("diagnostics_added", "new {}", "diagnostic"),
    ):
        names = run.get(key)
        if names:
            title = label.format(noun if len(names) == 1 else noun + "s")
            items = "".join(f"<li>{html.escape(name)}</li>" for name in names)
            details = '<details class="diagnostic-change">' if noun == "diagnostic" else "<details>"
            lines.append(f"{details}<summary>{len(names)} {title}</summary>"
                         f"<ul>{items}</ul></details>")
    return lines


def render_range(run, repository):
    base = html.escape(run["base_sha"], quote=True)
    sha = html.escape(run["sha"], quote=True)
    count = run["commit_count"]
    return f'<p><a href="{repository}/compare/{base}...{sha}">{count} commits since the previous run</a></p>'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--record-from", type=Path, help="Formatter checkout with main history")
    args = parser.parse_args()

    current = read_json(args.snapshot)
    if args.record_from:
        runs = read_history(args.archive)
        try:
            distance = commit_distance(args.record_from, runs[-1]["sha"], current["sha"]) if runs else None
        except ValueError as error:
            parser.exit(1, f"{error}. Skipping publication of this run.\n")
        record(args.archive, current, distance)

    template = (Path(__file__).parent / "templates" / "history.html").read_text(encoding="utf-8")
    content = render_history(args.archive, args.repository, current)
    (args.report / "history.html").write_text(
        template.replace("<!-- SUPPORT_HISTORY -->", content), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
