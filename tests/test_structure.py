from pathlib import Path
import ast
ROOT = Path(__file__).resolve().parents[1]

def test_parses():
    for path in (ROOT / "afterline").glob("*.py"):
        ast.parse(path.read_text())
    ast.parse((ROOT / "main.py").read_text())

def test_distribution_assets():
    assert (ROOT / "LICENSE").exists()
    assert (ROOT / "website/index.html").exists()
    assert (ROOT / ".github/workflows/ci.yml").exists()
    assert (ROOT / ".github/workflows/release.yml").exists()
