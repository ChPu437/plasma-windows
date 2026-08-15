import os
import sys

# breeze-icons ships "redirect" files: a .svg whose entire content is the
# name of another icon (e.g. "folder-activities.svg"). The qrc build turns
# these into proper aliases, but the filesystem install keeps them as-is,
# so QImageReader (and therefore KIconLoader) fails on them. Resolve each
# redirect to the target file's real content (recursively).
#
# Usage: fix-icon-redirects.py [<icons-root>]
# Default: CRAFT_ROOT env var, else the maintainer's local path.
root = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CRAFT_ROOT", r"D:\Projects\CraftRoot") + r"\bin\data\icons"
REDIRECT_LIMIT = 200  # bytes; real SVG files are much larger

resolved = 0
failed = []

for theme in ("breeze", "breeze-dark"):
    theme_root = os.path.join(root, theme)
    if not os.path.isdir(theme_root):
        continue
    for dirpath, _dirs, files in os.walk(theme_root):
        for name in files:
            if not name.endswith(".svg"):
                continue
            path = os.path.join(dirpath, name)
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            if size > REDIRECT_LIMIT:
                continue
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read().strip()
            if not content.endswith(".svg"):
                continue  # not a redirect
            target = os.path.join(dirpath, content)
            seen = set()
            while not os.path.exists(target) or os.path.getsize(target) <= REDIRECT_LIMIT:
                if target in seen:
                    break
                seen.add(target)
                if not os.path.exists(target):
                    break
                with open(target, "r", encoding="utf-8", errors="replace") as f:
                    inner = f.read().strip()
                if not inner.endswith(".svg"):
                    break
                target = os.path.join(dirpath, inner)
            if os.path.exists(target) and os.path.getsize(target) > REDIRECT_LIMIT:
                with open(target, "rb") as f:
                    data = f.read()
                with open(path, "wb") as f:
                    f.write(data)
                resolved += 1
                print("resolved:", os.path.relpath(path, root))
            else:
                failed.append(os.path.relpath(path, root))

print("resolved:", resolved, "failed:", len(failed))
for f in failed[:10]:
    print("  FAILED:", f)
