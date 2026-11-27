import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

TOOLS = Path(__file__).resolve().parents[3] / "tools" / "unsupported-report"


class ProjectsSiteTest(unittest.TestCase):
    def test_project_with_only_failed_scans_produces_no_report(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "project"
            project.mkdir()
            (project / "input.sv").touch()
            formatter = root / "formatter"
            formatter.write_text("#!/bin/sh\nexit 2\n", encoding="utf-8")
            formatter.chmod(0o755)
            output = root / "unsupported-project.html"

            result = subprocess.run(
                [sys.executable, str(TOOLS / "unsupported_report.py"), str(project),
                 "--formatter", str(formatter), "--site", "-o", str(output)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exit code 2", result.stderr)
            self.assertFalse(output.exists())

    def test_index_uses_only_available_projects_and_links_to_details(self):
        for projects in (("scr1", "ibex"), ("scr1",), ()):
            with self.subTest(projects=projects), TemporaryDirectory() as temporary:
                root = Path(temporary)
                site = root / "site"
                site.mkdir()
                artifacts = root / "artifacts"
                artifacts.mkdir()
                nav = '<nav class="report-nav" aria-label="Construct reports">'
                for page in ("unsupported.html", "constructs.html"):
                    (site / page).write_text(nav + f'<a href="{page}">Report</a></nav>',
                                             encoding="utf-8")
                for project in projects:
                    (artifacts / f"unsupported-{project}.html").write_text(
                        f"<h1>{project}</h1><table><tr>"
                        '<td data-label="unsupported token kind AutomaticKeyword">'
                        "automatic AutomaticKeyword</td><td>Token</td>"
                        "<td>2</td><td>source.sv:1</td></tr></table>",
                        encoding="utf-8",
                    )

                subprocess.run([sys.executable, str(TOOLS / "assemble_site.py"),
                                str(site), str(artifacts)], check=True)

                for page in ("unsupported.html", "constructs.html"):
                    self.assertIn('<a href="projects/index.html">In projects</a>',
                                  (site / page).read_text(encoding="utf-8"))
                index = (site / "projects/index.html").read_text(encoding="utf-8")
                self.assertIn('href="index.html" aria-current="page">In projects', index)
                self.assertEqual(
                    {p.name for p in (site / "projects/details").glob("*.html")},
                    {f"unsupported-{project}.html" for project in projects},
                )
                for project in ("scr1", "ibex"):
                    href = f'href="details/unsupported-{project}.html"'
                    self.assertEqual(href in index, project in projects)
                if projects:
                    self.assertIn(f'<td class="num">{2 * len(projects)}</td>', index)
                    self.assertIn('<code>automatic</code> <span class="muted">AutomaticKeyword</span>', index)
                else:
                    self.assertIn("No project reports are available for this run.", index)
                    self.assertNotIn("No unsupported constructs found.", index)
