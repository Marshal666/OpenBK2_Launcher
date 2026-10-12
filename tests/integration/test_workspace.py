"""Isolated test directories within the project's disposable build tree."""
from contextlib import contextmanager
from pathlib import Path
import shutil
import uuid


@contextmanager
def temporary_workspace():
    parent = (Path(__file__).resolve().parents[2] / "build/test-work").resolve()
    parent.mkdir(parents=True, exist_ok=True)
    root = parent / str(uuid.uuid4())
    root.mkdir(mode=0o755)
    try:
        yield root
    finally:
        # Never delete a path supplied by a user or resolved outside this tree.
        assert root.resolve().parent == parent and not root.is_symlink()
        shutil.rmtree(root)
